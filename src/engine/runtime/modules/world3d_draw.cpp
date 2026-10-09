#include "render3d.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_log.h"
#include "njin_texture.h"
#include "njin_world3d_impl.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>
#include <string>
#include <vector>

// The GPU side of the outdoor world (njin_world3d.h): terrain chunks drawn from
// one height texture with a shared grid per level of detail, instanced grass
// blades, Gerstner water, the sky behind everything and rain or snow round the
// camera. Everything lit uses render3d's lighting GLSL (shade(), the sun's
// shadow, lamps, fog), so it matches the rest of the 3D pass.

namespace njin {
namespace {
// Texture units, clear of the shadow maps (14, 15).
constexpr i32 unit_layer = 0;        // 0..3 layer images
constexpr i32 unit_layer_normal = 4; // 4..7 their normal maps
constexpr i32 unit_height = 8;
constexpr i32 unit_normal = 9;
constexpr i32 unit_splat = 10;
constexpr i32 unit_cover = 12;   // render3d's top-down cover depth (rain and snow)

constexpr i32 rain_drops = 9000;
constexpr i32 snow_flakes = 7000;

const char *const terrain_vs = R"(#version 330
in vec3 vertexPosition; // x, z: sample offset in the chunk; y: 1 on the skirt
uniform mat4 mvp;
uniform sampler2D heightMap;  // world heights, one texel per sample
uniform ivec2 chunkOrigin;    // the chunk's first sample
uniform ivec2 sampleLast;     // resolution - 1
uniform vec4 terrainGrid;     // origin x, origin z, spacing, skirt depth
out vec3 fragPos;
out vec2 fragGrid;
void main() {
  ivec2 s = min(chunkOrigin + ivec2(vertexPosition.xz + 0.5), sampleLast);
  float h = texelFetch(heightMap, s, 0).r - vertexPosition.y * terrainGrid.w;
  vec3 world = vec3(terrainGrid.x + float(s.x) * terrainGrid.z, h, terrainGrid.y + float(s.y) * terrainGrid.z);
  fragPos = world;
  fragGrid = vec2(s);
  gl_Position = mvp * vec4(world, 1.0);
}
)";

const char *const terrain_fs_main = R"(
in vec3 fragPos;
in vec2 fragGrid;
uniform sampler2D normalMap;   // the ground's normals, xyz * 0.5 + 0.5
uniform sampler2D splatMap;    // layer weights
uniform sampler2D layer0;
uniform sampler2D layer1;
uniform sampler2D layer2;
uniform sampler2D layer3;
uniform sampler2D layerNormal0;
uniform sampler2D layerNormal1;
uniform sampler2D layerNormal2;
uniform sampler2D layerNormal3;
uniform vec4 layerTile;        // metres per image, per layer
uniform vec4 layerColor[4];
uniform ivec4 layerHasNormal;
uniform int layerCount;
uniform vec2 gridSize;         // samples per side
uniform float wetness;

// A layer's image at this point: from above on gentle ground; on steep ground
// from the three axes (triplanar), so cliffs do not stretch it. A second
// sample at another scale hides the tiling.
vec4 layer_sample(sampler2D s, vec3 p, vec3 n, float tile) {
  vec3 b = pow(abs(n), vec3(4.0));
  b /= b.x + b.y + b.z;
  vec4 c;
  if (b.y > 0.97) {
    c = texture(s, p.xz / tile);
    c = mix(c, texture(s, p.xz / (tile * 3.7) + 0.31), 0.3);
  } else {
    c = texture(s, p.zy / tile) * b.x + texture(s, p.xz / tile) * b.y + texture(s, p.xy / tile) * b.z;
  }
  return c;
}

vec3 layer_normal(sampler2D s, vec3 p, vec3 n, float tile) {
  vec3 m = texture(s, p.xz / tile).xyz * 2.0 - 1.0;
  vec3 t = normalize(vec3(1.0, 0.0, 0.0) - n * n.x);
  vec3 b = cross(n, t); // the image's up (green) runs toward -z
  return normalize(t * m.x + b * m.y + n * max(m.z, 0.2));
}

void main() {
  vec2 uv = (fragGrid + 0.5) / gridSize;
  vec3 n = normalize(texture(normalMap, uv).xyz * 2.0 - 1.0);
  vec4 w = texture(splatMap, uv);
  if (layerCount < 4) w.w = 0.0;
  if (layerCount < 3) w.z = 0.0;
  if (layerCount < 2) w.y = 0.0;
  // A little crisper than the weights' own blend.
  w = pow(max(w, vec4(0.0)), vec4(1.6));
  float sum = w.x + w.y + w.z + w.w;
  w = sum > 1e-4 ? w / sum : vec4(1.0, 0.0, 0.0, 0.0);
  vec3 albedo = vec3(0.0);
  vec3 nd = vec3(0.0);
  if (w.x > 0.004) {
    albedo += layer_sample(layer0, fragPos, n, layerTile.x).rgb * layerColor[0].rgb * w.x;
    nd += (layerHasNormal.x == 1 ? layer_normal(layerNormal0, fragPos, n, layerTile.x) : n) * w.x;
  }
  if (w.y > 0.004) {
    albedo += layer_sample(layer1, fragPos, n, layerTile.y).rgb * layerColor[1].rgb * w.y;
    nd += (layerHasNormal.y == 1 ? layer_normal(layerNormal1, fragPos, n, layerTile.y) : n) * w.y;
  }
  if (w.z > 0.004) {
    albedo += layer_sample(layer2, fragPos, n, layerTile.z).rgb * layerColor[2].rgb * w.z;
    nd += (layerHasNormal.z == 1 ? layer_normal(layerNormal2, fragPos, n, layerTile.z) : n) * w.z;
  }
  if (w.w > 0.004) {
    albedo += layer_sample(layer3, fragPos, n, layerTile.w).rgb * layerColor[3].rgb * w.w;
    nd += (layerHasNormal.w == 1 ? layer_normal(layerNormal3, fragPos, n, layerTile.w) : n) * w.w;
  }
  n = length(nd) > 1e-4 ? normalize(nd) : n;
  // Wet ground is darker, most on the flat where water stands; dry under cover.
  albedo *= mix(1.0, 0.6, wetness * smoothstep(0.6, 0.95, n.y) * (1.0 - covered(fragPos)));
  finalColor = vec4(shade(albedo, n, fragPos, 1.0, vec3(0.0)), 1.0);
}
)";

const char *const terrain_depth_vs = R"(#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
uniform sampler2D heightMap;
uniform ivec2 chunkOrigin;
uniform ivec2 sampleLast;
uniform vec4 terrainGrid;
void main() {
  ivec2 s = min(chunkOrigin + ivec2(vertexPosition.xz + 0.5), sampleLast);
  float h = texelFetch(heightMap, s, 0).r - vertexPosition.y * terrainGrid.w;
  gl_Position = mvp * vec4(terrainGrid.x + float(s.x) * terrainGrid.z, h, terrainGrid.y + float(s.y) * terrainGrid.z, 1.0);
}
)";

const char *const depth_fs = R"(#version 330
out vec4 finalColor;
void main() { finalColor = vec4(1.0); }
)";

// Grass: one blade per instance, bent by the wind, shrinking away at the far
// edge of the field.
const char *const grass_vs_common = R"(
in vec3 vertexPosition;                    // x across the blade (-1..1), y up it (0..1)
layout(location = 12) in vec4 instance0;   // root, height
layout(location = 13) in vec4 instance1;   // facing (radians), width, shade, phase
uniform mat4 mvp;
uniform vec3 eyePos;
uniform float time;
uniform vec2 wind;
uniform float sway;
uniform vec2 fade;                         // start and end of the fade, metres
vec3 blade(out float t, out vec2 face) {
  float d = distance(eyePos.xz, instance0.xz);
  float k = 1.0 - smoothstep(fade.x, fade.y, d);
  float h = instance0.w * k;
  t = vertexPosition.y;
  face = vec2(cos(instance1.x), sin(instance1.x));
  vec3 side = vec3(face.x, 0.0, face.y);
  float speed = length(wind);
  vec2 dir = speed > 1e-3 ? wind / speed : vec2(1.0, 0.0);
  float gust = sin(time * 1.6 + instance1.w + dot(instance0.xz, dir) * 0.3) * 0.5 + 0.5;
  float flutter = sin(time * 4.7 + instance1.w * 3.0) * 0.12;
  float bend = (clamp(speed / 8.0, 0.0, 1.3) * (0.35 + 0.65 * gust) + flutter * clamp(speed / 3.0, 0.0, 1.0)) * sway;
  float t2 = t * t;
  vec3 lean = vec3(dir.x, 0.0, dir.y) * bend * h * t2;
  float drop = 0.5 * bend * bend * h * t2 * t;
  float w = instance1.y * (1.0 - 0.85 * t) * (0.4 + 0.6 * k);
  return instance0.xyz + side * vertexPosition.x * w + vec3(0.0, h * t - drop, 0.0) + lean;
}
)";

const char *const grass_vs_main = R"(
uniform sampler2D normalMap;
uniform vec4 terrainGrid; // origin x, origin z, spacing
uniform vec2 gridSize;
out vec3 fragPos;
out vec3 fragNormal;
out float fragT;
out float fragShade;
void main() {
  float t;
  vec2 face;
  vec3 p = blade(t, face);
  vec2 g = (instance0.xz - terrainGrid.xy) / terrainGrid.z;
  vec3 ground = normalize(texture(normalMap, (g + 0.5) / gridSize).xyz * 2.0 - 1.0);
  // Lit mostly like the ground it grows on: a field reads as one surface.
  fragNormal = normalize(ground * 0.8 + vec3(-face.y, 0.0, face.x) * 0.2 * (fract(instance1.w) - 0.5));
  fragPos = p;
  fragT = t;
  fragShade = instance1.z;
  gl_Position = mvp * vec4(p, 1.0);
}
)";

const char *const grass_depth_vs_main = R"(
void main() {
  float t;
  vec2 face;
  gl_Position = mvp * vec4(blade(t, face), 1.0);
}
)";

const char *const grass_fs_main = R"(
in vec3 fragPos;
in vec3 fragNormal;
in float fragT;
in float fragShade;
uniform vec3 baseColor;
uniform vec3 tipColor;
void main() {
  vec3 c = mix(baseColor, tipColor, fragT) * fragShade;
  c *= mix(0.6, 1.0, smoothstep(0.0, 0.45, fragT)); // the field shades its own roots
  finalColor = vec4(shade(c, normalize(fragNormal), fragPos, 1.0, vec3(0.0)), 1.0);
}
)";

// The sky as njin_world3d.cpp's sky_rgb() computes it, plus clouds and stars.
const char *const sky_glsl = R"(
uniform vec3 skySun;       // towards the sun
uniform vec3 skySunColor;
uniform vec3 skyZenith;
uniform vec3 skyHorizon;
uniform vec3 skyGround;
uniform vec3 skyGlow;
uniform vec4 skyParams;    // twilight, cloud cover, cloud darkness, sun size
uniform vec4 skyCloud;     // cloud height, cloud scale, stars, fog
uniform vec3 skyFogColor;
uniform vec2 skyWind;
uniform float skyTime;
uniform float skyDay;

float sky_hash(vec2 p) {
  p = fract(p * vec2(123.34, 456.21));
  p += dot(p, p + 45.32);
  return fract(p.x * p.y);
}
float sky_noise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(sky_hash(i), sky_hash(i + vec2(1.0, 0.0)), f.x),
             mix(sky_hash(i + vec2(0.0, 1.0)), sky_hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
float sky_fbm(vec2 p) {
  float v = 0.0;
  float a = 0.5;
  for (int i = 0; i < 5; i++) {
    v += sky_noise(p) * a;
    p = p * 2.03 + vec2(17.1, 9.2);
    a *= 0.5;
  }
  return v / 0.96875;
}

vec3 sky_base(vec3 dir) {
  float up = max(dir.y, 0.0);
  vec3 c = mix(skyHorizon, skyZenith, pow(up, 0.5));
  if (dir.y < 0.0)
    c = mix(skyHorizon, skyGround, smoothstep(0.0, 0.25, -dir.y));
  float cosang = dot(dir, skySun);
  float side = pow(max(cosang * 0.5 + 0.5, 0.0), 4.0);
  c += skyGlow * (skyParams.x * side * pow(1.0 - up, 3.0) * 0.8);
  float m = max(cosang, 0.0);
  c += skySunColor * ((pow(m, 600.0) * 1.2 + pow(m, 24.0) * 0.18) * (1.0 - skyParams.y * 0.7));
  float r = 0.0047 * skyParams.w;
  float disc = smoothstep(cos(r * 1.4), cos(r), cosang) * (1.0 - skyParams.y) * smoothstep(-0.02, 0.01, dir.y);
  c += skySunColor * (disc * 4.0);
  return c;
}

// Clouds on a plane `skyCloud.x` above the ground: colour and how much they cover.
vec4 sky_clouds(vec3 dir, vec3 eye) {
  if (dir.y <= 0.01 || skyParams.y <= 0.0)
    return vec4(0.0);
  float t = (skyCloud.x - eye.y) / dir.y;
  if (t <= 0.0)
    return vec4(0.0);
  float scale = 900.0 * skyCloud.y;
  vec2 p = (eye.xz + dir.xz * t + skyWind * skyTime * 3.0) / scale;
  float n = sky_fbm(p);
  float cover = skyParams.y;
  float dens = smoothstep(1.0 - cover - 0.12, 1.0 - cover + 0.3, n);
  dens = max(dens, smoothstep(0.9, 1.0, cover));
  // Thicker towards their middle, lit on the side facing the sun.
  float lit = clamp(n - sky_fbm(p + skySun.xz * 0.06) + 0.5, 0.0, 1.0);
  vec3 dark = skyHorizon * 0.55 + vec3(0.05);
  vec3 light = skyHorizon * 0.4 + skySunColor * 0.75 + vec3(0.1) * skyDay;
  vec3 col = mix(dark, light, lit) * (1.0 - 0.6 * skyParams.z);
  col += skyGlow * skyParams.x * 0.35;
  dens *= smoothstep(0.01, 0.15, dir.y);
  return vec4(col, clamp(dens, 0.0, 1.0));
}

vec3 sky_color(vec3 dir, vec3 eye, bool clouds) {
  dir = normalize(dir);
  vec3 c = sky_base(dir);
  if (skyCloud.z > 0.0 && dir.y > 0.0) {
    vec3 q = dir * 220.0;
    vec3 cell = floor(q);
    float h = sky_hash(cell.xy + cell.z * 7.13);
    float star = step(0.996, h) * (1.0 - smoothstep(0.08, 0.32, length(fract(q) - 0.5)));
    c += vec3(star) * skyCloud.z * (0.6 + 0.4 * sky_hash(cell.zx));
    // The moon, opposite the sun.
    vec3 moon = -skySun;
    float md = dot(dir, moon);
    c += vec3(0.85, 0.88, 0.95) * smoothstep(cos(0.012), cos(0.009), md) * skyCloud.z;
  }
  if (clouds) {
    vec4 cl = sky_clouds(dir, eye);
    c = mix(c, cl.rgb, cl.a);
  }
  float fog = clamp(skyCloud.w * 25.0, 0.0, 1.0);
  c = mix(c, skyFogColor, fog * (1.0 - 0.5 * max(dir.y, 0.0)));
  return min(c, vec3(1.0));
}
)";

const char *const sky_vs = R"(#version 330
in vec3 vertexPosition;
out vec2 fragNdc;
void main() {
  fragNdc = vertexPosition.xy;
  gl_Position = vec4(vertexPosition.xy, 1.0, 1.0); // at the far plane: only where nothing was drawn
}
)";

const char *const sky_fs_main = R"(
in vec2 fragNdc;
uniform mat4 invViewProj;
uniform vec3 eyePos;
out vec4 finalColor;
void main() {
  vec4 p = invViewProj * vec4(fragNdc, 1.0, 1.0);
  vec3 dir = normalize(p.xyz / p.w - eyePos);
  finalColor = vec4(sky_color(dir, eyePos, true), 1.0);
}
)";

// Gerstner waves, the same sums as water_displace() in njin_world3d.cpp.
const char *const waves_glsl = R"(
uniform vec4 waves[8];   // direction xy, wavenumber k, steepness
uniform vec4 waves2[8];  // phase speed, amplitude
uniform int waveCount;
uniform float time;
// `spacing`: how far apart the samples are; waves too short for it fade out.
vec3 gerstner(vec2 p, float spacing, out vec3 normal, out float crest) {
  vec3 off = vec3(0.0);
  vec3 tangent = vec3(1.0, 0.0, 0.0);
  vec3 binormal = vec3(0.0, 0.0, 1.0);
  crest = 0.0;
  float total = 0.0;
  for (int i = 0; i < 8; i++) {
    if (i >= waveCount)
      break;
    vec2 d = waves[i].xy;
    float k = waves[i].z;
    float len = 6.2831853 / k;
    float keep = smoothstep(2.0 * spacing, 4.0 * spacing, len);
    float s = waves[i].w * keep;
    float a = waves2[i].y * keep;
    float f = k * (dot(d, p) - waves2[i].x * time);
    float cs = cos(f);
    float sn = sin(f);
    off += vec3(d.x * a * cs, a * sn, d.y * a * cs);
    tangent += vec3(-d.x * d.x * s * sn, d.x * s * cs, -d.x * d.y * s * sn);
    binormal += vec3(-d.x * d.y * s * sn, d.y * s * cs, -d.y * d.y * s * sn);
    crest += a * sn;
    total += a;
  }
  normal = normalize(cross(binormal, tangent));
  crest = total > 0.0 ? crest / total : 0.0;
  return off;
}
)";

const char *const water_vs_main = R"(
in vec3 vertexPosition; // x, z: rest position from waterOrigin; y: how far apart the vertices are here
uniform mat4 mvp;
uniform vec3 waterOrigin; // x, z offset; y the still level
out vec3 fragPos;
out vec2 fragRest;
void main() {
  vec2 rest = waterOrigin.xz + vertexPosition.xz;
  vec3 n;
  float crest;
  vec3 off = gerstner(rest, vertexPosition.y, n, crest);
  vec3 world = vec3(rest.x + off.x, waterOrigin.y + off.y, rest.y + off.z);
  fragPos = world;
  fragRest = rest;
  gl_Position = mvp * vec4(world, 1.0);
}
)";

const char *const water_fs_main = R"(
in vec3 fragPos;
in vec2 fragRest;
uniform vec3 shallowColor;
uniform vec3 deepColor;
uniform vec3 foamColor;
uniform vec4 waterParams;  // depth fade, clarity, foam width, crest foam
uniform vec3 waterSpec;    // specular, shininess, ripples
uniform int hasGround;
uniform sampler2D groundHeight;
uniform vec4 groundGrid;   // origin x, origin z, spacing
uniform ivec2 groundLast;

float ground_at(vec2 xz) {
  vec2 g = clamp((xz - groundGrid.xy) / groundGrid.z, vec2(0.0), vec2(groundLast));
  ivec2 i = min(ivec2(g), groundLast - 1);
  vec2 f = g - vec2(i);
  float a = texelFetch(groundHeight, i, 0).r;
  float b = texelFetch(groundHeight, i + ivec2(1, 0), 0).r;
  float c = texelFetch(groundHeight, i + ivec2(0, 1), 0).r;
  float d = texelFetch(groundHeight, i + ivec2(1, 1), 0).r;
  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float ripple_noise(vec2 p) { return sky_noise(p); }

void main() {
  float spacing = max(length(fwidth(fragRest)), 0.01);
  vec3 n;
  float crest;
  gerstner(fragRest, spacing * 0.5, n, crest);
  // Small ripples riding on the waves.
  if (waterSpec.z > 0.0) {
    vec2 q = fragRest * 1.7 + vec2(time * 0.35, time * 0.21);
    vec2 r = fragRest * 3.1 - vec2(time * 0.27, -time * 0.31);
    float e = 0.05;
    vec2 grad = vec2(ripple_noise(q + vec2(e, 0.0)) - ripple_noise(q - vec2(e, 0.0)),
                     ripple_noise(q + vec2(0.0, e)) - ripple_noise(q - vec2(0.0, e))) / (2.0 * e);
    grad += 0.5 * vec2(ripple_noise(r + vec2(e, 0.0)) - ripple_noise(r - vec2(e, 0.0)),
                       ripple_noise(r + vec2(0.0, e)) - ripple_noise(r - vec2(0.0, e))) / (2.0 * e);
    float fadek = 1.0 - smoothstep(0.3, 2.0, spacing);
    n = normalize(n - vec3(grad.x, 0.0, grad.y) * 0.06 * waterSpec.z * fadek);
  }
  vec3 v = normalize(viewPos - fragPos);
  if (dot(n, v) < 0.0)
    n = normalize(n + v * (0.01 - dot(n, v)));
  float depth = 1000.0;
  if (hasGround == 1)
    depth = fragPos.y - ground_at(fragPos.xz);
  if (depth < -0.05)
    discard;
  depth = max(depth, 0.0);
  float deep = 1.0 - exp(-depth / max(waterParams.x, 0.01));
  vec3 body = mix(shallowColor, deepColor, deep);
  vec3 l = -lightDir;
  float sun = shadowOn == 1 ? sunlight(fragPos, vec3(0.0, 1.0, 0.0), l) : 1.0;
  vec3 lit = body * (ambient + lightColor * max(dot(n, l), 0.0) * 0.4 * sun);
  vec3 r = reflect(-v, n);
  r.y = abs(r.y);
  vec3 refl = sky_color(r, viewPos, true);
  float fres = 0.02 + 0.98 * pow(1.0 - max(dot(n, v), 0.0), 5.0);
  vec3 col = mix(lit, refl, fres);
  vec3 hv = normalize(l + v);
  col += lightColor * pow(max(dot(n, hv), 0.0), max(waterSpec.y, 1.0)) * waterSpec.x * sun;
  float speck = ripple_noise(fragRest * 2.3 + time * 0.2) * 0.6 + ripple_noise(fragRest * 5.1 - time * 0.3) * 0.4;
  float shore = waterParams.z > 0.0 ? 1.0 - smoothstep(0.0, waterParams.z, depth) : 0.0;
  // A thin line at the water's edge, broken into patches further out.
  float foam = smoothstep(0.55, 0.85, speck * 0.75 + shore * 0.45) * shore;
  foam = max(foam, waterParams.w * smoothstep(0.55, 0.95, crest) * smoothstep(0.35, 0.7, speck));
  foam = clamp(foam, 0.0, 1.0);
  col = mix(col, foamColor * (ambient + lightColor * max(dot(n, l), 0.0) * sun), foam);
  float alpha = clamp(1.0 - exp(-depth / max(waterParams.y, 0.01)), 0.0, 1.0);
  alpha = max(max(alpha, fres), foam);
  if (fogDensity > 0.0) {
    float f = fogDensity * length(viewPos - fragPos);
    col = mix(fogColor, col, exp(-f * f));
  }
  finalColor = vec4(col, alpha);
}
)";

// Where the sky is covered, for rain and snow and for wet ground: render3d's
// top-down depth of the casters round the camera (weather3d::cover_auto) and
// the boxes of weather3d_cover_set().
const char *const cover_glsl = R"(
uniform sampler2D coverMap;
uniform mat4 coverVP;
uniform int coverOn;       // 1 when the top-down map was drawn this pass
uniform float coverBias;   // in the map's depth units
uniform vec4 coverBox[32]; // per box: centre, then half size
uniform int coverCount;
// 1 where something stands above `p`, 0 under the open sky.
float covered(vec3 p) {
  float c = 0.0;
  if (coverOn == 1) {
    vec4 q = coverVP * vec4(p, 1.0);
    vec3 n = q.xyz / q.w * 0.5 + 0.5;
    if (n.x > 0.0 && n.y > 0.0 && n.x < 1.0 && n.y < 1.0 && n.z > textureLod(coverMap, n.xy, 0.0).r + coverBias)
      c = 1.0;
  }
  for (int i = 0; i < coverCount; i++) {
    vec3 ctr = coverBox[i * 2].xyz;
    vec3 h = coverBox[i * 2 + 1].xyz;
    if (p.y < ctr.y + h.y) {
      vec2 d = abs(p.xz - ctr.xz) - h.xz;
      c = max(c, 1.0 - smoothstep(-0.25, 0.0, max(d.x, d.y)));
    }
  }
  return c;
}
)";

// Rain streaks or snowflakes in a box that wraps round the camera: each one
// keeps its place in the world as the camera moves, and no CPU time is spent.
const char *const precip_vs_head = R"(#version 330
in vec3 vertexPosition; // x, y: corner of the quad (0..1); z: which drop
uniform mat4 mvp;
uniform vec3 eyePos;
uniform float time;
uniform vec2 wind;
uniform vec4 precip;     // box width, box height, fall speed, 1 for snow
out vec2 fragUv;
out float fragFade;
// PCG: an integer hash, even over tens of thousands of drops (a sine hash
// loses its float precision there and drops pile onto each other).
uint pcg(uint v) {
  uint state = v * 747796405u + 2891336453u;
  uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}
vec3 hash3(uint n) {
  return vec3(pcg(n), pcg(n + 7919u), pcg(n + 104729u)) * (1.0 / 4294967295.0);
}
)";
const char *const precip_vs_main = R"(
void main() {
  uint id = uint(vertexPosition.z + 0.5);
  vec3 r = hash3(id);
  vec3 box = vec3(precip.x, precip.y, precip.x);
  bool snow = precip.w > 0.5;
  // The speed has its own random number: tied to the start height (r.y), the
  // spread of speeds lined every drop up at one height whenever it had added up
  // to the box's height, and the rain fell as a flat sheet every few seconds.
  float speed = 0.75 + 0.5 * hash3(id + 15485863u).x;
  vec3 vel = vec3(wind.x, -precip.z * speed, wind.y) * (snow ? 0.6 : 1.0);
  // Most of the box above the eye: what falls below the ground is wasted.
  vec3 corner = eyePos - box * vec3(0.5, 0.25, 0.5);
  vec3 p = corner + mod(r * box + vel * time - corner, box);
  if (snow)
    p.xz += vec2(sin(time * 1.3 + r.x * 40.0), cos(time * 1.1 + r.z * 40.0)) * 0.3;
  vec3 to_eye = normalize(eyePos - p);
  vec3 pos;
  if (snow) {
    vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), to_eye));
    vec3 up = cross(to_eye, right);
    pos = p + (right * (vertexPosition.x - 0.5) + up * (vertexPosition.y - 0.5)) * (0.035 + 0.035 * r.x);
  } else {
    vec3 axis = normalize(vel);
    vec3 side = normalize(cross(axis, to_eye));
    pos = p + axis * (vertexPosition.y - 0.5) * 0.8 + side * (vertexPosition.x - 0.5) * 0.02;
  }
  float d = distance(eyePos, p);
  fragFade = smoothstep(0.4, 1.6, d) * (1.0 - smoothstep(box.x * 0.38, box.x * 0.5, length(p.xz - eyePos.xz)));
  fragFade *= 1.0 - covered(p);
  fragUv = vertexPosition.xy;
  gl_Position = mvp * vec4(pos, 1.0);
}
)";

const char *const precip_fs = R"(#version 330
in vec2 fragUv;
in float fragFade;
uniform vec3 ambient;
uniform vec3 lightColor;
uniform vec4 precip;
uniform float amount;
out vec4 finalColor;
void main() {
  float a;
  vec3 c;
  if (precip.w > 0.5) {
    a = 1.0 - smoothstep(0.55, 1.0, length(fragUv - 0.5) * 2.0);
    c = vec3(1.0) * (ambient * 1.1 + lightColor * 0.45);
    a *= 0.9;
  } else {
    a = (1.0 - abs(fragUv.x - 0.5) * 2.0) * smoothstep(0.0, 0.35, fragUv.y) * 0.45;
    c = vec3(0.85, 0.9, 1.0) * (ambient * 1.8 + lightColor * 0.3 + 0.08);
  }
  finalColor = vec4(min(c, vec3(1.0)), a * fragFade * amount);
}
)";

struct gpu_mesh {
  u32 vao = 0, vbo = 0, ebo = 0;
  i32 count = 0; // indices, or vertices when there are none
  bool indexed = false;
};

gpu_mesh make_mesh(const std::vector<f32> &xyz, const std::vector<u16> &indices) {
  gpu_mesh m;
  m.vao = rlLoadVertexArray();
  rlEnableVertexArray(m.vao);
  m.vbo = rlLoadVertexBuffer(xyz.data(), (i32)(xyz.size() * sizeof(f32)), false);
  rlSetVertexAttribute(0, 3, RL_FLOAT, false, 0, 0);
  rlEnableVertexAttribute(0);
  if (!indices.empty()) {
    m.ebo = rlLoadVertexBufferElement(indices.data(), (i32)(indices.size() * sizeof(u16)), false);
    m.count = (i32)indices.size();
    m.indexed = true;
  } else {
    m.count = (i32)(xyz.size() / 3);
  }
  rlDisableVertexArray();
  return m;
}

void free_mesh(gpu_mesh &m) {
  if (m.vao != 0)
    rlUnloadVertexArray(m.vao);
  if (m.vbo != 0)
    rlUnloadVertexBuffer(m.vbo);
  if (m.ebo != 0)
    rlUnloadVertexBuffer(m.ebo);
  m = gpu_mesh{};
}

void draw_mesh(const gpu_mesh &m) {
  if (m.vao == 0 || !rlEnableVertexArray(m.vao))
    return;
  if (m.indexed)
    rlDrawVertexArrayElements(0, m.count, nullptr);
  else
    rlDrawVertexArray(0, m.count);
  rlDisableVertexArray();
}

// One chunk's grid at one step: (n + 1)^2 vertices `step` samples apart, split
// along the same diagonal as the height field, and a skirt hanging from its
// edges (both faces: it only shows where two levels of detail leave a gap).
gpu_mesh make_chunk_grid(i32 chunk, i32 step) {
  const i32 n = chunk / step;
  std::vector<f32> xyz;
  std::vector<u16> idx;
  const auto vert = [&](i32 x, i32 z, f32 skirt) {
    xyz.insert(xyz.end(), {(f32)(x * step), skirt, (f32)(z * step)});
    return (u16)(xyz.size() / 3 - 1);
  };
  for (i32 z = 0; z <= n; z++)
    for (i32 x = 0; x <= n; x++)
      vert(x, z, 0.0f);
  const auto at = [&](i32 x, i32 z) { return (u16)(z * (n + 1) + x); };
  for (i32 z = 0; z < n; z++)
    for (i32 x = 0; x < n; x++) {
      idx.insert(idx.end(), {at(x, z), at(x, z + 1), at(x + 1, z + 1)});
      idx.insert(idx.end(), {at(x, z), at(x + 1, z + 1), at(x + 1, z)});
    }
  // The skirt: along each edge a strip down from the edge vertices.
  const auto strip = [&](auto edge) {
    for (i32 k = 0; k < n; k++) {
      i32 ax, az, bx, bz;
      edge(k, ax, az);
      edge(k + 1, bx, bz);
      const u16 a = at(ax, az), b = at(bx, bz);
      const u16 a2 = vert(ax, az, 1.0f), b2 = vert(bx, bz, 1.0f);
      idx.insert(idx.end(), {a, b, b2, a, b2, a2, a, b2, b, a, a2, b2});
    }
  };
  strip([&](i32 k, i32 &x, i32 &z) { x = k, z = 0; });
  strip([&](i32 k, i32 &x, i32 &z) { x = k, z = n; });
  strip([&](i32 k, i32 &x, i32 &z) { x = 0, z = k; });
  strip([&](i32 k, i32 &x, i32 &z) { x = n, z = k; });
  return make_mesh(xyz, idx);
}

// The open sea: rings round the camera, each a little wider than the last, so
// the vertices are about square everywhere: fine at the feet, coarse at the
// horizon. y carries how far apart they are there.
gpu_mesh make_ocean(f32 detail) {
  const i32 segments = std::clamp((i32)std::lround(128.0f * detail), 48, 192);
  const f32 grow = 1.0f + 2.0f * pi / (f32)segments;
  std::vector<f32> radii{0.0f};
  for (f32 r = 0.3f; r < 6000.0f; r *= grow)
    radii.push_back(r);
  radii.push_back(8000.0f);
  std::vector<f32> xyz;
  std::vector<u16> idx;
  xyz.insert(xyz.end(), {0.0f, 0.3f, 0.0f});
  for (usize k = 1; k < radii.size(); k++)
    for (i32 s = 0; s < segments; s++) {
      const f32 a = (f32)s / (f32)segments * 2.0f * pi;
      const f32 r = radii[k];
      xyz.insert(xyz.end(), {std::cos(a) * r, std::max(r * (grow - 1.0f), 0.3f), std::sin(a) * r});
    }
  const auto ring = [&](usize k, i32 s) { return (u16)(1 + (k - 1) * (usize)segments + (usize)(s % segments)); };
  for (i32 s = 0; s < segments; s++)
    idx.insert(idx.end(), {0, ring(1, s + 1), ring(1, s)});
  for (usize k = 1; k + 1 < radii.size(); k++)
    for (i32 s = 0; s < segments; s++) {
      idx.insert(idx.end(), {ring(k, s), ring(k, s + 1), ring(k + 1, s + 1)});
      idx.insert(idx.end(), {ring(k, s), ring(k + 1, s + 1), ring(k + 1, s)});
    }
  return make_mesh(xyz, idx);
}

gpu_mesh make_lake(vec2 size, f32 detail) {
  const f32 target = 0.5f / std::max(detail, 0.05f);
  const i32 nx = std::clamp((i32)std::ceil(size.x / target), 8, 254);
  const i32 nz = std::clamp((i32)std::ceil(size.y / target), 8, 254);
  const f32 sx = size.x / (f32)nx, sz = size.y / (f32)nz;
  std::vector<f32> xyz;
  std::vector<u16> idx;
  for (i32 z = 0; z <= nz; z++)
    for (i32 x = 0; x <= nx; x++)
      xyz.insert(xyz.end(), {-size.x * 0.5f + (f32)x * sx, std::max(sx, sz), -size.y * 0.5f + (f32)z * sz});
  const auto at = [&](i32 x, i32 z) { return (u16)(z * (nx + 1) + x); };
  for (i32 z = 0; z < nz; z++)
    for (i32 x = 0; x < nx; x++) {
      idx.insert(idx.end(), {at(x, z), at(x, z + 1), at(x + 1, z + 1)});
      idx.insert(idx.end(), {at(x, z), at(x + 1, z + 1), at(x + 1, z)});
    }
  return make_mesh(xyz, idx);
}

struct lake_mesh {
  u32 water = 0;
  vec2 size{};
  f32 detail = 0.0f;
  gpu_mesh mesh;
};
} // namespace

struct world3d_gpu {
  bool ready = false;
  bool failed = false;
  Shader terrain{}, terrain_depth{}, grass{}, grass_depth{}, sky{}, water{}, precip{};
  render3d_locations terrain_locs, grass_locs, water_locs;
  std::vector<std::pair<i64, gpu_mesh>> grids; // (chunk << 8 | step) -> grid
  gpu_mesh blade, sky_tri, precip_mesh, ocean;
  f32 ocean_detail = 0.0f;
  std::vector<lake_mesh> lakes;

  ~world3d_gpu() {
    if (!ready)
      return;
    for (auto &g : grids)
      free_mesh(g.second);
    for (lake_mesh &l : lakes)
      free_mesh(l.mesh);
    free_mesh(blade);
    free_mesh(sky_tri);
    free_mesh(precip_mesh);
    free_mesh(ocean);
    for (Shader *s : {&terrain, &terrain_depth, &grass, &grass_depth, &sky, &water, &precip})
      if (IsShaderValid(*s))
        UnloadShader(*s);
  }
};

world3d_store::world3d_store() = default;

world3d_store::~world3d_store() {
  // The GL objects of the slots, while the window (declared before the
  // stores in context) is still open.
  for (terrain3d_slot &t : terrains)
    world3d_free_terrain(t);
  for (grass3d_slot &g : grasses)
    world3d_free_grass(g);
}

void world3d_free_terrain(terrain3d_slot &t) {
  for (u32 *id : {&t.height_tex, &t.normal_tex, &t.splat_tex})
    if (*id != 0) {
      rlUnloadTexture(*id);
      *id = 0;
    }
}

void world3d_free_grass(grass3d_slot &g) {
  for (grass_cell &c : g.cells)
    if (c.vbo != 0)
      rlUnloadVertexBuffer(c.vbo);
  g.cells.clear();
}

namespace {
bool ensure_ready(world3d_gpu &g) {
  if (g.ready)
    return true;
  if (g.failed)
    return false;
  const std::string head = "#version 330\n";
  const std::string lighting = render3d_lighting_glsl();
  const std::string terrain_fs = head + lighting + cover_glsl + terrain_fs_main;
  const std::string precip_vs = std::string(precip_vs_head) + cover_glsl + precip_vs_main;
  const std::string grass_vs = head + grass_vs_common + grass_vs_main;
  const std::string grass_depth_vs = head + grass_vs_common + grass_depth_vs_main;
  const std::string grass_fs = head + lighting + grass_fs_main;
  const std::string sky_fs = head + sky_glsl + sky_fs_main;
  const std::string water_vs = head + waves_glsl + water_vs_main;
  const std::string water_fs = head + lighting + sky_glsl + waves_glsl + water_fs_main;
  g.terrain = LoadShaderFromMemory(terrain_vs, terrain_fs.c_str());
  g.terrain_depth = LoadShaderFromMemory(terrain_depth_vs, depth_fs);
  g.grass = LoadShaderFromMemory(grass_vs.c_str(), grass_fs.c_str());
  g.grass_depth = LoadShaderFromMemory(grass_depth_vs.c_str(), depth_fs);
  g.sky = LoadShaderFromMemory(sky_vs, sky_fs.c_str());
  g.water = LoadShaderFromMemory(water_vs.c_str(), water_fs.c_str());
  g.precip = LoadShaderFromMemory(precip_vs.c_str(), precip_fs);
  for (const Shader *s : {&g.terrain, &g.terrain_depth, &g.grass, &g.grass_depth, &g.sky, &g.water, &g.precip})
    if (!IsShaderValid(*s)) {
      NJIN_WARN("world3d: a shader failed to compile, the outdoor world is not drawn");
      g.failed = true;
      return false;
    }
  g.terrain_locs = render3d_find_locations(g.terrain);
  g.grass_locs = render3d_find_locations(g.grass);
  g.water_locs = render3d_find_locations(g.water);
  // A blade: three segments narrowing to a tip.
  std::vector<f32> blade;
  const f32 rows[3] = {0.0f, 0.38f, 0.7f};
  const auto quad = [&](f32 t0, f32 t1) {
    blade.insert(blade.end(), {-1, t0, 0, 1, t0, 0, 1, t1, 0, -1, t0, 0, 1, t1, 0, -1, t1, 0});
  };
  quad(rows[0], rows[1]);
  quad(rows[1], rows[2]);
  blade.insert(blade.end(), {-1, rows[2], 0, 1, rows[2], 0, 0, 1, 0});
  g.blade = make_mesh(blade, {});
  g.sky_tri = make_mesh({-1, -1, 0, 3, -1, 0, -1, 3, 0}, {});
  std::vector<f32> drops;
  drops.reserve((usize)std::max(rain_drops, snow_flakes) * 18);
  for (i32 i = 0; i < std::max(rain_drops, snow_flakes); i++) {
    const f32 k = (f32)i;
    drops.insert(drops.end(), {0, 0, k, 1, 0, k, 1, 1, k, 0, 0, k, 1, 1, k, 0, 1, k});
  }
  g.precip_mesh = make_mesh(drops, {});
  g.ready = true;
  return true;
}

world3d_gpu &gpu_of(context &ctx) {
  if (!ctx.world3d.gpu)
    ctx.world3d.gpu = std::make_unique<world3d_gpu>();
  return *ctx.world3d.gpu;
}

void set_i(u32 program, const char *name, i32 v) {
  const i32 loc = rlGetLocationUniform(program, name);
  if (loc >= 0)
    rlSetUniform(loc, &v, RL_SHADER_UNIFORM_INT, 1);
}
void set_f(u32 program, const char *name, f32 v) {
  const i32 loc = rlGetLocationUniform(program, name);
  if (loc >= 0)
    rlSetUniform(loc, &v, RL_SHADER_UNIFORM_FLOAT, 1);
}
void set_v2(u32 program, const char *name, vec2 v) {
  const i32 loc = rlGetLocationUniform(program, name);
  if (loc >= 0)
    rlSetUniform(loc, &v, RL_SHADER_UNIFORM_VEC2, 1);
}
void set_v3(u32 program, const char *name, vec3 v) {
  const i32 loc = rlGetLocationUniform(program, name);
  if (loc >= 0)
    rlSetUniform(loc, &v, RL_SHADER_UNIFORM_VEC3, 1);
}
void set_v4(u32 program, const char *name, vec4 v) {
  const i32 loc = rlGetLocationUniform(program, name);
  if (loc >= 0)
    rlSetUniform(loc, &v, RL_SHADER_UNIFORM_VEC4, 1);
}
void set_iv2(u32 program, const char *name, i32 x, i32 y) {
  const i32 loc = rlGetLocationUniform(program, name);
  const i32 v[2] = {x, y};
  if (loc >= 0)
    rlSetUniform(loc, v, RL_SHADER_UNIFORM_IVEC2, 1);
}
void set_m(u32 program, const char *name, const Matrix &m) {
  const i32 loc = rlGetLocationUniform(program, name);
  if (loc >= 0)
    rlSetUniformMatrix(loc, m);
}
void bind(u32 program, const char *name, i32 unit, u32 texture) {
  rlActiveTextureSlot(unit);
  rlEnableTexture(texture != 0 ? texture : rlGetTextureIdDefault());
  set_i(program, name, unit);
}
void unbind_units(i32 first, i32 last) {
  for (i32 u = first; u <= last; u++) {
    rlActiveTextureSlot(u);
    rlDisableTexture();
  }
  rlActiveTextureSlot(0);
}

// cover_glsl's uniforms: the top-down map when render3d drew it this pass, and
// the game's cover boxes. Unbind unit_cover after the draw.
void set_cover(const context &ctx, u32 program) {
  const render3d_state &s = ctx.render3d;
  const bool on = s.cover_drawn && s.cover.depth != 0;
  set_i(program, "coverOn", on ? 1 : 0);
  bind(program, "coverMap", unit_cover, on ? s.cover.depth : 0);
  if (on) {
    set_m(program, "coverVP", s.cover_vp);
    set_f(program, "coverBias", 0.15f / 600.0f);
  }
  vec4 boxes[32]{};
  const std::vector<weather3d_cover> &covers = ctx.world3d.covers;
  for (usize i = 0; i < covers.size() && i < 16; i++) {
    boxes[i * 2] = {covers[i].center.x, covers[i].center.y, covers[i].center.z, 0.0f};
    boxes[i * 2 + 1] = {covers[i].size.x * 0.5f, covers[i].size.y * 0.5f, covers[i].size.z * 0.5f, 0.0f};
  }
  const i32 loc = rlGetLocationUniform(program, "coverBox");
  if (loc >= 0 && !covers.empty())
    rlSetUniform(loc, boxes, RL_SHADER_UNIFORM_VEC4, (i32)std::min<usize>(covers.size(), 16) * 2);
  set_i(program, "coverCount", (i32)std::min<usize>(covers.size(), 16));
}

// Planes of a view-projection (render3d's convention: inward normal, offset).
std::array<vec4, 6> planes_of(const Matrix &m) {
  const vec4 r0{m.m0, m.m4, m.m8, m.m12}, r1{m.m1, m.m5, m.m9, m.m13}, r2{m.m2, m.m6, m.m10, m.m14},
      r3{m.m3, m.m7, m.m11, m.m15};
  const auto add = [](vec4 a, vec4 b, f32 k) { return vec4{a.x + b.x * k, a.y + b.y * k, a.z + b.z * k, a.w + b.w * k}; };
  return {add(r3, r0, 1.0f), add(r3, r0, -1.0f), add(r3, r1, 1.0f),
          add(r3, r1, -1.0f), add(r3, r2, 1.0f), add(r3, r2, -1.0f)};
}

bool box_in(const std::array<vec4, 6> &planes, vec3 lo, vec3 hi) {
  for (const vec4 &p : planes) {
    const vec3 far{p.x >= 0.0f ? hi.x : lo.x, p.y >= 0.0f ? hi.y : lo.y, p.z >= 0.0f ? hi.z : lo.z};
    if (p.x * far.x + p.y * far.y + p.z * far.z + p.w < 0.0f)
      return false;
  }
  return true;
}

f32 box_distance(vec3 p, vec3 lo, vec3 hi) {
  const vec3 q{clamp(p.x, lo.x, hi.x), clamp(p.y, lo.y, hi.y), clamp(p.z, lo.z, hi.z)};
  return distance(p, q);
}

const gpu_mesh &grid_of(world3d_gpu &g, i32 chunk, i32 step) {
  const i64 key = ((i64)chunk << 8) | (i64)step;
  for (const auto &e : g.grids)
    if (e.first == key)
      return e.second;
  g.grids.emplace_back(key, make_chunk_grid(chunk, step));
  return g.grids.back().second;
}

// A chunk's box: its samples' lowest and highest point, kept per chunk and
// redone where the ground changed.
void chunk_box(const terrain3d_slot &t, i32 cx, i32 cz, vec3 &lo, vec3 &hi) {
  const i32 i0 = cx * t.chunk, j0 = cz * t.chunk;
  const i32 i1 = std::min(i0 + t.chunk, t.res - 1), j1 = std::min(j0 + t.chunk, t.res - 1);
  const vec2 y = t.chunk_y[(usize)cz * (usize)t.chunks + (usize)cx];
  const vec3 o = t.desc.origin;
  lo = {o.x + (f32)i0 * t.spacing, y.x - 0.5f, o.z + (f32)j0 * t.spacing};
  hi = {o.x + (f32)i1 * t.spacing, y.y + 0.5f, o.z + (f32)j1 * t.spacing};
}

void measure_chunks(terrain3d_slot &t, i32 x0, i32 z0, i32 x1, i32 z1) {
  t.chunk_y.resize((usize)t.chunks * (usize)t.chunks);
  const i32 c0x = std::max(0, (x0 - 1) / t.chunk), c0z = std::max(0, (z0 - 1) / t.chunk);
  const i32 c1x = std::min(t.chunks - 1, x1 / t.chunk), c1z = std::min(t.chunks - 1, z1 / t.chunk);
  for (i32 cz = c0z; cz <= c1z; cz++)
    for (i32 cx = c0x; cx <= c1x; cx++) {
      const i32 i0 = cx * t.chunk, j0 = cz * t.chunk;
      const i32 i1 = std::min(i0 + t.chunk, t.res - 1), j1 = std::min(j0 + t.chunk, t.res - 1);
      f32 a = 1e30f, b = -1e30f;
      for (i32 j = j0; j <= j1; j++)
        for (i32 i = i0; i <= i1; i++) {
          const f32 h = t.heights[(usize)j * (usize)t.res + (usize)i];
          a = std::min(a, h);
          b = std::max(b, h);
        }
      t.chunk_y[(usize)cz * (usize)t.chunks + (usize)cx] = {a, b};
    }
}

// The terrain's textures, made or brought up to date with what changed.
void upload_terrain(terrain3d_slot &t) {
  if (t.height_tex == 0) {
    t.height_tex = rlLoadTexture(nullptr, t.res, t.res, RL_PIXELFORMAT_UNCOMPRESSED_R32, 1);
    t.normal_tex = rlLoadTexture(nullptr, t.res, t.res, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    t.splat_tex = rlLoadTexture(nullptr, t.res, t.res, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    for (u32 id : {t.height_tex, t.normal_tex, t.splat_tex}) {
      const i32 filter = id == t.height_tex ? RL_TEXTURE_FILTER_NEAREST : RL_TEXTURE_FILTER_LINEAR;
      rlTextureParameters(id, RL_TEXTURE_MIN_FILTER, filter);
      rlTextureParameters(id, RL_TEXTURE_MAG_FILTER, filter);
      rlTextureParameters(id, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
      rlTextureParameters(id, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
    }
    t.dirty = true;
    t.dx0 = 0, t.dz0 = 0, t.dx1 = t.res, t.dz1 = t.res;
  }
  if (!t.dirty)
    return;
  const i32 w = t.dx1 - t.dx0, h = t.dz1 - t.dz0;
  std::vector<f32> hs((usize)w * (usize)h);
  std::vector<u8> ns((usize)w * (usize)h * 4), ss((usize)w * (usize)h * 4);
  for (i32 j = 0; j < h; j++)
    for (i32 i = 0; i < w; i++) {
      const i32 x = t.dx0 + i, z = t.dz0 + j;
      const usize k = (usize)j * (usize)w + (usize)i;
      const usize src = (usize)z * (usize)t.res + (usize)x;
      hs[k] = t.heights[src];
      const vec3 n = terrain_sample_normal(t, x, z);
      ns[k * 4 + 0] = (u8)std::lround((n.x * 0.5f + 0.5f) * 255.0f);
      ns[k * 4 + 1] = (u8)std::lround((n.y * 0.5f + 0.5f) * 255.0f);
      ns[k * 4 + 2] = (u8)std::lround((n.z * 0.5f + 0.5f) * 255.0f);
      ns[k * 4 + 3] = 255;
      std::copy_n(&t.splat[src * 4], 4, &ss[k * 4]);
    }
  measure_chunks(t, t.dx0, t.dz0, t.dx1, t.dz1);
  rlUpdateTexture(t.height_tex, t.dx0, t.dz0, w, h, RL_PIXELFORMAT_UNCOMPRESSED_R32, hs.data());
  rlUpdateTexture(t.normal_tex, t.dx0, t.dz0, w, h, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, ns.data());
  rlUpdateTexture(t.splat_tex, t.dx0, t.dz0, w, h, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, ss.data());
  t.dirty = false;
}

void draw_terrain(context &ctx, terrain3d_slot &t, const Matrix &view_proj, bool depth) {
  world3d_gpu &g = gpu_of(ctx);
  const render3d_state &s = ctx.render3d;
  upload_terrain(t);
  const Shader shader = depth ? g.terrain_depth : g.terrain;
  const u32 p = shader.id;
  rlEnableShader(p);
  set_m(p, "mvp", view_proj);
  set_iv2(p, "sampleLast", t.res - 1, t.res - 1);
  bind(p, "heightMap", unit_height, t.height_tex);
  if (!depth) {
    const world3d_store &w = ctx.world3d;
    bind(p, "normalMap", unit_normal, t.normal_tex);
    bind(p, "splatMap", unit_splat, t.splat_tex);
    vec4 tiles{};
    vec4 colors[4]{};
    i32 has_normal[4] = {0, 0, 0, 0};
    static const char *const layer_names[4] = {"layer0", "layer1", "layer2", "layer3"};
    static const char *const normal_names[4] = {"layerNormal0", "layerNormal1", "layerNormal2", "layerNormal3"};
    for (i32 l = 0; l < 4; l++) {
      const terrain3d_layer &L = t.desc.layers[l];
      const texture_slot *albedo = texture_slot_of(ctx.texture, L.albedo);
      const texture_slot *normal = texture_slot_of(ctx.texture, L.normal);
      bind(p, layer_names[l], unit_layer + l, albedo != nullptr && !albedo->packed ? albedo->texture.id : 0);
      bind(p, normal_names[l], unit_layer_normal + l,
           normal != nullptr && !normal->packed ? normal->texture.id : 0);
      has_normal[l] = normal != nullptr && !normal->packed ? 1 : 0;
      (&tiles.x)[l] = std::max(L.tile, 0.01f);
      colors[l] = {L.color.r, L.color.g, L.color.b, L.color.a};
    }
    set_v4(p, "layerTile", tiles);
    const i32 loc = rlGetLocationUniform(p, "layerColor");
    if (loc >= 0)
      rlSetUniform(loc, colors, RL_SHADER_UNIFORM_VEC4, 4);
    const i32 hn = rlGetLocationUniform(p, "layerHasNormal");
    if (hn >= 0)
      rlSetUniform(hn, has_normal, RL_SHADER_UNIFORM_IVEC4, 1);
    set_i(p, "layerCount", t.desc.layer_count);
    set_v2(p, "gridSize", {(f32)t.res, (f32)t.res});
    set_f(p, "wetness", w.wetness);
    set_cover(ctx, p);
    material3d m{};
    m.specular = t.desc.specular + w.wetness * 0.6f;
    m.shininess = lerp(t.desc.shininess, 90.0f, w.wetness);
    render3d_set_draw_uniforms(shader, g.terrain_locs, m);
    rlEnableShader(p);
  }
  const std::array<vec4, 6> planes = depth ? planes_of(view_proj) : s.frustum;
  const vec3 eye = s.camera.position;
  for (i32 cz = 0; cz < t.chunks; cz++)
    for (i32 cx = 0; cx < t.chunks; cx++) {
      vec3 lo, hi;
      chunk_box(t, cx, cz, lo, hi);
      if (!box_in(planes, lo, hi))
        continue;
      const f32 d = box_distance(eye, lo, hi);
      i32 level = 0;
      if (d > t.desc.lod_distance && t.desc.lod_distance > 0.0f)
        level = (i32)std::floor(std::log2(d / t.desc.lod_distance)) + 1;
      level = std::clamp(level, 0, t.lods - 1);
      const i32 step = 1 << level;
      set_iv2(p, "chunkOrigin", cx * t.chunk, cz * t.chunk);
      // Deep enough to close any gap a coarser neighbour leaves.
      const f32 skirt = std::max(1.0f, (hi.y - lo.y) * 0.5f + t.spacing * (f32)step);
      set_v4(p, "terrainGrid", {t.desc.origin.x, t.desc.origin.z, t.spacing, skirt});
      draw_mesh(grid_of(g, t.chunk, step));
    }
  unbind_units(0, unit_splat);
  if (!depth)
    unbind_units(unit_cover, unit_cover);
  rlDisableShader();
}

void draw_grass(context &ctx, grass3d_slot &gr, const Matrix &view_proj, bool depth) {
  terrain3d_slot *t = terrain_of(ctx, gr.desc.terrain);
  if (t == nullptr)
    return;
  world3d_gpu &g = gpu_of(ctx);
  const render3d_state &s = ctx.render3d;
  world3d_store &w = ctx.world3d;
  const grass3d_desc &d = gr.desc;
  const vec3 eye = s.camera.position;
  const f32 range = std::max(d.draw_distance, 1.0f);
  const vec3 o = t->desc.origin;
  upload_terrain(*t);
  // Cells in range, nearest first; a few new ones per frame so walking into a
  // field never stalls a frame.
  if (!depth) {
    const i32 cells = (i32)std::ceil(t->desc.size / gr.cell);
    const i32 x0 = std::max(0, (i32)std::floor((eye.x - range - o.x) / gr.cell));
    const i32 z0 = std::max(0, (i32)std::floor((eye.z - range - o.z) / gr.cell));
    const i32 x1 = std::min(cells - 1, (i32)std::floor((eye.x + range - o.x) / gr.cell));
    const i32 z1 = std::min(cells - 1, (i32)std::floor((eye.z + range - o.z) / gr.cell));
    struct want {
      i32 cx, cz;
      f32 dist;
    };
    std::vector<want> wanted;
    for (i32 cz = z0; cz <= z1; cz++)
      for (i32 cx = x0; cx <= x1; cx++) {
        const f32 ax = o.x + (f32)cx * gr.cell, az = o.z + (f32)cz * gr.cell;
        const f32 dx = std::max({ax - eye.x, 0.0f, eye.x - (ax + gr.cell)});
        const f32 dz = std::max({az - eye.z, 0.0f, eye.z - (az + gr.cell)});
        const f32 dist = std::hypot(dx, dz);
        if (dist <= range)
          wanted.push_back({cx, cz, dist});
      }
    std::sort(wanted.begin(), wanted.end(), [](const want &a, const want &b) { return a.dist < b.dist; });
    i32 budget = 10;
    std::vector<f32> blades;
    for (const want &wc : wanted) {
      grass_cell *cell = nullptr;
      for (grass_cell &c : gr.cells)
        if (c.cx == wc.cx && c.cz == wc.cz)
          cell = &c;
      if (cell == nullptr || cell->stale) {
        if (budget <= 0)
          continue;
        budget--;
        if (cell == nullptr) {
          gr.cells.push_back(grass_cell{.cx = wc.cx, .cz = wc.cz});
          cell = &gr.cells.back();
        }
        grass_cell_blades(*t, gr, wc.cx, wc.cz, blades);
        if (cell->vbo != 0)
          rlUnloadVertexBuffer(cell->vbo);
        cell->vbo = blades.empty() ? 0 : rlLoadVertexBuffer(blades.data(), (i32)(blades.size() * sizeof(f32)), false);
        cell->count = (u32)(blades.size() / 8);
        cell->stale = false;
        f32 a = 1e30f, b = -1e30f;
        for (usize k = 0; k < blades.size(); k += 8) {
          a = std::min(a, blades[k + 1]);
          b = std::max(b, blades[k + 1] + blades[k + 3]);
        }
        const f32 ax = o.x + (f32)wc.cx * gr.cell, az = o.z + (f32)wc.cz * gr.cell;
        cell->lo = {ax - 0.5f, blades.empty() ? 0.0f : a - 0.5f, az - 0.5f};
        cell->hi = {ax + gr.cell + 0.5f, blades.empty() ? 0.0f : b + 0.5f, az + gr.cell + 0.5f};
      }
      cell->used = w.frame;
    }
    // Cells left behind for a while are let go.
    std::erase_if(gr.cells, [&](grass_cell &c) {
      if (w.frame - c.used <= 120)
        return false;
      if (c.vbo != 0)
        rlUnloadVertexBuffer(c.vbo);
      return true;
    });
  }
  const Shader shader = depth ? g.grass_depth : g.grass;
  const u32 p = shader.id;
  rlEnableShader(p);
  set_m(p, "mvp", view_proj);
  set_v3(p, "eyePos", eye);
  set_f(p, "time", w.time);
  set_v2(p, "wind", w.wind);
  set_f(p, "sway", std::max(d.sway, 0.0f));
  set_v2(p, "fade", {std::max(range - std::max(d.fade, 0.01f), 0.0f), range});
  if (!depth) {
    bind(p, "normalMap", unit_normal, t->normal_tex);
    set_v4(p, "terrainGrid", {o.x, o.z, t->spacing, 0.0f});
    set_v2(p, "gridSize", {(f32)t->res, (f32)t->res});
    set_v3(p, "baseColor", {d.base_color.r, d.base_color.g, d.base_color.b});
    set_v3(p, "tipColor", {d.tip_color.r, d.tip_color.g, d.tip_color.b});
    material3d m{};
    m.specular = 0.08f;
    m.shininess = 24.0f;
    render3d_set_draw_uniforms(shader, g.grass_locs, m);
    rlEnableShader(p);
  }
  const std::array<vec4, 6> planes = depth ? planes_of(view_proj) : s.frustum;
  rlDisableBackfaceCulling();
  i32 drawn = 0;
  if (rlEnableVertexArray(g.blade.vao)) {
    for (const grass_cell &c : gr.cells) {
      // The camera pass draws the cells it just kept; a shadow pass (which
      // comes first) those the last camera pass kept.
      if (c.vbo == 0 || c.count == 0 || (!depth && c.used != w.frame) || !box_in(planes, c.lo, c.hi))
        continue;
      // Thinner further out: blades are in random order, so the first part
      // of a cell is an even sample of it.
      const f32 dist = box_distance(eye, c.lo, c.hi);
      const f32 keep = clamp(1.0f - (dist - range * 0.35f) / (range * 0.65f) * 0.7f, 0.3f, 1.0f);
      const i32 n = std::max(1, (i32)((f32)c.count * keep));
      rlEnableVertexBuffer(c.vbo);
      for (i32 a = 0; a < 2; a++) {
        rlSetVertexAttribute((u32)(12 + a), 4, RL_FLOAT, false, 32, a * 16);
        rlEnableVertexAttribute((u32)(12 + a));
        rlSetVertexAttributeDivisor((u32)(12 + a), 1);
      }
      rlDrawVertexArrayInstanced(0, g.blade.count, n);
      drawn += n;
    }
    rlDisableVertexArray();
  }
  rlEnableBackfaceCulling();
  if (!depth)
    gr.drawn = drawn;
  unbind_units(0, unit_splat);
  rlDisableShader();
}

void set_sky_uniforms(u32 p, const sky_params &k, f32 time) {
  set_v3(p, "skySun", k.sun);
  set_v3(p, "skySunColor", k.sun_color);
  set_v3(p, "skyZenith", k.zenith);
  set_v3(p, "skyHorizon", k.horizon);
  set_v3(p, "skyGround", k.ground);
  set_v3(p, "skyGlow", k.glow);
  set_v4(p, "skyParams", {k.twilight, k.clouds, k.cloud_darkness, k.sun_size});
  set_v4(p, "skyCloud", {k.cloud_height, k.cloud_scale, k.stars, k.fog});
  set_v3(p, "skyFogColor", k.fog_color);
  set_v2(p, "skyWind", k.wind);
  set_f(p, "skyTime", time);
  set_f(p, "skyDay", k.day);
}

void draw_sky(context &ctx, const Matrix &view_proj) {
  world3d_gpu &g = gpu_of(ctx);
  const render3d_state &s = ctx.render3d;
  const u32 p = g.sky.id;
  rlEnableShader(p);
  set_m(p, "invViewProj", MatrixInvert(view_proj));
  set_v3(p, "eyePos", s.camera.position);
  set_sky_uniforms(p, ctx.world3d.sky, ctx.world3d.time);
  rlDisableDepthMask();
  draw_mesh(g.sky_tri);
  rlEnableDepthMask();
  rlDisableShader();
}

void draw_water(context &ctx, const water3d_slot &wslot, u32 id, const Matrix &view_proj) {
  world3d_gpu &g = gpu_of(ctx);
  const render3d_state &s = ctx.render3d;
  const world3d_store &w = ctx.world3d;
  const water3d_desc &d = wslot.desc;
  const bool ocean = d.size.x <= 0.0f || d.size.y <= 0.0f;
  const gpu_mesh *mesh = nullptr;
  vec2 origin = d.center;
  if (ocean) {
    if (g.ocean.vao == 0 || g.ocean_detail != d.detail) {
      free_mesh(g.ocean);
      g.ocean = make_ocean(d.detail);
      g.ocean_detail = d.detail;
    }
    mesh = &g.ocean;
    // Follows the camera in whole steps, so the waves do not slide along.
    const f32 snap = 2.0f;
    origin = {std::round(s.camera.position.x / snap) * snap, std::round(s.camera.position.z / snap) * snap};
  } else {
    lake_mesh *lake = nullptr;
    for (lake_mesh &l : g.lakes)
      if (l.water == id)
        lake = &l;
    if (lake == nullptr) {
      lake_mesh fresh;
      fresh.water = id;
      g.lakes.push_back(fresh);
      lake = &g.lakes.back();
    }
    if (lake->mesh.vao == 0 || lake->size != d.size || lake->detail != d.detail) {
      free_mesh(lake->mesh);
      lake->mesh = make_lake(d.size, d.detail);
      lake->size = d.size;
      lake->detail = d.detail;
    }
    mesh = &lake->mesh;
  }
  const u32 p = g.water.id;
  rlEnableShader(p);
  set_m(p, "mvp", view_proj);
  set_v3(p, "waterOrigin", {origin.x, d.level, origin.y});
  vec4 waves[water3d_wave_max]{}, waves2[water3d_wave_max]{};
  i32 count = 0;
  for (i32 i = 0; i < std::clamp(d.wave_count, 0, water3d_wave_max); i++) {
    const water3d_wave &wave = d.waves[i];
    const f32 len = length(wave.direction);
    if (len <= 1e-6f)
      continue;
    const f32 k = 2.0f * pi / std::max(wave.wavelength, 0.01f);
    const f32 st = clamp(wave.steepness, 0.0f, 1.0f);
    waves[count] = {wave.direction.x / len, wave.direction.y / len, k, st};
    waves2[count] = {std::sqrt(9.81f / k) * wave.speed, st / k, 0.0f, 0.0f};
    count++;
  }
  const i32 lw = rlGetLocationUniform(p, "waves"), lw2 = rlGetLocationUniform(p, "waves2");
  if (count > 0) {
    rlSetUniform(lw, waves, RL_SHADER_UNIFORM_VEC4, count);
    rlSetUniform(lw2, waves2, RL_SHADER_UNIFORM_VEC4, count);
  }
  set_i(p, "waveCount", count);
  set_f(p, "time", w.time);
  set_v3(p, "shallowColor", {d.shallow_color.r, d.shallow_color.g, d.shallow_color.b});
  set_v3(p, "deepColor", {d.deep_color.r, d.deep_color.g, d.deep_color.b});
  set_v3(p, "foamColor", {d.foam_color.r, d.foam_color.g, d.foam_color.b});
  set_v4(p, "waterParams", {d.depth_fade, d.clarity, std::max(d.foam_width, 0.0f), clamp(d.crest_foam, 0.0f, 1.0f)});
  set_v3(p, "waterSpec", {std::max(d.specular, 0.0f), d.shininess, clamp(d.ripples, 0.0f, 1.0f)});
  terrain3d_slot *t = terrain_of(ctx, d.terrain);
  set_i(p, "hasGround", t != nullptr ? 1 : 0);
  if (t != nullptr) {
    upload_terrain(*t);
    bind(p, "groundHeight", unit_height, t->height_tex);
    set_v4(p, "groundGrid", {t->desc.origin.x, t->desc.origin.z, t->spacing, 0.0f});
    set_iv2(p, "groundLast", t->res - 1, t->res - 1);
  }
  set_sky_uniforms(p, w.sky_drawn ? w.sky : sky_params_from_light(ctx), w.time);
  rlDisableBackfaceCulling();
  draw_mesh(*mesh);
  rlEnableBackfaceCulling();
  unbind_units(unit_height, unit_height);
  rlDisableShader();
}

void draw_precip(context &ctx, const Matrix &view_proj) {
  world3d_gpu &g = gpu_of(ctx);
  const render3d_state &s = ctx.render3d;
  const world3d_store &w = ctx.world3d;
  const u32 p = g.precip.id;
  rlEnableShader(p);
  set_m(p, "mvp", view_proj);
  set_v3(p, "eyePos", s.camera.position);
  set_f(p, "time", w.time);
  set_v2(p, "wind", w.sky.wind);
  set_v3(p, "ambient", {s.light.ambient.r, s.light.ambient.g, s.light.ambient.b});
  set_v3(p, "lightColor", {s.light.color.r, s.light.color.g, s.light.color.b});
  set_cover(ctx, p);
  rlDisableBackfaceCulling();
  if (rlEnableVertexArray(g.precip_mesh.vao)) {
    if (w.sky.rain > 0.0f) {
      set_v4(p, "precip", {32.0f, 22.0f, 9.0f, 0.0f});
      set_f(p, "amount", 1.0f);
      rlDrawVertexArray(0, (i32)((f32)rain_drops * w.sky.rain) * 6);
    }
    if (w.sky.snow > 0.0f) {
      set_v4(p, "precip", {26.0f, 16.0f, 1.2f, 1.0f});
      set_f(p, "amount", 1.0f);
      rlDrawVertexArray(0, (i32)((f32)snow_flakes * w.sky.snow) * 6);
    }
    rlDisableVertexArray();
  }
  rlEnableBackfaceCulling();
  unbind_units(unit_cover, unit_cover);
  rlDisableShader();
}

bool has_world(const render3d_state &s) {
  for (const draw3d_cmd &c : s.cmds)
    if (c.world != world3d_none)
      return true;
  return false;
}
} // namespace

void world3d_pass_uniforms(context &ctx, bool shadows, const Matrix &light_vp, bool lamps) {
  const render3d_state &s = ctx.render3d;
  if (!has_world(s))
    return;
  world3d_gpu &g = gpu_of(ctx);
  if (!ensure_ready(g))
    return;
  render3d_set_pass_uniforms(s, g.terrain, g.terrain_locs, shadows, light_vp, lamps);
  render3d_set_pass_uniforms(s, g.grass, g.grass_locs, shadows, light_vp, lamps);
  render3d_set_pass_uniforms(s, g.water, g.water_locs, shadows, light_vp, lamps);
}

void world3d_draw(context &ctx, const draw3d_cmd &c, const Matrix &view_proj) {
  world3d_gpu &g = gpu_of(ctx);
  if (!ensure_ready(g))
    return;
  const bool translucent = ctx.render3d.translucent_pass;
  rlDrawRenderBatchActive();
  world3d_store &w = ctx.world3d;
  switch (c.world) {
  case world3d_terrain: {
    terrain3d_slot *t = terrain_of(ctx, terrain3d_handle{c.world_id});
    if (!translucent && t != nullptr)
      draw_terrain(ctx, *t, view_proj, false);
    break;
  }
  case world3d_grass:
    if (!translucent && c.world_id >= 1 && c.world_id <= w.grasses.size() && w.grasses[c.world_id - 1].alive)
      draw_grass(ctx, w.grasses[c.world_id - 1], view_proj, false);
    break;
  case world3d_sky:
    if (!translucent)
      draw_sky(ctx, view_proj);
    break;
  case world3d_water:
    if (translucent && c.world_id >= 1 && c.world_id <= w.waters.size() && w.waters[c.world_id - 1].alive)
      draw_water(ctx, w.waters[c.world_id - 1], c.world_id, view_proj);
    break;
  case world3d_precip:
    if (translucent)
      draw_precip(ctx, view_proj);
    break;
  default:
    break;
  }
}

void world3d_draw_depth(context &ctx, const draw3d_cmd &c, const Matrix &view_proj) {
  world3d_gpu &g = gpu_of(ctx);
  if (!ensure_ready(g))
    return;
  rlDrawRenderBatchActive();
  if (c.world == world3d_terrain) {
    terrain3d_slot *t = terrain_of(ctx, terrain3d_handle{c.world_id});
    if (t != nullptr && t->desc.cast_shadows)
      draw_terrain(ctx, *t, view_proj, true);
  } else if (c.world == world3d_grass && c.world_id >= 1 && c.world_id <= ctx.world3d.grasses.size()) {
    grass3d_slot &gr = ctx.world3d.grasses[c.world_id - 1];
    if (gr.alive && gr.desc.cast_shadows)
      draw_grass(ctx, gr, view_proj, true);
  }
}

void world3d_pass_end(context &ctx) { ctx.world3d.sky_drawn = false; }
} // namespace njin
