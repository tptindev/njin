#include "post3d.h"
#include "render3d.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_log.h"
#include "njin_texture.h"
#include <algorithm>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>
#include <string>

namespace njin {
namespace {
// glBlitFramebuffer's buffer bits.
constexpr i32 color_bit = 0x00004000;
constexpr i32 depth_bit = 0x00000100;
// The unit decals read the depth copy from (DrawMesh binds the decal's image
// on unit 0 and nothing above it).
constexpr i32 decal_depth_unit = 9;

// What the full-screen passes share: the depth copy turned back into world
// positions, linear depth, and the surface normal from the depth.
constexpr const char *common_glsl = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform sampler2D depthTex;
uniform mat4 invViewProj;
uniform mat4 viewProj;
uniform vec2 planes;
uniform vec2 texel;
uniform vec3 eyePos;
vec3 world_at(vec2 uv, float d) {
  vec4 w = invViewProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
  return w.xyz / w.w;
}
float linear_depth(float d) {
  float z = d * 2.0 - 1.0;
  return 2.0 * planes.x * planes.y / (planes.y + planes.x - z * (planes.y - planes.x));
}
vec3 world_of(vec2 uv) { return world_at(uv, texture(depthTex, uv).r); }
// On each axis the neighbour nearer to this point, so an edge does not bend
// the normal of the surface it borders.
vec3 normal_at(vec2 uv, vec3 p) {
  vec3 r = world_of(uv + vec2(texel.x, 0.0)), l = world_of(uv - vec2(texel.x, 0.0));
  vec3 u = world_of(uv + vec2(0.0, texel.y)), d = world_of(uv - vec2(0.0, texel.y));
  vec3 dx = dot(r - p, r - p) < dot(p - l, p - l) ? r - p : p - l;
  vec3 dy = dot(u - p, u - p) < dot(p - d, p - d) ? u - p : p - d;
  vec3 n = normalize(cross(dx, dy));
  return dot(n, eyePos - p) < 0.0 ? -n : n;
}
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
)";

// Hemisphere samples round each point along its normal, projected back onto
// the depth: a sample behind a nearer surface (within the radius) occludes.
constexpr const char *ssao_main = R"(
uniform float radius;
uniform int samples;
void main() {
  // At half size a pixel centre falls between two depth texels: take one.
  vec2 uv = (floor(fragTexCoord / texel) + 0.5) * texel;
  float d = texture(depthTex, uv).r;
  if (d >= 1.0) {
    finalColor = vec4(1.0);
    return;
  }
  vec3 p = world_at(uv, d);
  vec3 n = normal_at(uv, p);
  vec3 t = normalize(abs(n.y) < 0.99 ? cross(n, vec3(0.0, 1.0, 0.0)) : cross(n, vec3(1.0, 0.0, 0.0)));
  vec3 b = cross(n, t);
  // A 4x4 pattern of turns, which the blur averages away.
  vec2 cell = mod(floor(gl_FragCoord.xy), 4.0);
  float spin = (cell.x * 4.0 + cell.y + 0.5) / 16.0 * 6.2831853;
  float here = linear_depth(d);
  float occlusion = 0.0;
  for (int i = 0; i < 32; i++) {
    if (i >= samples)
      break;
    float f = (float(i) + 0.5) / float(samples);
    float a = spin + float(i) * 2.3999632;
    float z = sqrt(1.0 - f);
    float side = sqrt(f);
    vec3 dir = t * (cos(a) * side) + b * (sin(a) * side) + n * z;
    float scale = mix(0.15, 1.0, f * f);
    vec3 s = p + n * (0.02 * radius) + dir * (radius * scale);
    vec4 c = viewProj * vec4(s, 1.0);
    if (c.w <= 0.0)
      continue;
    vec2 su = c.xy / c.w * 0.5 + 0.5;
    if (su.x < 0.0 || su.y < 0.0 || su.x > 1.0 || su.y > 1.0)
      continue;
    float sd = texture(depthTex, su).r;
    if (sd >= 1.0)
      continue;
    float surface = linear_depth(sd);
    float sample_depth = linear_depth(c.z / c.w * 0.5 + 0.5);
    float range = smoothstep(0.0, 1.0, radius / max(abs(here - surface), 1e-4));
    occlusion += surface < sample_depth - 0.01 * radius ? range : 0.0;
  }
  float ao = 1.0 - occlusion / float(samples);
  finalColor = vec4(ao, ao, ao, 1.0);
}
)";

// One direction of a blur that keeps to the surface: taps far from this
// pixel's depth count less.
constexpr const char *ssao_blur_main = R"(
uniform sampler2D texture0;
uniform vec2 direction;
void main() {
  vec2 uv = fragTexCoord;
  float center = linear_depth(texture(depthTex, uv).r);
  float sum = 0.0;
  float weight = 0.0;
  for (int i = -4; i <= 4; i++) {
    vec2 o = uv + direction * float(i);
    float z = linear_depth(texture(depthTex, o).r);
    float w = exp(-float(i * i) / 8.0) * max(0.0, 1.0 - abs(z - center) / (0.03 * center + 0.05));
    sum += texture(texture0, o).r * w;
    weight += w;
  }
  float ao = weight > 0.0 ? sum / weight : texture(texture0, uv).r;
  finalColor = vec4(ao, ao, ao, 1.0);
}
)";

// The occlusion multiplied into the image. From half size, four taps weighed
// by how near their depth is to this pixel's, so edges stay sharp.
constexpr const char *ssao_apply_main = R"(
uniform sampler2D texture0;
uniform vec2 aoTexel;
uniform float strength;
void main() {
  vec2 uv = fragTexCoord;
  float d = texture(depthTex, uv).r;
  if (d >= 1.0) {
    finalColor = vec4(1.0);
    return;
  }
  float center = linear_depth(d);
  float sum = 0.0;
  float weight = 0.0;
  for (int i = 0; i < 4; i++) {
    vec2 o = uv + aoTexel * vec2(i == 1 || i == 3 ? 0.5 : -0.5, i >= 2 ? 0.5 : -0.5);
    float z = linear_depth(texture(depthTex, o).r);
    float w = 1.0 / (1e-3 + abs(z - center));
    sum += texture(texture0, o).r * w;
    weight += w;
  }
  float ao = sum / weight;
  finalColor = vec4(vec3(mix(1.0, ao, strength)), 1.0);
}
)";

// Each reflecting pixel marches its reflected ray in world steps over the
// depth; the first step that goes behind a surface (by less than the
// thickness) is refined and takes the image's colour there.
constexpr const char *ssr_main = R"(
uniform sampler2D texture0;
uniform sampler2D maskTex;
uniform float strength;
uniform float maxDistance;
uniform int steps;
uniform float thickness;
uniform vec4 skyColor;
vec2 screen_of(vec3 s, out float depth, out bool ok) {
  vec4 c = viewProj * vec4(s, 1.0);
  ok = c.w > 0.0;
  depth = linear_depth(c.z / c.w * 0.5 + 0.5);
  return c.xy / c.w * 0.5 + 0.5;
}
void main() {
  vec2 uv = fragTexCoord;
  float m = texture(maskTex, uv).r;
  float d = texture(depthTex, uv).r;
  if (m <= 0.0 || d >= 1.0)
    discard;
  vec3 p = world_at(uv, d);
  vec3 n = normal_at(uv, p);
  vec3 v = normalize(p - eyePos);
  vec3 r = reflect(v, n);
  float facing = max(dot(n, -v), 0.0);
  float fresnel = m + (1.0 - m) * pow(1.0 - facing, 5.0);
  // Steps grow along the ray (fine near the surface, where contact matters,
  // coarse far away); a surface counts as hit when the ray went behind it by
  // less than the thickness or than the last step.
  float jitter = hash(gl_FragCoord.xy);
  float prev_t = 0.0;
  float t = 0.0;
  bool hit = false;
  vec2 hit_uv = uv;
  for (int i = 0; i < 128; i++) {
    if (i >= steps)
      break;
    float f = (float(i) + jitter) / float(steps);
    t = maxDistance * max(f * f, 0.002);
    float ray_depth;
    bool ok;
    vec2 su = screen_of(p + r * t, ray_depth, ok);
    if (!ok || su.x < 0.0 || su.y < 0.0 || su.x > 1.0 || su.y > 1.0)
      break;
    float sd = texture(depthTex, su).r;
    float surface = linear_depth(sd);
    if (sd < 1.0 && ray_depth > surface && ray_depth - surface < max(thickness, t - prev_t)) {
      float a = prev_t;
      float b = t;
      for (int k = 0; k < 6; k++) {
        float mid = (a + b) * 0.5;
        float md;
        bool mok;
        vec2 mu = screen_of(p + r * mid, md, mok);
        if (md > linear_depth(texture(depthTex, mu).r))
          b = mid;
        else
          a = mid;
      }
      float bd;
      bool bok;
      hit_uv = screen_of(p + r * b, bd, bok);
      hit = true;
      break;
    }
    prev_t = t;
  }
  float found = 0.0;
  vec3 color = skyColor.rgb;
  if (hit) {
    vec2 e = smoothstep(vec2(0.0), vec2(0.08), hit_uv) * (1.0 - smoothstep(vec2(0.92), vec2(1.0), hit_uv));
    found = e.x * e.y * (1.0 - smoothstep(0.75 * maxDistance, maxDistance, t));
    color = mix(skyColor.rgb, texture(texture0, hit_uv).rgb, found);
  }
  float alpha = strength * fresnel * mix(skyColor.a, 1.0, found);
  finalColor = vec4(color, clamp(alpha, 0.0, 1.0));
}
)";

// The sky near the sun: where nothing was drawn, brighter towards the sun.
constexpr const char *shafts_sky_main = R"(
uniform vec2 sun;
uniform float aspect;
void main() {
  vec2 uv = fragTexCoord;
  float sky = texture(depthTex, uv).r >= 1.0 ? 1.0 : 0.0;
  float dist = length((uv - sun) * vec2(aspect, 1.0));
  float glow = exp(-dist * 6.0);
  finalColor = vec4(vec3(sky * glow), 1.0);
}
)";

// Radial blur towards the sun: each pixel gathers the sky between it and the
// sun, fading with distance.
constexpr const char *shafts_blur_main = R"(
uniform sampler2D texture0;
uniform vec2 sun;
uniform float length;
void main() {
  vec2 uv = fragTexCoord;
  const int taps = 48;
  vec2 delta = (uv - sun) * length / float(taps);
  vec2 c = uv;
  float fade = 1.0;
  vec3 sum = vec3(0.0);
  for (int i = 0; i < taps; i++) {
    sum += texture(texture0, c).rgb * fade;
    fade *= 0.965;
    c -= delta;
  }
  finalColor = vec4(sum * (1.2 / float(taps)), 1.0);
}
)";

constexpr const char *add_fs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 tint;
out vec4 finalColor;
void main() { finalColor = vec4(texture(texture0, fragTexCoord).rgb * tint.rgb, 1.0); }
)";

// How much of the sun's disc shows: a ring of taps round it on the depth.
constexpr const char *flare_vis_main = R"(
uniform vec2 sun;
uniform float aspect;
uniform float onScreen;
void main() {
  float sky = 0.0;
  const int taps = 25;
  for (int i = 0; i < taps; i++) {
    float r = 0.012 * sqrt((float(i) + 0.5) / float(taps));
    float a = float(i) * 2.3999632;
    vec2 o = sun + vec2(cos(a) / aspect, sin(a)) * r;
    sky += o.x >= 0.0 && o.y >= 0.0 && o.x <= 1.0 && o.y <= 1.0 && texture(depthTex, o).r >= 1.0 ? 1.0 : 0.0;
  }
  float v = sky / float(taps) * onScreen;
  finalColor = vec4(v, v, v, 1.0);
}
)";

// Ghosts along the line from the sun through the centre, a halo ring on the
// sun's side, and a glow round the sun; as bright as the sun shows.
constexpr const char *flare_fs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D visTex;
uniform vec2 sun;
uniform float aspect;
uniform float strength;
uniform float halo;
uniform vec3 sunColor;
out vec4 finalColor;
void main() {
  float vis = texture(visTex, vec2(0.5)).r;
  vec2 uv = fragTexCoord;
  vec2 k = vec2(aspect, 1.0);
  vec2 to_center = vec2(0.5) - sun;
  const float at[5] = float[](0.45, 0.8, 1.25, 1.6, 2.1);
  const float size[5] = float[](0.06, 0.03, 0.09, 0.045, 0.12);
  const vec3 tint[5] = vec3[](vec3(1.0, 0.6, 0.3), vec3(0.5, 0.9, 0.6), vec3(0.4, 0.6, 1.0), vec3(1.0, 0.8, 0.5),
                              vec3(0.6, 0.5, 1.0));
  vec3 col = vec3(0.0);
  for (int i = 0; i < 5; i++) {
    float dist = length((uv - (sun + to_center * at[i])) * k);
    col += tint[i] * (1.0 - smoothstep(size[i] * 0.6, size[i], dist)) * 0.2;
  }
  vec2 from_center = (uv - vec2(0.5)) * k;
  float ring = exp(-pow((length(from_center) - 0.42) / 0.025, 2.0));
  float side = max(dot(normalize(from_center + 1e-5), normalize((sun - vec2(0.5)) * k + 1e-5)), 0.0);
  col += vec3(1.0, 0.85, 0.7) * ring * side * 0.25 * halo;
  col += vec3(1.0, 0.9, 0.75) * exp(-length((uv - sun) * k) * 14.0) * 0.5;
  finalColor = vec4(col * sunColor * strength * vis, 1.0);
}
)";

// Motion blur: how far each pixel slid on screen since the last pass (its
// motion vector where a mesh or model is; else its world position, from the
// opaque depth, seen through the last pass's view-projection); the image is
// averaged along that. The longest object motion a few pixels round is taken
// when it is longer, so a moving object smears over the background at its edge.
constexpr const char *blur_main = R"(
uniform sampler2D texture0;
uniform sampler2D velTex;
uniform float hasVel;
uniform mat4 prevViewProj;
uniform float strength;
uniform int samples;
uniform float maxLength;
void main() {
  vec2 uv = fragTexCoord;
  vec3 p = world_of(uv);
  vec4 c = prevViewProj * vec4(p, 1.0);
  vec2 prev = c.w > 0.0 ? c.xy / c.w * 0.5 + 0.5 : uv;
  vec2 m = uv - prev;
  if (hasVel > 0.5) {
    vec4 own = texture(velTex, uv);
    if (own.a > 0.5)
      m = own.xy;
    float best = length(m / texel);
    for (int ring = 1; ring <= 2; ring++)
      for (int i = 0; i < 8; i++) {
        float a = float(i) * 0.785398 + float(ring) * 0.39;
        vec4 o = texture(velTex, uv + vec2(cos(a), sin(a)) * float(ring * ring) * 5.0 * texel);
        float l = length(o.xy / texel);
        if (o.a > 0.5 && l > best + 1.0) {
          best = l;
          m = o.xy;
        }
      }
  }
  vec2 vel = m * strength;
  float len = length(vel * vec2(1.0, texel.x / texel.y));
  if (len > maxLength)
    vel *= maxLength / len;
  vec4 center = texture(texture0, uv);
  // Under half a pixel of motion (a still camera, up to rounding): untouched.
  if (length(vel / texel) < 0.5) {
    finalColor = center;
    return;
  }
  vec3 sum = vec3(0.0);
  for (int i = 0; i < 32; i++) {
    if (i >= samples)
      break;
    float t = float(i) / float(samples - 1) - 0.5;
    sum += texture(texture0, uv + vel * t).rgb;
  }
  finalColor = vec4(sum / float(samples), center.a);
}
)";

// TAA resolve, into the next history. The front-most pixel of the 3x3 round
// this one finds where it was last frame: by its motion vector where a mesh or
// model is, else by its depth through both frames' unjittered matrices (a
// still camera reads the history right here). The history there is clipped to
// the colour spread of the 3x3 now (YCoCg, mean +- gamma sigma), more loosely
// where the motion vector is known, and dropped where the depth it was drawn
// with does not hold this point.
constexpr const char *taa_main = R"(
uniform sampler2D texture0;
uniform sampler2D historyTex;
uniform sampler2D prevDepthTex;
uniform sampler2D velTex;
uniform float hasVel;
uniform mat4 viewProjUnj;
uniform mat4 prevViewProj;
uniform float reset;
uniform float feedback;
vec3 to_ycocg(vec3 c) {
  return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}
vec3 from_ycocg(vec3 c) {
  float t = c.x - c.z;
  return vec3(t + c.y, c.x + c.z, t - c.y);
}
// The history read with a Catmull-Rom filter (five bilinear taps): plain
// bilinear blurs a little more each frame the camera moves.
vec3 history_at(vec2 uv) {
  vec2 size = 1.0 / texel;
  vec2 pos = uv * size;
  vec2 c = floor(pos - 0.5) + 0.5;
  vec2 f = pos - c;
  vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
  vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
  vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
  vec2 w3 = f * f * (-0.5 + 0.5 * f);
  vec2 w12 = w1 + w2;
  vec2 t0 = (c - 1.0) * texel, t3 = (c + 2.0) * texel, t12 = (c + w2 / w12) * texel;
  vec3 r = texture(historyTex, vec2(t12.x, t0.y)).rgb * w12.x * w0.y +
           texture(historyTex, vec2(t0.x, t12.y)).rgb * w0.x * w12.y +
           texture(historyTex, t12).rgb * w12.x * w12.y +
           texture(historyTex, vec2(t3.x, t12.y)).rgb * w3.x * w12.y +
           texture(historyTex, vec2(t12.x, t3.y)).rgb * w12.x * w3.y;
  float wsum = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
  return max(r / wsum, vec3(0.0));
}
vec3 clip_box(vec3 lo, vec3 hi, vec3 h) {
  vec3 c = 0.5 * (hi + lo);
  vec3 e = 0.5 * (hi - lo) + 1e-4;
  vec3 v = h - c;
  vec3 a = abs(v / e);
  float m = max(a.x, max(a.y, a.z));
  return m > 1.0 ? c + v / m : h;
}
void main() {
  vec2 uv = fragTexCoord;
  vec3 cur = texture(texture0, uv).rgb;
  if (reset > 0.5) {
    finalColor = vec4(cur, 1.0);
    return;
  }
  vec2 near_uv = uv;
  float near_d = 1.0;
  vec3 m1 = vec3(0.0), m2 = vec3(0.0);
  for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++) {
      vec2 o = uv + vec2(float(x), float(y)) * texel;
      float d = texture(depthTex, o).r;
      if (d < near_d) {
        near_d = d;
        near_uv = o;
      }
      vec3 c = to_ycocg(texture(texture0, o).rgb);
      m1 += c;
      m2 += c * c;
    }
  m1 /= 9.0;
  m2 /= 9.0;
  vec3 p = world_at(near_uv, near_d);
  vec4 cu = viewProjUnj * vec4(p, 1.0);
  vec4 cp = prevViewProj * vec4(p, 1.0);
  vec2 cam = cp.w > 0.0 ? (cu.xy / cu.w - cp.xy / cp.w) * 0.5 : vec2(0.0);
  // Moving on its own: a mesh or model whose motion is not the camera's.
  vec4 ov = hasVel > 0.5 ? texture(velTex, near_uv) : vec4(0.0);
  bool own = ov.a > 0.5 && length((ov.xy - cam) / texel) > 0.5;
  vec2 vel = own ? ov.xy : cam;
  vec2 huv = uv - vel;
  float speed = length(vel / texel);
  float alpha = own ? mix(1.0 - feedback, 0.3, clamp(speed / 6.0, 0.0, 1.0))
                    : mix(1.0 - feedback, 0.25, clamp(speed / 3.0, 0.0, 1.0));
  if (any(lessThan(huv, vec2(0.0))) || any(greaterThan(huv, vec2(1.0))) || (!own && cp.w <= 0.0))
    alpha = 1.0;
  // The depth the history was drawn with, round where it is read: this point
  // must lie within it (with some slack), or the history shows something else.
  float expect = linear_depth(cp.z / cp.w * 0.5 + 0.5);
  if (own) {
    // This pixel's own surface then: background just uncovered beside a moving
    // object takes the object's motion (the front-most round it) but must not
    // take its history.
    vec4 sv = texture(velTex, uv);
    vec4 c0 = prevViewProj * vec4(world_at(uv, texture(depthTex, uv).r), 1.0);
    expect = sv.a > 0.5 ? sv.z : (c0.w > 0.0 ? c0.w : expect);
  }
  float lo_d = 1e30, hi_d = 0.0;
  for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++) {
      float l = linear_depth(texture(prevDepthTex, huv + vec2(float(x), float(y)) * texel).r);
      lo_d = min(lo_d, l);
      hi_d = max(hi_d, l);
    }
  if (near_d < 1.0 && (expect < lo_d * 0.97 - 0.05 || expect > hi_d * 1.03 + 0.05))
    alpha = 1.0;
  vec3 sigma = sqrt(max(m2 - m1 * m1, vec3(0.0)));
  float gamma = own ? mix(1.25, 1.0, clamp(speed / 4.0, 0.0, 1.0)) : mix(1.25, 0.75, clamp(speed / 4.0, 0.0, 1.0));
  vec3 hist = to_ycocg(speed < 0.01 ? texture(historyTex, huv).rgb : history_at(huv));
  hist = clip_box(m1 - gamma * sigma, m1 + gamma * sigma, hist);
  vec3 res = mix(from_ycocg(hist), cur, alpha);
  finalColor = vec4(max(res, vec3(0.0)), 1.0);
}
)";

// TAA output: the new history sharpened back (a resolve averages sub-pixel
// samples, which softens), into the world image with its alpha kept.
constexpr const char *taa_out_fs = R"(#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform sampler2D alphaTex;
uniform vec2 texel;
uniform float sharpen;
void main() {
  vec2 uv = fragTexCoord;
  vec3 c = texture(texture0, uv).rgb;
  vec3 n = texture(texture0, uv + vec2(0.0, texel.y)).rgb + texture(texture0, uv - vec2(0.0, texel.y)).rgb +
           texture(texture0, uv + vec2(texel.x, 0.0)).rgb + texture(texture0, uv - vec2(texel.x, 0.0)).rgb;
  vec3 s = c + (c * 4.0 - n) * sharpen * 0.25;
  finalColor = vec4(clamp(s, 0.0, 1.0), texture(alphaTex, uv).a);
}
)";

// A decal: a box drawn by its back faces over the image. Each pixel's world
// position from the depth copy, brought into the box; outside it, nothing.
constexpr const char *decal_vs = R"(#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
void main() { gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";

constexpr const char *decal_fs = R"(#version 330
uniform sampler2D texture0;
uniform sampler2D depthTex;
uniform mat4 invViewProj;
uniform mat4 toLocal;
uniform vec2 targetSize;
uniform vec4 color;
uniform vec4 source;
uniform int hasTexture;
uniform int mode;
uniform float fade;
uniform float angleFade;
uniform vec3 axis;
uniform vec3 eyePos;
uniform vec3 sunDir;
uniform vec3 sunColor;
uniform vec3 ambient;
out vec4 finalColor;
void main() {
  vec2 uv = gl_FragCoord.xy / targetSize;
  float d = texture(depthTex, uv).r;
  if (d >= 1.0)
    discard;
  vec4 w = invViewProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
  vec3 p = w.xyz / w.w;
  vec3 l = (toLocal * vec4(p, 1.0)).xyz;
  if (any(greaterThan(abs(l), vec3(0.5))))
    discard;
  vec3 n = normalize(cross(dFdx(p), dFdy(p)));
  if (dot(n, eyePos - p) < 0.0)
    n = -n;
  float k = smoothstep(angleFade, angleFade + 0.2, dot(n, axis));
  k *= 1.0 - smoothstep(0.35, 0.5, abs(l.y));
  vec2 tuv = l.xz + 0.5;
  vec4 c = color;
  if (hasTexture == 1)
    c *= texture(texture0, source.xy + tuv * source.zw);
  else
    c.a *= 1.0 - smoothstep(0.3, 0.5, length(tuv - 0.5));
  float a = clamp(c.a * k * fade, 0.0, 1.0);
  if (a <= 0.002)
    discard;
  if (mode == 0) {
    finalColor = vec4(mix(vec3(1.0), c.rgb, a), 1.0);
  } else {
    vec3 light = ambient + sunColor * max(dot(n, -normalize(sunDir)), 0.0);
    finalColor = vec4(c.rgb * light, a);
  }
}
)";

Shader load_fullscreen(const char *main) {
  const std::string fs = std::string(common_glsl) + main;
  return LoadShaderFromMemory(nullptr, fs.c_str());
}

bool load(post3d_state &st) {
  if (st.loaded || st.failed)
    return st.loaded;
  st.ssao = load_fullscreen(ssao_main);
  st.ssao_blur = load_fullscreen(ssao_blur_main);
  st.ssao_apply = load_fullscreen(ssao_apply_main);
  st.ssr = load_fullscreen(ssr_main);
  st.shafts_sky = load_fullscreen(shafts_sky_main);
  st.shafts_blur = load_fullscreen(shafts_blur_main);
  st.flare_vis = load_fullscreen(flare_vis_main);
  st.blur = load_fullscreen(blur_main);
  st.add = LoadShaderFromMemory(nullptr, add_fs);
  st.flare = LoadShaderFromMemory(nullptr, flare_fs);
  st.decal = LoadShaderFromMemory(decal_vs, decal_fs);
  st.taa = load_fullscreen(taa_main);
  st.taa_out = LoadShaderFromMemory(nullptr, taa_out_fs);
  for (const Shader *s : {&st.ssao, &st.ssao_blur, &st.ssao_apply, &st.ssr, &st.shafts_sky, &st.shafts_blur,
                          &st.flare_vis, &st.blur, &st.add, &st.flare, &st.decal, &st.taa, &st.taa_out})
    if (!IsShaderValid(*s)) {
      NJIN_WARN("post3d: built-in shaders failed to compile; 3D screen effects and decals are off");
      st.failed = true;
      return false;
    }
  st.loaded = true;
  return true;
}

void free_copy(post3d_state &st) {
  if (st.copy_fbo != 0)
    rlUnloadFramebuffer(st.copy_fbo);
  if (st.copy_color != 0)
    rlUnloadTexture(st.copy_color);
  if (st.copy_depth != 0)
    rlUnloadTexture(st.copy_depth);
  st.copy_fbo = st.copy_color = st.copy_depth = 0;
  st.w = st.h = 0;
}

// The copy of the world target: colour (filtered, for reflections) and a
// depth texture of the same format as the target's, so a blit can fill it.
bool ensure_copy(post3d_state &st, i32 w, i32 h) {
  if (st.copy_fbo != 0 && st.w == w && st.h == h)
    return true;
  free_copy(st);
  st.copy_fbo = rlLoadFramebuffer();
  st.copy_color = rlLoadTexture(nullptr, w, h, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
  st.copy_depth = rlLoadTextureDepth(w, h, false);
  rlTextureParameters(st.copy_color, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
  rlTextureParameters(st.copy_color, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
  for (const u32 t : {st.copy_color, st.copy_depth}) {
    rlTextureParameters(t, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
    rlTextureParameters(t, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
  }
  rlFramebufferAttach(st.copy_fbo, st.copy_color, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
  rlFramebufferAttach(st.copy_fbo, st.copy_depth, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
  if (!rlFramebufferComplete(st.copy_fbo)) {
    NJIN_WARN("post3d: copy framebuffer incomplete; 3D screen effects and decals are off");
    free_copy(st);
    st.failed = true;
    return false;
  }
  st.w = w;
  st.h = h;
  return true;
}

bool ensure_target(RenderTexture2D &t, i32 w, i32 h) {
  if (IsRenderTextureValid(t) && t.texture.width == w && t.texture.height == h)
    return true;
  if (IsRenderTextureValid(t))
    UnloadRenderTexture(t);
  t = LoadRenderTexture(w, h);
  if (!IsRenderTextureValid(t))
    return false;
  SetTextureFilter(t.texture, TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(t.texture, TEXTURE_WRAP_CLAMP);
  return true;
}

void blit(u32 from, u32 to, i32 w, i32 h, i32 bits) {
  rlDrawRenderBatchActive();
  rlBindFramebuffer(RL_READ_FRAMEBUFFER, from);
  rlBindFramebuffer(RL_DRAW_FRAMEBUFFER, to);
  rlBlitFramebuffer(0, 0, w, h, 0, 0, w, h, bits);
  rlDisableFramebuffer();
}

// Binds `fbo` (w x h) for full-screen drawing: pixel projection, no depth test.
void bind_2d(u32 fbo, i32 w, i32 h) {
  rlDrawRenderBatchActive();
  rlEnableFramebuffer(fbo);
  rlViewport(0, 0, w, h);
  rlSetFramebufferWidth(w);
  rlSetFramebufferHeight(h);
  rlMatrixMode(RL_PROJECTION);
  rlLoadIdentity();
  rlOrtho(0, w, h, 0, 0.0, 1.0);
  rlMatrixMode(RL_MODELVIEW);
  rlLoadIdentity();
  rlDisableDepthTest();
}

Texture2D texture_of(u32 id, i32 w, i32 h) {
  return Texture2D{.id = id, .width = w, .height = h, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
}

// One quad over the bound target (`w` x `h`) through `sh`, with `tex` as
// texture0; extra textures bound after the shader, as the batch wants them.
template <typename Fn> void quad(Shader sh, const Texture2D &tex, i32 w, i32 h, Fn &&samplers) {
  BeginShaderMode(sh);
  samplers();
  DrawTexturePro(tex, Rectangle{0.0f, 0.0f, (f32)tex.width, -(f32)tex.height}, Rectangle{0.0f, 0.0f, (f32)w, (f32)h},
                 Vector2{0.0f, 0.0f}, 0.0f, WHITE);
  EndShaderMode();
  rlDrawRenderBatchActive();
}

void set_f(Shader s, const char *name, f32 v) {
  SetShaderValue(s, GetShaderLocation(s, name), &v, SHADER_UNIFORM_FLOAT);
}
void set_i(Shader s, const char *name, i32 v) { SetShaderValue(s, GetShaderLocation(s, name), &v, SHADER_UNIFORM_INT); }
void set_v2(Shader s, const char *name, vec2 v) {
  SetShaderValue(s, GetShaderLocation(s, name), &v, SHADER_UNIFORM_VEC2);
}
void set_v3(Shader s, const char *name, vec3 v) {
  SetShaderValue(s, GetShaderLocation(s, name), &v, SHADER_UNIFORM_VEC3);
}
void set_v4(Shader s, const char *name, vec4 v) {
  SetShaderValue(s, GetShaderLocation(s, name), &v, SHADER_UNIFORM_VEC4);
}
void set_m(Shader s, const char *name, const Matrix &m) { SetShaderValueMatrix(s, GetShaderLocation(s, name), m); }
// rlSetUniformSampler writes to the bound program; inside BeginShaderMode the
// batch has not bound ours yet.
void sampler(Shader s, const char *name, u32 id) {
  rlEnableShader(s.id);
  rlSetUniformSampler(GetShaderLocation(s, name), id);
}

// The pass's view: what the full-screen passes need to go from the depth to
// the world and back.
struct pass_view {
  Matrix view{}, proj{}, view_proj{}, inv{};
  vec2 planes{};
  vec3 eye{};
  i32 w = 0, h = 0;
  u32 target = 0;
};

void set_view(Shader s, const pass_view &v) {
  set_m(s, "invViewProj", v.inv);
  set_m(s, "viewProj", v.view_proj);
  set_v2(s, "planes", v.planes);
  set_v2(s, "texel", {1.0f / (f32)v.w, 1.0f / (f32)v.h});
  set_v3(s, "eyePos", v.eye);
}

// Binds the world target with the pass's camera, for drawing in 3D.
void bind_3d(const pass_view &v) {
  rlDrawRenderBatchActive();
  rlEnableFramebuffer(v.target);
  rlViewport(0, 0, v.w, v.h);
  rlSetFramebufferWidth(v.w);
  rlSetFramebufferHeight(v.h);
  rlMatrixMode(RL_PROJECTION);
  rlLoadIdentity();
  rlMultMatrixf(MatrixToFloat(v.proj));
  rlMatrixMode(RL_MODELVIEW);
  rlLoadIdentity();
  rlMultMatrixf(MatrixToFloat(v.view));
}

pass_view current_view(context &ctx) {
  const render3d_state &s = ctx.render3d;
  pass_view v;
  v.view = rlGetMatrixModelview();
  v.proj = rlGetMatrixProjection();
  v.view_proj = MatrixMultiply(v.view, v.proj);
  v.inv = MatrixInvert(v.view_proj);
  v.planes = {s.camera.near_plane, s.camera.far_plane};
  v.eye = s.camera.position;
  v.target = ctx.post.target.id;
  v.w = ctx.post.target.texture.width;
  v.h = ctx.post.target.texture.height;
  return v;
}

Matrix decal_matrix(const decal3d_desc &d) {
  const vec3 r = d.rotation * (PI / 180.0f);
  Matrix m = MatrixScale(d.size.x, d.size.y, d.size.z);
  m = MatrixMultiply(m, MatrixRotateZ(r.z));
  m = MatrixMultiply(m, MatrixRotateX(r.x));
  m = MatrixMultiply(m, MatrixRotateY(r.y));
  return MatrixMultiply(m, MatrixTranslate(d.position.x, d.position.y, d.position.z));
}

f32 decal_fade(const post3d_decal &d) {
  if (d.desc.lifetime <= 0.0f)
    return 1.0f;
  const f32 left = d.desc.lifetime - d.age;
  return d.desc.fade > 0.0f ? clamp(left / d.desc.fade, 0.0f, 1.0f) : (left > 0.0f ? 1.0f : 0.0f);
}

void draw_decals(context &ctx, post3d_state &st, const pass_view &v) {
  if (st.live == 0)
    return;
  const render3d_state &s = ctx.render3d;
  bind_3d(v);
  rlDisableDepthTest();
  rlDisableDepthMask();
  rlEnableBackfaceCulling();
  rlSetCullFace(RL_CULL_FACE_FRONT);
  Shader sh = st.decal;
  set_m(sh, "invViewProj", v.inv);
  set_v2(sh, "targetSize", {(f32)v.w, (f32)v.h});
  set_v3(sh, "eyePos", v.eye);
  set_v3(sh, "sunDir", s.light.direction);
  set_v3(sh, "sunColor", {s.light.color.r, s.light.color.g, s.light.color.b});
  set_v3(sh, "ambient", {s.light.ambient.r, s.light.ambient.g, s.light.ambient.b});
  set_i(sh, "depthTex", decal_depth_unit);
  rlActiveTextureSlot(decal_depth_unit);
  rlEnableTexture(st.copy_depth);
  rlActiveTextureSlot(0);
  std::array<MaterialMap, 12> maps{};
  Material material{};
  material.shader = sh;
  material.maps = maps.data();
  // Oldest first, so a newer decal lies on top.
  std::vector<const post3d_decal *> order;
  for (const post3d_decal &d : st.decals)
    if (d.alive)
      order.push_back(&d);
  std::sort(order.begin(), order.end(), [](const post3d_decal *a, const post3d_decal *b) { return a->order < b->order; });
  i32 mode = -1;
  for (const post3d_decal *d : order) {
    const f32 fade = decal_fade(*d);
    if (fade <= 0.0f)
      continue;
    const decal3d_desc &desc = d->desc;
    Texture2D image{.id = rlGetTextureIdDefault(), .width = 1, .height = 1, .mipmaps = 1,
                    .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    bool has_image = false;
    const texture_slot *slot = texture_slot_of(ctx.texture, desc.texture);
    if (slot != nullptr && !slot->packed && slot->texture.id != 0) {
      image = slot->texture;
      has_image = true;
    }
    vec4 source{0.0f, 0.0f, 1.0f, 1.0f};
    if (has_image && desc.source.size.x > 0.0f && desc.source.size.y > 0.0f)
      source = {desc.source.pos.x / (f32)image.width, desc.source.pos.y / (f32)image.height,
                desc.source.size.x / (f32)image.width, desc.source.size.y / (f32)image.height};
    maps[MATERIAL_MAP_DIFFUSE].texture = image;
    maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    const Matrix model = decal_matrix(desc);
    const vec3 axis = normalize(vec3{model.m4, model.m5, model.m6});
    if (mode != (i32)desc.blend) {
      rlDrawRenderBatchActive();
      if (desc.blend == decal3d_multiply) {
        BeginBlendMode(BLEND_MULTIPLIED);
      } else {
        BeginBlendMode(BLEND_ALPHA);
      }
      mode = (i32)desc.blend;
    }
    set_m(sh, "toLocal", d->to_local);
    set_v4(sh, "color", {desc.color.r, desc.color.g, desc.color.b, desc.color.a});
    set_v4(sh, "source", source);
    set_i(sh, "hasTexture", has_image ? 1 : 0);
    set_i(sh, "mode", desc.blend == decal3d_multiply ? 0 : 1);
    set_f(sh, "fade", fade);
    set_f(sh, "angleFade", clamp(desc.angle_fade, 0.0f, 1.0f));
    set_v3(sh, "axis", axis);
    DrawMesh(s.cube, material, model);
  }
  EndBlendMode();
  rlSetCullFace(RL_CULL_FACE_BACK);
  rlEnableDepthMask();
  rlActiveTextureSlot(decal_depth_unit);
  rlDisableTexture();
  rlActiveTextureSlot(0);
  ctx.stats.post_passes += 1;
}

void run_ssao(context &ctx, post3d_state &st, const pass_view &v) {
  const post3d &p = st.settings;
  const i32 aw = p.ssao_half ? std::max(v.w / 2, 1) : v.w;
  const i32 ah = p.ssao_half ? std::max(v.h / 2, 1) : v.h;
  if (!ensure_target(st.ao_a, aw, ah) || !ensure_target(st.ao_b, aw, ah))
    return;
  const Texture2D depth = texture_of(st.copy_depth, v.w, v.h);
  // Occlusion.
  Shader sh = st.ssao;
  set_view(sh, v);
  set_f(sh, "radius", std::max(p.ssao_radius, 0.01f));
  set_i(sh, "samples", std::clamp(p.ssao_samples, 4, 32));
  bind_2d(st.ao_a.id, aw, ah);
  quad(sh, depth, aw, ah, [&] { sampler(sh, "depthTex", st.copy_depth); });
  // Blur across, then down.
  Shader b = st.ssao_blur;
  set_view(b, v);
  set_v2(b, "direction", {1.0f / (f32)aw, 0.0f});
  bind_2d(st.ao_b.id, aw, ah);
  quad(b, st.ao_a.texture, aw, ah, [&] { sampler(b, "depthTex", st.copy_depth); });
  set_v2(b, "direction", {0.0f, 1.0f / (f32)ah});
  bind_2d(st.ao_a.id, aw, ah);
  quad(b, st.ao_b.texture, aw, ah, [&] { sampler(b, "depthTex", st.copy_depth); });
  // Multiplied into the image.
  Shader a = st.ssao_apply;
  set_view(a, v);
  set_v2(a, "aoTexel", {1.0f / (f32)aw, 1.0f / (f32)ah});
  set_f(a, "strength", clamp(p.ssao, 0.0f, 1.0f));
  bind_2d(v.target, v.w, v.h);
  BeginBlendMode(BLEND_MULTIPLIED);
  quad(a, st.ao_a.texture, v.w, v.h, [&] { sampler(a, "depthTex", st.copy_depth); });
  EndBlendMode();
  ctx.stats.post_passes += 4;
}

void run_ssr(context &ctx, post3d_state &st, const pass_view &v) {
  const post3d &p = st.settings;
  if (!ensure_target(st.mask, v.w, v.h))
    return;
  // Which pixels reflect, and how much.
  bind_3d(v);
  rlEnableFramebuffer(st.mask.id);
  rlClearColor(0, 0, 0, 0);
  rlClearScreenBuffers();
  rlDisableDepthTest();
  if (!render3d_draw_reflectors(ctx, st.copy_depth, {(f32)v.w, (f32)v.h}, v.planes))
    return;
  // The image as it is now (with decals and occlusion), to reflect.
  blit(v.target, st.copy_fbo, v.w, v.h, color_bit);
  const light3d &light = ctx.render3d.light;
  Shader sh = st.ssr;
  set_view(sh, v);
  set_f(sh, "strength", clamp(p.ssr, 0.0f, 1.0f));
  set_f(sh, "maxDistance", std::max(p.ssr_distance, 0.1f));
  set_i(sh, "steps", std::clamp(p.ssr_steps, 8, 128));
  set_f(sh, "thickness", std::max(p.ssr_thickness, 0.01f));
  set_v4(sh, "skyColor", {light.fog_color.r, light.fog_color.g, light.fog_color.b, clamp(p.ssr_sky, 0.0f, 1.0f)});
  bind_2d(v.target, v.w, v.h);
  BeginBlendMode(BLEND_ALPHA);
  quad(sh, texture_of(st.copy_color, v.w, v.h), v.w, v.h, [&] {
    sampler(sh, "depthTex", st.copy_depth);
    sampler(sh, "maskTex", st.mask.texture.id);
  });
  EndBlendMode();
  ctx.stats.post_passes += 2;
}

// Where the sun is on screen (0..1, y up as the textures), and how much it is
// in view: 1 inside the frame, fading to 0 a quarter screen outside. False
// when it is behind the camera or below the horizon.
bool sun_on_screen(const context &ctx, const pass_view &v, vec2 &uv, f32 &on_screen) {
  const vec3 dir = normalize(ctx.render3d.light.direction);
  const vec3 to_sun = dir * -1.0f;
  if (to_sun.y < -0.05f)
    return false;
  const Matrix &m = v.view_proj;
  const f32 cx = m.m0 * to_sun.x + m.m4 * to_sun.y + m.m8 * to_sun.z;
  const f32 cy = m.m1 * to_sun.x + m.m5 * to_sun.y + m.m9 * to_sun.z;
  const f32 cw = m.m3 * to_sun.x + m.m7 * to_sun.y + m.m11 * to_sun.z;
  if (cw <= 1e-4f)
    return false;
  uv = {cx / cw * 0.5f + 0.5f, cy / cw * 0.5f + 0.5f};
  const f32 out = std::max({-uv.x, uv.x - 1.0f, -uv.y, uv.y - 1.0f, 0.0f});
  on_screen = 1.0f - std::clamp(out / 0.25f, 0.0f, 1.0f);
  on_screen = on_screen * on_screen * (3.0f - 2.0f * on_screen);
  return on_screen > 0.0f;
}

void run_shafts(context &ctx, post3d_state &st, const pass_view &v, vec2 sun, f32 on_screen) {
  const post3d &p = st.settings;
  const i32 hw = std::max(v.w / 2, 1);
  const i32 hh = std::max(v.h / 2, 1);
  if (!ensure_target(st.shafts_a, hw, hh) || !ensure_target(st.shafts_b, hw, hh))
    return;
  const f32 aspect = (f32)v.w / (f32)v.h;
  Shader sky = st.shafts_sky;
  set_view(sky, v);
  set_v2(sky, "sun", sun);
  set_f(sky, "aspect", aspect);
  bind_2d(st.shafts_a.id, hw, hh);
  quad(sky, texture_of(st.copy_depth, v.w, v.h), hw, hh, [&] { sampler(sky, "depthTex", st.copy_depth); });
  Shader blur = st.shafts_blur;
  set_v2(blur, "sun", sun);
  set_f(blur, "length", clamp(p.shafts_length, 0.0f, 1.0f));
  bind_2d(st.shafts_b.id, hw, hh);
  quad(blur, st.shafts_a.texture, hw, hh, [] {});
  const light3d &light = ctx.render3d.light;
  const f32 k = std::max(p.shafts, 0.0f) * on_screen;
  set_v4(st.add, "tint", {p.shafts_color.r * light.color.r * k, p.shafts_color.g * light.color.g * k,
                          p.shafts_color.b * light.color.b * k, 1.0f});
  bind_2d(v.target, v.w, v.h);
  BeginBlendMode(BLEND_ADDITIVE);
  quad(st.add, st.shafts_b.texture, v.w, v.h, [] {});
  EndBlendMode();
  ctx.stats.post_passes += 3;
}

void run_flare(context &ctx, post3d_state &st, const pass_view &v, vec2 sun, f32 on_screen) {
  const post3d &p = st.settings;
  if (!ensure_target(st.visible, 1, 1))
    return;
  const f32 aspect = (f32)v.w / (f32)v.h;
  Shader vis = st.flare_vis;
  set_view(vis, v);
  set_v2(vis, "sun", sun);
  set_f(vis, "aspect", aspect);
  set_f(vis, "onScreen", on_screen);
  bind_2d(st.visible.id, 1, 1);
  quad(vis, texture_of(st.copy_depth, v.w, v.h), 1, 1, [&] { sampler(vis, "depthTex", st.copy_depth); });
  Shader sh = st.flare;
  const light3d &light = ctx.render3d.light;
  set_v2(sh, "sun", sun);
  set_f(sh, "aspect", aspect);
  set_f(sh, "strength", std::max(p.flare, 0.0f));
  set_f(sh, "halo", std::max(p.flare_halo, 0.0f));
  set_v3(sh, "sunColor", {light.color.r, light.color.g, light.color.b});
  bind_2d(v.target, v.w, v.h);
  BeginBlendMode(BLEND_ADDITIVE);
  quad(sh, texture_of(st.copy_color, v.w, v.h), v.w, v.h, [&] { sampler(sh, "visTex", st.visible.texture.id); });
  EndBlendMode();
  ctx.stats.post_passes += 2;
}

void run_motion_blur(context &ctx, post3d_state &st, const pass_view &v, bool vel) {
  const post3d &p = st.settings;
  blit(v.target, st.copy_fbo, v.w, v.h, color_bit);
  Shader sh = st.blur;
  set_view(sh, v);
  set_m(sh, "prevViewProj", st.prev_view_proj);
  set_f(sh, "strength", clamp(p.motion_blur, 0.0f, 1.0f));
  set_i(sh, "samples", std::clamp(p.motion_blur_samples, 2, 32));
  set_f(sh, "maxLength", 0.06f);
  set_f(sh, "hasVel", vel ? 1.0f : 0.0f);
  bind_2d(v.target, v.w, v.h);
  // Replaces the image, alpha included.
  rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
  BeginBlendMode(BLEND_CUSTOM);
  quad(sh, texture_of(st.copy_color, v.w, v.h), v.w, v.h, [&] {
    sampler(sh, "depthTex", st.copy_depth);
    sampler(sh, "velTex", vel ? st.vel_tex : st.copy_depth);
  });
  EndBlendMode();
  ctx.stats.post_passes += 1;
}

void free_velocity(post3d_state &st) {
  if (st.vel_fbo != 0)
    rlUnloadFramebuffer(st.vel_fbo);
  if (st.vel_tex != 0)
    rlUnloadTexture(st.vel_tex);
  st.vel_fbo = st.vel_tex = 0;
  st.vel_w = st.vel_h = 0;
}

// The motion vectors' target (half float, read unfiltered), the image's size.
bool ensure_velocity_target(post3d_state &st, i32 w, i32 h) {
  if (st.vel_fbo != 0 && st.vel_w == w && st.vel_h == h)
    return true;
  free_velocity(st);
  st.vel_fbo = rlLoadFramebuffer();
  st.vel_tex = rlLoadTexture(nullptr, w, h, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
  rlTextureParameters(st.vel_tex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
  rlTextureParameters(st.vel_tex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
  rlTextureParameters(st.vel_tex, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
  rlTextureParameters(st.vel_tex, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
  rlFramebufferAttach(st.vel_fbo, st.vel_tex, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
  if (!rlFramebufferComplete(st.vel_fbo)) {
    NJIN_WARN("post3d: motion vector framebuffer incomplete; TAA and motion blur follow the camera only");
    free_velocity(st);
    st.vel_failed = true;
    return false;
  }
  st.vel_w = w;
  st.vel_h = h;
  return true;
}

Matrix unjittered(const pass_view &v);

// The motion vectors of the pass's meshes and models since the last motion
// pass (`prev_vp` its view-projection, unjittered). False when they could not
// be drawn: TAA and motion blur then use the camera's motion everywhere.
bool run_velocity(context &ctx, post3d_state &st, const pass_view &v, const Matrix &prev_vp) {
  if (st.vel_failed || !ensure_velocity_target(st, v.w, v.h))
    return false;
  const bool has_last = st.motion_time >= 0.0f && ctx.time.elapsed - st.motion_time < 0.25f;
  bind_3d(v);
  rlEnableFramebuffer(st.vel_fbo);
  rlClearColor(0, 0, 0, 0);
  rlClearScreenBuffers();
  rlDisableDepthTest();
  const bool ok = render3d_draw_velocity(ctx, st.copy_depth, {(f32)v.w, (f32)v.h}, v.planes, unjittered(v), prev_vp,
                                         has_last);
  ctx.stats.post_passes += 1;
  return ok;
}

void free_history(post3d_state &st) {
  for (i32 i = 0; i < 2; i++) {
    if (st.hist_fbo[i] != 0)
      rlUnloadFramebuffer(st.hist_fbo[i]);
    if (st.hist_tex[i] != 0)
      rlUnloadTexture(st.hist_tex[i]);
    st.hist_fbo[i] = st.hist_tex[i] = 0;
  }
  if (st.hdepth_fbo != 0)
    rlUnloadFramebuffer(st.hdepth_fbo);
  for (const u32 t : {st.hdepth_color, st.hdepth_tex})
    if (t != 0)
      rlUnloadTexture(t);
  st.hdepth_fbo = st.hdepth_color = st.hdepth_tex = 0;
  st.hist_w = st.hist_h = 0;
  st.has_history = false;
}

void filter_clamp(u32 t, i32 filter) {
  rlTextureParameters(t, RL_TEXTURE_MIN_FILTER, filter);
  rlTextureParameters(t, RL_TEXTURE_MAG_FILTER, filter);
  rlTextureParameters(t, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
  rlTextureParameters(t, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
}

// The two history images (half float, so a 10% blend each frame does not
// band) and the last depth, at the target's size; a new size starts over.
bool ensure_history(post3d_state &st, i32 w, i32 h) {
  if (st.hist_fbo[0] != 0 && st.hist_w == w && st.hist_h == h)
    return true;
  free_history(st);
  for (i32 i = 0; i < 2; i++) {
    st.hist_fbo[i] = rlLoadFramebuffer();
    st.hist_tex[i] = rlLoadTexture(nullptr, w, h, RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1);
    filter_clamp(st.hist_tex[i], RL_TEXTURE_FILTER_LINEAR);
    rlFramebufferAttach(st.hist_fbo[i], st.hist_tex[i], RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
  }
  st.hdepth_fbo = rlLoadFramebuffer();
  st.hdepth_color = rlLoadTexture(nullptr, w, h, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
  st.hdepth_tex = rlLoadTextureDepth(w, h, false);
  filter_clamp(st.hdepth_tex, RL_TEXTURE_FILTER_NEAREST);
  rlFramebufferAttach(st.hdepth_fbo, st.hdepth_color, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
  rlFramebufferAttach(st.hdepth_fbo, st.hdepth_tex, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
  if (!rlFramebufferComplete(st.hist_fbo[0]) || !rlFramebufferComplete(st.hist_fbo[1]) ||
      !rlFramebufferComplete(st.hdepth_fbo)) {
    NJIN_WARN("post3d: TAA history framebuffers incomplete; TAA is off");
    free_history(st);
    return false;
  }
  st.hist_w = w;
  st.hist_h = h;
  return true;
}

// The projection without TAA's jitter: load_camera's frustum is symmetric but
// for it, which shows only in these two terms.
Matrix unjittered(const pass_view &v) {
  Matrix proj = v.proj;
  proj.m8 = 0.0f;
  proj.m9 = 0.0f;
  return MatrixMultiply(v.view, proj);
}

void run_taa(context &ctx, post3d_state &st, const pass_view &v, bool vel) {
  const post3d &p = st.settings;
  if (!ensure_history(st, v.w, v.h))
    return;
  const Matrix vp = unjittered(v);
  const camera3d &cam = ctx.render3d.camera;
  const vec3 dir = normalize(cam.target - cam.position);
  const f64 now = GetTime();
  // A cut (the camera jumped or turned sharply), or no resolved pass a moment
  // ago: the old image would show where nothing is now.
  const vec3 moved = cam.position - st.hist_eye;
  const bool cut = !st.has_history || now - st.hist_time > 0.25 ||
                   moved.x * moved.x + moved.y * moved.y + moved.z * moved.z > 9.0f ||
                   dir.x * st.hist_dir.x + dir.y * st.hist_dir.y + dir.z * st.hist_dir.z < 0.9f;
  blit(v.target, st.copy_fbo, v.w, v.h, color_bit);
  const i32 next = 1 - st.hist_cur;
  Shader sh = st.taa;
  set_view(sh, v);
  set_m(sh, "viewProjUnj", vp);
  set_m(sh, "prevViewProj", st.hist_view_proj);
  set_f(sh, "reset", cut ? 1.0f : 0.0f);
  set_f(sh, "feedback", 0.9f);
  set_f(sh, "hasVel", vel ? 1.0f : 0.0f);
  bind_2d(st.hist_fbo[next], v.w, v.h);
  rlSetBlendFactors(RL_ONE, RL_ZERO, RL_FUNC_ADD);
  BeginBlendMode(BLEND_CUSTOM);
  quad(sh, texture_of(st.copy_color, v.w, v.h), v.w, v.h, [&] {
    sampler(sh, "depthTex", st.copy_depth);
    sampler(sh, "historyTex", st.hist_tex[st.hist_cur]);
    sampler(sh, "prevDepthTex", st.hdepth_tex);
    sampler(sh, "velTex", vel ? st.vel_tex : st.hdepth_tex);
  });
  // Back into the world image, sharpened, its alpha kept.
  Shader out = st.taa_out;
  set_v2(out, "texel", {1.0f / (f32)v.w, 1.0f / (f32)v.h});
  set_f(out, "sharpen", clamp(p.taa_sharpen, 0.0f, 1.0f));
  bind_2d(v.target, v.w, v.h);
  quad(out, texture_of(st.hist_tex[next], v.w, v.h), v.w, v.h, [&] { sampler(out, "alphaTex", st.copy_color); });
  EndBlendMode();
  // This pass's depth, for the next one.
  blit(st.copy_fbo, st.hdepth_fbo, v.w, v.h, depth_bit);
  st.hist_cur = next;
  st.hist_view_proj = vp;
  st.hist_eye = cam.position;
  st.hist_dir = dir;
  st.hist_time = now;
  st.has_history = true;
  ctx.stats.post_passes += 2;
}

bool effects_on(const post3d &p) {
  return p.ssao > 0.0f || p.ssr > 0.0f || p.motion_blur > 0.0f || p.shafts > 0.0f || p.flare > 0.0f || p.taa;
}

bool usable(const context &ctx) {
  return ctx.post.drawing && ctx.post.target.depth.id != 0 && ctx.post.target.texture.width > 0;
}

void kill(post3d_state &st, post3d_decal &d) {
  if (!d.alive)
    return;
  d.alive = false;
  st.live--;
}

post3d_decal *decal_of(post3d_state &st, decal3d_handle h) {
  if (h.id == 0 || h.id > st.decals.size())
    return nullptr;
  post3d_decal &d = st.decals[h.id - 1];
  return d.alive && d.gen == h.gen ? &d : nullptr;
}

void remove_oldest(post3d_state &st) {
  post3d_decal *oldest = nullptr;
  for (post3d_decal &d : st.decals)
    if (d.alive && (oldest == nullptr || d.order < oldest->order))
      oldest = &d;
  if (oldest != nullptr)
    kill(st, *oldest);
}

bool finite3(vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
} // namespace

post3d_state::~post3d_state() {
  free_copy(*this);
  free_history(*this);
  free_velocity(*this);
  for (RenderTexture2D *t : {&ao_a, &ao_b, &mask, &shafts_a, &shafts_b, &visible})
    if (IsRenderTextureValid(*t))
      UnloadRenderTexture(*t);
  if (loaded)
    for (Shader *s : {&ssao, &ssao_blur, &ssao_apply, &ssr, &decal, &shafts_sky, &shafts_blur, &add, &flare_vis, &flare,
                      &blur, &taa, &taa_out})
      UnloadShader(*s);
}

void post3d_frame_begin(context &ctx) {
  ctx.post3d.taa_claimed = false;
  ctx.post3d.vel_claimed = false;
}

vec2 post3d_taa_jitter(context &ctx) {
  post3d_state &st = ctx.post3d;
  st.taa_pass = false;
  if (!st.settings.taa || st.failed || st.taa_claimed || !usable(ctx))
    return {};
  st.taa_claimed = true;
  st.taa_pass = true;
  // Halton (2, 3), eight samples, centred on the pixel.
  const auto halton = [](u32 i, u32 base) {
    f32 f = 1.0f, r = 0.0f;
    while (i > 0) {
      f /= (f32)base;
      r += f * (f32)(i % base);
      i /= base;
    }
    return r;
  };
  st.taa_index = st.taa_index % 8 + 1;
  const f32 px = halton(st.taa_index, 2) - 0.5f;
  const f32 py = halton(st.taa_index, 3) - 0.5f;
  const f32 w = (f32)ctx.post.target.texture.width;
  const f32 h = (f32)ctx.post.target.texture.height;
  return {2.0f * px / w, 2.0f * py / h};
}

bool post3d_wanted(const context &ctx) {
  const post3d_state &st = ctx.post3d;
  return !st.failed && (effects_on(st.settings) || st.live > 0);
}

void post3d_after_opaque(context &ctx) {
  post3d_state &st = ctx.post3d;
  if (!usable(ctx) || !load(st))
    return;
  const pass_view v = current_view(ctx);
  if (!ensure_copy(st, v.w, v.h))
    return;
  blit(v.target, st.copy_fbo, v.w, v.h, color_bit | depth_bit);
  draw_decals(ctx, st, v);
  if (st.settings.ssao > 0.0f)
    run_ssao(ctx, st, v);
  if (st.settings.ssr > 0.0f)
    run_ssr(ctx, st, v);
}

void post3d_after_pass(context &ctx) {
  post3d_state &st = ctx.post3d;
  const bool taa = st.taa_pass;
  st.taa_pass = false;
  if (!usable(ctx) || !st.loaded || st.copy_fbo == 0)
    return;
  const pass_view v = current_view(ctx);
  if (v.w != st.w || v.h != st.h)
    return;
  const post3d &p = st.settings;
  // Last pass's view, if it was a moment ago (not a pass from before a pause
  // of the effect or a stall).
  const bool recent = st.has_prev && ctx.time.elapsed - st.prev_time < 0.25f;
  const bool blur = p.motion_blur > 0.0f && recent;
  // Motion vectors once a frame, for the pass TAA resolves or else the first
  // one motion blur smears; kept to compare with next frame's.
  bool vel = false;
  const bool motion = (taa || blur) && !st.vel_claimed;
  if (motion) {
    st.vel_claimed = true;
    vel = run_velocity(ctx, st, v, taa ? st.hist_view_proj : st.prev_view_proj);
  }
  if (taa)
    run_taa(ctx, st, v, vel);
  vec2 sun{};
  f32 on_screen = 0.0f;
  const bool sun_shows = (p.shafts > 0.0f || p.flare > 0.0f) && sun_on_screen(ctx, v, sun, on_screen);
  if (sun_shows && p.shafts > 0.0f)
    run_shafts(ctx, st, v, sun, on_screen);
  if (sun_shows && p.flare > 0.0f)
    run_flare(ctx, st, v, sun, on_screen);
  if (blur)
    run_motion_blur(ctx, st, v, vel);
  if (motion) {
    render3d_motion_commit(ctx);
    st.motion_time = ctx.time.elapsed;
  }
  st.prev_view_proj = unjittered(v);
  st.prev_time = ctx.time.elapsed;
  st.has_prev = true;
}

void post3d_update(context &ctx) {
  post3d_state &st = ctx.post3d;
  if (st.live == 0)
    return;
  const f32 dt = ctx.time.dt;
  for (post3d_decal &d : st.decals) {
    if (!d.alive)
      continue;
    d.age += dt;
    if (d.desc.lifetime > 0.0f && d.age >= d.desc.lifetime)
      kill(st, d);
  }
}

void post3d_set(context &ctx, const post3d &fx) { ctx.post3d.settings = fx; }

post3d post3d_get(const context &ctx) { return ctx.post3d.settings; }

// The box turns z, then x, then y (decal_matrix): x by acos(n.y) takes its y
// axis to (0, n.y, sin), and y by atan2(n.x, n.z) swings that onto n.
vec3 decal3d_rotation(vec3 normal) {
  const f32 len = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
  if (!(len > 1e-6f) || !std::isfinite(len))
    return {};
  const vec3 n{normal.x / len, normal.y / len, normal.z / len};
  const f32 a = std::acos(std::clamp(n.y, -1.0f, 1.0f));
  const f32 b = std::sqrt(n.x * n.x + n.z * n.z) > 1e-5f ? std::atan2(n.x, n.z) : 0.0f;
  return {a * RAD2DEG, b * RAD2DEG, 0.0f};
}

decal3d_handle decal3d_add(context &ctx, const decal3d_desc &desc) {
  if (!finite3(desc.position) || !finite3(desc.rotation) || !finite3(desc.size)) {
    NJIN_WARN("decal3d_add: position, rotation or size is not finite, ignored");
    return {};
  }
  post3d_state &st = ctx.post3d;
  if (st.live >= st.max_decals)
    remove_oldest(st);
  usize slot = st.decals.size();
  for (usize i = 0; i < st.decals.size(); i++)
    if (!st.decals[i].alive) {
      slot = i;
      break;
    }
  if (slot == st.decals.size())
    st.decals.emplace_back();
  post3d_decal &d = st.decals[slot];
  d.desc = desc;
  // A flat box would have no inside to find a surface in.
  d.desc.size = {std::max(std::fabs(desc.size.x), 1e-3f), std::max(std::fabs(desc.size.y), 1e-3f),
                 std::max(std::fabs(desc.size.z), 1e-3f)};
  d.to_local = MatrixInvert(decal_matrix(d.desc));
  d.age = 0.0f;
  d.gen++;
  d.order = st.next_order++;
  d.alive = true;
  st.live++;
  return decal3d_handle{(u32)slot + 1, d.gen};
}

void decal3d_remove(context &ctx, decal3d_handle handle) {
  post3d_state &st = ctx.post3d;
  if (post3d_decal *d = decal_of(st, handle))
    kill(st, *d);
}

void decal3d_clear(context &ctx) {
  post3d_state &st = ctx.post3d;
  for (post3d_decal &d : st.decals)
    kill(st, d);
}

i32 decal3d_count(const context &ctx) { return ctx.post3d.live; }

void decal3d_set_max(context &ctx, i32 max) {
  post3d_state &st = ctx.post3d;
  st.max_decals = std::clamp(max, 1, 4096);
  while (st.live > st.max_decals)
    remove_oldest(st);
}
} // namespace njin
