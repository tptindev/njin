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
// behind) is blended with the fog of the passes before, reprojected to where
// it now is and kept within this pass's 3 x 3 neighbourhood (so a moving
// thing leaves no trail): the per-pixel jitter of the steps averages out over
// frames into smooth, sharp shafts. Then it is laid over the image at full
// size, each pixel taking the half-size pixels at its own depth (so the fog
// does not bleed over edges): colour = scattered + colour * left.
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
uniform vec4 volE;   // longest step (0: volB.z steps a ray), 1 when the fog outside the sun's map is dark
// Where along the ray the light was scattered, weighed by how much: the
// resolve finds the fog's history there (-1: none scattered).
layout(location = 1) out vec4 scatterAt;

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

// The sun's share at a fog sample: the single map read once, not filtered
// 3 x 3 (nine reads a step was most of the fog's cost; the steps' jitter,
// averaged over frames, softens the edge instead); the cascades as the
// lighting reads them. Outside the map (or the last cascade): lit, or with
// light3d::volume_dark_outside (volE.y) dark.
float fog_sun(vec3 p, vec3 l) {
  float outside = volE.y > 0.5 ? 0.0 : 1.0;
  if (shadowOn == 2 && volE.y > 0.5) {
    float d = dot(p - viewPos, viewDir);
    for (int i = 0; i < 4; i++) {
      if (i >= shadowCascades)
        break;
      if (d > cascadeSplit[i])
        continue;
      float a = cascade_light(i, p, l, l);
      if (a >= 0.0)
        return a;
    }
    return outside;
  }
  if (shadowOn != 1)
    return sun_share(p, l, l);
  vec4 lp = lightVP * vec4(p, 1.0);
  vec3 c = lp.xyz / lp.w * 0.5 + 0.5;
  if (c.x <= 0.0 || c.x >= 1.0 || c.y <= 0.0 || c.y >= 1.0 || c.z >= 1.0)
    return outside;
  return c.z - shadowParams.x * 1.5 <= texture(shadowMap, c.xy).r ? 1.0 : 0.0;
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
  // light3d::volume_step_max: a long ray gets more steps, none longer than it.
  if (volE.x > 0.0)
    steps = clamp(int(ceil(len / volE.x)), steps, 256);
  float dt = len / float(steps);
  // Interleaved gradient noise, moved on each frame (TAA smooths it out).
  float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))) + volFrame * 0.618034);
  vec3 sun = lightColorLinear * volC.w;
  vec3 amb = ambientLinear * volB.w;
  vec3 l = -lightDir;
  float sun_phase = phase(dot(dir, l), volB.x);
  float T = 1.0;
  vec3 S = vec3(0.0);
  float wsum = 0.0, dsum = 0.0;
  for (int i = 0; i < 256; i++) {
    if (i >= steps)
      break;
    vec3 p = start + dir * ((float(i) + jitter) * dt);
    float d = density_at(p);
    if (d <= 0.0)
      continue;
    vec3 L = amb;
    if (dot(sun, sun) > 0.0)
      L += sun * fog_sun(p, l) * sun_phase;
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
    vec3 add = T * L * (1.0 - left) * volC.rgb;
    S += add;
    float w = dot(add, vec3(0.2126, 0.7152, 0.0722));
    wsum += w;
    dsum += w * (float(i) + jitter) * dt;
    T *= left;
    if (T < 0.01)
      break;
  }
  // Into the HDR image exposed, as lit things are.
  finalColor = vec4(hdrOut == 1 ? S * exposure : S, T);
  scatterAt = vec4(wsum > 1e-6 ? dsum / wsum : -1.0);
}
)";

// The march blended with the fog of the passes before. At the point along
// the half-size pixel's ray where its light was scattered (else its surface),
// where that point was last pass; its fog there, kept inside the range of this
// pass's 3 x 3 neighbourhood, weighs `keep`.
const char *const resolve_fs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;   // this pass's march
uniform sampler2D histTex;    // the fog of the passes before
uniform sampler2D depthTex;   // this pass's depth, full size
uniform sampler2D distTex;    // where along the ray the light was scattered
uniform mat4 invViewProj;
uniform mat4 prevViewProj;
uniform vec2 texel;           // one half-size pixel in uv
uniform float keep;           // 0: no history
out vec4 finalColor;
void main() {
  vec4 cur = texture(texture0, fragTexCoord);
  if (keep <= 0.0) {
    finalColor = cur;
    return;
  }
  // The 3 x 3 neighbourhood: its range keeps the history honest, its mean is
  // this pass's share (the steps' jitter pattern, averaged away, does not
  // print on the shafts' edges while the camera moves).
  vec4 lo = cur, hi = cur, mean = vec4(0.0);
  for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++) {
      vec4 c = texture(texture0, fragTexCoord + vec2(x, y) * texel);
      lo = min(lo, c);
      hi = max(hi, c);
      mean += c * ((x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0));
    }
  mean /= 16.0;
  float z = texture(depthTex, fragTexCoord).r;
  vec2 ndc = fragTexCoord * 2.0 - 1.0;
  vec4 w = invViewProj * vec4(ndc, z * 2.0 - 1.0, 1.0);
  vec3 at = w.xyz / w.w;
  float along = texture(distTex, fragTexCoord).r;
  if (along >= 0.0) {
    vec4 a = invViewProj * vec4(ndc, -1.0, 1.0);
    vec3 start = a.xyz / a.w;
    at = start + normalize(at - start) * along;
  }
  vec4 c = prevViewProj * vec4(at, 1.0);
  vec2 uv = c.xy / c.w * 0.5 + 0.5;
  if (c.w <= 0.0 || any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
    finalColor = mean;
    return;
  }
  // Widened by a tenth of the range: the history is found where its light
  // was, so it is trusted a little more than one half-size pass.
  vec4 margin = (hi - lo) * 0.1;
  vec4 raw = texture(histTex, uv);
  vec4 h = clamp(raw, lo - margin, hi + margin);
  // History well outside this pass's range is a change (a thing moved into
  // a shaft): taken up quickly.
  vec4 off = abs(raw - h) / max(hi - lo + margin, vec4(1e-3));
  float k = max(max(off.r, off.g), max(off.b, off.a)) > 0.5 ? min(keep, 0.6) : keep;
  finalColor = mix(mean, h, k);
}
)";

// Upsampled over the image: blended as one, source alpha (scattered plus
// the image times what is left). Each pixel takes the four half-size pixels
// round it by distance and by how near their surface is to its own.
const char *const compose_fs = R"(#version 330
in vec2 fragTexCoord;
uniform sampler2D texture0;  // the fog, half size
uniform sampler2D depthTex;  // the pass's depth, full size
uniform vec2 lowSize;        // the fog's size in pixels
uniform vec2 planes;         // near, far (near negated: orthographic)
out vec4 finalColor;
float linear_depth(float d) {
  if (planes.x < 0.0)
    return -planes.x + d * (planes.y + planes.x);
  return planes.x * planes.y / (planes.y - d * (planes.y - planes.x));
}
void main() {
  float z = linear_depth(texture(depthTex, fragTexCoord).r);
  vec2 t = fragTexCoord * lowSize - 0.5;
  vec2 f = fract(t);
  vec2 base = floor(t);
  vec4 sum = vec4(0.0);
  float wsum = 0.0;
  for (int y = 0; y <= 1; y++)
    for (int x = 0; x <= 1; x++) {
      vec2 q = clamp(base + vec2(x, y), vec2(0.0), lowSize - 1.0);
      vec2 uv = (q + 0.5) / lowSize;
      float zq = linear_depth(texture(depthTex, uv).r);
      float bw = (x == 1 ? f.x : 1.0 - f.x) * (y == 1 ? f.y : 1.0 - f.y);
      // Plain bilinear between pixels on the same surface (within a few
      // percent of the depth); one across an edge hardly counts.
      float rel = abs(zq - z) / max(z, 1e-3);
      float w = bw * exp(-rel * rel / (2.0 * 0.03 * 0.03)) + 1e-5;
      sum += texture(texture0, uv) * w;
      wsum += w;
    }
  finalColor = sum / max(wsum, 1e-6);
}
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

// The march's second target (scatterAt), beside its colour.
bool add_dist_target(volume_state &v, i32 w, i32 h) {
  v.dist_tex = rlLoadTexture(nullptr, w, h, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
  if (v.dist_tex == 0)
    return false;
  rlTextureParameters(v.dist_tex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
  rlTextureParameters(v.dist_tex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
  rlFramebufferAttach(v.fbo, v.dist_tex, RL_ATTACHMENT_COLOR_CHANNEL1, RL_ATTACHMENT_TEXTURE2D, 0);
  return rlFramebufferComplete(v.fbo);
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
  v.resolve = LoadShaderFromMemory(quad_vs, resolve_fs);
  v.ok = IsShaderValid(v.march) && IsShaderValid(v.compose) && IsShaderValid(v.resolve);
  if (!v.ok)
    NJIN_WARN("3d: the volumetric fog shaders failed to compile; light3d::volume_density is not drawn");
  return v.ok;
}
} // namespace

void volume_free(volume_state &v) {
  free_float_target(v.tex, v.fbo);
  if (v.dist_tex != 0)
    rlUnloadTexture(v.dist_tex);
  for (usize i = 0; i < 2; i++)
    free_float_target(v.hist_tex[i], v.hist_fbo[i]);
  for (Shader *s : {&v.march, &v.resolve, &v.compose})
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
    if (v.dist_tex != 0)
      rlUnloadTexture(v.dist_tex);
    v.dist_tex = 0;
    for (usize i = 0; i < 2; i++)
      free_float_target(v.hist_tex[i], v.hist_fbo[i]);
    v.hist_ok = false;
    if (!make_float_target(hw, hh, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, v.tex, v.fbo) ||
        !add_dist_target(v, hw, hh) ||
        !make_float_target(hw, hh, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, v.hist_tex[0], v.hist_fbo[0]) ||
        !make_float_target(hw, hh, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, v.hist_tex[1], v.hist_fbo[1]))
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
  set_v4(sh, "volE", {std::max(L.volume_step_max, 0.0f), L.volume_dark_outside ? 1.0f : 0.0f, 0.0f, 0.0f});
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
  rlActiveDrawBuffers(2);
  render3d_quad(sh, depth, {{"depthTex", depth}});
  rlActiveDrawBuffers(1);
  // Blended with the passes before, into the other history image.
  const i32 next = 1 - v.hist_at;
  Shader rs = v.resolve;
  SetShaderValueMatrix(rs, GetShaderLocation(rs, "invViewProj"), MatrixInvert(view_proj));
  SetShaderValueMatrix(rs, GetShaderLocation(rs, "prevViewProj"), v.prev_vp);
  const vec2 texel{1.0f / (f32)hw, 1.0f / (f32)hh};
  SetShaderValue(rs, GetShaderLocation(rs, "texel"), &texel, SHADER_UNIFORM_VEC2);
  set_f(rs, "keep", v.hist_ok ? 0.95f : 0.0f);
  bind(v.hist_fbo[(usize)next], hw, hh);
  render3d_quad(rs, v.tex, {{"histTex", v.hist_tex[(usize)v.hist_at]}, {"depthTex", depth}, {"distTex", v.dist_tex}});
  v.hist_at = next;
  v.hist_ok = true;
  v.prev_vp = view_proj;
  // Over the pass's image: scattered + image * left.
  const camera_post &post = ctx_ref<camera_post>(ctx);
  bind(s.hdr.active ? s.hdr.fbo : post.target.id, w, h);
  rlDrawRenderBatchActive();
  rlSetBlendFactors(RL_ONE, RL_SRC_ALPHA, RL_FUNC_ADD);
  rlSetBlendMode(RL_BLEND_CUSTOM);
  Shader cs = v.compose;
  const vec2 low{(f32)hw, (f32)hh};
  SetShaderValue(cs, GetShaderLocation(cs, "lowSize"), &low, SHADER_UNIFORM_VEC2);
  const vec2 planes = render3d_depth_planes(s.camera);
  SetShaderValue(cs, GetShaderLocation(cs, "planes"), &planes, SHADER_UNIFORM_VEC2);
  render3d_quad(cs, v.hist_tex[(usize)v.hist_at], {{"depthTex", depth}});
  rlDrawRenderBatchActive();
  rlSetBlendMode(RL_BLEND_ALPHA);
  ctx_ref<render_stats>(ctx)->post_passes += 3;
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
