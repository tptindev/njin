#include "render3d.h"
#include "post3d.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_quality_impl.h"
#include "njin_log.h"
#include "njin_texture.h"
#include "njin_world3d_impl.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
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
constexpr i32 unit_imprint = 11; // terrain3d_imprint_*: pressed, rim, stamp time
constexpr i32 unit_cover = 12;   // render3d's top-down cover depth (rain and snow)
constexpr i32 unit_relief = 13;  // terrain3d_layer::height, packed (terrain3d_slot::relief_tex)
constexpr i32 unit_flakes = 0;   // weather3d_snow_flakes_set's shapes (no layers in that pass)
constexpr i32 unit_scene = 5;    // 5, 6: the scene's colour and depth under refracting water (no layers there)

constexpr i32 rain_drops = 9000;
constexpr i32 snow_flakes = 7000;

// terrain3d_layer::relief, for both terrain vertex shaders: a relief grid's
// vertex sits between the samples (vertexPosition.xz in fractions of one); it
// is pushed along the ground's normal by the layers' height images, read the
// way terrain_fs_main reads their colour images (layer_sample), weighted as
// it weights them, so a crack in the colour image is a groove right there.
const char *const relief_glsl = R"(
uniform sampler2D heightMap;  // world heights, one texel per sample
uniform ivec2 sampleLast;     // resolution - 1
uniform vec4 terrainGrid;     // origin x, origin z, spacing
uniform int reliefOn;         // 1: this draw is of relief grids
uniform sampler2D reliefMap;  // layer l's height image in channel l
uniform sampler2D normalMap;
uniform sampler2D splatMap;
uniform vec4 layerTile;
uniform int layerCount;
uniform vec2 gridSize;
uniform vec4 reliefDepth;     // terrain3d_layer::relief, per layer (0: none)
uniform vec4 reliefFade;      // camera xyz, the distance the relief is gone by
uniform float reliefStep;     // samples between two vertices of a relief grid

// The ground between the samples (bilinear).
float relief_ground(vec2 c) {
  ivec2 i = min(ivec2(c), sampleLast - 1);
  vec2 f = c - vec2(i);
  float h00 = texelFetch(heightMap, i, 0).r, h10 = texelFetch(heightMap, i + ivec2(1, 0), 0).r;
  float h01 = texelFetch(heightMap, i + ivec2(0, 1), 0).r, h11 = texelFetch(heightMap, i + ivec2(1, 1), 0).r;
  return mix(mix(h00, h10, f.x), mix(h01, h11, f.x), f.y);
}

// Layer l's height at `p`, as layer_sample would read its colour (no detile),
// from the mip whose texels are about as far apart as the vertices.
float relief_layer(int l, vec3 p, vec3 b) {
  float t = layerTile[l];
  float lod = max(log2(reliefStep * terrainGrid.z * float(textureSize(reliefMap, 0).x) / t), 0.0);
  if (b.y > 0.97)
    return mix(textureLod(reliefMap, p.xz / t, lod)[l], textureLod(reliefMap, p.xz / (t * 3.7) + 0.31, lod)[l], 0.3);
  return textureLod(reliefMap, p.zy / t, lod)[l] * b.x + textureLod(reliefMap, p.xz / t, lod)[l] * b.y +
         textureLod(reliefMap, p.xy / t, lod)[l] * b.z;
}

// The ground at sample `s` (fractional) with the relief: `base` without it.
vec3 relief_point(vec2 s, out vec3 base) {
  vec2 c = clamp(s, vec2(0.0), vec2(sampleLast));
  base = vec3(terrainGrid.x + c.x * terrainGrid.z, relief_ground(c), terrainGrid.y + c.y * terrainGrid.z);
  vec2 uv = (c + 0.5) / gridSize;
  vec3 n = normalize(textureLod(normalMap, uv, 0.0).xyz * 2.0 - 1.0);
  // The layers' weights as terrain_fs_main takes them.
  vec4 w = textureLod(splatMap, uv, 0.0);
  if (layerCount < 4) w.w = 0.0;
  if (layerCount < 3) w.z = 0.0;
  if (layerCount < 2) w.y = 0.0;
  w = pow(max(w, vec4(0.0)), vec4(1.6));
  float sum = w.x + w.y + w.z + w.w;
  w = sum > 1e-4 ? w / sum : vec4(1.0, 0.0, 0.0, 0.0);
  vec3 b = pow(abs(n), vec3(4.0));
  b /= b.x + b.y + b.z;
  float d = 0.0;
  for (int l = 0; l < 4; l++)
    if (reliefDepth[l] > 0.0 && w[l] > 0.004)
      d += (relief_layer(l, base, b) - 0.5) * reliefDepth[l] * w[l];
  d *= 1.0 - smoothstep(reliefFade.w * 0.7, reliefFade.w, distance(base, reliefFade.xyz));
  return base + n * d;
}
)";

const char *const terrain_vs_head = R"(#version 330
in vec3 vertexPosition; // x, z: sample offset in the chunk; y: 1 on the skirt
// The chunk this instance draws: its first sample x, z and its skirt's depth.
// A level of detail's chunks are one instanced draw of its grid (draw_chunks).
layout(location = 12) in vec4 chunkInstance;
uniform mat4 mvp;
)";

const char *const terrain_vs_main = R"(
out vec3 fragPos;
out vec2 fragGrid;
out vec3 fragTex;    // where the layers' images are read: fragPos without the relief
out vec3 fragRelief; // the relief's own normal; zero off the relief grids
invariant gl_Position; // the depth program lands on the same depth (render3d_options::depth_prepass)
void main() {
  if (reliefOn == 0) {
    ivec2 s = min(ivec2(chunkInstance.xy + 0.5) + ivec2(vertexPosition.xz + 0.5), sampleLast);
    float h = texelFetch(heightMap, s, 0).r - vertexPosition.y * chunkInstance.z;
    vec3 world = vec3(terrainGrid.x + float(s.x) * terrainGrid.z, h, terrainGrid.y + float(s.y) * terrainGrid.z);
    fragPos = world;
    fragGrid = vec2(s);
    fragTex = world;
    fragRelief = vec3(0.0);
    gl_Position = mvp * vec4(world, 1.0);
    return;
  }
  vec2 s = chunkInstance.xy + vertexPosition.xz;
  vec3 base, unused;
  vec3 world = relief_point(s, base);
  // The shape's normal, from the next vertex along x and along z.
  vec3 ax = relief_point(s + vec2(reliefStep, 0.0), unused) - world;
  vec3 az = relief_point(s + vec2(0.0, reliefStep), unused) - world;
  vec3 rn = cross(az, ax);
  fragRelief = dot(rn, rn) > 1e-12 ? normalize(rn) : vec3(0.0, 1.0, 0.0);
  world.y -= vertexPosition.y * chunkInstance.z;
  fragPos = world;
  fragGrid = clamp(s, vec2(0.0), vec2(sampleLast));
  fragTex = base;
  gl_Position = mvp * vec4(world, 1.0);
}
)";

const char *const terrain_fs_main = R"(
in vec3 fragPos;
in vec2 fragGrid;
in vec3 fragTex;
in vec3 fragRelief;
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
uniform vec4 layerDetile;      // terrain3d_layer::detile, per layer
uniform vec4 layerDetileScale; // terrain3d_layer::detile_scale, per layer
uniform vec4 layerColor[4];
uniform ivec4 layerHasNormal;
uniform ivec4 layerHasRough;   // terrain3d_layer::roughness, in the normal map's alpha
uniform int layerCount;
uniform vec2 gridSize;         // samples per side
uniform float wetness;
uniform vec4 layerGlitter;     // terrain3d_layer::glitter, per layer
// terrain3d_imprint_*: texels addressed by absolute texel modulo the size.
uniform sampler2D imprintMap;  // pressed, rim, stamp time (1/100 s, 16 bits)
uniform vec4 imprintWin;       // the kept square's first corner x, z (metres), metres per texel, texels a side (0: off)
uniform vec4 imprintLook;      // depth, rim height, lifetime, fade
uniform float imprintNow;      // now, in the stamps' hundredths of a second (mod 65536)
uniform vec4 imprintTint;
// grass3d_desc::far_cover: the grass on this ground, for its look beyond reach.
uniform vec4 grassFar;   // the field's colour seen from afar, amount (0: off)
uniform vec4 grassFade;  // reach where blades start shrinking, where they end, layer (-1: any), min height
uniform vec2 grassSlope; // cos of the slope limit, and of 6 degrees under it
uniform vec4 snowLayer;  // terrain3d_desc::snow: rgb its colour, a the amount (0: off)

// Pressed and rim at a texel, faded by the stamp's age.
vec2 imprint_texel(vec2 cell) {
  ivec2 c = ivec2(mod(cell, imprintWin.w));
  vec4 v = texelFetch(imprintMap, c, 0) * 255.0;
  float age = mod(imprintNow - (v.b + v.a * 256.0) + 65536.0, 65536.0) * 0.01;
  return v.rg / 255.0 * clamp((imprintLook.z - age) / max(imprintLook.w, 1e-3), 0.0, 1.0);
}

// Bilinear over the texels round `xz`.
vec2 imprint_at(vec2 xz) {
  vec2 f = xz / imprintWin.z - 0.5;
  vec2 c = floor(f), a = f - c;
  return mix(mix(imprint_texel(c), imprint_texel(c + vec2(1.0, 0.0)), a.x),
             mix(imprint_texel(c + vec2(0.0, 1.0)), imprint_texel(c + vec2(1.0, 1.0)), a.x), a.y);
}

float imprint_height(vec2 xz) {
  vec2 v = imprint_at(xz);
  return -v.x * imprintLook.x + v.y * imprintLook.y;
}

// terrain3d_layer::detile: two copies of the image, each shifted by an
// amount picked by a smooth noise over the ground, blended where the noise
// moves from one shift to the next, so the tiles no longer line up into a
// grid (Inigo Quilez, "Texture repetition", the one-noise variant). The
// gradients are the plain ones: the shifts jumping would pick a wrong mip.
float detile_hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float detile_noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(detile_hash(i), detile_hash(i + vec2(1.0, 0.0)), f.x),
             mix(detile_hash(i + vec2(0.0, 1.0)), detile_hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// terrain3d_layer::detile_scale: each shift's copy is also read at its own
// size, so the image's features come larger and smaller over the ground.
// `detile`: x the amount, y the size spread.
vec4 top_sample(sampler2D s, vec2 uv, vec2 detile) {
  if (detile.x <= 0.0)
    return texture(s, uv);
  vec2 dx = dFdx(uv), dy = dFdy(uv);
  float l = detile_noise(uv * 0.45) * 8.0;
  float i = floor(l);
  float sa = 1.0 + detile.y * (detile_hash(vec2(i, 5.3)) * 2.0 - 1.0);
  float sb = 1.0 + detile.y * (detile_hash(vec2(i + 1.0, 5.3)) * 2.0 - 1.0);
  vec4 a = textureGrad(s, uv * sa + sin(vec2(3.0, 7.0) * i), dx * sa, dy * sa);
  vec4 b = textureGrad(s, uv * sb + sin(vec2(3.0, 7.0) * (i + 1.0)), dx * sb, dy * sb);
  vec4 c = mix(a, b, smoothstep(0.2, 0.8, l - i));
  return detile.x >= 1.0 ? c : mix(textureGrad(s, uv, dx, dy), c, detile.x);
}

// A layer's image at this point: from above on gentle ground; on steep ground
// from the three axes (triplanar), so cliffs do not stretch it. A second
// sample at another scale hides the tiling.
vec4 layer_sample(sampler2D s, vec3 p, vec3 n, float tile, vec2 detile) {
  vec3 b = pow(abs(n), vec3(4.0));
  b /= b.x + b.y + b.z;
  vec4 c;
  if (b.y > 0.97) {
    c = top_sample(s, p.xz / tile, detile);
    c = mix(c, top_sample(s, p.xz / (tile * 3.7) + 0.31, detile), 0.3);
  } else {
    c = texture(s, p.zy / tile) * b.x + top_sample(s, p.xz / tile, detile) * b.y + texture(s, p.xy / tile) * b.z;
  }
  return c;
}

// The roughness packed in a layer's normal map, read the way its normals are.
float layer_rough(sampler2D s, vec3 p, float tile, vec2 detile) { return top_sample(s, p.xz / tile, detile).a; }

vec3 layer_normal(sampler2D s, vec3 p, vec3 n, float tile, vec2 detile) {
  vec3 m = top_sample(s, p.xz / tile, detile).xyz * 2.0 - 1.0;
  vec3 t = normalize(vec3(1.0, 0.0, 0.0) - n * n.x);
  vec3 b = cross(n, t); // the image's up (green) runs toward -z
  return normalize(t * m.x + b * m.y + n * max(m.z, 0.2));
}

void main() {
  vec2 uv = (fragGrid + 0.5) / gridSize;
  vec3 n = normalize(texture(normalMap, uv).xyz * 2.0 - 1.0);
  vec3 ground = n;
  float up = n.y; // the ground's own slope, before the layers' normal maps
  vec2 foot = fwidth(fragPos.xz); // before any branch: the pixel's size on the ground
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
  // A layer without a roughness image counts as 0.5: its desc's own shine.
  bool rough = layerHasRough != ivec4(0);
  float r = 0.0;
  if (rough) {
    if (w.x > 0.004)
      r += (layerHasRough.x == 1 ? layer_rough(layerNormal0, fragTex, layerTile.x, vec2(layerDetile.x, layerDetileScale.x)) : 0.5) * w.x;
    if (w.y > 0.004)
      r += (layerHasRough.y == 1 ? layer_rough(layerNormal1, fragTex, layerTile.y, vec2(layerDetile.y, layerDetileScale.y)) : 0.5) * w.y;
    if (w.z > 0.004)
      r += (layerHasRough.z == 1 ? layer_rough(layerNormal2, fragTex, layerTile.z, vec2(layerDetile.z, layerDetileScale.z)) : 0.5) * w.z;
    if (w.w > 0.004)
      r += (layerHasRough.w == 1 ? layer_rough(layerNormal3, fragTex, layerTile.w, vec2(layerDetile.w, layerDetileScale.w)) : 0.5) * w.w;
  }
  if (w.x > 0.004) {
    albedo += layer_sample(layer0, fragTex, n, layerTile.x, vec2(layerDetile.x, layerDetileScale.x)).rgb * layerColor[0].rgb * w.x;
    nd += (layerHasNormal.x == 1 ? layer_normal(layerNormal0, fragTex, n, layerTile.x, vec2(layerDetile.x, layerDetileScale.x)) : n) * w.x;
  }
  if (w.y > 0.004) {
    albedo += layer_sample(layer1, fragTex, n, layerTile.y, vec2(layerDetile.y, layerDetileScale.y)).rgb * layerColor[1].rgb * w.y;
    nd += (layerHasNormal.y == 1 ? layer_normal(layerNormal1, fragTex, n, layerTile.y, vec2(layerDetile.y, layerDetileScale.y)) : n) * w.y;
  }
  if (w.z > 0.004) {
    albedo += layer_sample(layer2, fragTex, n, layerTile.z, vec2(layerDetile.z, layerDetileScale.z)).rgb * layerColor[2].rgb * w.z;
    nd += (layerHasNormal.z == 1 ? layer_normal(layerNormal2, fragTex, n, layerTile.z, vec2(layerDetile.z, layerDetileScale.z)) : n) * w.z;
  }
  if (w.w > 0.004) {
    albedo += layer_sample(layer3, fragTex, n, layerTile.w, vec2(layerDetile.w, layerDetileScale.w)).rgb * layerColor[3].rgb * w.w;
    nd += (layerHasNormal.w == 1 ? layer_normal(layerNormal3, fragTex, n, layerTile.w, vec2(layerDetile.w, layerDetileScale.w)) : n) * w.w;
  }
  n = length(nd) > 1e-4 ? normalize(nd) : n;
  // terrain3d_layer::relief: the shape's own normal, the layers' bumps tilting it.
  if (dot(fragRelief, fragRelief) > 0.25)
    n = normalize(n - ground + fragRelief);
  // Marks pressed into it: the normal tilts with their slopes, the pressed part tints.
  float pressed = 0.0;
  vec2 into = (fragPos.xz - imprintWin.xy) / max(imprintWin.z * imprintWin.w, 1e-4);
  if (imprintWin.w > 0.0 && all(greaterThan(into, vec2(0.002))) && all(lessThan(into, vec2(0.998)))) {
    vec2 here = imprint_at(fragPos.xz);
    pressed = here.x;
    if (here.x + here.y > 0.002 || imprint_at(fragPos.xz + vec2(imprintWin.z * 2.0, 0.0)).x > 0.002 ||
        imprint_at(fragPos.xz - vec2(imprintWin.z * 2.0, 0.0)).x > 0.002) {
      float e = imprintWin.z;
      float hx = imprint_height(fragPos.xz + vec2(e, 0.0)) - imprint_height(fragPos.xz - vec2(e, 0.0));
      float hz = imprint_height(fragPos.xz + vec2(0.0, e)) - imprint_height(fragPos.xz - vec2(0.0, e));
      n = normalize(n + vec3(-hx, 0.0, -hz) / (2.0 * e));
      albedo *= mix(vec3(1.0), imprintTint.rgb, pressed * imprintTint.a);
    }
  }
  // Beyond the grass's reach, where it would grow, the ground takes on the
  // field's colour as the blades shrink away, mottled in hand-sized tufts
  // (the finer ones only while a pixel is smaller than them), so a far
  // meadow still reads as grass rather than bare soil.
  if (grassFar.a > 0.0) {
    float reach = smoothstep(grassFade.x, grassFade.y, distance(viewPos.xz, fragPos.xz));
    int gl = int(grassFade.z);
    float cover = (gl < 0 ? 1.0 : w[gl]) * smoothstep(grassSlope.x, grassSlope.y, up) *
                  smoothstep(grassFade.w, grassFade.w + 0.4, fragPos.y);
    float k = reach * cover * grassFar.a;
    if (k > 0.0) {
      float fine = 1.0 - smoothstep(0.04, 0.1, length(foot));
      float tuft = detile_noise(fragPos.xz * 2.5) * 0.6 + mix(0.5, detile_noise(fragPos.xz * 8.0), fine) * 0.4;
      vec3 field = mix(albedo, grassFar.rgb, 0.6) * mix(0.7, 1.0, tuft); // the blades shade each other
      albedo = mix(albedo, field, k);
    }
  }
  // Snow lies on the flat in drifts, broad and small, that close up as it
  // deepens; it smooths the layers' bumps away. Slopes and marks stay bare.
  float snowy = 0.0;
  if (snowLayer.a > 0.0) {
    float drift = detile_noise(fragPos.xz * 0.07) * 0.55 + detile_noise(fragPos.xz * 0.5 + 7.0) * 0.3 +
                  detile_noise(fragPos.xz * 3.1 + 13.0) * 0.15;
    float edge = 1.0 - snowLayer.a * 1.25;
    snowy = smoothstep(edge - 0.06, edge + 0.06, drift) * smoothstep(0.62, 0.86, up) * (1.0 - pressed);
    albedo = mix(albedo, snowLayer.rgb, snowy);
    n = normalize(mix(n, normalize(texture(normalMap, uv).xyz * 2.0 - 1.0), snowy * 0.7));
  }
  // Wet ground is darker, most on the flat where water stands; dry under cover and snow.
  float wet = wetness * smoothstep(0.6, 0.95, n.y) * (1.0 - covered(fragPos)) * (1.0 - snowy);
  albedo *= mix(1.0, 0.6, wet);
  // Water over it shines whatever the ground's own roughness.
  if (rough)
    surfRough = mix(mix(clamp(r, 0.0, 1.0), 0.5, wet), 0.6, snowy);
  vec3 color = shade(albedo, n, fragPos, 1.0, vec3(0.0));
  color = glitter_add(color, glitter_at(fragPos, n, max(dot(w, layerGlitter), snowy) * (1.0 - pressed)));
  finalColor = vec4(color, 1.0);
}
)";

const char *const terrain_depth_vs_main = R"(
invariant gl_Position;
void main() {
  if (reliefOn == 0) {
    ivec2 s = min(ivec2(chunkInstance.xy + 0.5) + ivec2(vertexPosition.xz + 0.5), sampleLast);
    float h = texelFetch(heightMap, s, 0).r - vertexPosition.y * chunkInstance.z;
    gl_Position = mvp * vec4(terrainGrid.x + float(s.x) * terrainGrid.z, h, terrainGrid.y + float(s.y) * terrainGrid.z, 1.0);
    return;
  }
  vec3 base;
  vec3 world = relief_point(chunkInstance.xy + vertexPosition.xz, base);
  world.y -= vertexPosition.y * chunkInstance.z;
  gl_Position = mvp * vec4(world, 1.0);
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
layout(location = 14) in vec4 instance2;   // tint
uniform mat4 mvp;
uniform vec3 eyePos;
uniform float time;
uniform vec2 wind;
uniform float sway;
uniform float curve;                       // the blade's own bend, 0..1
uniform vec2 fade;                         // start and end of the fade, metres
uniform float widen;                       // the cell's blades thinned out by this much: wider by as much
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
  // Arched off the flat of the blade, each its own way round.
  vec3 arch = vec3(-face.y, 0.0, face.x) * (fract(instance1.w * 7.13) < 0.5 ? -1.0 : 1.0);
  float own = curve * (0.6 + 0.4 * fract(instance1.w * 3.71));
  vec3 lean = (vec3(dir.x, 0.0, dir.y) * bend + arch * own) * h * t2;
  float drop = 0.5 * (bend * bend + own * own) * h * t2 * t;
  float w = instance1.y * (1.0 - 0.85 * t) * (0.4 + 0.6 * k) * widen;
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
out float fragTint;
out vec3 fragGround; // grass3d_desc::ground_color: the ground's colour at the root, made on the CPU
invariant gl_Position;
void main() {
  float t;
  vec2 face;
  vec3 p = blade(t, face);
  vec2 g = (instance0.xz - terrainGrid.xy) / terrainGrid.z;
  vec3 ground = normalize(texture(normalMap, (g + 0.5) / gridSize).xyz * 2.0 - 1.0);
  fragGround = instance2.yzw;
  // Lit mostly like the ground it grows on: a field reads as one surface.
  fragNormal = normalize(ground * 0.8 + vec3(-face.y, 0.0, face.x) * 0.2 * (fract(instance1.w) - 0.5));
  fragPos = p;
  fragT = t;
  fragShade = instance1.z;
  fragTint = instance2.x;
  gl_Position = mvp * vec4(p, 1.0);
}
)";

const char *const grass_depth_vs_main = R"(
invariant gl_Position;
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
in float fragTint;
in vec3 fragGround;
uniform vec3 baseColor;
uniform vec3 tipColor;
uniform vec3 tintColor;
uniform float groundColor;
uniform float rootShade;
void main() {
  vec3 base = mix(baseColor, fragGround, groundColor);
  vec3 c = mix(base, mix(tipColor, tintColor, fragTint), fragT) * fragShade;
  c *= mix(rootShade, 1.0, smoothstep(0.0, 0.45, fragT)); // the field shades its own roots
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
uniform vec2 skyDrift;     // how far the wind has blown so far, metres
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
  vec2 p = (eye.xz + dir.xz * t + skyDrift * 3.0) / scale;
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
uniform int hdrOut;        // light3d::hdr: linear and exposed
uniform float hdrExposure;
out vec4 finalColor;
void main() {
  vec4 p = invViewProj * vec4(fragNdc, 1.0, 1.0);
  vec3 dir = normalize(p.xyz / p.w - eyePos);
  vec3 c = sky_color(dir, eyePos, true);
  if (hdrOut == 1) {
    c = max(c, vec3(0.0));
    c = mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c)) * hdrExposure;
  }
  finalColor = vec4(c, 1.0);
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
uniform vec4 waterIce;    // water3d_desc::ice, ice_snow
out vec3 fragPos;
out vec2 fragRest;
void main() {
  vec2 rest = waterOrigin.xz + vertexPosition.xz;
  vec3 n;
  float crest;
  vec3 off = gerstner(rest, vertexPosition.y, n, crest) * (1.0 - waterIce.x); // ice calms them
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
uniform vec2 waterBed;     // 1 when it has a flat bed (water3d_desc::bed), the bed's height
uniform int waterLamps;    // 1: the point and spot lights shine on it too
uniform vec4 waterPour;    // where a stream falls in (x, z), its strength, how far its rings reach
uniform vec4 waterRound;   // a round lake's centre (x, z) and half size; zero size: a rectangle
uniform mat4 mvp;
uniform sampler2D sceneColor; // refracting or reflecting: the opaque scene drawn so far
uniform sampler2D sceneDepth;
uniform vec4 waterScene;      // 1 when refracting, its strength, the camera's near and far
uniform vec3 waterScreen;     // the scene's size in pixels; 1 for an orthographic camera
uniform vec4 waterLook;       // caustics, caustic cell size, scene reflection (0: no scene)
uniform vec3 waterAbsorb;     // refracting: the light of each colour left after a metre of water
uniform vec4 waterRings[8];   // things fallen in: where (x, z), seconds since, strength (0: none)
uniform float waterRingSize[8]; // and how big each was, metres
uniform float waterRain;      // the sky's rain times water3d_desc::rain_rings, 0: none
uniform vec4 waterIce;        // water3d_desc::ice, ice_snow
uniform vec3 iceColor;
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

// Rain on the water: one drop at a time in each cell of a grid (cell units),
// at its own place and moment, each a ring running out and dying within a
// round; `rain` of the rounds have a drop. The slope the rings give here.
vec2 rain_rings(vec2 p, float rain, float t) {
  vec2 grad = vec2(0.0);
  vec2 i = floor(p);
  for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++) {
      vec2 c = i + vec2(x, y);
      float cycle = t * 1.4 + sky_hash(c) * 7.0;
      float turn = floor(cycle), age = fract(cycle);
      if (sky_hash(c + turn * 0.37 + 3.1) > rain)
        continue;
      vec2 at = c + vec2(sky_hash(c + turn + 11.7), sky_hash(c + turn + 23.3));
      vec2 dp = p - at;
      float d = length(dp);
      float x0 = (d - age * 0.9) / 0.1; // across the ring's front
      float fade = (1.0 - age) * (1.0 - age);
      grad += dp / max(d, 1e-4) * sin(x0 * 3.14159) * exp(-x0 * x0) * fade;
    }
  return grad;
}

// How far from the camera the scene is at `uv`, along its view.
float scene_depth(vec2 uv, float near, float far) {
  float z = texture(sceneDepth, uv).r;
  return waterScreen.z > 0.5 ? mix(near, far, z) : 2.0 * near * far / (far + near - (z * 2.0 - 1.0) * (far - near));
}

// Caustics: where the waves focus the sun on the bed, the bright seams
// between the cells of a drifting cell pattern; two of them, crossed.
float caustic_seams(vec2 p, float soft) {
  vec2 i = floor(p), f = fract(p);
  float f1 = 8.0, f2 = 8.0;
  for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 1; x++) {
      vec2 c = vec2(x, y);
      vec2 h = vec2(sky_hash(i + c), sky_hash(i + c + 17.31));
      vec2 o = c + 0.5 + 0.4 * sin(time * 0.8 + 6.2831853 * h) - f;
      float d = dot(o, o);
      if (d < f1) {
        f2 = f1;
        f1 = d;
      } else if (d < f2) {
        f2 = d;
      }
    }
  float s = 1.0 - smoothstep(0.0, 0.07 + soft, sqrt(f2) - sqrt(f1));
  return s * s * s;
}
// Bent by a slow noise so the cells are not straight sided and not all one
// size; bright mostly where the two cross, faint elsewhere, and only in
// scattered patches that drift, so the bed is not covered by one even net.
float caustic_web(vec2 p, float soft) {
  vec2 warp = vec2(sky_noise(p * 0.45 + time * 0.07), sky_noise(p * 0.45 + vec2(4.7, 2.3) - time * 0.06));
  p += (warp - 0.5) * 1.4;
  p *= 0.8 + 0.45 * sky_noise(p * 0.13 + vec2(2.9, 8.1));
  p += (vec2(sky_noise(p * 1.6 + time * 0.15), sky_noise(p * 1.6 + vec2(9.1, 3.3))) - 0.5) * 0.45;
  float a = caustic_seams(p, soft), b = caustic_seams(p * 1.37 + vec2(5.2, 1.7), soft);
  float patches = smoothstep(0.4, 0.8, sky_noise(p * 0.17 - time * 0.03));
  return (0.15 * (a + b) + 1.1 * a * b) * patches;
}

// Reflecting the scene: the reflected ray marched across the screen until it
// passes behind what was drawn there. `w`: how much to trust the hit, 0 for none.
vec3 scene_reflection(vec3 from, vec3 dir, float here, out float w) {
  w = 0.0;
  float near = waterScene.z, far = waterScene.w;
  float stepLen = max(here * 0.012, 0.05), t = stepLen, last = 0.0;
  for (int i = 0; i < 48; i++) {
    vec4 c = mvp * vec4(from + dir * t, 1.0);
    if (c.w <= near)
      return vec3(0.0);
    vec3 q = c.xyz / c.w * 0.5 + 0.5;
    if (q.x < 0.0 || q.y < 0.0 || q.x > 1.0 || q.y > 1.0)
      return vec3(0.0);
    float ray = waterScreen.z > 0.5 ? mix(near, far, q.z) : c.w;
    float behind = ray - scene_depth(q.xy, near, far);
    if (behind > 0.0) {
      if (behind > stepLen * 3.0 + 0.3)
        return vec3(0.0); // passed behind something thick in front: unknown
      // Back and forth onto the surface it went behind.
      float lo = last, hi = t;
      for (int k = 0; k < 5; k++) {
        float mid = (lo + hi) * 0.5;
        vec4 m = mvp * vec4(from + dir * mid, 1.0);
        vec3 mq = m.xyz / m.w * 0.5 + 0.5;
        float md = waterScreen.z > 0.5 ? mix(near, far, mq.z) : m.w;
        if (md > scene_depth(mq.xy, near, far))
          hi = mid;
        else
          lo = mid;
      }
      vec4 h = mvp * vec4(from + dir * hi, 1.0);
      vec2 uv = h.xy / h.w * 0.5 + 0.5;
      vec2 edge = smoothstep(vec2(0.0), vec2(0.08), uv) * smoothstep(vec2(1.0), vec2(0.92), uv);
      w = edge.x * edge.y;
      vec3 hit = texture(sceneColor, uv).rgb;
      // Back out of the HDR image into the water's own colours.
      return hdrOut == 1 ? linear_to_srgb(hit / max(exposure, 1e-4)) : hit;
    }
    last = t;
    t += stepLen;
    stepLen *= 1.08;
  }
  return vec3(0.0);
}

void main() {
  // A round lake: an ellipse in its rectangle, its edge wandering in and out,
  // the water thinning toward it.
  float edgeFade = 1.0;
  if (waterRound.z > 0.0) {
    vec2 q = (fragRest - waterRound.xy) / waterRound.zw;
    float edge = 1.0 - 0.22 * ripple_noise(q * 2.2 + waterRound.xy * 7.0);
    float r = length(q) / edge;
    if (r > 1.0)
      discard;
    edgeFade = 1.0 - smoothstep(0.8, 1.0, r);
  }
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
  // A stream falling in: rings running out from where it lands, ragged by a
  // little noise, and churned water right under it.
  float churn = 0.0;
  if (waterPour.z > 0.0) {
    float reach = max(waterPour.w, 0.01);
    vec2 dp = fragRest - waterPour.xy;
    float d = length(dp);
    float k = 62.831853 / reach; // ten rings in the reach
    float ragged = ripple_noise(dp * (40.0 / reach) + time * 1.3);
    float wave = cos(d * k - time * k * reach * 0.8 + ragged * 2.5);
    float fade = exp(-3.0 * d / reach) * smoothstep(0.0, reach * 0.04, d);
    float keep = 1.0 - smoothstep(0.5, 1.0, spacing * k * 0.16); // rings finer than a pixel fade out
    n = normalize(n - vec3(dp / max(d, 1e-4), 0.0).xzy * wave * fade * keep * 0.45 * waterPour.z);
    churn = waterPour.z * (1.0 - smoothstep(reach * 0.04, reach * 0.18, d));
    if (churn > 0.0) {
      vec2 q = dp * (90.0 / reach) + vec2(time * 2.1, -time * 1.7);
      float e = 0.1;
      vec2 grad = vec2(ripple_noise(q + vec2(e, 0.0)) - ripple_noise(q - vec2(e, 0.0)),
                       ripple_noise(q + vec2(0.0, e)) - ripple_noise(q - vec2(0.0, e))) / (2.0 * e);
      n = normalize(n - vec3(grad.x, 0.0, grad.y) * 0.25 * churn);
    }
  }
  // Things fallen in: a train of rings running out from each, a few waves
  // behind its front, dying away; churned for a moment where it went in.
  for (int i = 0; i < 8; i++) {
    float strength = waterRings[i].w;
    if (strength <= 0.0)
      continue;
    float age = waterRings[i].z, size = waterRingSize[i];
    float life = exp(-age / (1.2 + size * 4.0));
    if (life < 0.02)
      continue;
    vec2 dp = fragRest - waterRings[i].xy;
    float d = length(dp);
    float wl = max(size * 0.45, 0.025);    // wavelength
    float front = (0.18 + size * 1.2) * age + size * 0.3;
    float behind = front - d;               // > 0 inside the front
    float train = exp(-pow((behind - wl * 1.5) / (wl * 1.8), 2.0));
    float spread = 1.0 / (1.0 + d / max(size, 0.02));
    float wave = sin(6.2831853 * behind / wl);
    float keep = 1.0 - smoothstep(0.5, 1.0, spacing * 6.2831853 / wl * 0.16);
    n = normalize(n - vec3(dp / max(d, 1e-4), 0.0).xzy * wave * train * spread * life * strength * keep * 1.2);
    churn = max(churn, strength * exp(-age / 0.35) * (1.0 - smoothstep(size * 0.25, size * 0.7, d)));
  }
  // Rain: two grids of drops half a metre and a third apart; where a ring is
  // finer than a pixel, a rough speckle in its place.
  if (waterRain > 0.0) {
    float keep = 1.0 - smoothstep(0.03, 0.09, spacing);
    if (keep > 0.0) {
      vec2 g = rain_rings(fragRest / 0.5, waterRain, time) + rain_rings(fragRest / 0.33 + 5.3, waterRain, time + 0.37);
      n = normalize(n - vec3(g.x, 0.0, g.y) * 0.35 * keep);
    }
    vec2 q = fragRest * 9.0 + vec2(time * 3.1, -time * 2.7);
    float e = 0.1;
    vec2 grad = vec2(ripple_noise(q + vec2(e, 0.0)) - ripple_noise(q - vec2(e, 0.0)),
                     ripple_noise(q + vec2(0.0, e)) - ripple_noise(q - vec2(0.0, e))) / (2.0 * e);
    n = normalize(n - vec3(grad.x, 0.0, grad.y) * 0.05 * waterRain * (1.0 - keep));
  }
  vec3 v = normalize(viewPos - fragPos);
  if (dot(n, v) < 0.0)
    n = normalize(n + v * (0.01 - dot(n, v)));
  float depth = 1000.0;
  if (hasGround == 1)
    depth = fragPos.y - ground_at(fragPos.xz);
  else if (waterBed.x > 0.0)
    depth = fragPos.y - waterBed.y;
  float still = depth; // straight down, for where the ice reaches
  // Refracting: what lies under the water, seen along the ray bent into it
  // (n = 1.33), so it shows nearer the surface and wavers with the waves;
  // the depth is how far that ray runs through the water to it.
  vec3 under = vec3(0.0);
  bool see = waterScene.x > 0.0;
  float here = waterScreen.z > 0.5
                   ? mix(waterScene.z, waterScene.w, gl_FragCoord.z)
                   : 2.0 * waterScene.z * waterScene.w /
                         (waterScene.w + waterScene.z - (gl_FragCoord.z * 2.0 - 1.0) * (waterScene.w - waterScene.z));
  vec3 bent = refract(-v, n, 0.75);
  if (see) {
    vec2 suv = gl_FragCoord.xy / waterScreen.xy;
    float near = waterScene.z, far = waterScene.w;
    float straight = scene_depth(suv, near, far) - here;
    float run = clamp(straight, 0.0, 1.0);
    vec4 a = mvp * vec4(fragPos + bent * run, 1.0), b = mvp * vec4(fragPos - v * run, 1.0);
    vec2 ruv = clamp(suv + (a.xy / a.w - b.xy / b.w) * 0.5 * waterScene.y, vec2(0.001), vec2(0.999));
    float through = scene_depth(ruv, near, far) - here;
    if (through < 0.0) { // bent onto something above the water: look straight
      ruv = suv;
      through = straight;
    }
    under = texture(sceneColor, ruv).rgb;
    depth = max(through, 0.0) * max(abs(bent.y), 0.2);
  }
  if (depth < -0.05)
    discard;
  depth = max(depth, 0.0);
  float deep = 1.0 - exp(-depth / max(waterParams.x, 0.01));
  vec3 body = mix(shallowColor, deepColor, deep);
  vec3 l = -lightDir;
  float sun = sun_share(fragPos, vec3(0.0, 1.0, 0.0), l);
  // Caustics on the bed, where the ray seen through the water lands, drawn
  // where the sunlight reaching that spot came through the surface (so they
  // ride with the waves); a little apart in red and blue, as a prism splits.
  vec3 caustic = vec3(0.0);
  if (waterLook.x > 0.0 && l.y > 0.0 && depth < 1000.0) {
    float size = max(waterLook.y, 0.01);
    float far = smoothstep(0.12, 0.45, spacing / size);
    float shallow = smoothstep(0.0, 0.12, depth) * exp(-depth / max(waterParams.x * 0.5, 0.05));
    if (far < 1.0 && shallow > 0.01) {
      vec2 bed = fragPos.xz + bent.xz / max(-bent.y, 0.25) * depth;
      float lit = sun_share(vec3(bed.x, fragPos.y - depth, bed.y), vec3(0.0, 1.0, 0.0), l) * l.y;
      vec2 p = (bed + l.xz / max(l.y, 0.2) * depth) / size + n.xz * 0.8;
      vec2 split = vec2(0.012, 0.008) * (1.0 + depth);
      float soft = spacing / size;
      caustic = vec3(caustic_web(p + split, soft), caustic_web(p, soft), caustic_web(p - split, soft));
      caustic *= lightColor * waterLook.x * lit * shallow * (1.0 - far);
    }
  }
  // The lamps, as shade() lights everything else, their glints as sharp as the sun's.
  vec3 lampLight = vec3(0.0), lampGlint = vec3(0.0);
  if (waterLamps == 1)
    for (int i = 0; i < lightCount; i++) {
      vec3 d = lightPos[i].xyz - fragPos;
      float dist = length(d);
      float reach = lightPos[i].w;
      if (dist >= reach)
        continue;
      vec3 li = d / max(dist, 1e-5);
      float fall = 1.0 - dist / reach;
      float att = fall * fall;
      if (lightSpot[i].w > -1.5) {
        float co = lightSpot[i].w;
        float ci = lightColors[i].w;
        att *= clamp((dot(-li, lightSpot[i].xyz) - co) / max(ci - co, 1e-4), 0.0, 1.0);
      }
      if (att > 0.0 && lightShadow[i] >= 0)
        att *= lamplight(lightShadow[i], lightSpot[i].w < -1.5, lightPos[i].xyz, fragPos, vec3(0.0, 1.0, 0.0), li);
      lampLight += lightColors[i].rgb * max(dot(n, li), 0.0) * att;
      lampGlint += lightColors[i].rgb * pow(max(dot(n, normalize(li + v)), 0.0), max(waterSpec.y, 1.0)) * att;
    }
  if (waterLamps == 1) {
    // The extended lights (light3d::lights_max), the same way.
    int first, count;
    lamp_range(fragPos, first, count);
    for (int j = 0; j < count; j++) {
      lamp a = lamp_at(lamp_index(first, j));
      vec3 li;
      float att = lamp_share_ext(a, fragPos, vec3(0.0, 1.0, 0.0), li);
      if (att <= 0.0)
        continue;
      lampLight += a.color.rgb * max(dot(n, li), 0.0) * att;
      lampGlint += a.color.rgb * pow(max(dot(n, normalize(li + v)), 0.0), max(waterSpec.y, 1.0)) * att;
    }
  }
  vec3 lit = body * (ambient + (lightColor * max(dot(n, l), 0.0) * sun + lampLight) * 0.4);
  vec3 r = reflect(-v, n);
  r.y = abs(r.y);
  vec3 refl = sky_color(r, viewPos, true);
  float fres = 0.02 + 0.98 * pow(1.0 - max(dot(n, v), 0.0), 5.0);
  // The scene in the water where the reflected ray finds it on screen, more
  // of it than Fresnel gives as the strength goes up; no sun glint off it.
  float mirror = 0.0;
  if (waterLook.z > 0.0) {
    vec3 seen = scene_reflection(fragPos, normalize(vec3(r.x, max(r.y, 0.02), r.z)), here, mirror);
    refl = mix(refl, seen, mirror);
  }
  float share = fres + (1.0 - fres) * mirror * waterLook.z * 0.6;
  vec3 col = mix(lit, refl, share);
  if (!see) // not refracting: the light gathers on the water layer over the bed
    col += caustic * 0.5 * (1.0 - deep);
  vec3 hv = normalize(l + v);
  col += (lightColor * pow(max(dot(n, hv), 0.0), max(waterSpec.y, 1.0)) * sun * (1.0 - mirror) + lampGlint) *
         waterSpec.x;
  float speck = ripple_noise(fragRest * 2.3 + time * 0.2) * 0.6 + ripple_noise(fragRest * 5.1 - time * 0.3) * 0.4;
  float shore = waterParams.z > 0.0 ? 1.0 - smoothstep(0.0, waterParams.z, depth) : 0.0;
  // A thin line at the water's edge, broken into patches further out.
  float foam = smoothstep(0.55, 0.85, speck * 0.75 + shore * 0.45) * shore;
  foam = max(foam, waterParams.w * smoothstep(0.55, 0.95, crest) * smoothstep(0.35, 0.7, speck));
  if (churn > 0.0) {
    float fizz = ripple_noise((fragRest - waterPour.xy) * (120.0 / max(waterPour.w, 0.01)) + time * 4.0);
    foam = max(foam, churn * smoothstep(0.45, 0.75, fizz) * 0.8);
  }
  foam = clamp(foam, 0.0, 1.0);
  col = mix(col, foamColor * (ambient + lightColor * max(dot(n, l), 0.0) * sun + lampLight), foam);
  float alpha = clamp(1.0 - exp(-depth / max(waterParams.y, 0.01)), 0.0, 1.0);
  alpha = max(max(alpha, share), foam) * edgeFade;
  // Ice, from the shallows out, its edge ragged: flat and milky, mottled with
  // frost, a pale sky in it and a soft glint; snow in drifts on top.
  if (waterIce.x > 0.0) {
    float mottle = ripple_noise(fragRest * 0.9) * 0.6 + ripple_noise(fragRest * 4.3 + 3.7) * 0.4;
    float ragged = ripple_noise(fragRest * 0.3) * 0.7 + ripple_noise(fragRest * 1.1 + 9.0) * 0.3;
    float reach = waterIce.x * 1.3 - (1.0 - exp(-max(still, 0.0) / 1.5)) + (ragged - 0.5) * 0.12;
    float frozen = smoothstep(-0.03, 0.03, reach);
    if (frozen > 0.0) {
      vec3 up = vec3(0.0, 1.0, 0.0);
      vec3 ice = iceColor * mix(0.86, 1.06, mottle);
      float drift = ripple_noise(fragRest * 0.07) * 0.55 + ripple_noise(fragRest * 0.5 + 7.0) * 0.3 +
                    ripple_noise(fragRest * 3.1 + 13.0) * 0.15;
      float edge = 1.0 - waterIce.y * 1.25;
      float snowy = waterIce.y > 0.0 ? smoothstep(edge - 0.06, edge + 0.06, drift) : 0.0;
      ice = mix(ice, vec3(0.95, 0.96, 1.0), snowy);
      vec3 iceLit = ice * (ambient + lightColor * max(l.y, 0.0) * sun + lampLight);
      float iceFres = (0.02 + 0.98 * pow(1.0 - max(v.y, 0.0), 5.0)) * 0.35 * (1.0 - snowy);
      iceLit = mix(iceLit, sky_color(reflect(-v, up), viewPos, true), iceFres);
      iceLit += lightColor * pow(max(dot(up, normalize(l + v)), 0.0), 60.0) * sun * 0.25 * (1.0 - snowy);
      col = mix(col, iceLit, frozen);
      alpha = mix(alpha, edgeFade, frozen);
    }
  }
  if (!see)
    alpha = max(alpha, max(caustic.r, max(caustic.g, caustic.b)) * 0.5 * edgeFade);
  if (fogDensity > 0.0) {
    float f = fogDensity * length(viewPos - fragPos);
    col = mix(fogColor, col, exp(-f * f));
  }
  // Into the HDR image (light3d::hdr): linear and exposed, as lit things are;
  // the scene under it is already.
  if (hdrOut == 1)
    col = srgb_to_linear(col) * exposure;
  // Refracting, the water lays itself over the bent view of what is under it,
  // brightened where the caustics fall, darkened and coloured along the
  // light's way down through the water and back up to the eye.
  under *= (1.0 + caustic * 1.5) * pow(waterAbsorb, vec3(depth * (1.0 + 1.0 / max(abs(bent.y), 0.2))));
  finalColor = see ? vec4(mix(under, col, alpha), 1.0) : vec4(col, alpha);
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
uniform vec2 drift;      // how far the wind has blown so far, metres
uniform vec4 precip;     // box width, box height, fall speed, 1 for snow
uniform vec2 flakeGrid;  // weather3d_snow_flakes_set's columns and rows, 0 for soft dots
uniform float sparkle;   // weather3d_snow_flakes_set's glints on the near flakes, 0 off
out vec2 fragUv;
flat out vec2 fragCell;  // the flake's cell in flakeGrid
out float fragFade;
out float fragGlint;     // how much this flake catches the light now, 0..sparkle
out float fragGrow;      // the quad's size over the flake's, wider for a glint's star
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
  // Carried by the wind's drift, not wind times time: a changing wind would jump every drop.
  vec3 moved = vec3(drift.x, -precip.z * speed * time, drift.y) * (snow ? 0.6 : 1.0);
  vec3 p = corner + mod(r * box + moved - corner, box);
  if (snow)
    p.xz += vec2(sin(time * 1.3 + r.x * 40.0), cos(time * 1.1 + r.z * 40.0)) * 0.3;
  vec3 to_eye = normalize(eyePos - p);
  vec3 pos;
  fragCell = vec2(0.0);
  fragGlint = 0.0;
  fragGrow = 1.0;
  if (snow) {
    vec3 right = normalize(cross(vec3(0.0, 1.0, 0.0), to_eye));
    vec3 up = cross(to_eye, right);
    float size = 0.035 + 0.035 * r.x;
    if (flakeGrid.x > 0.0) {
      // A shape: its own cell, turning slowly one way or the other as it falls,
      // a little larger since a shape fills less of its quad than a dot.
      vec3 k = hash3(id + 32452843u);
      fragCell = floor(k.xy * flakeGrid);
      float spin = k.z * 6.2832 + time * (k.x - 0.5) * 2.4;
      vec3 r0 = right * cos(spin) + up * sin(spin);
      up = up * cos(spin) - right * sin(spin);
      right = r0;
      size *= 1.4;
      // A crystal face turns to the light six times a turn; a slow wobble of
      // its own keeps the flakes that hardly turn from never catching it.
      float facet = 3.0 * spin + time * (1.5 + 2.0 * k.y) + k.z * 40.0;
      float near = 1.0 - smoothstep(2.0, 6.0, distance(eyePos, p));
      fragGlint = sparkle * near * pow(0.5 + 0.5 * cos(facet), 24.0);
      fragGrow = 1.0 + 2.0 * min(fragGlint, 1.0);
      size *= fragGrow;
    }
    pos = p + (right * (vertexPosition.x - 0.5) + up * (vertexPosition.y - 0.5)) * size;
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
flat in vec2 fragCell;
in float fragFade;
in float fragGlint;
in float fragGrow;
uniform vec3 ambient;
uniform vec3 lightColor;
uniform vec4 precip;
uniform vec2 flakeGrid;
uniform sampler2D flakeMap;
uniform float amount;
uniform int hdrOut;        // light3d::hdr: linear and exposed
uniform float hdrExposure;
out vec4 finalColor;
void main() {
  float a;
  vec3 c;
  if (precip.w > 0.5) {
    if (flakeGrid.x > 0.0) {
      vec2 uv = (fragUv - 0.5) * fragGrow + 0.5; // the flake keeps its size in a grown quad
      bool inside = all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
      a = inside ? texture(flakeMap, (fragCell + vec2(uv.x, 1.0 - uv.y)) / flakeGrid).a : 0.0;
    } else
      a = 1.0 - smoothstep(0.55, 1.0, length(fragUv - 0.5) * 2.0);
    c = vec3(1.0) * (ambient * 1.1 + lightColor * 0.45);
    a *= 0.9;
    // A glint: the flake flares, with a soft core and a thin four-pointed star
    // reaching past it to the edges of the grown quad.
    vec2 q = abs(fragUv - 0.5);
    float star = exp(-q.x * 60.0 - q.y * 6.0) + exp(-q.y * 60.0 - q.x * 6.0) + exp(-length(q) * 12.0);
    a = max(a, min(star, 1.0) * min(fragGlint, 1.0));
  } else {
    a = (1.0 - abs(fragUv.x - 0.5) * 2.0) * smoothstep(0.0, 0.35, fragUv.y) * 0.45;
    c = vec3(0.85, 0.9, 1.0) * (ambient * 1.8 + lightColor * 0.3 + 0.08);
  }
  c = min(c, vec3(1.0));
  if (hdrOut == 1)
    c = mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c)) * hdrExposure;
  // Past white where the output is linear, so the bloom takes it.
  c += (lightColor + 0.3) * fragGlint * (hdrOut == 1 ? 4.0 * hdrExposure : 1.0);
  if (hdrOut != 1)
    c = min(c, vec3(1.0));
  finalColor = vec4(c, a * fragFade * amount);
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
gpu_mesh make_grid(i32 n, f32 step) {
  std::vector<f32> xyz;
  std::vector<u16> idx;
  const auto vert = [&](i32 x, i32 z, f32 skirt) {
    xyz.insert(xyz.end(), {(f32)x * step, skirt, (f32)z * step});
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

gpu_mesh make_chunk_grid(i32 chunk, i32 step) { return make_grid(chunk / step, (f32)step); }

// terrain3d_desc::relief_detail: the vertices a cell of a relief grid over
// `quads` x `quads` cells, no more than 16-bit indices allow.
i32 relief_detail(i32 quads, i32 want) { return std::clamp(want, 1, std::clamp(250 / std::max(quads, 1), 1, 8)); }

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

// Uniform locations by program and name, so a frame does not ask the driver
// for each by its name again. Every name passed is a string literal (or an
// entry of a table of them), so its address is the key. Cleared when the
// shaders go (~world3d_gpu): a later context may get the same program ids.
std::map<std::pair<u32, const char *>, i32> &uniform_cache() {
  thread_local std::map<std::pair<u32, const char *>, i32> cache;
  return cache;
}

i32 location(u32 program, const char *name) {
  auto &cache = uniform_cache();
  const auto [it, added] = cache.try_emplace({program, name}, -1);
  if (added)
    it->second = rlGetLocationUniform(program, name);
  return it->second;
}

// The instance buffers of draw_chunks, taken in turn: a pass draws a terrain's
// levels of detail with one each, and a frame has several passes, so a buffer
// is written again only long after the GPU read it.
struct chunk_ring {
  static constexpr usize size = 64;
  u32 vbo[size]{};
  i32 capacity[size]{}; // bytes
  usize next = 0;
};

struct world3d_gpu {
  bool ready = false;
  bool failed = false;
  chunk_ring chunks;
  Shader terrain{}, terrain_depth{}, grass{}, grass_depth{}, sky{}, water{}, precip{};
  render3d_locations terrain_locs, grass_locs, water_locs;
  // The other variant of the lit shaders (world3d_use_ext): the extended one
  // while the plain one is in use, and the other way round.
  Shader terrain_alt{}, grass_alt{}, water_alt{};
  render3d_locations terrain_locs_alt, grass_locs_alt, water_locs_alt;
  bool ext_tried = false, ext_ok = false, ext_active = false;
  std::vector<std::pair<i64, gpu_mesh>> grids; // (chunk << 8 | step) -> grid
  gpu_mesh blade, blade_mid, blade_low, sky_tri, precip_mesh, ocean;
  f32 ocean_detail = 0.0f;
  std::vector<lake_mesh> lakes;

  ~world3d_gpu() {
    uniform_cache().clear();
    if (!ready)
      return;
    for (auto &g : grids)
      free_mesh(g.second);
    for (u32 vbo : chunks.vbo)
      if (vbo != 0)
        rlUnloadVertexBuffer(vbo);
    for (lake_mesh &l : lakes)
      free_mesh(l.mesh);
    free_mesh(blade);
    free_mesh(blade_mid);
    free_mesh(blade_low);
    free_mesh(sky_tri);
    free_mesh(precip_mesh);
    free_mesh(ocean);
    for (Shader *s : {&terrain, &terrain_depth, &grass, &grass_depth, &sky, &water, &precip, &terrain_alt, &grass_alt,
                      &water_alt})
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
  for (u32 *id : {&t.height_tex, &t.normal_tex, &t.splat_tex, &t.imprint.tex, &t.layer_pack[0], &t.layer_pack[1],
                  &t.layer_pack[2], &t.layer_pack[3], &t.relief_tex})
    if (*id != 0) {
      rlUnloadTexture(*id);
      *id = 0;
    }
}

void world3d_free_grass(grass3d_slot &g) {
  g.portable_cells.clear();g.portable_ready.clear();g.portable_edits=~0u;
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
  const std::string terrain_vs = std::string(terrain_vs_head) + relief_glsl + terrain_vs_main;
  const std::string terrain_depth_vs = std::string(terrain_vs_head) + relief_glsl + terrain_depth_vs_main;
  g.terrain = LoadShaderFromMemory(terrain_vs.c_str(), terrain_fs.c_str());
  g.terrain_depth = LoadShaderFromMemory(terrain_depth_vs.c_str(), depth_fs);
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
  // A blade: five segments narrowing to a tip, enough for a tall one to arch
  // smoothly.
  std::vector<f32> blade;
  const f32 rows[5] = {0.0f, 0.25f, 0.5f, 0.72f, 0.88f};
  const auto quad = [&](f32 t0, f32 t1) {
    blade.insert(blade.end(), {-1, t0, 0, 1, t0, 0, 1, t1, 0, -1, t0, 0, 1, t1, 0, -1, t1, 0});
  };
  for (i32 r = 0; r < 4; r++)
    quad(rows[r], rows[r + 1]);
  blade.insert(blade.end(), {-1, rows[4], 0, 1, rows[4], 0, 0, 1, 0});
  g.blade = make_mesh(blade, {});
  // The same blade further off: two segments, then one triangle.
  g.blade_mid = make_mesh({-1, 0, 0, 1, 0, 0, 1, 0.5f, 0, -1, 0, 0, 1, 0.5f, 0, -1, 0.5f, 0, -1, 0.5f, 0, 1, 0.5f, 0, 0, 1, 0},
                          {});
  g.blade_low = make_mesh({-1, 0, 0, 1, 0, 0, 0, 1, 0}, {});
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
  if (!ctx_ref<world3d_store>(ctx)->gpu)
    ctx_ref<world3d_store>(ctx)->gpu = std::make_unique<world3d_gpu>();
  return *ctx_ref<world3d_store>(ctx)->gpu;
}

void set_i(u32 program, const char *name, i32 v) {
  const i32 loc = location(program, name);
  if (loc >= 0)
    rlSetUniform(loc, &v, RL_SHADER_UNIFORM_INT, 1);
}
void set_f(u32 program, const char *name, f32 v) {
  const i32 loc = location(program, name);
  if (loc >= 0)
    rlSetUniform(loc, &v, RL_SHADER_UNIFORM_FLOAT, 1);
}
void set_v2(u32 program, const char *name, vec2 v) {
  const i32 loc = location(program, name);
  if (loc >= 0)
    rlSetUniform(loc, &v, RL_SHADER_UNIFORM_VEC2, 1);
}
void set_v3(u32 program, const char *name, vec3 v) {
  const i32 loc = location(program, name);
  if (loc >= 0)
    rlSetUniform(loc, &v, RL_SHADER_UNIFORM_VEC3, 1);
}
void set_v4(u32 program, const char *name, vec4 v) {
  const i32 loc = location(program, name);
  if (loc >= 0)
    rlSetUniform(loc, &v, RL_SHADER_UNIFORM_VEC4, 1);
}
void set_iv2(u32 program, const char *name, i32 x, i32 y) {
  const i32 loc = location(program, name);
  const i32 v[2] = {x, y};
  if (loc >= 0)
    rlSetUniform(loc, v, RL_SHADER_UNIFORM_IVEC2, 1);
}
void set_m(u32 program, const char *name, const Matrix &m) {
  const i32 loc = location(program, name);
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
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  const bool on = s.cover_drawn && s.cover.depth != 0;
  set_i(program, "coverOn", on ? 1 : 0);
  bind(program, "coverMap", unit_cover, on ? s.cover.depth : 0);
  if (on) {
    set_m(program, "coverVP", s.cover_vp);
    set_f(program, "coverBias", 0.15f / 600.0f);
  }
  vec4 boxes[32]{};
  const std::vector<weather3d_cover> &covers = ctx_ref<world3d_store>(ctx)->covers;
  for (usize i = 0; i < covers.size() && i < 16; i++) {
    boxes[i * 2] = {covers[i].center.x, covers[i].center.y, covers[i].center.z, 0.0f};
    boxes[i * 2 + 1] = {covers[i].size.x * 0.5f, covers[i].size.y * 0.5f, covers[i].size.z * 0.5f, 0.0f};
  }
  const i32 loc = location(program, "coverBox");
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

// The relief grid over `quads` x `quads` cells (terrain3d_layer::relief),
// `detail` vertices a cell; kept with the chunk grids under step 0.
const gpu_mesh &relief_grid_of(world3d_gpu &g, i32 quads, i32 detail) {
  const i64 key = ((i64)quads << 16) | ((i64)detail << 8); // step 0: not a chunk grid
  for (const auto &e : g.grids)
    if (e.first == key)
      return e.second;
  g.grids.emplace_back(key, make_grid(quads * detail, 1.0f / (f32)detail));
  return g.grids.back().second;
}

const gpu_mesh &grid_of(world3d_gpu &g, i32 chunk, i32 step) {
  const i64 key = ((i64)chunk << 8) | (i64)step;
  for (const auto &e : g.grids)
    if (e.first == key)
      return e.second;
  g.grids.emplace_back(key, make_chunk_grid(chunk, step));
  return g.grids.back().second;
}

// `grid` once per chunk of `data` (four floats each: the chunk's first sample
// x, z, its skirt depth, unused), as the shaders' chunkInstance, through the
// next buffer of the ring.
void draw_chunks(world3d_gpu &g, const gpu_mesh &grid, const f32 *data, i32 count) {
  if (grid.vao == 0 || count <= 0 || !rlEnableVertexArray(grid.vao))
    return;
  chunk_ring &r = g.chunks;
  const usize slot = r.next;
  r.next = (r.next + 1) % chunk_ring::size;
  const i32 bytes = count * 4 * (i32)sizeof(f32);
  if (r.vbo[slot] == 0 || r.capacity[slot] < bytes) {
    if (r.vbo[slot] != 0)
      rlUnloadVertexBuffer(r.vbo[slot]);
    r.capacity[slot] = std::max(bytes, 4096);
    r.vbo[slot] = rlLoadVertexBuffer(nullptr, r.capacity[slot], true);
  }
  rlUpdateVertexBuffer(r.vbo[slot], data, bytes, 0);
  rlEnableVertexBuffer(r.vbo[slot]);
  rlSetVertexAttribute(12, 4, RL_FLOAT, false, 16, 0);
  rlEnableVertexAttribute(12);
  rlSetVertexAttributeDivisor(12, 1);
  if (grid.indexed)
    rlDrawVertexArrayElementsInstanced(0, grid.count, nullptr, count);
  else
    rlDrawVertexArrayInstanced(0, grid.count, count);
  rlDisableVertexArray();
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

// The imprint texels changed since the last draw (terrain3d_imprint_*), each
// rectangle cut where the square's addressing wraps.
void upload_imprint(terrain_imprint &im) {
  const i32 n = im.desc.size;
  if (n <= 0)
    return;
  if (im.tex != 0 && im.tex_size != n) {
    rlUnloadTexture(im.tex);
    im.tex = 0;
  }
  if (im.tex == 0) {
    im.tex = rlLoadTexture(im.texels.data(), n, n, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1);
    im.tex_size = n;
    rlTextureParameters(im.tex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters(im.tex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
    im.dirty.clear();
    return;
  }
  std::vector<u8> part;
  const auto wrap = [n](i32 v) { return ((v % n) + n) % n; };
  for (const terrain_imprint::mark &r : im.dirty) {
    if (r.x1 - r.x0 >= n || r.z1 - r.z0 >= n) {
      rlUpdateTexture(im.tex, 0, 0, n, n, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, im.texels.data());
      continue;
    }
    for (i32 z = r.z0; z < r.z1;) {
      const i32 tz = wrap(z), h = std::min(r.z1 - z, n - tz);
      for (i32 x = r.x0; x < r.x1;) {
        const i32 tx = wrap(x), w = std::min(r.x1 - x, n - tx);
        part.resize((usize)w * (usize)h * 4);
        for (i32 row = 0; row < h; row++)
          std::memcpy(&part[(usize)row * (usize)w * 4], &im.texels[((usize)(tz + row) * (usize)n + (usize)tx) * 4],
                      (usize)w * 4);
        rlUpdateTexture(im.tex, tx, tz, w, h, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, part.data());
        x += w;
      }
      z += h;
    }
  }
  im.dirty.clear();
}

// terrain3d_layer::roughness: the layer's normal map (flat when it has none)
// with the roughness image's red in alpha, so the shader binds no more
// texture units. Made again when either image changes; 0 without roughness.
u32 layer_pack(context &ctx, terrain3d_slot &t, i32 l) {
  const terrain3d_layer &L = t.desc.layers[l];
  const texture_store &store = ctx_ref<texture_store>(ctx);
  const texture_slot *rough = texture_slot_of(store, L.roughness);
  const texture_slot *normal = texture_slot_of(store, L.normal);
  if (normal != nullptr && normal->packed)
    normal = nullptr;
  u32 &tex = t.layer_pack[l];
  if (rough == nullptr || rough->packed) {
    if (tex != 0)
      rlUnloadTexture(tex);
    tex = 0;
    return 0;
  }
  const u32 from[4] = {normal != nullptr ? normal->texture.id : 0, normal != nullptr ? normal->version : 0,
                       rough->texture.id, rough->version};
  if (tex != 0 && std::equal(from, from + 4, t.pack_from[l]))
    return tex;
  if (tex != 0)
    rlUnloadTexture(tex);
  Image r = LoadImageFromTexture(rough->texture);
  Image img = normal != nullptr ? LoadImageFromTexture(normal->texture)
                                : GenImageColor(r.width, r.height, Color{128, 128, 255, 255});
  ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
  ImageFormat(&r, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
  if (r.width != img.width || r.height != img.height)
    ImageResize(&r, img.width, img.height);
  u8 *dst = (u8 *)img.data;
  const u8 *src = (const u8 *)r.data;
  for (usize k = 0; k < (usize)img.width * (usize)img.height; k++)
    dst[k * 4 + 3] = src[k * 4];
  Texture2D packed = LoadTextureFromImage(img);
  UnloadImage(img);
  UnloadImage(r);
  GenTextureMipmaps(&packed);
  SetTextureFilter(packed, packed.mipmaps > 1 ? TEXTURE_FILTER_TRILINEAR : TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(packed, TEXTURE_WRAP_REPEAT);
  tex = packed.id;
  std::copy(from, from + 4, t.pack_from[l]);
  return tex;
}

// terrain3d_slot::relief_tex: the height images of the layers with a relief,
// layer l in channel l at the size of the first (0.5, no relief, elsewhere),
// made again when one of them changes. 0 when no layer has a relief.
u32 relief_pack(context &ctx, terrain3d_slot &t) {
  const texture_store &store = ctx_ref<texture_store>(ctx);
  const texture_slot *height[4] = {};
  u32 from[8] = {};
  bool any = false;
  for (i32 l = 0; l < t.desc.layer_count && l < 4; l++) {
    const terrain3d_layer &L = t.desc.layers[l];
    const texture_slot *h = texture_slot_of(store, L.height);
    if (h == nullptr || h->packed || h->texture.id == 0 || !std::isfinite(L.relief) || L.relief <= 0.0f)
      continue;
    height[l] = h;
    from[l * 2] = h->texture.id;
    from[l * 2 + 1] = h->version;
    any = true;
  }
  u32 &tex = t.relief_tex;
  if (tex != 0 && (!any || !std::equal(from, from + 8, t.relief_from))) {
    rlUnloadTexture(tex);
    tex = 0;
  }
  if (!any || tex != 0)
    return tex;
  Image out{};
  for (i32 l = 0; l < 4; l++) {
    if (height[l] == nullptr)
      continue;
    Image img = LoadImageFromTexture(height[l]->texture);
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    if (out.data == nullptr)
      out = GenImageColor(img.width, img.height, Color{128, 128, 128, 128});
    if (img.width != out.width || img.height != out.height)
      ImageResize(&img, out.width, out.height);
    u8 *dst = (u8 *)out.data;
    const u8 *src = (const u8 *)img.data;
    for (usize k = 0; k < (usize)out.width * (usize)out.height; k++)
      dst[k * 4 + (usize)l] = src[k * 4];
    UnloadImage(img);
  }
  Texture2D packed = LoadTextureFromImage(out);
  UnloadImage(out);
  GenTextureMipmaps(&packed);
  SetTextureFilter(packed, packed.mipmaps > 1 ? TEXTURE_FILTER_TRILINEAR : TEXTURE_FILTER_BILINEAR);
  SetTextureWrap(packed, TEXTURE_WRAP_REPEAT);
  tex = packed.id;
  std::copy(from, from + 8, t.relief_from);
  return tex;
}

// Whether a layer with a relief covers any of the samples [i0, i0 + quads] x
// [j0, j0 + quads] (terrain3d_slot::splat, before the shader's own blend).
bool relief_here(const terrain3d_slot &t, i32 i0, i32 j0, i32 quads) {
  bool layer[4] = {};
  bool any = false;
  for (i32 l = 0; l < t.desc.layer_count && l < 4; l++)
    any |= layer[l] = std::isfinite(t.desc.layers[l].relief) && t.desc.layers[l].relief > 0.0f;
  if (!any || t.splat.size() < (usize)t.res * (usize)t.res * 4)
    return false;
  const i32 i1 = std::min(i0 + quads, t.res - 1), j1 = std::min(j0 + quads, t.res - 1);
  for (i32 j = j0; j <= j1; j++)
    for (i32 i = i0; i <= i1; i++) {
      const u8 *w = &t.splat[((usize)j * (usize)t.res + (usize)i) * 4];
      for (i32 l = 0; l < 4; l++)
        if (layer[l] && w[l] > 4)
          return true;
    }
  return false;
}

// terrain_fs_main's grassFar*: the first grass on this terrain with
// grass3d_desc::far_cover, faded where its blades shrink away.
void grass_far_uniforms(context &ctx, u32 p, const terrain3d_slot &t) {
  for (const grass3d_slot &gr : ctx_ref<world3d_store>(ctx)->grasses) {
    const grass3d_desc &d = gr.desc;
    if (!gr.alive || terrain_of(ctx, d.terrain) != &t || !std::isfinite(d.far_cover) || d.far_cover <= 0.0f)
      continue;
    const f32 range = std::max(d.draw_distance * ctx_ref<quality_state>(ctx)->q.view_distance, 1.0f);
    const f32 tint = std::isfinite(d.tint) ? clamp(d.tint, 0.0f, 1.0f) * 0.5f : 0.0f;
    const rgba c{lerp(d.tip_color.r, d.tint_color.r, tint), lerp(d.tip_color.g, d.tint_color.g, tint),
                 lerp(d.tip_color.b, d.tint_color.b, tint), 1.0f};
    set_v4(p, "grassFar", {c.r, c.g, c.b, clamp(d.far_cover, 0.0f, 1.0f)});
    set_v4(p, "grassFade", {std::max(range - std::max(d.fade, 0.01f), 0.0f), range,
                            d.layer >= 0 && d.layer < terrain3d_layer_max ? (f32)d.layer : -1.0f,
                            d.min_height > -1.0e8f ? d.min_height : -1.0e9f});
    set_v2(p, "grassSlope", {std::cos(clamp(d.max_slope, 0.0f, 90.0f) * (pi / 180.0f)),
                             std::cos(clamp(d.max_slope - 6.0f, 0.0f, 90.0f) * (pi / 180.0f))});
    return;
  }
  set_v4(p, "grassFar", {0.0f, 0.0f, 0.0f, 0.0f});
}

void draw_terrain(context &ctx, terrain3d_slot &t, const Matrix &view_proj, bool depth) {
  world3d_gpu &g = gpu_of(ctx);
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  upload_terrain(t);
  const Shader shader = depth ? g.terrain_depth : g.terrain;
  const u32 p = shader.id;
  rlEnableShader(p);
  set_m(p, "mvp", view_proj);
  set_iv2(p, "sampleLast", t.res - 1, t.res - 1);
  bind(p, "heightMap", unit_height, t.height_tex);
  if (!depth) {
    const world3d_store &w = ctx_ref<world3d_store>(ctx);
    bind(p, "normalMap", unit_normal, t.normal_tex);
    bind(p, "splatMap", unit_splat, t.splat_tex);
    vec4 tiles{};
    vec4 colors[4]{};
    vec4 glitter{};
    vec4 detile{};
    vec4 detile_scale{};
    i32 has_normal[4] = {0, 0, 0, 0};
    i32 has_rough[4] = {0, 0, 0, 0};
    static const char *const layer_names[4] = {"layer0", "layer1", "layer2", "layer3"};
    static const char *const normal_names[4] = {"layerNormal0", "layerNormal1", "layerNormal2", "layerNormal3"};
    for (i32 l = 0; l < 4; l++) {
      const terrain3d_layer &L = t.desc.layers[l];
      const texture_slot *albedo = texture_slot_of(ctx_ref<texture_store>(ctx), L.albedo);
      const texture_slot *normal = texture_slot_of(ctx_ref<texture_store>(ctx), L.normal);
      bind(p, layer_names[l], unit_layer + l, albedo != nullptr && !albedo->packed ? albedo->texture.id : 0);
      const u32 pack = layer_pack(ctx, t, l);
      bind(p, normal_names[l], unit_layer_normal + l,
           pack != 0 ? pack : normal != nullptr && !normal->packed ? normal->texture.id : 0);
      has_normal[l] = pack != 0 || (normal != nullptr && !normal->packed) ? 1 : 0;
      has_rough[l] = pack != 0 ? 1 : 0;
      (&tiles.x)[l] = std::max(L.tile, 0.01f);
      colors[l] = {L.color.r, L.color.g, L.color.b, L.color.a};
      (&glitter.x)[l] = std::isfinite(L.glitter) ? std::clamp(L.glitter, 0.0f, 4.0f) : 0.0f;
      (&detile.x)[l] = std::isfinite(L.detile) ? std::clamp(L.detile, 0.0f, 1.0f) : 0.0f;
      (&detile_scale.x)[l] = std::isfinite(L.detile_scale) ? std::clamp(L.detile_scale, 0.0f, 0.9f) : 0.0f;
    }
    set_v4(p, "layerTile", tiles);
    set_v4(p, "layerGlitter", glitter);
    set_v4(p, "layerDetile", detile);
    set_v4(p, "layerDetileScale", detile_scale);
    const i32 loc = location(p, "layerColor");
    if (loc >= 0)
      rlSetUniform(loc, colors, RL_SHADER_UNIFORM_VEC4, 4);
    const i32 hn = location(p, "layerHasNormal");
    if (hn >= 0)
      rlSetUniform(hn, has_normal, RL_SHADER_UNIFORM_IVEC4, 1);
    const i32 hr = location(p, "layerHasRough");
    if (hr >= 0)
      rlSetUniform(hr, has_rough, RL_SHADER_UNIFORM_IVEC4, 1);
    set_i(p, "layerCount", t.desc.layer_count);
    set_v2(p, "gridSize", {(f32)t.res, (f32)t.res});
    set_f(p, "wetness", w.wetness);
    upload_imprint(t.imprint);
    const terrain_imprint &im = t.imprint;
    const bool marks = im.desc.size > 0 && im.tex != 0;
    bind(p, "imprintMap", unit_imprint, marks ? im.tex : 0);
    set_v4(p, "imprintWin", {(f32)im.x0 * im.desc.texel, (f32)im.z0 * im.desc.texel, im.desc.texel,
                             marks ? (f32)im.desc.size : 0.0f});
    set_v4(p, "imprintLook", {im.desc.depth, im.desc.rim, im.desc.lifetime, im.desc.fade});
    set_f(p, "imprintNow", std::fmod(std::max(elapsed(ctx), 0.0f) * 100.0f, 65536.0f));
    set_v4(p, "imprintTint", {im.desc.tint.r, im.desc.tint.g, im.desc.tint.b, im.desc.tint.a});
    grass_far_uniforms(ctx, p, t);
    const rgba sc = t.desc.snow_color;
    set_v4(p, "snowLayer", {sc.r, sc.g, sc.b, std::isfinite(t.desc.snow) ? std::clamp(t.desc.snow, 0.0f, 1.0f) : 0.0f});
    set_cover(ctx, p);
    material3d m{};
    m.specular = t.desc.specular + w.wetness * 0.6f;
    m.shininess = lerp(t.desc.shininess, 90.0f, w.wetness);
    render3d_set_draw_uniforms(shader, g.terrain_locs, m);
    rlEnableShader(p);
  }
  // terrain3d_layer::relief: what the relief grids read, in both programs.
  const u32 relief = relief_pack(ctx, t);
  const f32 relief_reach = std::isfinite(t.desc.relief_distance) ? std::max(t.desc.relief_distance, 0.0f) : 0.0f;
  const bool relief_on = relief != 0 && relief_reach > 0.0f;
  const i32 quarter = t.chunk / 2;
  const i32 detail = relief_detail(quarter, t.desc.relief_detail);
  set_i(p, "reliefOn", 0);
  if (relief_on) {
    bind(p, "reliefMap", unit_relief, relief);
    vec4 depths{}, tiles{};
    for (i32 l = 0; l < 4; l++) {
      const terrain3d_layer &L = t.desc.layers[l];
      (&depths.x)[l] = std::isfinite(L.relief) && l < t.desc.layer_count ? std::max(L.relief, 0.0f) : 0.0f;
      (&tiles.x)[l] = std::max(L.tile, 0.01f);
    }
    set_v4(p, "reliefDepth", depths);
    set_v4(p, "reliefFade", {s.camera.position.x, s.camera.position.y, s.camera.position.z, relief_reach});
    set_f(p, "reliefStep", 1.0f / (f32)detail);
    if (depth) {
      bind(p, "normalMap", unit_normal, t.normal_tex);
      bind(p, "splatMap", unit_splat, t.splat_tex);
      set_v4(p, "layerTile", tiles);
      set_i(p, "layerCount", t.desc.layer_count);
      set_v2(p, "gridSize", {(f32)t.res, (f32)t.res});
    }
  }
  const std::array<vec4, 6> planes = depth ? planes_of(view_proj) : s.frustum;
  const vec3 eye = s.camera.position;
  // The ground is one sheet, not a solid: a slope turned from a low sun shows
  // the light only its underside, which still has to block it (the mountain
  // shades the valley in front of it).
  if (depth)
    rlDisableBackfaceCulling();
  // The chunks in view by level of detail: each level's are one instanced
  // draw of its grid, the chunk's origin and skirt depth per instance.
  thread_local std::array<std::vector<f32>, 6> levels;
  for (std::vector<f32> &l : levels)
    l.clear();
  // Near chunks under a layer with a relief go by quarters: those with the
  // layer on a relief grid, the rest on a plain grid a quarter the size.
  thread_local std::vector<f32> quarters, reliefs;
  quarters.clear();
  reliefs.clear();
  // graphics_quality::view_distance brings the coarser levels nearer.
  const f32 lod_distance = t.desc.lod_distance * ctx_ref<quality_state>(ctx)->q.view_distance;
  for (i32 cz = 0; cz < t.chunks; cz++)
    for (i32 cx = 0; cx < t.chunks; cx++) {
      vec3 lo, hi;
      chunk_box(t, cx, cz, lo, hi);
      if (!box_in(planes, lo, hi))
        continue;
      const f32 d = box_distance(eye, lo, hi);
      i32 level = 0;
      if (d > lod_distance && lod_distance > 0.0f)
        level = (i32)std::floor(std::log2(d / lod_distance)) + 1;
      level = std::clamp(level, 0, t.lods - 1);
      const i32 step = 1 << level;
      // Deep enough to close any gap a coarser neighbour leaves, and no deeper
      // than a cell or so on a small terrain (a model on a table).
      const f32 skirt = std::max(t.spacing, (hi.y - lo.y) * 0.5f + t.spacing * (f32)step);
      if (relief_on && level == 0 && d < relief_reach && relief_here(t, cx * t.chunk, cz * t.chunk, t.chunk)) {
        for (i32 q = 0; q < 4; q++) {
          const i32 i0 = cx * t.chunk + (q & 1) * quarter, j0 = cz * t.chunk + (q >> 1) * quarter;
          const vec3 qlo{t.desc.origin.x + (f32)i0 * t.spacing, lo.y, t.desc.origin.z + (f32)j0 * t.spacing};
          const vec3 qhi{qlo.x + (f32)quarter * t.spacing, hi.y, qlo.z + (f32)quarter * t.spacing};
          if (!box_in(planes, qlo - vec3{1.0f, 1.0f, 1.0f}, qhi + vec3{1.0f, 1.0f, 1.0f}))
            continue;
          const bool near = box_distance(eye, qlo, qhi) < relief_reach && relief_here(t, i0, j0, quarter);
          (near ? reliefs : quarters).insert((near ? reliefs : quarters).end(), {(f32)i0, (f32)j0, skirt, 0.0f});
        }
        continue;
      }
      levels[(usize)level].insert(levels[(usize)level].end(), {(f32)(cx * t.chunk), (f32)(cz * t.chunk), skirt, 0.0f});
    }
  set_v4(p, "terrainGrid", {t.desc.origin.x, t.desc.origin.z, t.spacing, 0.0f});
  for (i32 level = 0; level < t.lods; level++) {
    const std::vector<f32> &chunks = levels[(usize)level];
    if (!chunks.empty())
      draw_chunks(g, grid_of(g, t.chunk, 1 << level), chunks.data(), (i32)(chunks.size() / 4));
  }
  if (!quarters.empty())
    draw_chunks(g, grid_of(g, quarter, 1), quarters.data(), (i32)(quarters.size() / 4));
  if (!reliefs.empty()) {
    set_i(p, "reliefOn", 1);
    draw_chunks(g, relief_grid_of(g, quarter, detail), reliefs.data(), (i32)(reliefs.size() / 4));
    set_i(p, "reliefOn", 0);
  }
  if (depth)
    rlEnableBackfaceCulling();
  unbind_units(0, unit_splat);
  if (!depth)
    unbind_units(unit_imprint, unit_cover);
  if (relief_on)
    unbind_units(unit_relief, unit_relief);
  rlDisableShader();
}

// The small CPU copies of the terrain's layer images the blades take their
// ground colour from, made again when an image changes (hot reload), and the
// blades with them.
void refresh_ground_images(context &ctx, grass3d_slot &gr, const terrain3d_slot &t) {
  constexpr i32 side = 256;
  bool changed = false;
  for (i32 l = 0; l < 4; l++) {
    const texture_slot *albedo = texture_slot_of(ctx_ref<texture_store>(ctx), t.desc.layers[l].albedo);
    const bool usable = albedo != nullptr && !albedo->packed && albedo->texture.id != 0;
    const u32 id = usable ? albedo->texture.id : 0, version = usable ? albedo->version : 0;
    grass_ground_image &im = gr.ground[l];
    if (im.tex == id && im.version == version && (id == 0 || im.size > 0))
      continue;
    changed = true;
    im = grass_ground_image{.tex = id, .version = version};
    if (!usable)
      continue;
    Image img = LoadImageFromTexture(albedo->texture);
    if (img.data == nullptr)
      continue;
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    ImageResize(&img, side, side);
    im.size = side;
    im.rgba.assign((const u8 *)img.data, (const u8 *)img.data + (usize)side * side * 4);
    UnloadImage(img);
  }
  if (changed)
    for (grass_cell &c : gr.cells)
      c.stale = true;
}

void draw_grass(context &ctx, grass3d_slot &gr, const Matrix &view_proj, bool depth) {
  terrain3d_slot *t = terrain_of(ctx, gr.desc.terrain);
  if (t == nullptr)
    return;
  world3d_gpu &g = gpu_of(ctx);
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  world3d_store &w = ctx_ref<world3d_store>(ctx);
  const grass3d_desc &d = gr.desc;
  const vec3 eye = s.camera.position;
  const f32 range = std::max(d.draw_distance * ctx_ref<quality_state>(ctx)->q.view_distance, 1.0f);
  const vec3 o = t->desc.origin;
  upload_terrain(*t);
  // Cells in range, nearest first; a few new ones per frame so walking into a
  // field never stalls a frame.
  if (!depth) {
    if (std::isfinite(d.ground_color) && d.ground_color > 0.0f)
      refresh_ground_images(ctx, gr, *t);
    const i32 cells = (i32)std::ceil(t->desc.size / gr.cell);
    const i32 x0 = std::max(0, (i32)std::floor((eye.x - range - o.x) / gr.cell));
    const i32 z0 = std::max(0, (i32)std::floor((eye.z - range - o.z) / gr.cell));
    const i32 x1 = std::min(cells - 1, (i32)std::floor((eye.x + range - o.x) / gr.cell));
    const i32 z1 = std::min(cells - 1, (i32)std::floor((eye.z + range - o.z) / gr.cell));
    struct want {
      i32 cx, cz;
      f32 dist;
    };
    // Kept between frames: no allocation once they have grown.
    thread_local std::vector<want> wanted;
    thread_local std::vector<f32> blades;
    thread_local std::vector<std::pair<u64, usize>> known; // cell key -> index in gr.cells, sorted
    wanted.clear();
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
    const auto key_of = [](i32 cx, i32 cz) { return ((u64)(u32)cx << 32) | (u32)cz; };
    known.clear();
    for (usize k = 0; k < gr.cells.size(); k++)
      known.emplace_back(key_of(gr.cells[k].cx, gr.cells[k].cz), k);
    std::sort(known.begin(), known.end());
    for (const want &wc : wanted) {
      // Cells added below are new keys, never looked for again this frame,
      // so the index made before the loop stays right.
      grass_cell *cell = nullptr;
      const u64 key = key_of(wc.cx, wc.cz);
      const auto at = std::lower_bound(known.begin(), known.end(), std::pair<u64, usize>{key, 0});
      if (at != known.end() && at->first == key)
        cell = &gr.cells[at->second];
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
        cell->count = (u32)(blades.size() / grass_blade_floats);
        cell->stale = false;
        f32 a = 1e30f, b = -1e30f;
        for (usize k = 0; k < blades.size(); k += grass_blade_floats) {
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
  set_f(p, "curve", clamp(d.curve, 0.0f, 1.0f));
  set_v2(p, "fade", {std::max(range - std::max(d.fade, 0.01f), 0.0f), range});
  if (!depth) {
    bind(p, "normalMap", unit_normal, t->normal_tex);
    set_v4(p, "terrainGrid", {o.x, o.z, t->spacing, 0.0f});
    set_v2(p, "gridSize", {(f32)t->res, (f32)t->res});
    set_v3(p, "baseColor", {d.base_color.r, d.base_color.g, d.base_color.b});
    set_v3(p, "tipColor", {d.tip_color.r, d.tip_color.g, d.tip_color.b});
    set_v3(p, "tintColor", {d.tint_color.r, d.tint_color.g, d.tint_color.b});
    set_f(p, "groundColor", std::isfinite(d.ground_color) ? clamp(d.ground_color, 0.0f, 1.0f) : 0.0f);
    set_f(p, "rootShade", std::isfinite(d.root_shade) ? clamp(d.root_shade, 0.0f, 1.0f) : 0.6f);
    material3d m{};
    m.specular = 0.08f;
    m.shininess = 24.0f;
    render3d_set_draw_uniforms(shader, g.grass_locs, m);
    rlEnableShader(p);
  }
  // The camera's depth prepass (render3d_options::depth_prepass) must lay
  // down only what the lit draw then covers to the bit: the cells it will
  // keep (kept last frame, unchanged, still in range), cut by the same frustum.
  const bool prepass = depth && s.exact_depth;
  const std::array<vec4, 6> planes = depth && !prepass ? planes_of(view_proj) : s.frustum;
  rlDisableBackfaceCulling();
  i32 drawn = 0;
  // Blades a few pixels tall need no curve: a far cell draws a blade of two
  // segments, or one triangle, by how tall a blade stands on screen there.
  const f32 per_metre = s.camera.projection == camera3d_orthographic
                            ? 1e9f
                            : (f32)GetRenderHeight() / (2.0f * std::tan(s.camera.fovy * (pi / 360.0f)));
  {
    for (const grass_cell &c : gr.cells) {
      // The camera pass draws the cells it just kept; a shadow pass (which
      // comes first) those the last camera pass kept.
      if (c.vbo == 0 || c.count == 0 || (!depth && c.used != w.frame) || !box_in(planes, c.lo, c.hi))
        continue;
      if (prepass) {
        const f32 ax = o.x + (f32)c.cx * gr.cell, az = o.z + (f32)c.cz * gr.cell;
        const f32 dx = std::max({ax - eye.x, 0.0f, eye.x - (ax + gr.cell)});
        const f32 dz = std::max({az - eye.z, 0.0f, eye.z - (az + gr.cell)});
        if (c.stale || w.frame - c.used > 1 || std::hypot(dx, dz) > range)
          continue;
      }
      // Thinner further out: blades are in random order, so the first part
      // of a cell is an even sample of it. The ones kept are wider by as
      // much, so the field covers the ground as thickly as near the camera.
      const f32 dist = box_distance(eye, c.lo, c.hi);
      const f32 keep = clamp(1.0f - (dist - range * 0.35f) / (range * 0.65f) * 0.7f, 0.3f, 1.0f);
      const i32 n = std::max(1, (i32)((f32)c.count * keep));
      set_f(p, "widen", (f32)c.count / (f32)n);
      const f32 pixels = d.height * per_metre / std::max(dist, 0.1f);
      const gpu_mesh &blade = pixels > 48.0f ? g.blade : pixels > 14.0f ? g.blade_mid : g.blade_low;
      if (!rlEnableVertexArray(blade.vao))
        continue;
      rlEnableVertexBuffer(c.vbo);
      for (i32 a = 0; a < 3; a++) {
        rlSetVertexAttribute((u32)(12 + a), 4, RL_FLOAT, false, (i32)(grass_blade_floats * sizeof(f32)), a * 16);
        rlEnableVertexAttribute((u32)(12 + a));
        rlSetVertexAttributeDivisor((u32)(12 + a), 1);
      }
      rlDrawVertexArrayInstanced(0, blade.count, n);
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

void set_sky_uniforms(u32 p, const sky_params &k, vec2 drift) {
  set_v3(p, "skySun", k.sun);
  set_v3(p, "skySunColor", k.sun_color);
  set_v3(p, "skyZenith", k.zenith);
  set_v3(p, "skyHorizon", k.horizon);
  set_v3(p, "skyGround", k.ground);
  set_v3(p, "skyGlow", k.glow);
  set_v4(p, "skyParams", {k.twilight, k.clouds, k.cloud_darkness, k.sun_size});
  set_v4(p, "skyCloud", {k.cloud_height, k.cloud_scale, k.stars, k.fog});
  set_v3(p, "skyFogColor", k.fog_color);
  set_v2(p, "skyDrift", drift);
  set_f(p, "skyDay", k.day);
}

void draw_sky(context &ctx, const Matrix &view_proj) {
  world3d_gpu &g = gpu_of(ctx);
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  const u32 p = g.sky.id;
  rlEnableShader(p);
  set_m(p, "invViewProj", MatrixInvert(view_proj));
  set_v3(p, "eyePos", s.camera.position);
  set_sky_uniforms(p, ctx_ref<world3d_store>(ctx)->sky, ctx_ref<world3d_store>(ctx)->drift);
  set_i(p, "hdrOut", s.hdr_out ? 1 : 0);
  set_f(p, "hdrExposure", render3d_out_exposure(s));
  rlDisableDepthMask();
  draw_mesh(g.sky_tri);
  rlEnableDepthMask();
  rlDisableShader();
}

void draw_water(context &ctx, const water3d_slot &wslot, u32 id, const Matrix &view_proj) {
  world3d_gpu &g = gpu_of(ctx);
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  const world3d_store &w = ctx_ref<world3d_store>(ctx);
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
  const f32 ice = std::isfinite(d.ice) ? clamp(d.ice, 0.0f, 1.0f) : 0.0f;
  set_v4(p, "waterIce", {ice, std::isfinite(d.ice_snow) ? clamp(d.ice_snow, 0.0f, 1.0f) : 0.0f, 0.0f, 0.0f});
  set_v3(p, "iceColor", {d.ice_color.r, d.ice_color.g, d.ice_color.b});
  const f32 rain = w.sky_drawn && std::isfinite(d.rain_rings) ? w.sky.rain * clamp(d.rain_rings, 0.0f, 1.0f) : 0.0f;
  set_f(p, "waterRain", clamp(rain, 0.0f, 1.0f));
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
  const i32 lw = location(p, "waves"), lw2 = location(p, "waves2");
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
  set_v2(p, "waterBed", {d.bed > 0.0f ? 1.0f : 0.0f, d.level - d.bed});
  set_i(p, "waterLamps", d.lamps ? 1 : 0);
  set_v4(p, "waterPour", {d.pour_at.x, d.pour_at.y, clamp(d.pour, 0.0f, 1.0f), std::max(d.pour_reach, 0.01f)});
  set_v4(p, "waterRound", d.round && !ocean ? vec4{d.center.x, d.center.y, d.size.x * 0.5f, d.size.y * 0.5f} : vec4{});
  vec4 rings[water3d_splash_max]{};
  f32 ring_sizes[water3d_splash_max]{};
  for (i32 i = 0; i < water3d_splash_max; i++) {
    const water3d_ring &r = wslot.rings[i];
    rings[i] = {r.at.x, r.at.y, w.time - r.start, r.strength};
    ring_sizes[i] = r.size;
  }
  rlSetUniform(location(p, "waterRings"), rings, RL_SHADER_UNIFORM_VEC4, water3d_splash_max);
  rlSetUniform(location(p, "waterRingSize"), ring_sizes, RL_SHADER_UNIFORM_FLOAT, water3d_splash_max);
  u32 scene_color = 0, scene_depth = 0;
  i32 scene_w = 0, scene_h = 0;
  const bool scene = (d.refract > 0.0f || d.reflect > 0.0f) &&
                     post3d_scene(ctx, scene_color, scene_depth, scene_w, scene_h);
  const bool see = scene && d.refract > 0.0f;
  set_v4(p, "waterScene", {see ? 1.0f : 0.0f, d.refract, s.camera.near_plane, s.camera.far_plane});
  set_v3(p, "waterAbsorb", {clamp(d.absorb.r, 0.001f, 1.0f), clamp(d.absorb.g, 0.001f, 1.0f),
                            clamp(d.absorb.b, 0.001f, 1.0f)});
  set_v4(p, "waterLook", {std::max(d.caustics, 0.0f), std::max(d.caustic_size, 0.01f),
                          scene ? clamp(d.reflect, 0.0f, 1.0f) : 0.0f, 0.0f});
  if (scene) {
    bind(p, "sceneColor", unit_scene, scene_color);
    bind(p, "sceneDepth", unit_scene + 1, scene_depth);
    set_v3(p, "waterScreen",
           {(f32)scene_w, (f32)scene_h, s.camera.projection == camera3d_orthographic ? 1.0f : 0.0f});
  }
  set_sky_uniforms(p, w.sky_drawn ? w.sky : sky_params_from_light(ctx), w.drift);
  rlDisableBackfaceCulling();
  draw_mesh(*mesh);
  rlEnableBackfaceCulling();
  unbind_units(unit_height, unit_height);
  unbind_units(unit_scene, unit_scene + 1);
  rlDisableShader();
}

void draw_precip(context &ctx, const Matrix &view_proj) {
  world3d_gpu &g = gpu_of(ctx);
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  const world3d_store &w = ctx_ref<world3d_store>(ctx);
  const u32 p = g.precip.id;
  rlEnableShader(p);
  set_m(p, "mvp", view_proj);
  set_v3(p, "eyePos", s.camera.position);
  set_f(p, "time", w.time);
  set_v2(p, "wind", w.sky.wind);
  set_v2(p, "drift", w.drift);
  set_v3(p, "ambient", {s.light.ambient.r, s.light.ambient.g, s.light.ambient.b});
  set_v3(p, "lightColor", {s.light.color.r, s.light.color.g, s.light.color.b});
  set_i(p, "hdrOut", s.hdr_out ? 1 : 0);
  set_f(p, "hdrExposure", render3d_out_exposure(s));
  set_cover(ctx, p);
  rlDisableBackfaceCulling();
  if (rlEnableVertexArray(g.precip_mesh.vao)) {
    if (w.sky.rain > 0.0f) {
      set_v4(p, "precip", {32.0f, 22.0f, 9.0f, 0.0f});
      set_f(p, "amount", 1.0f);
      rlDrawVertexArray(0, (i32)((f32)rain_drops * w.sky.rain) * 6);
    }
    if (w.sky.snow > 0.0f) {
      const texture_slot *flakes = texture_slot_of(ctx_ref<texture_store>(ctx), w.flakes);
      const bool shapes = flakes != nullptr && !flakes->packed;
      set_v4(p, "precip", {26.0f, 16.0f, 1.2f, 1.0f});
      set_v2(p, "flakeGrid", shapes ? vec2{(f32)w.flake_columns, (f32)w.flake_rows} : vec2{0.0f, 0.0f});
      set_f(p, "sparkle", shapes ? w.flake_sparkle : 0.0f);
      bind(p, "flakeMap", unit_flakes, shapes ? flakes->texture.id : 0);
      set_f(p, "amount", 1.0f);
      rlDrawVertexArray(0, (i32)((f32)snow_flakes * w.sky.snow) * 6);
    }
    rlDisableVertexArray();
  }
  rlEnableBackfaceCulling();
  unbind_units(unit_flakes, unit_flakes);
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
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  if (!has_world(s))
    return;
  world3d_gpu &g = gpu_of(ctx);
  if (!ensure_ready(g))
    return;
  render3d_set_pass_uniforms(s, g.terrain, g.terrain_locs, shadows, light_vp, lamps);
  render3d_set_pass_uniforms(s, g.grass, g.grass_locs, shadows, light_vp, lamps);
  render3d_set_pass_uniforms(s, g.water, g.water_locs, shadows, light_vp, lamps);
}

void world3d_use_ext(context &ctx, bool on) {
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  if (!has_world(s))
    return;
  world3d_gpu &g = gpu_of(ctx);
  if (!ensure_ready(g))
    return;
  if (on && !g.ext_tried) {
    g.ext_tried = true;
    const std::string head = "#version 330\n#define NJIN_EXT\n";
    const std::string lighting = render3d_lighting_glsl();
    const std::string terrain_fs = head + lighting + cover_glsl + terrain_fs_main;
    const std::string grass_vs = head + grass_vs_common + grass_vs_main;
    const std::string grass_fs = head + lighting + grass_fs_main;
    const std::string water_vs = head + waves_glsl + water_vs_main;
    const std::string water_fs = head + lighting + sky_glsl + waves_glsl + water_fs_main;
    const std::string terrain_vs = std::string(terrain_vs_head) + relief_glsl + terrain_vs_main;
    g.terrain_alt = LoadShaderFromMemory(terrain_vs.c_str(), terrain_fs.c_str());
    g.grass_alt = LoadShaderFromMemory(grass_vs.c_str(), grass_fs.c_str());
    g.water_alt = LoadShaderFromMemory(water_vs.c_str(), water_fs.c_str());
    g.ext_ok = IsShaderValid(g.terrain_alt) && IsShaderValid(g.grass_alt) && IsShaderValid(g.water_alt);
    if (g.ext_ok) {
      g.terrain_locs_alt = render3d_find_locations(g.terrain_alt);
      g.grass_locs_alt = render3d_find_locations(g.grass_alt);
      g.water_locs_alt = render3d_find_locations(g.water_alt);
    } else {
      for (Shader *sh : {&g.terrain_alt, &g.grass_alt, &g.water_alt})
        if (IsShaderValid(*sh))
          UnloadShader(*sh);
      g.terrain_alt = g.grass_alt = g.water_alt = Shader{};
      NJIN_WARN("world3d: the extended shaders failed to compile; the outdoor world lights without the extended lights");
    }
  }
  const bool want = on && g.ext_ok;
  if (want == g.ext_active)
    return;
  std::swap(g.terrain, g.terrain_alt);
  std::swap(g.grass, g.grass_alt);
  std::swap(g.water, g.water_alt);
  std::swap(g.terrain_locs, g.terrain_locs_alt);
  std::swap(g.grass_locs, g.grass_locs_alt);
  std::swap(g.water_locs, g.water_locs_alt);
  g.ext_active = want;
}

void world3d_draw(context &ctx, const draw3d_cmd &c, const Matrix &view_proj) {
  world3d_gpu &g = gpu_of(ctx);
  if (!ensure_ready(g))
    return;
  const bool translucent = ctx_ref<render3d_state>(ctx)->translucent_pass;
  rlDrawRenderBatchActive();
  world3d_store &w = ctx_ref<world3d_store>(ctx);
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
  } else if (c.world == world3d_grass && c.world_id >= 1 && c.world_id <= ctx_ref<world3d_store>(ctx)->grasses.size()) {
    grass3d_slot &gr = ctx_ref<world3d_store>(ctx)->grasses[c.world_id - 1];
    if (gr.alive && (gr.desc.cast_shadows || ctx_ref<render3d_state>(ctx)->exact_depth))
      draw_grass(ctx, gr, view_proj, true);
  }
}

void world3d_pass_end(context &ctx) { ctx_ref<world3d_store>(ctx)->sky_drawn = false; }
} // namespace njin
