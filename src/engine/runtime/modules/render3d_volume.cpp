// Volumetric fog (light3d::volume_*) and order-independent transparency
// (post3d::oit); render3d_ext.h.
//
// The fog is marched at half size after the pass's opaque draws: from each
// pixel's near point to its depth (or light3d::volume_distance), in
// volume_steps steps jittered per pixel and frame, the density of each step
// (even, thinning with height, broken into drifting patches by noise) lit by
// the sun through its shadow (render3d's sun_share, cascades included), the
// lamps through theirs and the ambient light, with a Henyey-Greenstein phase.
// The result (light scattered towards the eye, what is left of the light
// behind) is laid over the image: colour = scattered + colour * left.
//
// OIT is McGuire and Bavoil's weighted blended transparency: the see-through
// draws go twice into two half-float images tested against the pass's depth,
// a weighted sum (one, one) and the revealage (zero, one minus source), and
// their average is laid over the image by how much is covered.
#include "render3d.h"
#include "camera.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_log.h"
#include "render_stats.h"
#include <algorithm>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>
#include <string>

namespace njin {
namespace {
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

// After render3d's lighting GLSL (sun_share, lamp_share, the extended lights).
const char *const march_main = R"(
in vec2 fragTexCoord;
uniform sampler2D depthTex;
uniform mat4 invViewProj;
uniform vec4 volA;   // density, height falloff, base height, noise
uniform vec4 volB;   // anisotropy, distance, steps, ambient share
uniform vec4 volC;   // colour, sun share
uniform vec4 volD;   // noise size, wind
uniform float volTime;
uniform float volFrame;
uniform int volLamps;

float vhash(vec3 p) {
  p = fract(p * 0.3183099 + 0.1);
  p *= 17.0;
  return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float vnoise(vec3 x) {
  vec3 i = floor(x), f = fract(x);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(vhash(i), vhash(i + vec3(1, 0, 0)), f.x), mix(vhash(i + vec3(0, 1, 0)), vhash(i + vec3(1, 1, 0)), f.x), f.y),
             mix(mix(vhash(i + vec3(0, 0, 1)), vhash(i + vec3(1, 0, 1)), f.x), mix(vhash(i + vec3(0, 1, 1)), vhash(i + vec3(1, 1, 1)), f.x), f.y),
             f.z);
}

float density_at(vec3 p) {
  float d = volA.x;
  if (volA.y > 0.0)
    d *= exp(-volA.y * max(p.y - volA.z, 0.0));
  if (volA.w > 0.0) {
    vec3 q = (p - volD.yzw * volTime) / max(volD.x, 0.01);
    float n = vnoise(q) * 0.65 + vnoise(q * 2.03 + 17.0) * 0.35;
    d *= mix(1.0, smoothstep(0.3, 0.7, n) * 2.0, clamp(volA.w, 0.0, 1.0));
  }
  return d;
}

// Henyey-Greenstein, times 4 pi so an even spread is 1.
float phase(float c, float g) {
  float g2 = g * g;
  return (1.0 - g2) / pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5);
}

void main() {
  float z = texture(depthTex, fragTexCoord).r;
  vec2 ndc = fragTexCoord * 2.0 - 1.0;
  vec4 a = invViewProj * vec4(ndc, -1.0, 1.0);
  vec4 b = invViewProj * vec4(ndc, z * 2.0 - 1.0, 1.0);
  vec3 start = a.xyz / a.w;
  vec3 ray = b.xyz / b.w - start;
  float len = length(ray);
  vec3 dir = ray / max(len, 1e-5);
  len = min(len, volB.y);
  int steps = int(volB.z);
  float dt = len / float(steps);
  // Interleaved gradient noise, moved on each frame (TAA smooths it out).
  float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))) + volFrame * 0.618034);
  vec3 sun = lightColorLinear * volC.w;
  vec3 amb = ambientLinear * volB.w;
  vec3 l = -lightDir;
  float sun_phase = phase(dot(dir, l), volB.x);
  float T = 1.0;
  vec3 S = vec3(0.0);
  for (int i = 0; i < 128; i++) {
    if (i >= steps)
      break;
    vec3 p = start + dir * ((float(i) + jitter) * dt);
    float d = density_at(p);
    if (d <= 0.0)
      continue;
    vec3 L = amb;
    if (dot(sun, sun) > 0.0)
      L += sun * sun_share(p, l, l) * sun_phase;
    if (volLamps == 1) {
      for (int k = 0; k < lightCount; k++) {
        vec3 to = normalize(lightPos[k].xyz - p);
        vec3 li;
        float att = lamp_share(k, p, to, li);
        if (att > 0.0)
          L += lightColorsLinear[k].rgb * att * phase(dot(li, dir), volB.x);
      }
      int first, count;
      lamp_range(p, first, count);
      for (int j = 0; j < count; j++) {
        lamp e = lamp_at(lamp_index(first, j));
        vec3 to = normalize(e.pos.xyz - p);
        vec3 li;
        float att = lamp_share_ext(e, p, to, li);
        if (att > 0.0)
          L += e.linear.rgb * att * phase(dot(li, dir), volB.x);
      }
    }
    float left = exp(-d * dt);
    S += T * L * (1.0 - left) * volC.rgb;
    T *= left;
    if (T < 0.01)
      break;
  }
  // Into the HDR image exposed, as lit things are.
  finalColor = vec4(hdrOut == 1 ? S * exposure : S, T);
}
)";

// Upsampled over the image: blended as one, source alpha (scattered plus
// the image times what is left).
const char *const compose_fs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
out vec4 finalColor;
void main() { finalColor = texture(texture0, fragTexCoord); }
)";

// OIT: the weighted sum's average, by how much of the pixel is covered.
const char *const oit_fs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform sampler2D revealTex;
out vec4 finalColor;
void main() {
  float r = texture(revealTex, fragTexCoord).r;
  if (r >= 0.9999)
    discard;
  vec4 a = texture(texture0, fragTexCoord);
  finalColor = vec4(a.rgb / clamp(a.a, 1e-4, 5e4), 1.0 - r);
}
)";

// Clears the bound colour to `fill`.
const char *const fill_fs = R"(#version 330
uniform vec4 fill;
out vec4 finalColor;
void main() { finalColor = fill; }
)";

void set_f(Shader s, const char *name, f32 v) { SetShaderValue(s, GetShaderLocation(s, name), &v, SHADER_UNIFORM_FLOAT); }
void set_i(Shader s, const char *name, i32 v) { SetShaderValue(s, GetShaderLocation(s, name), &v, SHADER_UNIFORM_INT); }
void set_v4(Shader s, const char *name, vec4 v) { SetShaderValue(s, GetShaderLocation(s, name), &v, SHADER_UNIFORM_VEC4); }

void bind(u32 fbo, i32 w, i32 h) {
  rlDrawRenderBatchActive();
  rlEnableFramebuffer(fbo);
  rlSetFramebufferWidth(w);
  rlSetFramebufferHeight(h);
  rlViewport(0, 0, w, h);
}

bool load(volume_state &v) {
  if (v.tried)
    return v.ok;
  v.tried = true;
  // With the extended lights (it reads their clusters); a driver that cannot
  // build that variant gets the plain one, lit by the 16 lamps.
  const std::string march = std::string("#version 330\n#define NJIN_EXT\n") + render3d_lighting_glsl() + march_main;
  v.march = LoadShaderFromMemory(quad_vs, march.c_str());
  if (!IsShaderValid(v.march)) {
    const std::string plain = std::string("#version 330\n") + render3d_lighting_glsl() + march_main;
    v.march = LoadShaderFromMemory(quad_vs, plain.c_str());
  }
  v.compose = LoadShaderFromMemory(quad_vs, compose_fs);
  v.ok = IsShaderValid(v.march) && IsShaderValid(v.compose);
  if (!v.ok)
    NJIN_WARN("3d: the volumetric fog shaders failed to compile; light3d::volume_density is not drawn");
  return v.ok;
}
} // namespace

void volume_free(volume_state &v) {
  free_float_target(v.tex, v.fbo);
  for (Shader *s : {&v.march, &v.compose})
    if (IsShaderValid(*s))
      UnloadShader(*s);
  v = volume_state{};
}

void volume_run(context &ctx, u32 depth, i32 w, i32 h, bool shadows, const Matrix &light_vp, bool lamps,
                const Matrix &view_proj) {
  render3d_state &s = ctx_ref<render3d_state>(ctx);
  volume_state &v = s.volume;
  if (!load(v))
    return;
  const i32 hw = std::max(w / 2, 1), hh = std::max(h / 2, 1);
  if (v.fbo == 0 || v.w != hw || v.h != hh) {
    free_float_target(v.tex, v.fbo);
    if (!make_float_target(hw, hh, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, v.tex, v.fbo))
      return;
    v.w = hw;
    v.h = hh;
  }
  const light3d &L = s.light;
  Shader sh = v.march;
  render3d_set_pass_uniforms(s, sh, render3d_find_locations(sh), shadows, light_vp, lamps);
  // Half-size pixels: the light clusters' tiles are half as many pixels.
  if (s.lext.on)
    lights_ext_uniforms(s, sh, 0.5f);
  SetShaderValueMatrix(sh, GetShaderLocation(sh, "invViewProj"), MatrixInvert(view_proj));
  set_v4(sh, "volA", {std::max(L.volume_density, 0.0f), std::max(L.volume_falloff, 0.0f), L.volume_base,
                      clamp(L.volume_noise, 0.0f, 1.0f)});
  set_v4(sh, "volB", {clamp(L.volume_anisotropy, -0.95f, 0.95f), std::max(L.volume_distance, 0.1f),
                      (f32)std::clamp(L.volume_steps, 8, 128), std::max(L.volume_ambient, 0.0f)});
  const rgb_linear c = linear_of(L.volume_color);
  set_v4(sh, "volC", {c.r, c.g, c.b, std::max(L.volume_sun, 0.0f)});
  set_v4(sh, "volD", {std::max(L.volume_noise_size, 0.01f), L.volume_wind.x, L.volume_wind.y, L.volume_wind.z});
  set_f(sh, "volTime", ctx_ref<time_state>(ctx).elapsed);
  set_f(sh, "volFrame", (f32)(v.frame++ % 64u));
  set_i(sh, "volLamps", L.volume_lamps ? 1 : 0);
  // The maps the lighting reads, on their units (post3d may have used others).
  if (shadows) {
    rlActiveTextureSlot(sun_shadow_unit);
    rlEnableTexture(s.shadow.depth);
  }
  if (lamps) {
    rlActiveTextureSlot(lamp_shadow_unit);
    rlEnableTexture(s.lamp.depth);
  }
  if (s.lext.on)
    lights_ext_bind(s.lext);
  rlActiveTextureSlot(0);
  rlDrawRenderBatchActive();
  rlDisableDepthTest();
  rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
  rlSetBlendMode(RL_BLEND_CUSTOM);
  bind(v.fbo, hw, hh);
  render3d_quad(sh, depth, {{"depthTex", depth}});
  // Over the pass's image: scattered + image * left.
  const camera_post &post = ctx_ref<camera_post>(ctx);
  bind(s.hdr.active ? s.hdr.fbo : post.target.id, w, h);
  rlDrawRenderBatchActive();
  rlSetBlendFactors(RL_ONE, RL_SRC_ALPHA, RL_FUNC_ADD);
  rlSetBlendMode(RL_BLEND_CUSTOM);
  render3d_quad(v.compose, v.tex);
  rlDrawRenderBatchActive();
  rlSetBlendMode(RL_BLEND_ALPHA);
  ctx_ref<render_stats>(ctx)->post_passes += 2;
}

namespace {
void free_oit_targets(oit_state &o) {
  free_float_target(o.accum_tex, o.accum_clear);
  free_float_target(o.reveal_tex, o.reveal_clear);
  for (u32 *f : {&o.accum_fbo, &o.reveal_fbo})
    if (*f != 0) {
      rlUnloadFramebuffer(*f);
      *f = 0;
    }
  o.w = o.h = 0;
  o.depth = 0;
}
} // namespace

void oit_free(oit_state &o) {
  free_oit_targets(o);
  for (Shader *s : {&o.compose, &o.fill})
    if (IsShaderValid(*s))
      UnloadShader(*s);
  o = oit_state{};
}

bool oit_begin(oit_state &o, u32 depth, i32 w, i32 h) {
  if (!o.tried) {
    o.tried = true;
    o.compose = LoadShaderFromMemory(quad_vs, oit_fs);
    o.fill = LoadShaderFromMemory(quad_vs, fill_fs);
    o.ok = IsShaderValid(o.compose) && IsShaderValid(o.fill);
    if (!o.ok)
      NJIN_WARN("3d: the OIT shaders failed to compile; post3d::oit is off");
  }
  if (!o.ok)
    return false;
  if (o.accum_fbo == 0 || o.w != w || o.h != h || o.depth != depth) {
    free_oit_targets(o);
    // Each colour twice: with the pass's depth to draw, without to clear.
    if (!make_float_target(w, h, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, o.accum_tex, o.accum_clear) ||
        !make_float_target(w, h, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, o.reveal_tex, o.reveal_clear))
      return false;
    for (const auto &[fbo, tex] : {std::pair<u32 *, u32>{&o.accum_fbo, o.accum_tex}, {&o.reveal_fbo, o.reveal_tex}}) {
      *fbo = rlLoadFramebuffer();
      rlFramebufferAttach(*fbo, tex, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
      rlFramebufferAttach(*fbo, depth, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
      if (!rlFramebufferComplete(*fbo))
        return false;
    }
    o.w = w;
    o.h = h;
    o.depth = depth;
  }
  // Clear the colours alone: the depth is the pass's.
  rlDrawRenderBatchActive();
  rlDisableDepthTest();
  rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
  rlSetBlendMode(RL_BLEND_CUSTOM);
  bind(o.accum_clear, w, h);
  set_v4(o.fill, "fill", {0.0f, 0.0f, 0.0f, 0.0f});
  render3d_quad(o.fill, 0);
  bind(o.reveal_clear, w, h);
  set_v4(o.fill, "fill", {1.0f, 1.0f, 1.0f, 1.0f});
  render3d_quad(o.fill, 0);
  rlDrawRenderBatchActive();
  rlSetBlendMode(RL_BLEND_ALPHA);
  return true;
}

void oit_bind(const oit_state &o, i32 pass) { bind(pass == 1 ? o.accum_fbo : o.reveal_fbo, o.w, o.h); }

void oit_compose(oit_state &o, u32 target) {
  bind(target, o.w, o.h);
  rlDisableDepthTest();
  rlDrawRenderBatchActive();
  rlSetBlendMode(RL_BLEND_ALPHA);
  render3d_quad(o.compose, o.accum_tex, {{"revealTex", o.reveal_tex}});
  rlDrawRenderBatchActive();
}
} // namespace njin
