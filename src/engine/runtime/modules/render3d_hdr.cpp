// light3d::hdr, post3d::bloom, light3d::auto_exposure and post3d::aa
// (render3d_ext.h).
//
// The HDR image is a half-float copy of the world target, opened by end_3d
// before the pass's draws: the world so far is taken into it in linear light,
// its depth copied. The lighting shaders write exposed linear light into it
// (hdrOut), post3d's effects run on it, and hdr_resolve() then
//   1. bloom: the image halved down a chain of mips (Jimenez's 13-tap filter,
//      Karis' average on the first so a single bright pixel does not flicker)
//      and added back up with a tent filter, each level adding to the next;
//      mixed in by post3d::bloom (as Bevy and Unreal, no threshold),
//   2. auto exposure: the centre-weighted mean log luminance down to one
//      pixel, read back at the next pass (a frame later, so the read does not
//      wait for the GPU) and eased towards light3d::auto_exposure_key,
//   3. the tone curve and the sRGB encoding into the world target, the depth
//      copied back for the passes and post_fx after it.
// aa_apply() then runs FXAA or SMAA (iryoku/smaa, MIT, built in from its
// SMAA.hlsl and lookup images) over the world target once a frame.
#include "render3d.h"
#include "camera.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_log.h"
#include "post3d.h"
#include "render_stats.h"
#include <AreaTex.h>
#include <SearchTex.h>
#include <algorithm>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>
#include <string>
#include <vector>

namespace njin {
extern const unsigned char smaa_hlsl[];
extern const unsigned long long smaa_hlsl_size;

namespace {
constexpr i32 color_bit = 0x00004000; // GL_COLOR_BUFFER_BIT
constexpr i32 depth_bit = 0x00000100; // GL_DEPTH_BUFFER_BIT

const char *const quad_vs = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 mvp;
out vec2 fragTexCoord;
void main() {
  fragTexCoord = vertexTexCoord;
  gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

// The world image so far into the HDR image, in linear light.
const char *const init_fs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
out vec4 finalColor;
void main() {
  vec3 c = max(texture(texture0, fragTexCoord).rgb, vec3(0.0));
  finalColor = vec4(mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c)), 1.0);
}
)";

// The HDR image through the tone curve (render3d's lighting GLSL is put in
// front for tone_out, with exposure 1: the shaders exposed it already).
const char *const resolve_main = R"(
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform sampler2D bloomTex;
uniform float bloom;       // post3d::bloom, 0 none
uniform float bloomLevels; // the chain's sum is this many levels
void main() {
  vec3 c = max(texture(texture0, fragTexCoord).rgb, vec3(0.0));
  if (bloom > 0.0)
    c = mix(c, texture(bloomTex, fragTexCoord).rgb / bloomLevels, bloom);
  finalColor = vec4(tone_out(c), 1.0);
}
)";

// One step down the bloom chain: Jimenez's 13 taps ("Next Generation Post
// Processing in Call of Duty: Advanced Warfare"), with Karis' average on the
// first (each group weighted by 1 / (1 + its luminance)).
const char *const down_fs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 srcTexel;
uniform int karis;
out vec4 finalColor;
vec3 at(vec2 o) { return max(texture(texture0, fragTexCoord + o * srcTexel).rgb, vec3(0.0)); }
float weight(vec3 c) { return 1.0 / (1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722))); }
void main() {
  vec3 a = at(vec2(-2.0, 2.0)), b = at(vec2(0.0, 2.0)), c = at(vec2(2.0, 2.0));
  vec3 d = at(vec2(-2.0, 0.0)), e = at(vec2(0.0, 0.0)), f = at(vec2(2.0, 0.0));
  vec3 g = at(vec2(-2.0, -2.0)), h = at(vec2(0.0, -2.0)), i = at(vec2(2.0, -2.0));
  vec3 j = at(vec2(-1.0, 1.0)), k = at(vec2(1.0, 1.0)), l = at(vec2(-1.0, -1.0)), m = at(vec2(1.0, -1.0));
  vec3 o;
  if (karis == 1) {
    vec3 g0 = (j + k + l + m) * 0.25;
    vec3 g1 = (a + b + d + e) * 0.25, g2 = (b + c + e + f) * 0.25, g3 = (d + e + g + h) * 0.25, g4 = (e + f + h + i) * 0.25;
    float w0 = weight(g0) * 0.5, w1 = weight(g1) * 0.125, w2 = weight(g2) * 0.125, w3 = weight(g3) * 0.125,
          w4 = weight(g4) * 0.125;
    o = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / max(w0 + w1 + w2 + w3 + w4, 1e-4);
  } else {
    o = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
  }
  finalColor = vec4(max(o, vec3(1e-5)), 1.0);
}
)";

// One step up: a 3x3 tent of `radius` (texture coordinates), added onto the
// next larger level.
const char *const up_fs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 radius;
out vec4 finalColor;
vec3 at(vec2 o) { return texture(texture0, fragTexCoord + o * radius).rgb; }
void main() {
  vec3 s = at(vec2(0.0)) * 4.0;
  s += (at(vec2(0.0, 1.0)) + at(vec2(-1.0, 0.0)) + at(vec2(1.0, 0.0)) + at(vec2(0.0, -1.0))) * 2.0;
  s += at(vec2(-1.0, 1.0)) + at(vec2(1.0, 1.0)) + at(vec2(-1.0, -1.0)) + at(vec2(1.0, -1.0));
  finalColor = vec4(s / 16.0, 1.0);
}
)";

// Auto exposure: each sample's log luminance, weighted towards the centre.
const char *const lum_fs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
out vec4 finalColor;
void main() {
  vec3 c = max(texture(texture0, fragTexCoord).rgb, vec3(0.0));
  float l = max(dot(c, vec3(0.2126, 0.7152, 0.0722)), 1e-4);
  vec2 d = fragTexCoord - 0.5;
  float w = exp(-dot(d, d) * 6.0);
  finalColor = vec4(log2(l) * w, w, 0.0, 1.0);
}
)";

// Halves the luminance image: the sum of four texels' weighted logs and weights.
const char *const lum_down_fs = R"(#version 330
uniform sampler2D texture0;
out vec4 finalColor;
void main() {
  ivec2 p = ivec2(gl_FragCoord.xy) * 2;
  ivec2 last = textureSize(texture0, 0) - 1;
  vec2 s = texelFetch(texture0, min(p, last), 0).rg + texelFetch(texture0, min(p + ivec2(1, 0), last), 0).rg +
           texelFetch(texture0, min(p + ivec2(0, 1), last), 0).rg + texelFetch(texture0, min(p + ivec2(1, 1), last), 0).rg;
  finalColor = vec4(s * 0.25, 0.0, 1.0);
}
)";

// FXAA after Timothy Lottes: the luma of the four diagonal neighbours gives
// the edge's direction; two blends along it, the wider one kept unless it
// steps outside the neighbourhood's luma range.
const char *const fxaa_fs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 texel;
out vec4 finalColor;
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
void main() {
  vec2 uv = fragTexCoord;
  vec3 nw = texture(texture0, uv + vec2(-1.0, -1.0) * texel).rgb;
  vec3 ne = texture(texture0, uv + vec2(1.0, -1.0) * texel).rgb;
  vec3 sw = texture(texture0, uv + vec2(-1.0, 1.0) * texel).rgb;
  vec3 se = texture(texture0, uv + vec2(1.0, 1.0) * texel).rgb;
  vec3 m = texture(texture0, uv).rgb;
  float lnw = luma(nw), lne = luma(ne), lsw = luma(sw), lse = luma(se), lm = luma(m);
  float lo = min(lm, min(min(lnw, lne), min(lsw, lse)));
  float hi = max(lm, max(max(lnw, lne), max(lsw, lse)));
  if (hi - lo < max(0.0312, hi * 0.125)) {
    finalColor = vec4(m, 1.0);
    return;
  }
  vec2 dir = vec2(-((lnw + lne) - (lsw + lse)), (lnw + lsw) - (lne + lse));
  float reduce = max((lnw + lne + lsw + lse) * (0.25 * 0.125), 1.0 / 128.0);
  float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
  dir = clamp(dir * rcp, vec2(-8.0), vec2(8.0)) * texel;
  vec3 a = 0.5 * (texture(texture0, uv + dir * (1.0 / 3.0 - 0.5)).rgb + texture(texture0, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
  vec3 b = a * 0.5 + 0.25 * (texture(texture0, uv - dir * 0.5).rgb + texture(texture0, uv + dir * 0.5).rgb);
  float lb = luma(b);
  finalColor = vec4(lb < lo || lb > hi ? a : b, 1.0);
}
)";

// SMAA's porting macros: GLSL, every lookup with y flipped so the passes see
// the images top-down as SMAA's Direct3D origin wants (its lookup images are
// uploaded flipped to match).
const char *const smaa_head = R"(#version 330
uniform vec4 smaaMetrics;
#define SMAA_RT_METRICS smaaMetrics
#define SMAA_PRESET_HIGH 1
#define SMAA_CUSTOM_SL 1
#define SMAATexture2D(tex) sampler2D tex
#define SMAATexturePass2D(tex) tex
#define SMAA_Y(c) vec2((c).x, 1.0 - (c).y)
#define SMAASampleLevelZero(tex, coord) textureLod(tex, SMAA_Y(coord), 0.0)
#define SMAASampleLevelZeroPoint(tex, coord) textureLod(tex, SMAA_Y(coord), 0.0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) textureLod(tex, SMAA_Y((coord) + vec2(offset) * SMAA_RT_METRICS.xy), 0.0)
#define SMAASample(tex, coord) texture(tex, SMAA_Y(coord))
#define SMAASamplePoint(tex, coord) texture(tex, SMAA_Y(coord))
#define SMAASampleOffset(tex, coord, offset) texture(tex, SMAA_Y((coord) + vec2(offset) * SMAA_RT_METRICS.xy))
#define SMAA_FLATTEN
#define SMAA_BRANCH
#define lerp(a, b, t) mix(a, b, t)
#define saturate(a) clamp(a, 0.0, 1.0)
#define mad(a, b, c) (a * b + c)
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define int2 ivec2
#define int3 ivec3
#define int4 ivec4
#define bool2 bvec2
#define bool3 bvec3
#define bool4 bvec4
)";

const char *const smaa_edge_vs = R"(
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 mvp;
out vec2 texcoord;
out vec4 offset[3];
void main() {
  texcoord = vec2(vertexTexCoord.x, 1.0 - vertexTexCoord.y);
  SMAAEdgeDetectionVS(texcoord, offset);
  gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";
const char *const smaa_edge_fs = R"(
in vec2 texcoord;
in vec4 offset[3];
uniform sampler2D texture0;
out vec4 finalColor;
void main() {
  finalColor = vec4(SMAALumaEdgeDetectionPS(texcoord, offset, texture0), 0.0, 1.0);
}
)";
const char *const smaa_weight_vs = R"(
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 mvp;
out vec2 texcoord;
out vec2 pixcoord;
out vec4 offset[3];
void main() {
  texcoord = vec2(vertexTexCoord.x, 1.0 - vertexTexCoord.y);
  SMAABlendingWeightCalculationVS(texcoord, pixcoord, offset);
  gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";
const char *const smaa_weight_fs = R"(
in vec2 texcoord;
in vec2 pixcoord;
in vec4 offset[3];
uniform sampler2D texture0;
uniform sampler2D areaTex;
uniform sampler2D searchTex;
out vec4 finalColor;
void main() {
  finalColor = SMAABlendingWeightCalculationPS(texcoord, pixcoord, offset, texture0, areaTex, searchTex, vec4(0.0));
}
)";
const char *const smaa_blend_vs = R"(
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 mvp;
out vec2 texcoord;
out vec4 offset;
void main() {
  texcoord = vec2(vertexTexCoord.x, 1.0 - vertexTexCoord.y);
  SMAANeighborhoodBlendingVS(texcoord, offset);
  gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";
const char *const smaa_blend_fs = R"(
in vec2 texcoord;
in vec4 offset;
uniform sampler2D texture0;
uniform sampler2D blendTex;
out vec4 finalColor;
void main() {
  finalColor = vec4(SMAANeighborhoodBlendingPS(texcoord, offset, texture0, blendTex).rgb, 1.0);
}
)";

Shader smaa_shader(const std::string &source, const char *vs_main, const char *fs_main) {
  const std::string vs = std::string(smaa_head) + "#define SMAA_INCLUDE_VS 1\n#define SMAA_INCLUDE_PS 0\n" + source + vs_main;
  const std::string fs = std::string(smaa_head) + "#define SMAA_INCLUDE_VS 0\n#define SMAA_INCLUDE_PS 1\n" + source + fs_main;
  return LoadShaderFromMemory(vs.c_str(), fs.c_str());
}

// SMAA's lookup images, flipped vertically (see smaa_head), as RGBA8.
u32 smaa_image(const unsigned char *bytes, i32 w, i32 h, i32 channels, bool linear) {
  std::vector<unsigned char> px((usize)w * (usize)h * 4);
  for (i32 y = 0; y < h; y++)
    for (i32 x = 0; x < w; x++) {
      const unsigned char *src = bytes + ((usize)(h - 1 - y) * (usize)w + (usize)x) * (usize)channels;
      unsigned char *dst = &px[((usize)y * (usize)w + (usize)x) * 4];
      dst[0] = src[0];
      dst[1] = channels > 1 ? src[1] : src[0];
      dst[2] = 0;
      dst[3] = 255;
    }
  const u32 id = rlLoadTexture(px.data(), w, h, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
  const i32 filter = linear ? RL_TEXTURE_FILTER_LINEAR : RL_TEXTURE_FILTER_NEAREST;
  rlTextureParameters(id, RL_TEXTURE_MIN_FILTER, filter);
  rlTextureParameters(id, RL_TEXTURE_MAG_FILTER, filter);
  rlTextureParameters(id, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
  rlTextureParameters(id, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
  return id;
}

bool load(hdr_state &h) {
  if (h.loaded || h.load_failed)
    return h.loaded;
  h.init = LoadShaderFromMemory(quad_vs, init_fs);
  const std::string resolve = std::string("#version 330\n") + render3d_lighting_glsl() + resolve_main;
  h.resolve = LoadShaderFromMemory(quad_vs, resolve.c_str());
  h.down = LoadShaderFromMemory(quad_vs, down_fs);
  h.up = LoadShaderFromMemory(quad_vs, up_fs);
  h.lum = LoadShaderFromMemory(quad_vs, lum_fs);
  h.lum_down = LoadShaderFromMemory(quad_vs, lum_down_fs);
  h.fxaa = LoadShaderFromMemory(quad_vs, fxaa_fs);
  for (const Shader *s : {&h.init, &h.resolve, &h.down, &h.up, &h.lum, &h.lum_down, &h.fxaa})
    if (!IsShaderValid(*s)) {
      NJIN_WARN("3d: the HDR shaders failed to compile; light3d::hdr and post3d::aa are off");
      h.load_failed = true;
      return false;
    }
  // SMAA is optional: FXAA stands in when its shaders do not build.
  const std::string source((const char *)smaa_hlsl, (usize)smaa_hlsl_size);
  h.smaa_edge = smaa_shader(source, smaa_edge_vs, smaa_edge_fs);
  h.smaa_weight = smaa_shader(source, smaa_weight_vs, smaa_weight_fs);
  h.smaa_blend = smaa_shader(source, smaa_blend_vs, smaa_blend_fs);
  h.smaa_ok = IsShaderValid(h.smaa_edge) && IsShaderValid(h.smaa_weight) && IsShaderValid(h.smaa_blend);
  if (h.smaa_ok) {
    h.area_tex = smaa_image(areaTexBytes, AREATEX_WIDTH, AREATEX_HEIGHT, 2, true);
    h.search_tex = smaa_image(searchTexBytes, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 1, false);
  } else {
    NJIN_WARN("3d: the SMAA shaders failed to compile; post3d::aa uses FXAA instead");
  }
  h.loaded = true;
  return true;
}

void free_image(hdr_state &h) {
  free_float_target(h.color, h.color_fbo);
  if (h.fbo != 0)
    rlUnloadFramebuffer(h.fbo);
  if (h.depth != 0)
    rlUnloadTexture(h.depth);
  h.fbo = h.depth = 0;
  h.w = h.h = 0;
}

// The HDR image of `w` x `h`: colour (half float) with depth, and the colour alone.
bool ensure_image(hdr_state &h, i32 w, i32 hh) {
  if (h.fbo != 0 && h.w == w && h.h == hh)
    return true;
  free_image(h);
  if (!make_float_target(w, hh, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, h.color, h.color_fbo))
    return false;
  h.depth = rlLoadTextureDepth(w, hh, false);
  h.fbo = rlLoadFramebuffer();
  rlFramebufferAttach(h.fbo, h.color, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
  rlFramebufferAttach(h.fbo, h.depth, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
  if (!rlFramebufferComplete(h.fbo)) {
    free_image(h);
    return false;
  }
  h.w = w;
  h.h = hh;
  return true;
}

void free_bloom(hdr_state &h) {
  for (usize i = 0; i < h.mip_tex.size(); i++)
    free_float_target(h.mip_tex[i], h.mip_fbo[i]);
  h.mip_tex.clear();
  h.mip_fbo.clear();
  h.mip_w.clear();
  h.mip_h.clear();
}

// The bloom chain for the image's size: halves down to about 8 pixels, at
// most 7 levels.
bool ensure_bloom(hdr_state &h) {
  i32 w = std::max(h.w / 2, 1), hh = std::max(h.h / 2, 1);
  if (!h.mip_tex.empty() && h.mip_w[0] == w && h.mip_h[0] == hh)
    return true;
  free_bloom(h);
  for (i32 i = 0; i < 7 && std::min(w, hh) >= 8; i++) {
    u32 tex = 0, fbo = 0;
    if (!make_float_target(w, hh, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, tex, fbo)) {
      free_bloom(h);
      return false;
    }
    h.mip_tex.push_back(tex);
    h.mip_fbo.push_back(fbo);
    h.mip_w.push_back(w);
    h.mip_h.push_back(hh);
    w = std::max(w / 2, 1);
    hh = std::max(hh / 2, 1);
  }
  return !h.mip_tex.empty();
}

bool ensure_lum(hdr_state &h) {
  if (h.lum_tex[0] != 0)
    return true;
  for (i32 i = 0; i < 7; i++) {
    const i32 size = 64 >> i;
    if (!make_float_target(size, size, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, h.lum_tex[(usize)i],
                           h.lum_fbo[(usize)i], false)) {
      for (i32 k = 0; k < 7; k++)
        free_float_target(h.lum_tex[(usize)k], h.lum_fbo[(usize)k]);
      return false;
    }
  }
  return true;
}

void bind(u32 fbo, i32 w, i32 h) {
  rlDrawRenderBatchActive();
  rlEnableFramebuffer(fbo);
  rlSetFramebufferWidth(w);
  rlSetFramebufferHeight(h);
  rlViewport(0, 0, w, h);
}

void blit(u32 from, u32 to, i32 w, i32 h, i32 bits) {
  rlDrawRenderBatchActive();
  rlBindFramebuffer(RL_READ_FRAMEBUFFER, from);
  rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, to);
  rlBlitFramebuffer(0, 0, w, h, 0, 0, w, h, bits);
  rlDisableFramebuffer();
}

void set_f(Shader s, const char *name, f32 v) { SetShaderValue(s, GetShaderLocation(s, name), &v, SHADER_UNIFORM_FLOAT); }
void set_i(Shader s, const char *name, i32 v) { SetShaderValue(s, GetShaderLocation(s, name), &v, SHADER_UNIFORM_INT); }
void set_v2(Shader s, const char *name, vec2 v) { SetShaderValue(s, GetShaderLocation(s, name), &v, SHADER_UNIFORM_VEC2); }
void set_v4(Shader s, const char *name, vec4 v) { SetShaderValue(s, GetShaderLocation(s, name), &v, SHADER_UNIFORM_VEC4); }

// Replaces what is under the quad instead of blending with it.
void replace_begin() {
  rlDrawRenderBatchActive();
  rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
  rlSetBlendMode(RL_BLEND_CUSTOM);
}
void blend_end() {
  rlDrawRenderBatchActive();
  rlSetBlendMode(RL_BLEND_ALPHA);
}

// Last pass's luminance measure, read back now, eased into the exposure.
void read_exposure(context &ctx, hdr_state &h) {
  const light3d &light = ctx_ref<render3d_state>(ctx)->light;
  const f64 now = GetTime();
  if (!light.auto_exposure) {
    h.adapted = 1.0f;
    h.measured = false;
    h.adapting = false;
    h.adapted_at = now;
    return;
  }
  const f32 lo = std::max(light.auto_exposure_min, 1e-3f);
  const f32 hi = std::max(light.auto_exposure_max, lo);
  if (h.measured) {
    f32 *px = (f32 *)rlReadTexturePixels(h.lum_tex[6], 1, 1, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32);
    if (px != nullptr) {
      const f32 mean_log = px[0] / std::max(px[1], 1e-6f);
      // rlgl allocates through raylib's allocator (njin's counting one).
      MemFree(px);
      const f32 seen = std::exp2(std::clamp(mean_log, -20.0f, 20.0f));
      const f32 target = std::clamp(std::max(light.auto_exposure_key, 1e-3f) * h.adapted_then / std::max(seen, 1e-5f), lo, hi);
      if (!h.adapting) {
        h.adapted = target; // the first measure: no easing from a stale value
      } else {
        const f32 dt = (f32)std::clamp(now - h.adapted_at, 0.0, 0.5);
        const f32 k = 1.0f - std::exp(-dt * std::max(light.auto_exposure_speed, 0.0f));
        h.adapted = std::exp(std::log(h.adapted) + (std::log(target) - std::log(h.adapted)) * k);
      }
      h.adapting = true;
    }
    h.measured = false;
  }
  h.adapted = std::clamp(h.adapted, lo, hi);
  h.adapted_at = now;
}

void run_bloom(hdr_state &h, f32 radius) {
  const i32 n = (i32)h.mip_tex.size();
  rlDisableDepthTest();
  replace_begin();
  for (i32 i = 0; i < n; i++) {
    const u32 src = i == 0 ? h.color : h.mip_tex[(usize)i - 1];
    const i32 sw = i == 0 ? h.w : h.mip_w[(usize)i - 1];
    const i32 sh = i == 0 ? h.h : h.mip_h[(usize)i - 1];
    bind(h.mip_fbo[(usize)i], h.mip_w[(usize)i], h.mip_h[(usize)i]);
    set_v2(h.down, "srcTexel", {1.0f / (f32)sw, 1.0f / (f32)sh});
    set_i(h.down, "karis", i == 0 ? 1 : 0);
    render3d_quad(h.down, src);
  }
  // Each level added onto the next larger one.
  rlDrawRenderBatchActive();
  rlSetBlendFactors(RL_ONE, RL_ONE, RL_FUNC_ADD);
  rlSetBlendMode(RL_BLEND_CUSTOM);
  const f32 r = std::clamp(radius, 0.25f, 2.0f);
  for (i32 i = n - 1; i > 0; i--) {
    bind(h.mip_fbo[(usize)i - 1], h.mip_w[(usize)i - 1], h.mip_h[(usize)i - 1]);
    set_v2(h.up, "radius", {r / (f32)h.mip_w[(usize)i], r / (f32)h.mip_h[(usize)i]});
    render3d_quad(h.up, h.mip_tex[(usize)i]);
  }
  blend_end();
}

void measure(hdr_state &h) {
  if (!ensure_lum(h))
    return;
  rlDisableDepthTest();
  replace_begin();
  bind(h.lum_fbo[0], 64, 64);
  render3d_quad(h.lum, h.color);
  for (i32 i = 1; i < 7; i++) {
    const i32 size = 64 >> i;
    bind(h.lum_fbo[(usize)i], size, size);
    render3d_quad(h.lum_down, h.lum_tex[(usize)i - 1]);
  }
  blend_end();
  h.measured = true;
  h.adapted_then = h.adapted;
}

void free_aa(hdr_state &h) {
  free_float_target(h.aa_tex, h.aa_fbo);
  free_float_target(h.edges_tex, h.edges_fbo);
  free_float_target(h.weights_tex, h.weights_fbo);
  h.aa_w = h.aa_h = 0;
}

bool ensure_aa(hdr_state &h, i32 w, i32 hh) {
  if (h.aa_fbo != 0 && h.aa_w == w && h.aa_h == hh)
    return true;
  free_aa(h);
  if (!make_float_target(w, hh, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, h.aa_tex, h.aa_fbo) ||
      !make_float_target(w, hh, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, h.edges_tex, h.edges_fbo) ||
      !make_float_target(w, hh, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, h.weights_tex, h.weights_fbo)) {
    free_aa(h);
    return false;
  }
  h.aa_w = w;
  h.aa_h = hh;
  return true;
}
} // namespace

void render3d_quad(Shader shader, u32 tex0, std::initializer_list<std::pair<const char *, u32>> samplers) {
  rlDrawRenderBatchActive();
  // Saved and set, not pushed: rlPushMatrix in modelview mode switches to a
  // CPU-side transform.
  const Matrix projection = rlGetMatrixProjection();
  const Matrix modelview = rlGetMatrixModelview();
  rlMatrixMode(RL_PROJECTION);
  rlLoadIdentity();
  rlMatrixMode(RL_MODELVIEW);
  rlLoadIdentity();
  BeginShaderMode(shader);
  // rlSetUniformSampler writes to the bound program; the batch has not bound
  // ours yet. It binds the textures itself when it draws.
  for (const auto &s : samplers) {
    rlEnableShader(shader.id);
    rlSetUniformSampler(GetShaderLocation(shader, s.first), s.second);
  }
  rlSetTexture(tex0);
  rlBegin(RL_QUADS);
  rlColor4ub(255, 255, 255, 255);
  rlTexCoord2f(0.0f, 0.0f);
  rlVertex2f(-1.0f, -1.0f);
  rlTexCoord2f(1.0f, 0.0f);
  rlVertex2f(1.0f, -1.0f);
  rlTexCoord2f(1.0f, 1.0f);
  rlVertex2f(1.0f, 1.0f);
  rlTexCoord2f(0.0f, 1.0f);
  rlVertex2f(-1.0f, 1.0f);
  rlEnd();
  rlSetTexture(0);
  rlDrawRenderBatchActive();
  EndShaderMode();
  rlSetMatrixProjection(projection);
  rlSetMatrixModelview(modelview);
}

bool make_float_target(i32 w, i32 h, i32 format, u32 &tex, u32 &fbo, bool linear) {
  tex = rlLoadTexture(nullptr, w, h, format, 1);
  fbo = rlLoadFramebuffer();
  if (tex == 0 || fbo == 0) {
    free_float_target(tex, fbo);
    return false;
  }
  const i32 filter = linear ? RL_TEXTURE_FILTER_LINEAR : RL_TEXTURE_FILTER_NEAREST;
  rlTextureParameters(tex, RL_TEXTURE_MIN_FILTER, filter);
  rlTextureParameters(tex, RL_TEXTURE_MAG_FILTER, filter);
  rlTextureParameters(tex, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
  rlTextureParameters(tex, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
  rlFramebufferAttach(fbo, tex, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
  if (!rlFramebufferComplete(fbo)) {
    free_float_target(tex, fbo);
    return false;
  }
  return true;
}

void free_float_target(u32 &tex, u32 &fbo) {
  if (fbo != 0)
    rlUnloadFramebuffer(fbo);
  if (tex != 0)
    rlUnloadTexture(tex);
  tex = fbo = 0;
}

void hdr_free(hdr_state &h) {
  free_image(h);
  free_bloom(h);
  for (usize i = 0; i < h.lum_tex.size(); i++)
    free_float_target(h.lum_tex[i], h.lum_fbo[i]);
  free_aa(h);
  for (u32 *id : {&h.area_tex, &h.search_tex})
    if (*id != 0) {
      rlUnloadTexture(*id);
      *id = 0;
    }
  if (h.loaded)
    for (Shader *s : {&h.init, &h.resolve, &h.down, &h.up, &h.lum, &h.lum_down, &h.fxaa, &h.smaa_edge, &h.smaa_weight,
                      &h.smaa_blend})
      if (IsShaderValid(*s))
        UnloadShader(*s);
  h = hdr_state{};
}

bool hdr_wanted(const context &ctx) {
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  return s.light.hdr && s.light.tonemap != tonemap3d_none && !s.hdr.failed;
}

bool hdr_begin(context &ctx) {
  render3d_state &s = ctx_ref<render3d_state>(ctx);
  hdr_state &h = s.hdr;
  const camera_post &post = ctx_ref<camera_post>(ctx);
  if (h.failed || !post.drawing || post.target.depth.id == 0 || !load(h))
    return false;
  const i32 w = post.target.texture.width, hh = post.target.texture.height;
  if (!ensure_image(h, w, hh)) {
    NJIN_WARN("3d: could not make the HDR image (half float framebuffer); light3d::hdr is off");
    h.failed = true;
    return false;
  }
  read_exposure(ctx, h);
  // The world so far, in linear light, and its depth.
  bind(h.color_fbo, w, hh);
  rlDisableDepthTest();
  replace_begin();
  render3d_quad(h.init, post.target.texture.id);
  blend_end();
  blit(post.target.id, h.fbo, w, hh, depth_bit);
  h.active = true;
  ctx_ref<render_stats>(ctx)->post_passes += 1;
  return true;
}

void hdr_bind(context &ctx) {
  const hdr_state &h = ctx_ref<render3d_state>(ctx)->hdr;
  bind(h.fbo, h.w, h.h);
}

f32 hdr_exposure(const context &ctx) { return render3d_out_exposure(ctx_ref<render3d_state>(ctx)); }

void hdr_resolve(context &ctx) {
  render3d_state &s = ctx_ref<render3d_state>(ctx);
  hdr_state &h = s.hdr;
  if (!h.active)
    return;
  const post3d &p = post3d_settings(ctx);
  rlDrawRenderBatchActive();
  rlDisableDepthTest();
  const bool bloom = p.bloom > 0.0f && ensure_bloom(h);
  if (bloom)
    run_bloom(h, p.bloom_radius);
  if (s.light.auto_exposure)
    measure(h);
  const camera_post &post = ctx_ref<camera_post>(ctx);
  bind(post.target.id, h.w, h.h);
  replace_begin();
  set_i(h.resolve, "toneMap", (i32)s.light.tonemap);
  set_f(h.resolve, "exposure", 1.0f);
  set_f(h.resolve, "bloom", bloom ? clamp(p.bloom, 0.0f, 1.0f) : 0.0f);
  set_f(h.resolve, "bloomLevels", bloom ? (f32)h.mip_tex.size() : 1.0f);
  render3d_quad(h.resolve, h.color, {{"bloomTex", bloom ? h.mip_tex[0] : h.color}});
  blend_end();
  blit(h.fbo, post.target.id, h.w, h.h, depth_bit);
  h.active = false;
  ctx_ref<render_stats>(ctx)->post_passes += bloom ? 2 + (i32)h.mip_tex.size() * 2 : 1;
}

void aa_apply(context &ctx) {
  hdr_state &h = ctx_ref<render3d_state>(ctx)->hdr;
  const post3d &p = post3d_settings(ctx);
  const camera_post &post = ctx_ref<camera_post>(ctx);
  if (h.aa_claimed || p.aa == aa3d_none || !post.drawing || !load(h))
    return;
  h.aa_claimed = true;
  const i32 w = post.target.texture.width, hh = post.target.texture.height;
  if (!ensure_aa(h, w, hh))
    return;
  rlDrawRenderBatchActive();
  rlDisableDepthTest();
  blit(post.target.id, h.aa_fbo, w, hh, color_bit);
  replace_begin();
  if (p.aa == aa3d_smaa && h.smaa_ok) {
    const vec4 metrics{1.0f / (f32)w, 1.0f / (f32)hh, (f32)w, (f32)hh};
    for (Shader *s : {&h.smaa_edge, &h.smaa_weight, &h.smaa_blend})
      set_v4(*s, "smaaMetrics", metrics);
    bind(h.edges_fbo, w, hh);
    rlClearColor(0, 0, 0, 0);
    rlClearScreenBuffers();
    render3d_quad(h.smaa_edge, h.aa_tex);
    bind(h.weights_fbo, w, hh);
    rlClearColor(0, 0, 0, 0);
    rlClearScreenBuffers();
    render3d_quad(h.smaa_weight, h.edges_tex, {{"areaTex", h.area_tex}, {"searchTex", h.search_tex}});
    bind(post.target.id, w, hh);
    render3d_quad(h.smaa_blend, h.aa_tex, {{"blendTex", h.weights_tex}});
  } else {
    bind(post.target.id, w, hh);
    set_v2(h.fxaa, "texel", {1.0f / (f32)w, 1.0f / (f32)hh});
    render3d_quad(h.fxaa, h.aa_tex);
  }
  blend_end();
  ctx_ref<render_stats>(ctx)->post_passes += p.aa == aa3d_smaa ? 3 : 1;
}

void hdr_frame_begin(context &ctx) { ctx_ref<render3d_state>(ctx)->hdr.aa_claimed = false; }
} // namespace njin
