#include "render3d.h"
#include "njin_world3d_impl.h"
#include "camera.h"
#include "fx.h"
#include "gizmo.h"
#include "njin2rl.h"
#include "njin_cfg.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "debug.h"
#include "render_stats.h"
#include "njin_audio_impl.h"
#include "njin_quality_impl.h"
#include "njin_view.h"
#include "njin_log.h"
#include "njin_log_impl.h"
#include "njin_instance.h"
#include "njin_model.h"
#include "njin_shader.h"
#include "njin_texture.h"
#include "particles3d.h"
#include "post3d.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <raymath.h>
#include <rlgl.h>
#include <string>
#include <vector>

namespace njin {
namespace {
// Past raylib's material map slots (0..11), so DrawMesh never rebinds them.
constexpr i32 shadow_unit = 14;
constexpr i32 lamp_unit = 15;
// A game shader's extra textures (shader_set_texture()): above the units a
// model's own maps take (DrawMesh binds up to MATERIAL_MAP_BRDF), below the
// shadow maps.
constexpr i32 game_texture_unit = 10;
static_assert(game_texture_unit + (i32)shader_max_textures <= shadow_unit);
// The shadow atlas of the point and spot lights: one row per shadowed light,
// one tile per face (a point light has 6, a spot light 1).
constexpr i32 lamp_faces = 6;

const char *const lit_vs = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec2 fragTexCoord;
out vec2 fragTexCoord2;
out vec4 fragColor;
out vec3 fragNormal;
out vec3 fragPos;
void main() {
  fragTexCoord = vertexTexCoord;
  fragTexCoord2 = vertexTexCoord2;
  fragColor = vertexColor;
  fragNormal = (matNormal * vec4(vertexNormal, 0.0)).xyz;
  fragPos = (matModel * vec4(vertexPosition, 1.0)).xyz;
  gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

// Lighting shared by the mesh shader and the SDF shader: the sun (with its
// shadow), up to 16 point/spot lights, ambient, Blinn-Phong, rim, emission,
// flash, dissolve edge and fog. `pos` is the world position being shaded.
const char *const lighting_glsl = R"(
uniform sampler2D shadowMap;
uniform vec4 colDiffuse;
uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 ambient;
uniform vec3 viewPos;
uniform vec3 fogColor;
uniform float fogDensity;
uniform int lightCount;
uniform vec4 lightPos[16];    // xyz, radius
uniform vec4 lightColors[16]; // rgb * intensity, cos of the inner cone
uniform vec4 lightSpot[16];   // direction, cos of the outer cone (-2 for a point light)
uniform vec2 surface;         // specular strength, shininess
uniform vec4 emission;        // rgb, strength
uniform vec4 rim;             // rgb, strength
uniform int unlit;
uniform int shadowOn;
uniform mat4 lightVP;
uniform vec4 shadowParams;    // texel size (0..1), softness in texels, texel size in world units
uniform vec4 flash;
uniform vec4 dissolve;        // amount, edge width, grain, seed
uniform vec4 viewMask;        // fx3d mask: centre xyz, radius (0 off)
uniform vec4 edgeColor;
// Shadows of point and spot lights: the atlas row of each light (-1 = none),
// each face's view-projection, and per row near, far, the size of a texel
// one unit away, and 1 / tile size.
uniform sampler2D lampShadowMap;
uniform int lightShadow[16];
uniform mat4 lampVP[24];
uniform vec4 lampParams[4];
uniform float lampRows;   // rows in the atlas: the lamps that cast, so far
// Tone mapping (light3d::tonemap): 0 off, the colours as authored; else the
// lighting is worked out in linear space from the linear copies of the
// lights below (the sRGB ones above are left as they are for game shaders
// and the water), then exposed, tone mapped and encoded back to sRGB.
uniform int toneMap;          // 1 linear, 2 ACES, 3 AgX, 4 Neutral
uniform float exposure;
uniform vec3 lightColorLinear;
uniform vec3 ambientLinear;
uniform vec4 lightColorsLinear[16];
uniform vec4 emissionLinear;
// PBR (material3d::pbr): x 1 = on, y metallic, z roughness, w occlusion strength.
uniform vec4 pbrParams;
// The environment (light3d::environment): its irradiance as spherical
// harmonics (linear, already times the intensity), and its radiance prefiltered
// for `envLevels` roughnesses, one equirectangular image per level, stacked
// top (roughness 0) to bottom (roughness 1).
uniform int envOn;
uniform vec3 envSH[9];
uniform sampler2D envMap;
uniform vec4 envParams;       // x levels, y intensity, z rotation (radians), w texel height of a level
// The maps of a PBR material, read by the main shader: multiplied into the
// factors. Negative when the shader has none (the SDF shapes, the outdoor world).
float surfMetal = -1.0;
float surfRough = -1.0;
float surfOcclusion = 1.0;
out vec4 finalColor;

const float PI = 3.14159265;

vec3 srgb_to_linear(vec3 c) {
  c = max(c, vec3(0.0));
  return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

vec3 linear_to_srgb(vec3 c) {
  c = max(c, vec3(0.0));
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

// ACES filmic, Stephen Hill's fit of the RRT and ODT, as three.js uses it (its
// exposure divided by 0.6 too, so the same exposure looks the same).
vec3 tone_aces(vec3 c) {
  const mat3 into = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
  const mat3 outof = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
  c = into * (c / 0.6);
  vec3 a = c * (c + 0.0245786) - 0.000090537;
  vec3 b = c * (0.983729 * c + 0.4329510) + 0.238081;
  return clamp(outof * (a / b), 0.0, 1.0);
}

// AgX: log encoding between -12.47 and +4.03 stops around mid grey, a sigmoid
// fitted to Blender's, and back to linear.
vec3 tone_agx(vec3 c) {
  const mat3 inset = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                          0.0784335999999992, 0.878468636469772, 0.0784336,
                          0.0792237451477643, 0.0791661274605434, 0.879142973793104);
  const mat3 outset = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                           -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                           -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
  const float lo = -12.47393, hi = 4.026069;
  c = inset * max(c, vec3(1e-10));
  c = clamp((log2(c) - lo) / (hi - lo), 0.0, 1.0);
  vec3 x2 = c * c, x4 = x2 * x2;
  c = 15.5 * x4 * x2 - 40.14 * x4 * c + 31.96 * x4 - 6.868 * x2 * c + 0.4298 * x2 + 0.1191 * c - 0.00232;
  return clamp(pow(max(outset * c, vec3(0.0)), vec3(2.2)), 0.0, 1.0);
}

// Khronos PBR Neutral.
vec3 tone_neutral(vec3 c) {
  const float start = 0.76, desaturation = 0.15;
  float x = min(c.r, min(c.g, c.b));
  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
  c -= offset;
  float peak = max(c.r, max(c.g, c.b));
  if (peak < start)
    return c;
  float d = 1.0 - start;
  float top = 1.0 - d * d / (peak + d - start);
  c *= top / peak;
  float g = 1.0 - 1.0 / (desaturation * (peak - top) + 1.0);
  return mix(c, vec3(top), g);
}

// Linear light to what the screen shows (sRGB), by light3d::tonemap.
vec3 tone_out(vec3 c) {
  c *= exposure;
  if (toneMap == 2)
    c = tone_aces(c);
  else if (toneMap == 3)
    c = tone_agx(c);
  else if (toneMap == 4)
    c = tone_neutral(c);
  return linear_to_srgb(clamp(c, 0.0, 1.0));
}

float hash(vec3 p) {
  p = fract(p * 0.3183099 + vec3(0.71, 0.113, 0.419));
  p *= 17.0;
  return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// The dissolve value of this point; discards it when it is gone.
float dissolve_cut(vec3 pos) {
  if (dissolve.x <= 0.0)
    return 1.0;
  float cut = hash(floor(pos / dissolve.z) + dissolve.w * 17.31);
  if (cut < dissolve.x)
    discard;
  return cut;
}

// Sphere masking (fx3d::mask_*): what lies between the camera and the
// mask's centre, inside a cone widening to its radius there, is dropped in
// an ordered dither, thinning out toward the cone's edge.
void view_mask_cut(vec3 pos) {
  if (viewMask.w <= 0.0)
    return;
  vec3 ab = viewMask.xyz - viewPos;
  float len2 = dot(ab, ab);
  if (len2 <= 1e-6)
    return;
  float t = dot(pos - viewPos, ab) / len2;
  if (t <= 0.0 || t >= 0.97)
    return;
  float r = viewMask.w * mix(0.35, 1.0, t);
  // The ground he stands on stays: only what rises above his feet is cut.
  if (pos.y < viewMask.y - viewMask.w * 0.6)
    return;
  float d = length(pos - (viewPos + ab * t));
  float k = 1.0 - smoothstep(r * 0.6, r, d); // 1 in the middle, 0 at the edge
  if (k <= 0.0)
    return;
  const float bayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0,
                                    3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
  ivec2 c = ivec2(gl_FragCoord.xy) & 3;
  if ((bayer[c.y * 4 + c.x] + 0.5) / 16.0 < k)
    discard;
}

// Share of the sun reaching `pos`, 3x3 percentage-closer filtering.
float sunlight(vec3 pos, vec3 n, vec3 l) {
  // Looked up a little off the surface along its normal, so faces almost
  // parallel to the light do not shadow themselves in stripes.
  vec4 lp = lightVP * vec4(pos + n * shadowParams.z * 1.5, 1.0);
  vec3 c = lp.xyz / lp.w * 0.5 + 0.5;
  if (c.x <= 0.0 || c.x >= 1.0 || c.y <= 0.0 || c.y >= 1.0 || c.z >= 1.0)
    return 1.0;
  float bias = shadowParams.x * (1.5 + 2.0 * (1.0 - max(dot(n, l), 0.0)));
  float step = shadowParams.x * max(shadowParams.y, 0.0);
  float lit = 0.0;
  for (int x = -1; x <= 1; x++)
    for (int y = -1; y <= 1; y++)
      lit += c.z - bias <= texture(shadowMap, c.xy + vec2(x, y) * step).r ? 1.0 : 0.0;
  return lit / 9.0;
}

// Share of point/spot light `i` (atlas row `row`) reaching `pos`, 3x3 PCF on
// linear depth. A point light picks the cube face its direction falls in.
float lamplight(int row, bool point, vec3 lpos, vec3 pos, vec3 n, vec3 l) {
  vec3 d = pos - lpos;
  int face = 0;
  if (point) {
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z)
      face = d.x > 0.0 ? 0 : 1;
    else if (a.y >= a.z)
      face = d.y > 0.0 ? 2 : 3;
    else
      face = d.z > 0.0 ? 4 : 5;
  }
  vec4 p = lampParams[row];
  float texel = length(d) * p.z;
  vec4 c = lampVP[row * 6 + face] * vec4(pos + n * texel * 1.5, 1.0);
  if (c.w <= 0.0)
    return 1.0;
  vec2 uv = c.xy / c.w * 0.5 + 0.5;
  if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0)
    return 1.0;
  float depth = c.w;
  float bias = texel * (1.5 + 2.0 * (1.0 - max(dot(n, l), 0.0)));
  vec2 cell = vec2(float(face), float(row));
  vec2 lo = vec2(0.5 * p.w);
  vec2 hi = vec2(1.0) - lo;
  float lit = 0.0;
  for (int x = -1; x <= 1; x++)
    for (int y = -1; y <= 1; y++) {
      vec2 q = clamp(uv + vec2(x, y) * p.w, lo, hi);
      float z = texture(lampShadowMap, (cell + q) / vec2(6.0, lampRows)).r;
      float linear = p.x * p.y / (p.y - z * (p.y - p.x));
      lit += depth - bias <= linear ? 1.0 : 0.0;
    }
  return lit / 9.0;
}

// Share of point/spot light `i` reaching `pos` (cone, fall-off, shadow), and
// the direction to it; 0 when out of reach.
float lamp_share(int i, vec3 pos, vec3 n, out vec3 li) {
  vec3 d = lightPos[i].xyz - pos;
  float dist = length(d);
  float reach = lightPos[i].w;
  li = d / max(dist, 1e-5);
  if (dist >= reach)
    return 0.0;
  float fall = 1.0 - dist / reach;
  float att = fall * fall;
  if (lightSpot[i].w > -1.5) {
    float co = lightSpot[i].w;
    float ci = lightColors[i].w;
    att *= clamp((dot(-li, lightSpot[i].xyz) - co) / max(ci - co, 1e-4), 0.0, 1.0);
  }
  float nli = max(dot(n, li), 0.0);
  if (nli > 0.0 && att > 0.0 && lightShadow[i] >= 0)
    att *= lamplight(lightShadow[i], lightSpot[i].w < -1.5, lightPos[i].xyz, pos, n, li);
  return att;
}

// Blinn-Phong: the sun, the lamps, ambient and rim, with the lights' colours
// as given (`lin`: their linear copies).
vec3 classic_light(vec3 base, vec3 n, vec3 pos, vec3 v, bool lin) {
  vec3 diffuse = lin ? ambientLinear : ambient;
  vec3 spec = vec3(0.0);
  vec3 sunColor = lin ? lightColorLinear : lightColor;
  vec3 l = -lightDir;
  float nl = max(dot(n, l), 0.0);
  if (nl > 0.0) {
    float sun = shadowOn == 1 ? sunlight(pos, n, l) : 1.0;
    diffuse += sunColor * nl * sun;
    spec += sunColor * pow(max(dot(n, normalize(l + v)), 0.0), surface.y) * sun;
  }
  for (int i = 0; i < lightCount; i++) {
    vec3 li;
    float att = lamp_share(i, pos, n, li);
    if (att <= 0.0)
      continue;
    vec3 c = lin ? lightColorsLinear[i].rgb : lightColors[i].rgb;
    float nli = max(dot(n, li), 0.0);
    diffuse += c * nli * att;
    if (nli > 0.0)
      spec += c * pow(max(dot(n, normalize(li + v)), 0.0), surface.y) * att;
  }
  vec3 rimColor = lin ? srgb_to_linear(rim.rgb) : rim.rgb;
  return base * diffuse + spec * surface.x + rimColor * rim.a * pow(1.0 - max(dot(n, v), 0.0), 3.0);
}

// Cook-Torrance with GGX for one light of colour `c` from `l`. In njin's scale
// a white light straight onto a white rough face gives the light's colour
// (the colours carry the pi a radiance would be divided by), as Blinn-Phong.
vec3 pbr_direct(vec3 albedo, float metal, float rough, vec3 f0, vec3 n, vec3 v, vec3 l, vec3 c) {
  float nl = max(dot(n, l), 0.0);
  if (nl <= 0.0)
    return vec3(0.0);
  vec3 h = normalize(l + v);
  float nv = max(dot(n, v), 1e-4), nh = max(dot(n, h), 0.0), vh = max(dot(v, h), 0.0);
  float a = rough * rough, a2 = a * a;
  float q = nh * nh * (a2 - 1.0) + 1.0;
  float D = a2 / (PI * q * q);
  float k = (rough + 1.0) * (rough + 1.0) / 8.0;
  float G = nv / (nv * (1.0 - k) + k) * nl / (nl * (1.0 - k) + k);
  vec3 F = f0 + (1.0 - f0) * pow(1.0 - vh, 5.0);
  vec3 spec = D * G * F / (4.0 * nv * nl);
  vec3 kd = (1.0 - F) * (1.0 - metal);
  return (kd * albedo + spec * PI) * c * nl;
}

// Scale and bias of F0 for light from all around, an analytic fit of the
// split-sum lookup (Karis, "Physically Based Shading on Mobile").
vec2 env_brdf(float rough, float nv) {
  const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
  const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
  vec4 r = rough * c0 + c1;
  float a = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
  return vec2(-1.04, 1.04) * a + r.zw;
}

// A direction turned by the environment's rotation, as the maps were made.
vec3 env_dir(vec3 d) {
  float c = cos(envParams.z), s = sin(envParams.z);
  return vec3(c * d.x - s * d.z, d.y, s * d.x + c * d.z);
}

// Irradiance from the spherical harmonics (Ramamoorthi and Hanrahan).
vec3 env_irradiance(vec3 n) {
  n = env_dir(n);
  return envSH[0] * 0.886227 + envSH[1] * (1.023328 * n.y) + envSH[2] * (1.023328 * n.z) +
         envSH[3] * (1.023328 * n.x) + envSH[4] * (0.858086 * n.x * n.y) + envSH[5] * (0.858086 * n.y * n.z) +
         envSH[6] * (0.247708 * (3.0 * n.z * n.z - 1.0)) + envSH[7] * (0.858086 * n.x * n.z) +
         envSH[8] * (0.429043 * (n.x * n.x - n.y * n.y));
}

// The environment's light from direction `d` blurred for `rough`, between the
// two nearest prefiltered levels.
vec3 env_radiance(vec3 d, float rough) {
  d = env_dir(d);
  vec2 uv = vec2(atan(d.z, d.x) / (2.0 * PI) + 0.5, acos(clamp(d.y, -1.0, 1.0)) / PI);
  float levels = envParams.x;
  float at = clamp(rough, 0.0, 1.0) * (levels - 1.0);
  float k0 = floor(at);
  float k1 = min(k0 + 1.0, levels - 1.0);
  // Kept half a texel inside its level, so a lookup never bleeds into the next.
  float v = clamp(uv.y, envParams.w, 1.0 - envParams.w);
  vec3 a = textureLod(envMap, vec2(uv.x, (k0 + v) / levels), 0.0).rgb;
  vec3 b = textureLod(envMap, vec2(uv.x, (k1 + v) / levels), 0.0).rgb;
  return mix(a, b, at - k0) * envParams.y;
}

// The PBR material: the sun, the lamps, and the ambient light or the
// environment, with the lights' colours as given (`lin`: their linear copies).
vec3 pbr_light(vec3 albedo, vec3 n, vec3 pos, vec3 v, bool lin) {
  float metal = clamp(surfMetal >= 0.0 ? surfMetal : pbrParams.y, 0.0, 1.0);
  float rough = clamp(surfRough >= 0.0 ? surfRough : pbrParams.z, 0.045, 1.0);
  vec3 f0 = mix(vec3(0.04), albedo, metal);
  vec3 color = vec3(0.0);
  vec3 l = -lightDir;
  if (dot(n, l) > 0.0) {
    float sun = shadowOn == 1 ? sunlight(pos, n, l) : 1.0;
    color += pbr_direct(albedo, metal, rough, f0, n, v, l, (lin ? lightColorLinear : lightColor) * sun);
  }
  for (int i = 0; i < lightCount; i++) {
    vec3 li;
    float att = lamp_share(i, pos, n, li);
    if (att > 0.0)
      color += pbr_direct(albedo, metal, rough, f0, n, v, li, (lin ? lightColorsLinear[i].rgb : lightColors[i].rgb) * att);
  }
  // Light from all around: the environment, or the ambient colour as an even
  // sky. With the energy single scattering loses on rough surfaces given back
  // as multiple scattering (Fdez-Aguera, "A Multiple-Scattering Microfacet
  // Model for Real-Time Image-Based Lighting", as three.js does), so a rough
  // metal in an even white light stays white.
  float nv = max(dot(n, v), 1e-4);
  vec2 ab = env_brdf(rough, nv);
  vec3 single = f0 * ab.x + ab.y;
  float ess = ab.x + ab.y;
  vec3 favg = f0 + (1.0 - f0) * 0.047619;
  vec3 multi = single * favg / (1.0 - (1.0 - ess) * favg) * (1.0 - ess);
  vec3 diffuseLight, specLight;
  if (envOn == 1) {
    diffuseLight = env_irradiance(n) / PI;
    specLight = env_radiance(reflect(-v, n), rough);
  } else {
    diffuseLight = lin ? ambientLinear : ambient;
    specLight = diffuseLight;
  }
  vec3 total = single + multi;
  vec3 kd = albedo * (1.0 - metal) * (1.0 - max(total.r, max(total.g, total.b)));
  float occlusion = mix(1.0, surfOcclusion, clamp(pbrParams.w, 0.0, 1.0));
  color += (kd * diffuseLight + single * specLight + multi * diffuseLight) * occlusion;
  vec3 rimColor = lin ? srgb_to_linear(rim.rgb) : rim.rgb;
  return color + rimColor * rim.a * pow(1.0 - nv, 3.0);
}

vec3 shade(vec3 base, vec3 n, vec3 pos, float cut, vec3 glow) {
  vec3 color = base;
  vec3 v = normalize(viewPos - pos);
  if (unlit == 0 && toneMap > 0) {
    // In linear light, emission included, then to the screen.
    vec3 albedo = srgb_to_linear(base);
    vec3 lit = pbrParams.x > 0.5 ? pbr_light(albedo, n, pos, v, true) : classic_light(albedo, n, pos, v, true);
    lit += emissionLinear.rgb * emissionLinear.a + srgb_to_linear(glow);
    color = tone_out(lit);
  } else {
    // Without tone mapping, as njin always drew it (PBR or not).
    if (unlit == 0)
      color = pbrParams.x > 0.5 ? pbr_light(base, n, pos, v, false) : classic_light(base, n, pos, v, false);
    color += emission.rgb * emission.a + glow;
  }
  color = mix(color, flash.rgb, flash.a);
  if (dissolve.x > 0.0 && cut < dissolve.x + dissolve.y)
    color = mix(color, edgeColor.rgb, edgeColor.a);
  if (fogDensity > 0.0) {
    float f = fogDensity * length(viewPos - pos);
    color = mix(fogColor, color, exp(-f * f));
  }
  return color;
}
)";

const char *const lit_fs_main = R"(
in vec2 fragTexCoord;
in vec2 fragTexCoord2;
in vec4 fragColor;
in vec3 fragNormal;
in vec3 fragPos;
uniform sampler2D texture0;    // albedo
uniform sampler2D texture2;    // normal map
uniform sampler2D emissionMap;
uniform vec4 emissionColor;    // tint of the emission map
uniform int useNormalMap;
uniform int useEmissionMap;
uniform float worldUv;         // > 0: texture coordinates from the world position, a tile this size
uniform sampler2D underMap;    // material3d::under, under the albedo where it has worn off
uniform sampler2D underNormalMap;
uniform float underAmount;     // 0 none of it shows (or no under layer)
uniform int useUnderNormal;
uniform sampler2D metalRoughMap; // material3d::metal_rough: g roughness, b metallic
uniform sampler2D occlusionMap;  // material3d::occlusion: r
uniform int usePbrMaps;          // 1 metal_rough, 2 occlusion; 4, 8: that map on the second texture coordinates

// Normal mapping without tangents: the frame comes from screen derivatives
// (Christian Schueler, "Normal Mapping Without Precomputed Tangents").
vec3 mapped_normal(vec3 n, vec2 uv) {
  vec3 dp1 = dFdx(fragPos);
  vec3 dp2 = dFdy(fragPos);
  vec2 duv1 = dFdx(uv);
  vec2 duv2 = dFdy(uv);
  vec3 dp2perp = cross(dp2, n);
  vec3 dp1perp = cross(n, dp1);
  vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
  vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
  float invmax = inversesqrt(max(max(dot(t, t), dot(b, b)), 1e-12));
  vec3 m = texture(texture2, uv).xyz * 2.0 - 1.0;
  return normalize(mat3(t * invmax, b * invmax, n) * m);
}

// material3d::world_uv: a wall's u along it and v up (the image's top up, its
// green +Y up too), a floor's u along x and v along z. The frame is exact, so
// the normal map needs no derivatives.
void world_frame(vec3 n, out vec2 uv, out vec3 t, out vec3 b) {
  b = abs(n.y) > 0.7 ? vec3(0.0, 0.0, n.y > 0.0 ? -1.0 : 1.0) : vec3(0.0, 1.0, 0.0);
  t = normalize(cross(b, n));
  b = cross(n, t);
  // Texture rows go down the image: v grows downward on it.
  uv = vec2(dot(fragPos, t), -dot(fragPos, b)) / worldUv;
}

// Smooth value noise, about one bump per unit of `p`.
float value_noise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  float a = hash(vec3(i, 0.0)), b = hash(vec3(i + vec2(1.0, 0.0), 0.0));
  float c = hash(vec3(i + vec2(0.0, 1.0), 0.0)), d = hash(vec3(i + vec2(1.0, 1.0), 0.0));
  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// A world-mapped image without its tile showing (Inigo Quilez, "texture
// repetition", the cheap way): a slow noise steps through eight offsets of
// the image and two neighbouring ones are blended, so no two tiles nearby
// read the same. Offsets only: the image keeps its up (streaks run down, the
// normal map's frame holds). The normal map takes the same offsets and blend.
void untile(vec2 uv, out vec2 offa, out vec2 offb, out float k) {
  float l = value_noise(uv * 0.45) * 8.0;
  float ia = floor(l);
  offa = sin(vec2(3.0, 7.0) * ia);
  offb = sin(vec2(3.0, 7.0) * (ia + 1.0));
  k = fract(l);
}

vec4 untiled(sampler2D s, vec2 uv, vec2 offa, vec2 offb, float k) {
  vec2 dx = dFdx(uv), dy = dFdy(uv);
  vec4 a = textureGrad(s, uv + offa, dx, dy);
  vec4 b = textureGrad(s, uv + offb, dx, dy);
  return mix(a, b, smoothstep(0.2, 0.8, k));
}

void main() {
  vec2 uv = fragTexCoord;
  vec3 n = normalize(fragNormal);
  vec3 wt, wb;
  vec2 offa, offb;
  float k = 0.0;
  vec4 albedo;
  if (worldUv > 0.0) {
    world_frame(n, uv, wt, wb);
    untile(uv, offa, offb, k);
    albedo = untiled(texture0, uv, offa, offb, k);
    // Large patches a little lighter or darker: a wall is never even.
    albedo.rgb *= mix(0.86, 1.1, value_noise(uv * 0.21 + 17.0));
  } else {
    albedo = texture(texture0, uv);
  }
  vec4 base = albedo * colDiffuse * fragColor;
  // The under layer where the top one has worn off: patches from a slow
  // noise, more of them near the ground, a dark rim at their edge. Its
  // offsets snap to eighths of the tile, so a pattern on that grid (brick
  // courses) keeps its joints.
  float bare = 0.0;
  vec2 ua = floor(offa * 8.0 + 0.5) / 8.0, ub = floor(offb * 8.0 + 0.5) / 8.0;
  if (worldUv > 0.0 && underAmount > 0.0) {
    // Patches drawn out downward, as damp and grime run, and fewer of them
    // up the wall: the top layer gathers at the foot.
    float wear = value_noise(vec2(uv.x * 0.9, uv.y * 0.3) + 31.0) * 0.6 + value_noise(uv * 1.7 + 7.0) * 0.4;
    wear -= 0.25 * exp(-max(fragPos.y / worldUv, 0.0) * 1.2);
    float t = 1.0 - underAmount;
    // A wide edge: the two layers fade into each other, no seam.
    bare = smoothstep(t - 0.16, t + 0.16, wear);
    vec3 under = untiled(underMap, uv, ua, ub, k).rgb;
    // The top layer's own marks (stains, cracks, streaks) carried onto the
    // under one, as the image against its own blurred self: they stay where
    // the under layer shows, only fainter.
    vec2 dx = dFdx(uv), dy = dFdy(uv);
    vec3 top = untiled(texture0, uv, offa, offb, k).rgb;
    vec3 blur = textureLod(texture0, uv + offa, 7.0).rgb;
    float marks = dot(top, vec3(0.3, 0.59, 0.11)) / max(dot(blur, vec3(0.3, 0.59, 0.11)), 0.05);
    under *= mix(1.0, clamp(marks, 0.5, 1.4), 0.55);
    base.rgb = mix(base.rgb, under, bare);
  }
  view_mask_cut(fragPos);
  float cut = dissolve_cut(fragPos);
  if (useNormalMap == 1 || bare > 0.0) {
    if (worldUv > 0.0) {
      vec3 m = useNormalMap == 1 ? untiled(texture2, uv, offa, offb, k).xyz * 2.0 - 1.0 : vec3(0.0, 0.0, 1.0);
      if (bare > 0.0 && useUnderNormal == 1)
        m = mix(m, untiled(underNormalMap, uv, ua, ub, k).xyz * 2.0 - 1.0, bare);
      n = normalize(mat3(wt, wb, n) * m);
    } else {
      n = mapped_normal(n, uv);
    }
  }
  vec3 glow = useEmissionMap == 1 ? texture(emissionMap, uv).rgb * emissionColor.rgb : vec3(0.0);
  if (pbrParams.x > 0.5) {
    if ((usePbrMaps & 1) != 0) {
      vec4 mr = texture(metalRoughMap, (usePbrMaps & 4) != 0 ? fragTexCoord2 : uv);
      surfMetal = pbrParams.y * mr.b;
      surfRough = pbrParams.z * mr.g;
    }
    if ((usePbrMaps & 2) != 0)
      surfOcclusion = texture(occlusionMap, (usePbrMaps & 8) != 0 ? fragTexCoord2 : uv).r;
  }
  finalColor = vec4(shade(base.rgb, n, fragPos, cut, glow), base.a);
}
)";

// Signed distance shapes, sphere-traced inside their bounding box. The box is
// drawn with its back faces, so the camera may be inside it; the ray starts
// where it enters the box (or at the eye) and the depth written is the hit's.
// Ray origin and direction: from the eye (perspective) or along `rayDir`
// (the sun's orthographic shadow pass, `depthOnly`).
const char *const sdf_vs = R"(#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
uniform mat4 matModel;
out vec3 fragPos;
void main() {
  fragPos = (matModel * vec4(vertexPosition, 1.0)).xyz;
  gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

const char *const sdf_fs_main = R"(
in vec3 fragPos;
uniform int shapeKind;
uniform vec4 shapeDims;
uniform vec3 shapeBounds;  // half size of the box the shape fits in
uniform mat4 shapeToLocal; // world -> shape space (rotation and position only)
uniform mat4 shapeToWorld;
uniform mat4 matVP;        // for the depth of the hit
uniform int rayOrtho;
uniform vec3 rayDir;
uniform int depthOnly;
// A blended shape (shapeKind 5, draw_sdf_blend): rounded cones from
// blendA[i].xyz (radius .w) to blendB[i].xyz (radius .w), each melted into
// the ones before it as softly as blendK[i].
uniform vec4 blendA[32];
uniform vec4 blendB[32];
uniform float blendK[32];
uniform int blendCount;
uniform vec2 claySurface; // amount, frequency relative to each part radius

// Rounded cone from a (radius r1) to b (radius r2); Inigo Quilez's.
float round_cone(vec3 p, vec3 a, vec3 b, float r1, float r2) {
  vec3 ba = b - a;
  float l2 = dot(ba, ba);
  if (l2 < 1e-10)
    return length(p - a) - max(r1, r2);
  float rr = r1 - r2;
  float a2 = l2 - rr * rr;
  float il2 = 1.0 / l2;
  vec3 pa = p - a;
  float y = dot(pa, ba);
  float z = y - l2;
  vec3 xv = pa * l2 - ba * y;
  float x2 = dot(xv, xv);
  float y2 = y * y * l2;
  float z2 = z * z * l2;
  float k = sign(rr) * rr * rr * x2;
  if (sign(z) * a2 * z2 > k)
    return sqrt(x2 + z2) * il2 - r2;
  if (sign(y) * a2 * y2 < k)
    return sqrt(x2 + y2) * il2 - r1;
  return (sqrt(x2 * a2 * il2) + y * rr) * il2 - r1;
}

// Polynomial smooth minimum: the two meet in a fillet about k wide.
float smin(float a, float b, float k) {
  if (k <= 0.0)
    return min(a, b);
  float h = max(k - abs(a - b), 0.0) / k;
  return min(a, b) - h * h * k * 0.25;
}

float sdf(vec3 p) {
  if (shapeKind == 5) {
    float d = 1e9;
    for (int i = 0; i < blendCount; i++)
      d = smin(d, round_cone(p, blendA[i].xyz, blendB[i].xyz, blendA[i].w, blendB[i].w), blendK[i]);
    return d;
  }
  vec4 d = shapeDims;
  if (shapeKind == 0)
    return length(p) - d.x;
  if (shapeKind == 1) {
    vec3 q = abs(p) - d.xyz + d.w;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - d.w;
  }
  if (shapeKind == 2) {
    p.y -= clamp(p.y, -d.y, d.y);
    return length(p) - d.x;
  }
  if (shapeKind == 3) {
    vec2 q = vec2(length(p.xz) - d.x + d.w, abs(p.y) - d.y + d.w);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - d.w;
  }
  vec2 q = vec2(length(p.xz) - d.x, p.y);
  return length(q) - d.y;
}


// Continuous group-local value noise. Translation follows the SDF draw;
// the field is seamless across the short cones used to round a limb.
float clay_hash(vec3 p) {
  p = fract(p * 0.1031);
  p += dot(p, p.yzx + 33.33);
  return fract((p.x + p.y) * p.z);
}
float clay_noise(vec3 p) {
  vec3 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(clay_hash(i), clay_hash(i+vec3(1,0,0)), f.x),
                 mix(clay_hash(i+vec3(0,1,0)), clay_hash(i+vec3(1,1,0)), f.x), f.y),
             mix(mix(clay_hash(i+vec3(0,0,1)), clay_hash(i+vec3(1,0,1)), f.x),
                 mix(clay_hash(i+vec3(0,1,1)), clay_hash(i+vec3(1)), f.x), f.y), f.z);
}
void clay_shade(vec3 p, inout vec3 n, inout vec3 albedo) {
  if (claySurface.x <= 0.0) return;
  // One continuous field per SDF group. Resetting the coordinate frame at
  // every short cone makes grain seams read as bracelets around elbows/knees.
  float radius = max(length(shapeBounds) * 0.2, 0.0001);
  if (shapeKind == 5) {
    radius = 0.0001;
    for (int i=0; i<blendCount; ++i)
      radius = max(radius, max(blendA[i].w, blendB[i].w));
  }
  vec3 q = p / radius * claySurface.y;
  // Fade grain below pixel resolution to avoid sparkly noise in an RTS crowd.
  float footprint = max(length(dFdx(q)),length(dFdy(q)));
  float amount = claySurface.x * (1.0-smoothstep(0.35,1.4,footprint));
  vec3 e=vec3(0.08,0,0);
  vec3 g=vec3(clay_noise(q+e.xyy)-clay_noise(q-e.xyy),
              clay_noise(q+e.yxy)-clay_noise(q-e.yxy),
              clay_noise(q+e.yyx)-clay_noise(q-e.yyx))/0.16;
  n=normalize(n-amount*0.32*(g-n*dot(g,n)));
  albedo*=1.0+amount*0.12*(clay_noise(q*0.32)-0.5);
}

vec3 sdf_normal(vec3 p, float e) {
  vec2 k = vec2(1.0, -1.0);
  return normalize(k.xyy * sdf(p + k.xyy * e) + k.yyx * sdf(p + k.yyx * e) + k.yxy * sdf(p + k.yxy * e) +
                   k.xxx * sdf(p + k.xxx * e));
}

void main() {
  vec3 dir = rayOrtho == 1 ? normalize(rayDir) : normalize(fragPos - viewPos);
  vec3 origin = rayOrtho == 1 ? fragPos - dir * 4.0 * length(shapeBounds) : viewPos;
  vec3 ro = (shapeToLocal * vec4(origin, 1.0)).xyz;
  vec3 rd = mat3(shapeToLocal) * dir;
  // Where the ray is inside the bounding box.
  vec3 inv = 1.0 / rd;
  vec3 t0 = (-shapeBounds - ro) * inv;
  vec3 t1 = (shapeBounds - ro) * inv;
  vec3 tmin = min(t0, t1);
  vec3 tmax = max(t0, t1);
  float t = max(max(max(tmin.x, tmin.y), tmin.z), 0.0);
  float far = min(min(tmax.x, tmax.y), tmax.z);
  float eps = 0.0004 * length(shapeBounds);
  bool hit = false;
  for (int i = 0; i < 96 && t <= far; i++) {
    float d = sdf(ro + rd * t);
    if (d < eps) {
      hit = true;
      break;
    }
    t += d;
  }
  if (!hit)
    discard;
  vec3 local = ro + rd * t;
  vec3 pos = (shapeToWorld * vec4(local, 1.0)).xyz;
  vec4 clip = matVP * vec4(pos, 1.0);
  gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;
  if (depthOnly == 1) {
    finalColor = vec4(1.0);
    return;
  }
  view_mask_cut(pos);
  float cut = dissolve_cut(pos);
  vec3 localNormal = sdf_normal(local, eps);
  vec3 albedo = colDiffuse.rgb;
  clay_shade(local, localNormal, albedo);
  vec3 n = normalize(mat3(shapeToWorld) * localNormal);
  finalColor = vec4(shade(albedo, n, pos, cut, vec3(0.0)), colDiffuse.a);
}
)";

// draw_instanced3d: the model matrix comes from the instance data
// (njin_3d.h): instance0 position and uniform scale, instance1 colour,
// instance2 rotation in degrees (z, then x, then y), instance3 per-axis scale.
const char *const instanced_glsl = R"(
// Pinned past every attribute a raylib mesh uses (0..8, 9..12 for its own
// instancing), so pointing them at the instance buffer never touches the
// mesh's vertex array.
layout(location = 12) in vec4 instance0;
layout(location = 13) in vec4 instance1;
layout(location = 14) in vec4 instance2;
layout(location = 15) in vec4 instance3;
uniform int instanceFloats;
mat3 instance_rotation() {
  if (instanceFloats < 12)
    return mat3(1.0);
  vec3 r = radians(instance2.xyz);
  vec3 c = cos(r);
  vec3 s = sin(r);
  mat3 rx = mat3(1.0, 0.0, 0.0, 0.0, c.x, s.x, 0.0, -s.x, c.x);
  mat3 ry = mat3(c.y, 0.0, -s.y, 0.0, 1.0, 0.0, s.y, 0.0, c.y);
  mat3 rz = mat3(c.z, s.z, 0.0, -s.z, c.z, 0.0, 0.0, 0.0, 1.0);
  return ry * rx * rz;
}
vec3 instance_scale() {
  float k = instance0.w != 0.0 ? instance0.w : 1.0;
  vec3 axes = instanceFloats >= 16 ? instance3.xyz : vec3(1.0);
  axes = mix(axes, vec3(1.0), vec3(equal(axes, vec3(0.0))));
  return axes * k;
}
)";

const char *const lit_instanced_vs_main = R"(
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec2 fragTexCoord2;
out vec4 fragColor;
out vec3 fragNormal;
out vec3 fragPos;
void main() {
  mat3 r = instance_rotation();
  vec3 k = instance_scale();
  vec3 world = r * (vertexPosition * k) + instance0.xyz;
  fragTexCoord = vertexTexCoord;
  fragTexCoord2 = vertexTexCoord2;
  fragColor = vertexColor * (instanceFloats >= 8 ? instance1 : vec4(1.0));
  fragNormal = r * (vertexNormal / k);
  fragPos = world;
  gl_Position = mvp * vec4(world, 1.0);
}
)";

const char *const depth_instanced_vs_main = R"(
in vec3 vertexPosition;
uniform mat4 mvp;
void main() {
  vec3 world = instance_rotation() * (vertexPosition * instance_scale()) + instance0.xyz;
  gl_Position = mvp * vec4(world, 1.0);
}
)";

// Skeletal animation on the GPU: the bone indices and weights njin_model.cpp
// uploads at locations 10 and 11, and the pose's bone matrices (up to
// skin_max_bones), as raylib computes them (inverse bind pose, then the pose).
const char *const skin_glsl = R"(
layout(location = 10) in vec4 vertexBoneIds;
layout(location = 11) in vec4 vertexBoneWeights;
uniform mat4 boneMatrices[128];
mat4 skin_matrix() {
  vec4 w = vertexBoneWeights;
  float sum = w.x + w.y + w.z + w.w;
  if (sum < 1e-4)
    return mat4(1.0);
  return (w.x * boneMatrices[int(vertexBoneIds.x)] + w.y * boneMatrices[int(vertexBoneIds.y)] +
          w.z * boneMatrices[int(vertexBoneIds.z)] + w.w * boneMatrices[int(vertexBoneIds.w)]) / sum;
}
)";

const char *const lit_skinned_vs_main = R"(
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec2 fragTexCoord;
out vec2 fragTexCoord2;
out vec4 fragColor;
out vec3 fragNormal;
out vec3 fragPos;
void main() {
  mat4 k = skin_matrix();
  vec4 p = k * vec4(vertexPosition, 1.0);
  fragTexCoord = vertexTexCoord;
  fragTexCoord2 = vertexTexCoord2;
  fragColor = vertexColor;
  fragNormal = (matNormal * vec4(mat3(k) * vertexNormal, 0.0)).xyz;
  fragPos = (matModel * p).xyz;
  gl_Position = mvp * p;
}
)";

const char *const depth_skinned_vs_main = R"(
in vec3 vertexPosition;
uniform mat4 mvp;
void main() { gl_Position = mvp * (skin_matrix() * vec4(vertexPosition, 1.0)); }
)";

const char *const depth_vs = R"(#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
void main() { gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";

const char *const depth_fs = R"(#version 330
out vec4 finalColor;
void main() { finalColor = vec4(1.0); }
)";

// The reflecting surfaces for post3d's SSR: `maskValue` where this fragment is
// the nearest surface of the pass (its depth copy), nothing behind it.
const char *const mask_fs = R"(#version 330
uniform sampler2D sceneDepth;
uniform vec2 sceneSize;
uniform vec2 planes;
uniform float maskValue;
out vec4 finalColor;
float linear_depth(float d) {
  // A negative near plane marks an orthographic camera, whose depth is linear.
  if (planes.x < 0.0)
    return -planes.x + d * (planes.y + planes.x);
  float z = d * 2.0 - 1.0;
  return 2.0 * planes.x * planes.y / (planes.y + planes.x - z * (planes.y - planes.x));
}
void main() {
  float scene = texture(sceneDepth, gl_FragCoord.xy / sceneSize).r;
  float a = linear_depth(gl_FragCoord.z);
  float b = linear_depth(scene);
  if (a > b * 1.002 + 0.02)
    discard;
  finalColor = vec4(maskValue, 0.0, 0.0, 1.0);
}
)";
// The unit the mask programs read the depth copy from: free while they draw
// (DrawMesh binds only the material's maps, from unit 0).
constexpr i32 mask_depth_unit = 9;

// Motion vectors for post3d (render3d_draw_velocity): each vertex now (curVP,
// matModel, the pose's bones) and in the last motion pass (prevVP, prevModel,
// the bones then); `mvp` (jittered, as the pass drew) only places the fragment.
const char *const velocity_vs = R"(#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 prevModel;
uniform mat4 curVP;
uniform mat4 prevVP;
out vec4 curClip;
out vec4 prevClip;
void main() {
  vec4 p = vec4(vertexPosition, 1.0);
  curClip = curVP * (matModel * p);
  prevClip = prevVP * (prevModel * p);
  gl_Position = mvp * p;
}
)";

// The bones of the last motion pass come from a texture (four texels per
// matrix): a second array of 128 matrices would pass the uniform limit.
const char *const velocity_skinned_vs_main = R"(
in vec3 vertexPosition;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 prevModel;
uniform mat4 curVP;
uniform mat4 prevVP;
uniform sampler2D prevBones;
uniform int prevBoneCount;
out vec4 curClip;
out vec4 prevClip;
mat4 prev_bone(float i) {
  int b = int(i) * 4;
  return mat4(texelFetch(prevBones, ivec2(b, 0), 0), texelFetch(prevBones, ivec2(b + 1, 0), 0),
              texelFetch(prevBones, ivec2(b + 2, 0), 0), texelFetch(prevBones, ivec2(b + 3, 0), 0));
}
mat4 prev_skin(mat4 now) {
  vec4 w = vertexBoneWeights;
  float sum = w.x + w.y + w.z + w.w;
  if (prevBoneCount == 0 || sum < 1e-4)
    return now;
  return (w.x * prev_bone(vertexBoneIds.x) + w.y * prev_bone(vertexBoneIds.y) + w.z * prev_bone(vertexBoneIds.z) +
          w.w * prev_bone(vertexBoneIds.w)) / sum;
}
void main() {
  vec4 p = vec4(vertexPosition, 1.0);
  mat4 k = skin_matrix();
  vec4 now = k * p;
  curClip = curVP * (matModel * now);
  prevClip = prevVP * (prevModel * (prev_skin(k) * p));
  gl_Position = mvp * now;
}
)";

// Where this fragment is the nearest surface of the pass: its screen motion
// (uv), its distance from the eye in the last pass and 1. A jump of more than a quarter of the
// screen (a teleport) and points behind either eye give 0: the camera's motion
// is used there.
const char *const velocity_fs = R"(#version 330
in vec4 curClip;
in vec4 prevClip;
uniform sampler2D sceneDepth;
uniform vec2 sceneSize;
uniform vec2 planes;
out vec4 finalColor;
float linear_depth(float d) {
  // A negative near plane marks an orthographic camera, whose depth is linear.
  if (planes.x < 0.0)
    return -planes.x + d * (planes.y + planes.x);
  float z = d * 2.0 - 1.0;
  return 2.0 * planes.x * planes.y / (planes.y + planes.x - z * (planes.y - planes.x));
}
void main() {
  float scene = texture(sceneDepth, gl_FragCoord.xy / sceneSize).r;
  if (linear_depth(gl_FragCoord.z) > linear_depth(scene) * 1.002 + 0.02)
    discard;
  if (curClip.w <= 0.0 || prevClip.w <= 0.0) {
    finalColor = vec4(0.0);
    return;
  }
  vec2 v = (curClip.xy / curClip.w - prevClip.xy / prevClip.w) * 0.5;
  if (dot(v, v) > 0.0625) {
    finalColor = vec4(0.0);
    return;
  }
  // The distance from the eye then (clip w), not the window depth: half float
  // keeps a distance to 1/2048, but not depths crowded just below 1.
  finalColor = vec4(v, prevClip.w, 1.0);
}
)";
constexpr i32 prev_bones_unit = 10;

Vector3 rl3(vec3 v) {
  Vector3 out{};
  to_raylib(v, out);
  return out;
}

vec3 rgb(rgba c) { return vec3{c.r, c.g, c.b}; }
vec4 v4(rgba c) { return {c.r, c.g, c.b, c.a}; }

void set_i32(Shader s, i32 loc, i32 v) { SetShaderValue(s, loc, &v, SHADER_UNIFORM_INT); }
void set_f32(Shader s, i32 loc, f32 v) { SetShaderValue(s, loc, &v, SHADER_UNIFORM_FLOAT); }
void set_vec2(Shader s, i32 loc, vec2 v) { SetShaderValue(s, loc, &v, SHADER_UNIFORM_VEC2); }
void set_vec3(Shader s, i32 loc, vec3 v) { SetShaderValue(s, loc, &v, SHADER_UNIFORM_VEC3); }
void set_vec4(Shader s, i32 loc, vec4 v) { SetShaderValue(s, loc, &v, SHADER_UNIFORM_VEC4); }

Texture2D default_texture() {
  return Texture2D{.id = rlGetTextureIdDefault(), .width = 1, .height = 1, .mipmaps = 1,
                   .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
}

render3d_locations find_locations(Shader shader) {
  render3d_locations l;
  const auto loc = [&](const char *name) { return GetShaderLocation(shader, name); };
  l.light_dir = loc("lightDir");
  l.light_color = loc("lightColor");
  l.ambient = loc("ambient");
  l.view_pos = loc("viewPos");
  l.fog_color = loc("fogColor");
  l.fog_density = loc("fogDensity");
  l.light_count = loc("lightCount");
  l.light_pos = loc("lightPos");
  l.light_colors = loc("lightColors");
  l.light_spot = loc("lightSpot");
  l.surface = loc("surface");
  l.emission = loc("emission");
  l.rim = loc("rim");
  l.emission_color = loc("emissionColor");
  l.unlit = loc("unlit");
  l.use_normal_map = loc("useNormalMap");
  l.use_emission_map = loc("useEmissionMap");
  l.world_uv = loc("worldUv");
  l.under_amount = loc("underAmount");
  l.use_under_normal = loc("useUnderNormal");
  l.shadow_on = loc("shadowOn");
  l.shadow_map = loc("shadowMap");
  l.light_vp = loc("lightVP");
  l.shadow_params = loc("shadowParams");
  l.flash = loc("flash");
  l.dissolve = loc("dissolve");
  l.view_mask = loc("viewMask");
  l.edge_color = loc("edgeColor");
  l.clay_surface = loc("claySurface");
  l.shape_kind = loc("shapeKind");
  l.shape_dims = loc("shapeDims");
  l.shape_bounds = loc("shapeBounds");
  l.shape_to_local = loc("shapeToLocal");
  l.shape_to_world = loc("shapeToWorld");
  l.mat_vp = loc("matVP");
  l.ray_ortho = loc("rayOrtho");
  l.ray_dir = loc("rayDir");
  l.depth_only = loc("depthOnly");
  l.blend_a = loc("blendA");
  l.blend_b = loc("blendB");
  l.blend_count = loc("blendCount");
  l.blend_k = loc("blendK");
  l.light_shadow = loc("lightShadow");
  l.lamp_vp = loc("lampVP");
  l.lamp_params = loc("lampParams");
  l.lamp_rows = loc("lampRows");
  l.lamp_map = loc("lampShadowMap");
  l.bones = loc("boneMatrices");
  l.tone_map = loc("toneMap");
  l.exposure = loc("exposure");
  l.light_color_linear = loc("lightColorLinear");
  l.ambient_linear = loc("ambientLinear");
  l.light_colors_linear = loc("lightColorsLinear");
  l.emission_linear = loc("emissionLinear");
  l.pbr_params = loc("pbrParams");
  l.use_pbr_maps = loc("usePbrMaps");
  l.env_on = loc("envOn");
  l.env_sh = loc("envSH");
  l.env_map = loc("envMap");
  l.env_params = loc("envParams");
  return l;
}

bool ensure_ready(render3d_state &s) {
  if (s.ready)
    return true;
  if (s.failed)
    return false;
  const std::string head = "#version 330\n";
  const std::string lit_fs = head + lighting_glsl + lit_fs_main;
  const std::string sdf_fs = head + lighting_glsl + sdf_fs_main;
  s.lit = LoadShaderFromMemory(lit_vs, lit_fs.c_str());
  s.sdf = LoadShaderFromMemory(sdf_vs, sdf_fs.c_str());
  s.depth = LoadShaderFromMemory(depth_vs, depth_fs);
  const std::string lit_instanced_vs = head + instanced_glsl + lit_instanced_vs_main;
  const std::string depth_instanced_vs = head + instanced_glsl + depth_instanced_vs_main;
  s.lit_instanced = LoadShaderFromMemory(lit_instanced_vs.c_str(), lit_fs.c_str());
  s.depth_instanced = LoadShaderFromMemory(depth_instanced_vs.c_str(), depth_fs);
  if (!IsShaderValid(s.lit) || !IsShaderValid(s.sdf) || !IsShaderValid(s.depth) ||
      !IsShaderValid(s.lit_instanced) || !IsShaderValid(s.depth_instanced)) {
    NJIN_WARN("3d: the built-in shaders failed to compile, nothing 3D is drawn");
    s.failed = true;
    return false;
  }
  s.locs = find_locations(s.lit);
  s.sdf_locs = find_locations(s.sdf);
  s.instanced_locs = find_locations(s.lit_instanced);
  // Skinning needs a large uniform array; a driver that cannot take it still
  // draws models, in their rest pose.
  const std::string lit_skinned_vs = head + skin_glsl + lit_skinned_vs_main;
  const std::string depth_skinned_vs = head + skin_glsl + depth_skinned_vs_main;
  s.lit_skinned = LoadShaderFromMemory(lit_skinned_vs.c_str(), lit_fs.c_str());
  s.depth_skinned = LoadShaderFromMemory(depth_skinned_vs.c_str(), depth_fs);
  s.skin_ok = IsShaderValid(s.lit_skinned) && IsShaderValid(s.depth_skinned);
  if (s.skin_ok) {
    s.skinned_locs = find_locations(s.lit_skinned);
    s.depth_skinned_bones = GetShaderLocation(s.depth_skinned, "boneMatrices");
    s.lit_skinned.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(s.lit_skinned, "emissionMap");
    s.lit_skinned.locs[SHADER_LOC_MAP_ROUGHNESS] = GetShaderLocation(s.lit_skinned, "underMap");
    s.lit_skinned.locs[SHADER_LOC_MAP_OCCLUSION] = GetShaderLocation(s.lit_skinned, "underNormalMap");
    s.lit_skinned.locs[SHADER_LOC_MAP_METALNESS] = GetShaderLocation(s.lit_skinned, "metalRoughMap");
    s.lit_skinned.locs[SHADER_LOC_MAP_HEIGHT] = GetShaderLocation(s.lit_skinned, "occlusionMap");
  } else {
    NJIN_WARN("3d: the skinning shader failed to compile, animated models are drawn in their rest pose");
  }
  // DrawMesh binds the emission map through the emission slot's location,
  // and material3d::under through two slots the built-in shader has no
  // other use for (roughness, occlusion).
  s.lit.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(s.lit, "emissionMap");
  s.lit.locs[SHADER_LOC_MAP_ROUGHNESS] = GetShaderLocation(s.lit, "underMap");
  s.lit.locs[SHADER_LOC_MAP_OCCLUSION] = GetShaderLocation(s.lit, "underNormalMap");
  // And the PBR maps through the metalness and height slots.
  s.lit.locs[SHADER_LOC_MAP_METALNESS] = GetShaderLocation(s.lit, "metalRoughMap");
  s.lit.locs[SHADER_LOC_MAP_HEIGHT] = GetShaderLocation(s.lit, "occlusionMap");
  // Enough segments that a sphere or capsule filling a good part of the
  // screen still reads as round.
  s.cube = GenMeshCube(1.0f, 1.0f, 1.0f);
  s.sphere = GenMeshSphere(1.0f, 32, 48);
  s.plane = GenMeshPlane(1.0f, 1.0f, 1, 1);
  s.cylinder = GenMeshCylinder(1.0f, 1.0f, 48);
  // Few faces, for thousands of small copies (mesh3d_sphere_low and
  // mesh3d_cylinder_low): each is a few pixels on screen. The same 8 sides
  // round, so a sphere on a cylinder closes into a capsule.
  s.sphere_low = GenMeshSphere(1.0f, 6, 8);
  s.cylinder_low = GenMeshCylinder(1.0f, 1.0f, 8);
  s.maps[MATERIAL_MAP_DIFFUSE].texture = default_texture();
  s.ready = true;
  return true;
}

void free_shadow(shadow_target &t) {
  if (t.fbo != 0)
    rlUnloadFramebuffer(t.fbo);
  if (t.depth != 0)
    rlUnloadTexture(t.depth);
  if (t.color != 0)
    rlUnloadTexture(t.color);
  t = shadow_target{};
}

bool ensure_shadow(shadow_target &t, i32 width, i32 height) {
  if (t.fbo != 0 && t.size == width && t.height == height)
    return true;
  free_shadow(t);
  t.fbo = rlLoadFramebuffer();
  t.color = rlLoadTexture(nullptr, width, height, RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, 1);
  t.depth = rlLoadTextureDepth(width, height, false);
  rlFramebufferAttach(t.fbo, t.color, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
  rlFramebufferAttach(t.fbo, t.depth, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
  if (!rlFramebufferComplete(t.fbo)) {
    NJIN_WARN("3d: shadow map framebuffer incomplete, shadows are off");
    free_shadow(t);
    return false;
  }
  t.size = width;
  t.height = height;
  return true;
}

// camera_shake() for a 3D camera: the 2D shake's screen offset becomes a turn
// of the view by the same share of the screen, its roll a tilt of `up`.
camera3d shaken(const context &ctx, camera3d camera) {
  vec2 offset{};
  f32 roll = 0.0f;
  if (!fx_shake_sample(ctx, offset, roll))
    return camera;
  const vec2 screen = screen_size(ctx);
  const vec3 forward = normalize(camera.target - camera.position);
  const vec3 right = normalize(cross(forward, camera.up));
  const vec3 up = cross(right, forward);
  if (camera.projection == camera3d_orthographic) {
    // Parallel rays: the view slides by the same share of the screen.
    const f32 units = screen.y > 0.0f ? camera.ortho_height / screen.y : 0.0f;
    const vec3 move = right * (-offset.x * units) + up * (offset.y * units);
    const Vector3 u = Vector3RotateByAxisAngle(rl3(up), rl3(forward), roll * DEG2RAD);
    camera.position = camera.position + move;
    camera.target = camera.target + move;
    camera.up = {u.x, u.y, u.z};
    return camera;
  }
  const f32 per_pixel = screen.y > 0.0f ? camera.fovy / screen.y : 0.0f;
  Vector3 f = rl3(forward);
  f = Vector3RotateByAxisAngle(f, rl3(up), -offset.x * per_pixel * DEG2RAD);
  f = Vector3RotateByAxisAngle(f, rl3(right), -offset.y * per_pixel * DEG2RAD);
  const Vector3 u = Vector3RotateByAxisAngle(rl3(up), f, roll * DEG2RAD);
  const f32 dist = distance(camera.position, camera.target);
  camera.target = camera.position + vec3{f.x, f.y, f.z} * dist;
  camera.up = {u.x, u.y, u.z};
  return camera;
}

// The size of what the open pass draws into: its render texture, or the
// logical screen.
vec2 pass_size(const context &ctx) {
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  return s.target_fbo != 0 ? s.target_size : screen_size(ctx);
}

// Binds what the open pass draws into again (after the shadow passes).
void bind_pass_target(context &ctx) {
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  if (s.target_fbo == 0) {
    world_target_rebind(ctx);
    return;
  }
  rlDrawRenderBatchActive();
  rlEnableFramebuffer(s.target_fbo);
  rlViewport(0, 0, (i32)s.target_size.x, (i32)s.target_size.y);
  rlSetFramebufferWidth((i32)s.target_size.x);
  rlSetFramebufferHeight((i32)s.target_size.y);
}

// Projection and view of `camera` on the current matrices.
void load_camera(const context &ctx, const camera3d &camera) {
  // BeginMode3D takes the aspect from the window framebuffer, which is wrong
  // on the virtual image; the logical screen size is the right one.
  const vec2 screen = pass_size(ctx);
  const f64 aspect = screen.y > 0.0f ? (f64)screen.x / (f64)screen.y : 1.0;
  const bool ortho = camera.projection == camera3d_orthographic;
  const f64 top = ortho ? (f64)std::max(camera.ortho_height, 1e-4f) * 0.5
                        : (f64)camera.near_plane * std::tan((f64)camera.fovy * 0.5 * (f64)DEG2RAD);
  const f64 right = top * aspect;
  // TAA's jitter: an off-axis frustum, the image moved by a fraction of a pixel.
  const vec2 j = ctx_ref<render3d_state>(ctx)->jitter;
  const f64 jx = (f64)j.x * right;
  const f64 jy = (f64)j.y * top;
  rlMatrixMode(RL_PROJECTION);
  rlLoadIdentity();
  if (ortho)
    rlOrtho(-right + jx, right + jx, -top + jy, top + jy, camera.near_plane, camera.far_plane);
  else
    rlFrustum(-right + jx, right + jx, -top + jy, top + jy, camera.near_plane, camera.far_plane);
  rlMatrixMode(RL_MODELVIEW);
  rlLoadIdentity();
  rlMultMatrixf(MatrixToFloat(MatrixLookAt(rl3(camera.position), rl3(camera.target), rl3(camera.up))));
}

} // namespace

vec2 render3d_depth_planes(const camera3d &camera) {
  const bool ortho = camera.projection == camera3d_orthographic;
  return {ortho ? -camera.near_plane : camera.near_plane, camera.far_plane};
}

vec3 render3d_post_eye(const camera3d &camera) {
  if (camera.projection != camera3d_orthographic)
    return camera.position;
  const vec3 forward = normalize(camera.target - camera.position);
  return camera.target - forward * (1000.0f * std::max({camera.far_plane, camera.ortho_height, 1.0f}));
}

namespace {
// The frustum of the matrices load_camera() set, as six planes. Row i of the
// view-projection is (m[i], m[i + 4], m[i + 8], m[i + 12]) in raylib's layout.
std::array<vec4, 6> frustum_planes(const Matrix &m) {
  const vec4 r0{m.m0, m.m4, m.m8, m.m12}, r1{m.m1, m.m5, m.m9, m.m13}, r2{m.m2, m.m6, m.m10, m.m14},
      r3{m.m3, m.m7, m.m11, m.m15};
  const auto add = [](vec4 a, vec4 b, f32 k) { return vec4{a.x + b.x * k, a.y + b.y * k, a.z + b.z * k, a.w + b.w * k}; };
  return {add(r3, r0, 1.0f), add(r3, r0, -1.0f), add(r3, r1, 1.0f),
          add(r3, r1, -1.0f), add(r3, r2, 1.0f), add(r3, r2, -1.0f)};
}

void set_frustum(render3d_state &s) {
  s.frustum = frustum_planes(MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection()));
  s.tan_half_fovy = std::tan(s.camera.fovy * 0.5f * DEG2RAD);
}

// Whether the box (centre, half size) lies wholly outside one of `planes`.
bool box_outside(const std::array<vec4, 6> &planes, vec3 center, vec3 extent) {
  for (const vec4 &p : planes) {
    const f32 reach = std::abs(p.x) * extent.x + std::abs(p.y) * extent.y + std::abs(p.z) * extent.z;
    if (p.x * center.x + p.y * center.y + p.z * center.z + p.w + reach < 0.0f)
      return true;
  }
  return false;
}

// The world box of the local box (centre `mid`, half size `half`) moved by `t`.
void place_box(draw3d_cmd &c, const Matrix &t, vec3 mid, vec3 half) {
  c.boxed = true;
  c.box_center = {t.m0 * mid.x + t.m4 * mid.y + t.m8 * mid.z + t.m12,
                  t.m1 * mid.x + t.m5 * mid.y + t.m9 * mid.z + t.m13,
                  t.m2 * mid.x + t.m6 * mid.y + t.m10 * mid.z + t.m14};
  c.box_extent = {std::abs(t.m0) * half.x + std::abs(t.m4) * half.y + std::abs(t.m8) * half.z,
                  std::abs(t.m1) * half.x + std::abs(t.m5) * half.y + std::abs(t.m9) * half.z,
                  std::abs(t.m2) * half.x + std::abs(t.m6) * half.y + std::abs(t.m10) * half.z};
}

// Pass state for a draw call: only inside begin_3d/end_3d.
const render3d_state *open_pass(const context &ctx) {
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  return s.active ? &s : nullptr;
}

// Mixes two numbers into a draw's motion key (splitmix64); never 0, which is "no key".
u64 mix_key(u64 a, u64 b) {
  u64 x = a * 0x9E3779B97F4A7C15ull ^ (b + 0x632BE59BD9B4E019ull + (a << 6) + (a >> 2));
  x ^= x >> 31;
  x *= 0xBF58476D1CE4E5B9ull;
  x ^= x >> 27;
  x *= 0x94D049BB133111EBull;
  x ^= x >> 31;
  return x | 1u;
}

// The motion key of the draw being recorded: the game's draw3d_motion_id(),
// else `base` (the mesh or model) and how many draws of it came before it in
// the pass, so a scene drawn in the same order each frame matches itself.
u64 next_motion_key(const render3d_state &s, u64 base) {
  if (s.next_motion_id != 0) {
    const u64 id = s.next_motion_id;
    s.next_motion_id = 0;
    return mix_key(1, id);
  }
  return mix_key(base, s.motion_seen[base]++);
}

void record(const context &ctx, const Mesh *mesh, model_handle model, const Matrix &transform, rgba color) {
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  const u64 key = next_motion_key(s, mix_key(3, (u64)(uintptr_t)mesh));
  s.cmds.push_back(draw3d_cmd{.is_shape = false,
                              .shape = {},
                              .mesh = mesh,
                              .model = model,
                              .transform = transform,
                              .color = color,
                              .shader = ctx_ref<shader_store>(ctx)->active,
                              .fx = s.fx,
                              .material = s.material,
                              .buffer = {},
                              .first = 0,
                              .count = 0,
                              .bone_first = 0,
                              .bone_count = 0});
  s.cmds.back().motion_key = key;
}

// The +y unit cylinder stretched from `from` to `to`.
bool cylinder_transform(vec3 from, vec3 to, f32 radius, Matrix &out) {
  const vec3 axis = to - from;
  const f32 len = length(axis);
  if (len <= 0.0f)
    return false;
  const vec3 dir = axis / len;
  const vec3 up{0.0f, 1.0f, 0.0f};
  const vec3 turn = cross(up, dir);
  const f32 turn_len = length(turn);
  Matrix rotation = MatrixIdentity();
  if (turn_len > 1e-6f)
    rotation = MatrixRotate(rl3(turn / turn_len), std::atan2(turn_len, dot(up, dir)));
  else if (dir.y < 0.0f)
    rotation = MatrixRotateX(PI);
  out = MatrixMultiply(MatrixScale(radius, len, radius), rotation);
  out = MatrixMultiply(out, MatrixTranslate(from.x, from.y, from.z));
  return true;
}

Matrix scale_then_move(vec3 scale, vec3 pos) {
  return MatrixMultiply(MatrixScale(scale.x, scale.y, scale.z), MatrixTranslate(pos.x, pos.y, pos.z));
}

// A texture of the store usable on a mesh: whole images only, an atlas page
// would put the wrong UVs on it.
bool mesh_texture(const context &ctx, texture_handle handle, Texture2D &out) {
  const texture_slot *slot = texture_slot_of(ctx_ref<texture_store>(ctx), handle);
  if (slot == nullptr || slot->packed)
    return false;
  out = slot->texture;
  return true;
}

// Which PBR maps a draw has, as the shader's usePbrMaps bits.
i32 pbr_map_bits(const Texture2D &metal_rough, const Texture2D &occlusion) {
  return (metal_rough.id > 0 ? 1 : 0) | (occlusion.id > 0 ? 2 : 0);
}

// Uniforms of the built-in shader for one draw.
void set_draw_uniforms(Shader sh, const render3d_locations &l, const fx3d &fx, const material3d &m,
                       rgba emission_color, bool normal_map, bool emission_map, bool under_map = false,
                       bool under_normal = false, i32 pbr_maps = 0) {
  set_vec4(sh, l.flash, v4(fx.flash));
  set_vec4(sh, l.dissolve,
           {clamp(fx.dissolve, 0.0f, 1.0f), fx.edge_width, fx.grain > 0.0f ? fx.grain : 0.1f, fx.seed});
  set_vec4(sh, l.edge_color, v4(fx.edge_color));
  set_vec4(sh, l.view_mask, {fx.mask_center.x, fx.mask_center.y, fx.mask_center.z, std::max(fx.mask_radius, 0.0f)});
  set_vec2(sh, l.surface, {m.specular, std::max(m.shininess, 1.0f)});
  set_vec2(sh, l.clay_surface, {clamp(m.clay, 0.0f, 1.0f), std::max(m.clay_detail, 0.1f)});
  set_vec4(sh, l.emission, v4(m.emission));
  set_vec4(sh, l.rim, v4(m.rim));
  set_vec4(sh, l.emission_color, v4(emission_color));
  set_i32(sh, l.unlit, m.unlit ? 1 : 0);
  set_i32(sh, l.use_normal_map, normal_map ? 1 : 0);
  set_i32(sh, l.use_emission_map, emission_map ? 1 : 0);
  set_f32(sh, l.world_uv, std::max(m.world_uv, 0.0f));
  set_f32(sh, l.under_amount, under_map && m.world_uv > 0.0f ? std::clamp(m.under_amount, 0.0f, 1.0f) : 0.0f);
  set_i32(sh, l.use_under_normal, under_normal ? 1 : 0);
  set_vec4(sh, l.pbr_params, {m.pbr ? 1.0f : 0.0f, clamp(m.metallic, 0.0f, 1.0f), clamp(m.roughness, 0.0f, 1.0f),
                              clamp(m.occlusion_strength, 0.0f, 1.0f)});
  set_i32(sh, l.use_pbr_maps, m.pbr ? pbr_maps : 0);
  const rgb_linear e = linear_of(m.emission);
  set_vec4(sh, l.emission_linear, {e.r, e.g, e.b, m.emission.a});
}

// The shader for a draw recorded with `handle` bound: the game's, given the
// sun under the built-in names, or false for the built-in one.
bool game_shader(const context &ctx, shader_handle handle, Shader &out) {
  const shader_slot *slot = shader_slot_of(ctx_ref<shader_store>(ctx), handle);
  if (slot == nullptr)
    return false;
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  shader_slot_set_optional_vec3(*slot, "lightDir", normalize(s.light.direction));
  shader_slot_set_optional_vec3(*slot, "lightColor", rgb(s.light.color));
  shader_slot_set_optional_vec3(*slot, "ambient", rgb(s.light.ambient));
  shader_slot_set_optional_vec3(*slot, "viewPos", s.camera.position);
  shader_slot_set_optional_vec3(*slot, "fogColor", rgb(s.light.fog_color));
  shader_slot_set_optional_f32(*slot, "fogDensity", std::max(s.light.fog_density, 0.0f));
  // Tone mapping, for a game shader that matches the scene (njin_3d.h).
  shader_slot_set_optional_i32(*slot, "toneMap", s.light.tonemap == tonemap3d_none ? 0 : (i32)s.light.tonemap);
  shader_slot_set_optional_f32(*slot, "exposure", std::max(s.light.exposure, 0.0f));
  const rgb_linear sun = linear_of(s.light.color), amb = linear_of(s.light.ambient);
  shader_slot_set_optional_vec3(*slot, "lightColorLinear", {sun.r, sun.g, sun.b});
  shader_slot_set_optional_vec3(*slot, "ambientLinear", {amb.r, amb.g, amb.b});
  // Not drawn instanced, unless draw_instanced_cmd() says otherwise: one
  // shader can then serve a model drawn both ways.
  shader_slot_set_optional_i32(*slot, "instanceFloats", 0);
  shader_bind_textures_from(ctx, *slot, game_texture_unit);
  out = slot->shader;
  return true;
}

// Translucent or dissolving draws cast no shadow.
bool casts(const draw3d_cmd &c) { return c.color.a >= 1.0f && c.fx.dissolve <= 0.0f; }

using map_set = std::array<MaterialMap, 12>;

// Every mesh of a model with the transform of a draw, and the maps of the
// material it uses with the game's overrides and the draw's tint applied.
// `fn` also gets whether the mesh is drawn posed: the draw has a pose, the
// mesh has bone buffers and the skinning shaders work.
template <typename Fn> void for_each_model_mesh(const context &ctx, const draw3d_cmd &c, Fn &&fn) {
  const model_slot *slot = model_slot_of(ctx_ref<model_store>(ctx), c.model);
  if (slot == nullptr)
    return;
  const Model &model = slot->model;
  const Matrix transform = MatrixMultiply(model.transform, c.transform);
  for (i32 i = 0; i < model.meshCount; i++) {
    const i32 index = model.meshMaterial[i];
    const Material &file = model.materials[index];
    const model_material &mm = slot->materials[(usize)index];
    // See-through parts only in the translucent pass, and never as shadow casters.
    if ((mm.color.a < 1.0f) != ctx_ref<render3d_state>(ctx)->translucent_pass)
      continue;
    map_set maps{};
    for (usize m = 0; m < maps.size(); m++)
      maps[m] = file.maps[m];
    Texture2D texture{};
    if (mesh_texture(ctx, mm.albedo, texture))
      maps[MATERIAL_MAP_DIFFUSE].texture = texture;
    if (mesh_texture(ctx, mm.normal, texture))
      maps[MATERIAL_MAP_NORMAL].texture = texture;
    if (mesh_texture(ctx, mm.emission, texture))
      maps[MATERIAL_MAP_EMISSION].texture = texture;
    // A file's roughness and occlusion maps are not drawn; those slots carry
    // the under layer when the game gave one.
    maps[MATERIAL_MAP_ROUGHNESS].texture = Texture2D{};
    maps[MATERIAL_MAP_OCCLUSION].texture = Texture2D{};
    if (mesh_texture(ctx, mm.surface.under, texture))
      maps[MATERIAL_MAP_ROUGHNESS].texture = texture;
    if (maps[MATERIAL_MAP_ROUGHNESS].texture.id > 0 && mesh_texture(ctx, mm.surface.under_normal, texture))
      maps[MATERIAL_MAP_OCCLUSION].texture = texture;
    if (maps[MATERIAL_MAP_DIFFUSE].texture.id == 0)
      maps[MATERIAL_MAP_DIFFUSE].texture = default_texture();
    // The PBR maps, only for a PBR material: the game's, else the file's
    // (loaded on this first use).
    maps[MATERIAL_MAP_METALNESS].texture = Texture2D{};
    maps[MATERIAL_MAP_HEIGHT].texture = Texture2D{};
    i32 uv2 = 0; // usePbrMaps bits of the file's maps on the second texture coordinates
    if (mm.surface.pbr) {
      if (!mesh_texture(ctx, mm.metal_rough, texture)) {
        texture = model_file_pbr_map(*slot, index, false);
        uv2 |= model_file_pbr_map_uv2(*slot, index, false) ? 4 : 0;
      }
      maps[MATERIAL_MAP_METALNESS].texture = texture;
      if (!mesh_texture(ctx, mm.occlusion, texture)) {
        texture = model_file_pbr_map(*slot, index, true);
        uv2 |= model_file_pbr_map_uv2(*slot, index, true) ? 8 : 0;
      }
      maps[MATERIAL_MAP_HEIGHT].texture = texture;
    }
    // Carried to the draw in the metalness slot's unused value.
    maps[MATERIAL_MAP_METALNESS].value = (f32)uv2;
    rgba own = mm.color;
    for (u32 r = 0; r < c.recolor_count; r++) {
      const model_recolor &rc = ctx_ref<render3d_state>(ctx)->recolors[c.recolor_first + r];
      if (rc.material == index) {
        own = rgba{rc.color.r, rc.color.g, rc.color.b, mm.color.a};
        break;
      }
    }
    const rgba color{own.r * c.color.r, own.g * c.color.g, own.b * c.color.b, own.a * c.color.a};
    to_raylib(color, maps[MATERIAL_MAP_DIFFUSE].color);
    // A mesh with morphs gets the draw's weights, at its level of detail when
    // that level carries them (model_lod_build() keeps them), else in full.
    const bool morphed = (usize)i < slot->morphs.size() && !slot->morphs[(usize)i].slot.empty();
    const model_lod_mesh *lod = model_lod_of(*slot, c.lod, i);
    if (morphed) {
      const f32 *w = c.morph_count > 0 ? &ctx_ref<render3d_state>(ctx)->morphs[c.morph_first] : nullptr;
      if (lod != nullptr && lod->morph.slot.empty())
        lod = nullptr;
      if (lod != nullptr)
        morph_upload(lod->morph, lod->mesh, w);
      else
        model_morph_upload(*slot, i, w);
    }
    const bool posed = c.bone_count > 0 && ctx_ref<render3d_state>(ctx)->skin_ok &&
                       (lod != nullptr ? lod->bone_vbo != 0
                                       : (usize)i < slot->bone_vbo.size() && slot->bone_vbo[(usize)i] != 0);
    if (mm.double_sided)
      rlDisableBackfaceCulling();
    fn(lod != nullptr ? lod->mesh : model.meshes[i], maps, mm, transform, posed);
    if (mm.double_sided)
      rlEnableBackfaceCulling();
  }
}

// The pose of a draw on `shader`'s bone array (its program is left bound).
void set_bones(const render3d_state &s, Shader shader, i32 loc, const draw3d_cmd &c) {
  rlEnableShader(shader.id);
  rlSetUniformMatrices(loc, &s.bones[c.bone_first], (i32)c.bone_count);
}

void draw_shape(const render3d_state &s, const draw3d_cmd &c, const Matrix &view_proj, bool ortho, vec3 ray_dir,
                bool depth_only);
void draw_instanced_cmd(context &ctx, const draw3d_cmd &c, bool depth_only);
void draw_casters(context &ctx, const Matrix &view_proj, bool ortho, vec3 dir, bool cover = false);

// Depth of every opaque shadow caster into the bound target, seen through the
// current matrices (`view_proj`): along `dir` for the sun (orthographic), from
// the SDF shader's viewPos for a lamp.
void draw_casters(context &ctx, const Matrix &view_proj, bool ortho, vec3 dir, bool cover) {
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  map_set maps = s.maps;
  Material depth{};
  depth.shader = s.depth;
  depth.maps = maps.data();
  Material skinned = depth;
  skinned.shader = s.depth_skinned;
  // Only what can land in this map: a caster outside the light's frustum
  // (another cube face of a lamp, beyond its reach, far from the sun's box)
  // would be clipped away anyway.
  const std::array<vec4, 6> planes = frustum_planes(view_proj);
  for (const draw3d_cmd &c : s.cmds) {
    if (!casts(c) || (c.boxed && box_outside(planes, c.box_center, c.box_extent)))
      continue;
    if (c.world != world3d_none) {
      // Grass blades would leave the ground under them dry in patches.
      if (!(cover && c.world == world3d_grass))
        world3d_draw_depth(ctx, c, view_proj);
      continue;
    }
    if (c.is_shape) {
      if (c.material.cast_shadows && !c.material.unlit)
        draw_shape(s, c, view_proj, ortho, dir, true);
      continue;
    }
    if (c.buffer.id != 0) {
      if (c.material.cast_shadows && !c.material.unlit)
        draw_instanced_cmd(ctx, c, true);
      continue;
    }
    if (c.mesh != nullptr) {
      if (c.material.cast_shadows && !c.material.unlit)
        DrawMesh(*c.mesh, depth, c.transform);
      continue;
    }
    bool bones_set = false;
    for_each_model_mesh(ctx, c,
                        [&](const Mesh &mesh, map_set &, const model_material &mm, const Matrix &transform, bool posed) {
                          if (!mm.surface.cast_shadows)
                            return;
                          if (posed && !bones_set) {
                            set_bones(s, s.depth_skinned, s.depth_skinned_bones, c);
                            bones_set = true;
                          }
                          DrawMesh(mesh, posed ? skinned : depth, transform);
                        });
  }
}

// Depth of every opaque shadow caster, seen from the sun, into the shadow map.
// Returns the sun's view-projection, for the lit shader to look up.
Matrix render_shadow(context &ctx) {
  render3d_state &s = ctx_ref<render3d_state>(ctx);
  const light3d &sun = s.light;
  const f32 range = std::max(sun.shadow_range, 0.1f);
  const vec3 dir = normalize(sun.direction);
  const vec3 forward = normalize(s.camera.target - s.camera.position);
  // An orthographic eye usually sits far back; the shadow goes round what it
  // looks at.
  vec3 focus = s.camera.projection == camera3d_orthographic ? s.camera.target
                                                            : s.camera.position + forward * (range * 0.5f);
  // Snap the box to whole shadow texels across the light, so moving the
  // camera does not make the shadow edges crawl.
  const vec3 helper = std::fabs(dir.y) < 0.99f ? vec3{0.0f, 1.0f, 0.0f} : vec3{0.0f, 0.0f, 1.0f};
  const vec3 right = normalize(cross(dir, helper));
  const vec3 up = cross(right, dir);
  const f32 texel = 2.0f * range / (f32)s.shadow.size;
  const f32 fx = dot(focus, right);
  const f32 fy = dot(focus, up);
  focus -= right * (fx - std::floor(fx / texel) * texel) + up * (fy - std::floor(fy / texel) * texel);

  rlDrawRenderBatchActive();
  rlEnableFramebuffer(s.shadow.fbo);
  rlViewport(0, 0, s.shadow.size, s.shadow.size);
  rlSetFramebufferWidth(s.shadow.size);
  rlSetFramebufferHeight(s.shadow.size);
  rlClearColor(255, 255, 255, 255);
  rlClearScreenBuffers();
  rlMatrixMode(RL_PROJECTION);
  rlLoadIdentity();
  rlOrtho(-range, range, -range, range, 0.1, range * 4.0);
  rlMatrixMode(RL_MODELVIEW);
  rlLoadIdentity();
  rlMultMatrixf(MatrixToFloat(MatrixLookAt(rl3(focus - dir * (range * 2.0f)), rl3(focus), rl3(up))));
  const Matrix light_vp = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
  rlEnableDepthTest();
  draw_casters(ctx, light_vp, true, dir);
  rlDisableFramebuffer();
  return light_vp;
}

constexpr i32 cover_size = 256;
constexpr f32 cover_half = 18.0f; // metres each side of the camera: the rain box and a little more

// Rain and snow fall only where the sky shows: with weather3d::cover_auto,
// the depth of every opaque caster seen straight down, round the camera, so
// drops under a roof are hidden and the ground under it stays dry.
bool cover_wanted(const context &ctx) {
  const world3d_store &w = ctx_ref<world3d_store>(ctx);
  return w.sky_drawn && w.cover_auto && (w.sky.rain > 0.0f || w.sky.snow > 0.0f);
}

Matrix render_cover(context &ctx) {
  render3d_state &s = ctx_ref<render3d_state>(ctx);
  const f32 texel = 2.0f * cover_half / (f32)cover_size;
  // Snapped to whole texels, so roof edges do not shimmer as the camera moves.
  const f32 cx = std::floor(s.camera.position.x / texel) * texel;
  const f32 cz = std::floor(s.camera.position.z / texel) * texel;
  const f32 top = s.camera.position.y + 200.0f;
  rlDrawRenderBatchActive();
  rlEnableFramebuffer(s.cover.fbo);
  rlViewport(0, 0, cover_size, cover_size);
  rlSetFramebufferWidth(cover_size);
  rlSetFramebufferHeight(cover_size);
  rlClearColor(255, 255, 255, 255);
  rlClearScreenBuffers();
  rlMatrixMode(RL_PROJECTION);
  rlLoadIdentity();
  rlOrtho(-cover_half, cover_half, -cover_half, cover_half, 0.1, 600.0);
  rlMatrixMode(RL_MODELVIEW);
  rlLoadIdentity();
  rlMultMatrixf(MatrixToFloat(MatrixLookAt({cx, top, cz}, {cx, top - 1.0f, cz}, {0.0f, 0.0f, -1.0f})));
  const Matrix vp = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
  rlEnableDepthTest();
  draw_casters(ctx, vp, true, {0.0f, -1.0f, 0.0f}, true);
  rlDisableFramebuffer();
  return vp;
}

// Depth of the point and spot lights with `shadows` into the lamp atlas: a
// row per light, a 90 degree face per cube side for a point light, one face
// covering the cone for a spot light. Fills s.lamps; false when none casts.
bool render_lamp_shadows(context &ctx) {
  render3d_state &s = ctx_ref<render3d_state>(ctx);
  lamp_shadows &ls = s.lamps;
  ls.row.fill(-1);
  ls.rows = 0;
  const graphics_quality &q = ctx_ref<quality_state>(ctx)->q;
  if (!q.lamp_shadows)
    return false;
  const i32 count = std::min((i32)s.lights.size(), light3d_max);
  for (i32 i = 0; i < count && ls.rows < light3d_shadow_max; i++)
    if (s.lights[(usize)i].shadows && s.lights[(usize)i].radius > 0.0f)
      ls.row[(usize)i] = ls.rows++;
  if (ls.rows == 0 || s.cmds.empty())
    return false;
  i32 tile = std::clamp(s.light.source_shadow_size, 64, 2048);
  if (q.lamp_shadow_size_max > 0)
    tile = std::max(64, std::min(tile, q.lamp_shadow_size_max));
  // A row per lamp that casts, not light3d_shadow_max of them: one lamp costs
  // a quarter of the atlas. Grown, never shrunk, while the tile size stays, so
  // a lamp that comes and goes does not make a new atlas each time.
  i32 rows = ls.rows;
  if (s.lamp.fbo != 0 && s.lamp.size == tile * lamp_faces)
    rows = std::max(rows, s.lamp.height / tile);
  if (!ensure_shadow(s.lamp, tile * lamp_faces, tile * rows)) {
    ls.row.fill(-1);
    ls.rows = 0;
    return false;
  }

  rlDrawRenderBatchActive();
  rlEnableFramebuffer(s.lamp.fbo);
  rlSetFramebufferWidth(s.lamp.size);
  rlSetFramebufferHeight(s.lamp.height);
  rlViewport(0, 0, s.lamp.size, s.lamp.height);
  rlClearColor(255, 255, 255, 255);
  rlClearScreenBuffers();
  rlEnableDepthTest();
  // Cube faces in the order the lighting shader picks them: +x -x +y -y +z -z.
  static const vec3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  static const vec3 ups[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
  for (i32 i = 0; i < count; i++) {
    const i32 row = ls.row[(usize)i];
    if (row < 0)
      continue;
    const light3d_source &src = s.lights[(usize)i];
    const bool spot = src.kind == light3d_spot;
    const f32 far_plane = std::max(src.radius, 0.1f);
    const f32 near_plane = std::max(far_plane * 0.005f, 0.02f);
    const f32 fov = spot ? clamp(src.cone + 10.0f, 10.0f, 150.0f) : 90.0f;
    const f32 half = std::tan(fov * 0.5f * DEG2RAD);
    ls.params[(usize)row] = {near_plane, far_plane, 2.0f * half / (f32)tile, 1.0f / (f32)tile};
    // Rays from the lamp for the SDF shapes.
    set_vec3(s.sdf, s.sdf_locs.view_pos, src.position);
    const i32 faces = spot ? 1 : lamp_faces;
    for (i32 f = 0; f < faces; f++) {
      vec3 dir = dirs[f];
      vec3 up = ups[f];
      if (spot) {
        dir = normalize(src.direction);
        up = std::fabs(dir.y) < 0.99f ? vec3{0.0f, 1.0f, 0.0f} : vec3{0.0f, 0.0f, 1.0f};
      }
      rlViewport(f * tile, row * tile, tile, tile);
      rlMatrixMode(RL_PROJECTION);
      rlLoadIdentity();
      const f64 edge = (f64)(near_plane * half);
      rlFrustum(-edge, edge, -edge, edge, near_plane, far_plane);
      rlMatrixMode(RL_MODELVIEW);
      rlLoadIdentity();
      rlMultMatrixf(MatrixToFloat(MatrixLookAt(rl3(src.position), rl3(src.position + dir), rl3(up))));
      const Matrix vp = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
      ls.vp[(usize)(row * lamp_faces + f)] = vp;
      draw_casters(ctx, vp, false, {});
    }
  }
  rlDisableFramebuffer();
  return true;
}

void set_pass_uniforms(const render3d_state &s, Shader sh, const render3d_locations &l, bool shadows,
                       const Matrix &light_vp, bool lamps) {
  const light3d &sun = s.light;
  set_vec3(sh, l.light_dir, normalize(sun.direction));
  set_vec3(sh, l.light_color, rgb(sun.color));
  set_vec3(sh, l.ambient, rgb(sun.ambient));
  set_vec3(sh, l.view_pos, s.camera.position);
  set_vec3(sh, l.fog_color, rgb(sun.fog_color));
  set_f32(sh, l.fog_density, std::max(sun.fog_density, 0.0f));
  // Tone mapping works on linear copies of the lights; the sRGB ones above
  // stay for game shaders and the water, which read them as they are.
  const i32 tone = sun.tonemap == tonemap3d_none ? 0 : (i32)sun.tonemap;
  set_i32(sh, l.tone_map, tone);
  set_f32(sh, l.exposure, std::max(sun.exposure, 0.0f));
  const rgb_linear sc = linear_of(sun.color), ac = linear_of(sun.ambient);
  set_vec3(sh, l.light_color_linear, {sc.r, sc.g, sc.b});
  set_vec3(sh, l.ambient_linear, {ac.r, ac.g, ac.b});
  env_set_uniforms(s.env, sun, sh, l.env_on, l.env_sh, l.env_map, l.env_params);

  vec4 pos[light3d_max]{};
  vec4 colors_linear[light3d_max]{};
  vec4 colors[light3d_max]{};
  vec4 spot[light3d_max]{};
  const i32 count = std::min((i32)s.lights.size(), light3d_max);
  for (i32 i = 0; i < count; i++) {
    const light3d_source &src = s.lights[(usize)i];
    const f32 k = src.intensity;
    const f32 half = clamp(src.cone, 0.0f, 179.0f) * 0.5f * DEG2RAD;
    const f32 inner = half * (1.0f - clamp(src.softness, 0.0f, 1.0f));
    const vec3 d = normalize(src.direction);
    pos[i] = {src.position.x, src.position.y, src.position.z, std::max(src.radius, 0.001f)};
    colors[i] = {src.color.r * k, src.color.g * k, src.color.b * k, std::cos(inner)};
    const rgb_linear lc = linear_of(src.color);
    colors_linear[i] = {lc.r * k, lc.g * k, lc.b * k, std::cos(inner)};
    spot[i] = {d.x, d.y, d.z, src.kind == light3d_spot ? std::cos(half) : -2.0f};
  }
  set_i32(sh, l.light_count, count);
  if (count > 0) {
    SetShaderValueV(sh, l.light_pos, pos, SHADER_UNIFORM_VEC4, count);
    SetShaderValueV(sh, l.light_colors, colors, SHADER_UNIFORM_VEC4, count);
    SetShaderValueV(sh, l.light_spot, spot, SHADER_UNIFORM_VEC4, count);
    SetShaderValueV(sh, l.light_colors_linear, colors_linear, SHADER_UNIFORM_VEC4, count);
  }

  const lamp_shadows &ls = s.lamps;
  i32 rows[light3d_max];
  for (i32 i = 0; i < light3d_max; i++)
    rows[i] = lamps && i < count ? ls.row[(usize)i] : -1;
  SetShaderValueV(sh, l.light_shadow, rows, SHADER_UNIFORM_INT, light3d_max);
  set_i32(sh, l.lamp_map, lamp_unit);
  if (lamps) {
    SetShaderValueV(sh, l.lamp_params, ls.params.data(), SHADER_UNIFORM_VEC4, ls.rows);
    const f32 atlas_rows = (f32)std::max(s.lamp.height / std::max(s.lamp.size / lamp_faces, 1), 1);
    SetShaderValue(sh, l.lamp_rows, &atlas_rows, SHADER_UNIFORM_FLOAT);
    rlEnableShader(sh.id);
    rlSetUniformMatrices(l.lamp_vp, ls.vp.data(), ls.rows * lamp_faces);
    rlDisableShader();
  }

  set_i32(sh, l.shadow_on, shadows ? 1 : 0);
  if (shadows) {
    SetShaderValueMatrix(sh, l.light_vp, light_vp);
    const f32 texel_world = 2.0f * std::max(sun.shadow_range, 0.1f) / (f32)s.shadow.size;
    set_vec4(sh, l.shadow_params, {1.0f / (f32)s.shadow.size, sun.shadow_softness, texel_world, 0.0f});
    set_i32(sh, l.shadow_map, shadow_unit);
  }
}

shape_frame frame_of(const shape3d &sh) {
  const vec3 r = sh.rotation * (PI / 180.0f);
  Matrix m = MatrixRotateZ(r.z);
  m = MatrixMultiply(m, MatrixRotateX(r.x));
  m = MatrixMultiply(m, MatrixRotateY(r.y));
  m = MatrixMultiply(m, MatrixTranslate(sh.position.x, sh.position.y, sh.position.z));
  const f32 radius = std::max(sh.radius, 0.0f);
  switch (sh.kind) {
  case shape3d_box: {
    const vec3 half = sh.size * 0.5f;
    const f32 round = clamp(sh.rounding, 0.0f, std::min(half.x, std::min(half.y, half.z)));
    return {m, half, {half.x, half.y, half.z, round}};
  }
  case shape3d_capsule: {
    const f32 half = std::max(sh.height * 0.5f, radius);
    return {m, {radius, half, radius}, {radius, half - radius, 0.0f, 0.0f}};
  }
  case shape3d_cylinder: {
    const f32 half = sh.height * 0.5f;
    const f32 round = clamp(sh.rounding, 0.0f, std::min(radius, half));
    return {m, {radius, half, radius}, {radius, half, 0.0f, round}};
  }
  case shape3d_torus: {
    const f32 tube = std::max(sh.thickness, 0.0f);
    return {m, {radius + tube, tube, radius + tube}, {radius, tube, 0.0f, 0.0f}};
  }
  case shape3d_sphere:
  default:
    return {m, {radius, radius, radius}, {radius, 0.0f, 0.0f, 0.0f}};
  }
}

// Draws one SDF shape through `s.sdf`, whose pass uniforms are set: the
// back faces of its box, so a camera inside it still sees it.
// The box round a blended shape's parts (with room for its fillets), and the
// parts moved into it, for the SDF shader's blendA and blendB.
shape_frame blend_frame(const render3d_state &s, const draw3d_cmd &c, vec4 *a, vec4 *b, f32 *k) {
  vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
  for (u32 i = 0; i < c.blend_count; i++) {
    const sdf_part &p = s.blend_parts[c.blend_first + i];
    for (const auto &[at, r] : {std::pair{p.a, p.ra}, std::pair{p.b, p.rb}}) {
      lo = {std::min(lo.x, at.x - r), std::min(lo.y, at.y - r), std::min(lo.z, at.z - r)};
      hi = {std::max(hi.x, at.x + r), std::max(hi.y, at.y + r), std::max(hi.z, at.z + r)};
    }
  }
  const vec3 mid = (lo + hi) * 0.5f;
  f32 most = 0.0f;
  for (u32 i = 0; i < c.blend_count; i++) {
    const sdf_part &p = s.blend_parts[c.blend_first + i];
    a[i] = {p.a.x - mid.x, p.a.y - mid.y, p.a.z - mid.z, std::max(p.ra, 0.0f)};
    b[i] = {p.b.x - mid.x, p.b.y - mid.y, p.b.z - mid.z, std::max(p.rb, 0.0f)};
    k[i] = p.blend < 0.0f ? c.blend_k : p.blend;
    most = std::max(most, k[i]);
  }
  const vec3 half = (hi - lo) * 0.5f + vec3{most, most, most};
  return {MatrixTranslate(mid.x, mid.y, mid.z), half, {}};
}

void draw_shape(const render3d_state &s, const draw3d_cmd &c, const Matrix &view_proj, bool ortho, vec3 ray_dir,
                bool depth_only) {
  const render3d_locations &l = s.sdf_locs;
  vec4 blend_a[sdf_blend_max], blend_b[sdf_blend_max];
  f32 blend_k[sdf_blend_max];
  const shape_frame f = c.blend_count > 0 ? blend_frame(s, c, blend_a, blend_b, blend_k) : frame_of(c.shape);
  if (c.blend_count > 0) {
    SetShaderValueV(s.sdf, l.blend_a, blend_a, SHADER_UNIFORM_VEC4, (i32)c.blend_count);
    SetShaderValueV(s.sdf, l.blend_b, blend_b, SHADER_UNIFORM_VEC4, (i32)c.blend_count);
    set_i32(s.sdf, l.blend_count, (i32)c.blend_count);
    SetShaderValueV(s.sdf, l.blend_k, blend_k, SHADER_UNIFORM_FLOAT, (i32)c.blend_count);
  }
  // A hair of margin so the surface never touches the box's faces.
  const vec3 b = f.bounds * 1.02f + vec3{1e-3f, 1e-3f, 1e-3f};
  const Matrix box = MatrixMultiply(MatrixScale(b.x * 2.0f, b.y * 2.0f, b.z * 2.0f), f.to_world);
  set_i32(s.sdf, l.shape_kind, c.blend_count > 0 ? 5 : (i32)c.shape.kind);
  set_vec4(s.sdf, l.shape_dims, f.dims);
  set_vec3(s.sdf, l.shape_bounds, b);
  SetShaderValueMatrix(s.sdf, l.shape_to_world, f.to_world);
  SetShaderValueMatrix(s.sdf, l.shape_to_local, MatrixInvert(f.to_world));
  SetShaderValueMatrix(s.sdf, l.mat_vp, view_proj);
  set_i32(s.sdf, l.ray_ortho, ortho ? 1 : 0);
  set_vec3(s.sdf, l.ray_dir, ray_dir);
  set_i32(s.sdf, l.depth_only, depth_only ? 1 : 0);
  if (!depth_only)
    set_draw_uniforms(s.sdf, l, c.fx, c.material, colors::white, false, false);
  map_set maps = s.maps;
  to_raylib(c.color, maps[MATERIAL_MAP_DIFFUSE].color);
  Material material{};
  material.shader = s.sdf;
  material.maps = maps.data();
  rlSetCullFace(RL_CULL_FACE_FRONT);
  DrawMesh(s.cube, material, box);
  rlSetCullFace(RL_CULL_FACE_BACK);
}

// A game shader may have its instance attributes where the mesh keeps one of
// its own (the linker places them): after the draw, point that location back
// at the mesh's buffer as UploadMesh set it, or turn it off.
void restore_mesh_attribute(const Mesh &mesh, i32 loc) {
  struct layout {
    i32 size;
    i32 type;
    bool normalized;
  };
  // Position, texcoord, normal, colour, tangent, texcoord2: the buffers every
  // raylib build gives a mesh (bone buffers exist only with GPU skinning).
  static const layout layouts[6] = {{3, RL_FLOAT, false}, {2, RL_FLOAT, false},       {3, RL_FLOAT, false},
                                    {4, RL_UNSIGNED_BYTE, true}, {4, RL_FLOAT, false}, {2, RL_FLOAT, false}};
  if (loc >= 0 && loc < 6 && mesh.vboId != nullptr && mesh.vboId[loc] != 0) {
    const layout &l = layouts[loc];
    rlEnableVertexBuffer(mesh.vboId[loc]);
    rlSetVertexAttribute((u32)loc, l.size, l.type, l.normalized, 0, 0);
    rlEnableVertexAttribute((u32)loc);
  } else {
    rlDisableVertexAttribute((u32)loc);
  }
}

// One draw_instanced3d call: every mesh of it drawn with one instanced GL call,
// with the instance attributes of `shader` pointed at the buffer from `first`.
// The mesh vertex arrays are shared, so the instance attributes are turned off
// again after each draw. `depth_only` is the shadow pass.
void draw_instanced_cmd(context &ctx, const draw3d_cmd &c, bool depth_only) {
  render3d_state &s = ctx_ref<render3d_state>(ctx);
  instance_slot *slot = instance_slot_of(ctx_ref<instance_store>(ctx), c.buffer);
  if (slot == nullptr || slot->vbo == 0 || c.first >= slot->count)
    return;
  const u32 count = std::min(c.count, slot->count - c.first);
  if (count == 0)
    return;
  Shader call_shader = depth_only ? s.depth_instanced : s.lit_instanced;
  const bool own = !depth_only && game_shader(ctx, c.shader, call_shader);
  const bool call_built_in = !depth_only && !own;

  // The meshes to draw, each with its albedo texture and colour, and the
  // normal map the game set (a file's own normal map is not used here).
  struct part {
    const Mesh *mesh;
    Texture2D texture;
    rgba color;
    const material3d *surface;
    bool double_sided = false;
    Texture2D normal{};
    Texture2D under{}, under_normal{};
    shader_handle shader{}; // the part's own (model_material::shader)
    Texture2D metal_rough{}, occlusion{}; // PBR maps, for a PBR material
    i32 uv2 = 0;                          // usePbrMaps bits of those on the second texture coordinates
  };
  // Kept between calls: no allocation per instanced draw once it has grown.
  thread_local std::vector<part> parts;
  parts.clear();
  Texture2D texture = default_texture();
  if (c.mesh != nullptr && s.translucent_pass) {
    // A built-in shape has no see-through material.
  } else if (c.mesh != nullptr) {
    mesh_texture(ctx, c.material.texture, texture);
    parts.push_back({c.mesh, texture, colors::white, &c.material});
    mesh_texture(ctx, c.material.normal, parts.back().normal);
    if (c.material.pbr) {
      mesh_texture(ctx, c.material.metal_rough, parts.back().metal_rough);
      mesh_texture(ctx, c.material.occlusion, parts.back().occlusion);
    }
  } else if (const model_slot *m = model_slot_of(ctx_ref<model_store>(ctx), c.model)) {
    for (i32 i = 0; i < m->model.meshCount; i++) {
      const i32 index = m->model.meshMaterial[i];
      const model_material &mm = m->materials[(usize)index];
      if ((mm.color.a < 1.0f) != (s.translucent_pass && !depth_only))
        continue;
      Texture2D t = m->model.materials[index].maps[MATERIAL_MAP_DIFFUSE].texture;
      if (!mesh_texture(ctx, mm.albedo, t) && t.id == 0)
        t = default_texture();
      // Every instance shares one shape: the call's weights, or the file's.
      model_morph_upload(*m, i, c.morph_count > 0 ? &s.morphs[c.morph_first] : m->morph_defaults.data());
      parts.push_back({&m->model.meshes[i], t, mm.color, &mm.surface, mm.double_sided});
      parts.back().shader = mm.shader;
      mesh_texture(ctx, mm.normal, parts.back().normal);
      if (mm.surface.pbr) {
        if (!mesh_texture(ctx, mm.metal_rough, parts.back().metal_rough)) {
          parts.back().metal_rough = model_file_pbr_map(*m, index, false);
          parts.back().uv2 |= model_file_pbr_map_uv2(*m, index, false) ? 4 : 0;
        }
        if (!mesh_texture(ctx, mm.occlusion, parts.back().occlusion)) {
          parts.back().occlusion = model_file_pbr_map(*m, index, true);
          parts.back().uv2 |= model_file_pbr_map_uv2(*m, index, true) ? 8 : 0;
        }
      }
    }
  }

  if (parts.empty())
    return;
  for (part &p : parts)
    if (mesh_texture(ctx, p.surface->under, p.under))
      mesh_texture(ctx, p.surface->under_normal, p.under_normal);
  if (!s.translucent_pass)
    ctx_ref<render_stats>(ctx)->instanced_calls++;
  const Matrix mvp = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
  const i32 floats = (i32)slot->floats;
  const i32 stride = (i32)(slot->floats * sizeof(f32));
  for (const part &p : parts) {
    // A part with a shader of its own draws with it, as in draw_model().
    Shader shader = call_shader;
    bool built_in = call_built_in;
    if (!depth_only && game_shader(ctx, p.shader, shader))
      built_in = false;
    if (built_in)
      set_draw_uniforms(shader, s.instanced_locs, c.fx, *p.surface, colors::white, p.normal.id > 0, false,
                        p.under.id > 0, p.under_normal.id > 0, pbr_map_bits(p.metal_rough, p.occlusion) | p.uv2);
    rlEnableShader(shader.id);
    rlSetUniformMatrix(rlGetLocationUniform(shader.id, "mvp"), mvp);
    rlSetUniform(rlGetLocationUniform(shader.id, "instanceFloats"), &floats, RL_SHADER_UNIFORM_INT, 1);
    if (depth_only) {
      // Only the reflector mask (render3d_draw_reflectors) has it.
      const i32 mask_loc = rlGetLocationUniform(shader.id, "maskValue");
      const f32 reflect = clamp(p.surface->reflect, 0.0f, 1.0f);
      if (mask_loc >= 0)
        rlSetUniform(mask_loc, &reflect, RL_SHADER_UNIFORM_FLOAT, 1);
    }
    if (!depth_only) {
      const vec4 color = v4(p.color);
      rlSetUniform(rlGetLocationUniform(shader.id, "colDiffuse"), &color, RL_SHADER_UNIFORM_VEC4, 1);
      const i32 unit = 0;
      rlActiveTextureSlot(0);
      rlEnableTexture(p.texture.id);
      rlSetUniform(rlGetLocationUniform(shader.id, "texture0"), &unit, RL_SHADER_UNIFORM_INT, 1);
      // The normal map and the under layer on the units DrawMesh would give them.
      const struct { u32 id; i32 unit; const char *name; } extra[] = {
          {p.normal.id, 2, "texture2"}, {p.under.id, 3, "underMap"}, {p.under_normal.id, 4, "underNormalMap"},
          {p.metal_rough.id, MATERIAL_MAP_METALNESS, "metalRoughMap"}, {p.occlusion.id, MATERIAL_MAP_HEIGHT, "occlusionMap"}};
      for (const auto &e : extra)
        if (e.id > 0) {
          rlActiveTextureSlot(e.unit);
          rlEnableTexture(e.id);
          rlSetUniform(rlGetLocationUniform(shader.id, e.name), &e.unit, RL_SHADER_UNIFORM_INT, 1);
        }
      rlActiveTextureSlot(0);
    }
    if (!rlEnableVertexArray(p.mesh->vaoId))
      continue;
    // A mesh without colours reads white, as DrawMesh does.
    const i32 color_loc = rlGetLocationAttrib(shader.id, "vertexColor");
    if (color_loc >= 0 && p.mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR] == 0) {
      const f32 white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
      rlSetVertexAttributeDefault(color_loc, white, RL_SHADER_ATTRIB_VEC4, 4);
    }
    rlEnableVertexBuffer(slot->vbo);
    i32 locs[4] = {-1, -1, -1, -1};
    for (i32 i = 0; i < 4; i++) {
      char name[16] = "instance0";
      name[8] = (char)('0' + i);
      locs[i] = rlGetLocationAttrib(shader.id, name);
      if (locs[i] < 0 || i >= floats / 4)
        continue;
      rlSetVertexAttribute((u32)locs[i], 4, RL_FLOAT, false, stride, (i32)(c.first * (u32)stride) + i * 16);
      rlEnableVertexAttribute((u32)locs[i]);
      rlSetVertexAttributeDivisor((u32)locs[i], 1);
    }
    if (p.double_sided)
      rlDisableBackfaceCulling();
    if (p.mesh->indices != nullptr)
      rlDrawVertexArrayElementsInstanced(0, p.mesh->triangleCount * 3, nullptr, (i32)count);
    else
      rlDrawVertexArrayInstanced(0, p.mesh->vertexCount, (i32)count);
    if (p.double_sided)
      rlEnableBackfaceCulling();
    for (i32 i = 0; i < 4; i++) {
      if (locs[i] < 0 || i >= floats / 4)
        continue;
      rlSetVertexAttributeDivisor((u32)locs[i], 0);
      restore_mesh_attribute(*p.mesh, locs[i]);
    }
    rlDisableVertexArray();
    if (!depth_only)
      for (const i32 unit : {2, 3, 4}) {
        rlActiveTextureSlot(unit);
        rlDisableTexture();
      }
    rlActiveTextureSlot(0);
  }
  if (!depth_only)
    rlDisableTexture();
  rlDisableShader();
}

void draw_main(context &ctx, const draw3d_cmd &c, const Matrix &view_proj) {
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  if (c.world != world3d_none) {
    world3d_draw(ctx, c, view_proj);
    return;
  }
  if (s.translucent_pass && (c.is_shape || (c.mesh != nullptr && c.buffer.id == 0)))
    return;
  if (c.culled && (c.is_shape || c.mesh != nullptr))
    return;
  if (c.is_shape) {
    const bool ortho = s.camera.projection == camera3d_orthographic;
    draw_shape(s, c, view_proj, ortho, ortho ? normalize(s.camera.target - s.camera.position) : vec3{}, false);
    return;
  }
  if (c.buffer.id != 0) {
    draw_instanced_cmd(ctx, c, false);
    return;
  }
  if (c.mesh == nullptr) {
    if (c.culled) {
      if (!s.translucent_pass)
        ctx_ref<render_stats>(ctx)->models3d_culled++;
      return;
    }
    if (!s.translucent_pass)
      ctx_ref<render_stats>(ctx)->models3d++;
  }
  Shader custom{};
  const bool own = game_shader(ctx, c.shader, custom);
  if (c.mesh != nullptr) {
    map_set maps = s.maps;
    Texture2D texture{};
    if (mesh_texture(ctx, c.material.texture, texture))
      maps[MATERIAL_MAP_DIFFUSE].texture = texture;
    const bool normal_map = mesh_texture(ctx, c.material.normal, texture);
    if (normal_map)
      maps[MATERIAL_MAP_NORMAL].texture = texture;
    const bool under = mesh_texture(ctx, c.material.under, texture);
    if (under)
      maps[MATERIAL_MAP_ROUGHNESS].texture = texture;
    const bool under_normal = under && mesh_texture(ctx, c.material.under_normal, texture);
    if (under_normal)
      maps[MATERIAL_MAP_OCCLUSION].texture = texture;
    if (c.material.pbr && mesh_texture(ctx, c.material.metal_rough, texture))
      maps[MATERIAL_MAP_METALNESS].texture = texture;
    if (c.material.pbr && mesh_texture(ctx, c.material.occlusion, texture))
      maps[MATERIAL_MAP_HEIGHT].texture = texture;
    to_raylib(c.color, maps[MATERIAL_MAP_DIFFUSE].color);
    if (!own)
      set_draw_uniforms(s.lit, s.locs, c.fx, c.material, colors::white, normal_map, false, under, under_normal,
                        pbr_map_bits(maps[MATERIAL_MAP_METALNESS].texture, maps[MATERIAL_MAP_HEIGHT].texture) |
                            (i32)maps[MATERIAL_MAP_METALNESS].value);
    Material material{};
    material.shader = own ? custom : s.lit;
    material.maps = maps.data();
    DrawMesh(*c.mesh, material, c.transform);
    return;
  }
  bool bones_set = false;
  for_each_model_mesh(ctx, c, [&](const Mesh &mesh, map_set &maps, const model_material &mm, const Matrix &transform,
                                  bool posed) {
    Shader shader = s.lit;
    if (!game_shader(ctx, mm.shader, shader) && own)
      shader = custom;
    // A game shader does not skin: that part stays in the rest pose.
    const bool built_in = shader.id == s.lit.id;
    if (built_in && posed) {
      shader = s.lit_skinned;
      if (!bones_set) {
        set_bones(s, shader, s.skinned_locs.bones, c);
        bones_set = true;
      }
    }
    if (built_in)
      set_draw_uniforms(shader, posed ? s.skinned_locs : s.locs, c.fx, mm.surface, mm.emission_color,
                        maps[MATERIAL_MAP_NORMAL].texture.id > 0, maps[MATERIAL_MAP_EMISSION].texture.id > 0,
                        maps[MATERIAL_MAP_ROUGHNESS].texture.id > 0, maps[MATERIAL_MAP_OCCLUSION].texture.id > 0,
                        pbr_map_bits(maps[MATERIAL_MAP_METALNESS].texture, maps[MATERIAL_MAP_HEIGHT].texture) |
                            (i32)maps[MATERIAL_MAP_METALNESS].value);
    Material material{};
    material.shader = shader;
    material.maps = maps.data();
    DrawMesh(mesh, material, transform);
  });
}
} // namespace

namespace {
Matrix transform_matrix(const transform3d &t) {
  const vec3 r = t.rotation * (PI / 180.0f);
  Matrix m = MatrixScale(t.scale.x, t.scale.y, t.scale.z);
  m = MatrixMultiply(m, MatrixRotateZ(r.z));
  m = MatrixMultiply(m, MatrixRotateX(r.x));
  m = MatrixMultiply(m, MatrixRotateY(r.y));
  return MatrixMultiply(m, MatrixTranslate(t.position.x, t.position.y, t.position.z));
}

// raylib samples glTF clips at this rate (GLTF_FRAMERATE in rmodels.c).
constexpr f32 anim_fps = 60.0f;

// Bone `b` of clip `anim` at `time` seconds, in model space as raylib keeps
// the keyframes; the rest pose for anim < 0. `m` is the slot that owns the
// clips (model_anim_owner).
Transform sample_bone(const model_slot &m, i32 anim, f32 time, bool loop, i32 b) {
  if (anim < 0 || anim >= m.anim_kept)
    return m.model.skeleton.bindPose[b];
  const ModelAnimation &a = m.anims[anim];
  const i32 last = a.keyframeCount - 1;
  if (last <= 0)
    return a.keyframePoses[0][b];
  f32 f = time * anim_fps;
  if (loop) {
    f = std::fmod(f, (f32)last);
    if (f < 0.0f)
      f += (f32)last;
  } else {
    f = clamp(f, 0.0f, (f32)last);
  }
  const i32 i0 = std::min((i32)f, last);
  const i32 i1 = std::min(i0 + 1, last);
  const f32 k = f - (f32)i0;
  const Transform &p0 = a.keyframePoses[i0][b];
  const Transform &p1 = a.keyframePoses[i1][b];
  return Transform{Vector3Lerp(p0.translation, p1.translation, k), QuaternionSlerp(p0.rotation, p1.rotation, k),
                   Vector3Lerp(p0.scale, p1.scale, k)};
}

Matrix pose_matrix(const Transform &t) {
  return MatrixMultiply(MatrixMultiply(MatrixScale(t.scale.x, t.scale.y, t.scale.z), QuaternionToMatrix(t.rotation)),
                        MatrixTranslate(t.translation.x, t.translation.y, t.translation.z));
}

// Appends the bone matrices of `pose` to the pass's pool, as raylib's
// UpdateModelAnimation computes them. False (nothing added) for the rest pose.
// Bone `b` of `pose`, model space, blended as the pose asks; `clips` owns
// the clips (model_anim_owner).
Transform posed_bone(const model_slot &clips, const model_pose &pose, i32 b) {
  const f32 k = clamp(pose.blend, 0.0f, 1.0f);
  Transform t = sample_bone(clips, pose.anim, pose.time, pose.loop, b);
  if (pose.blend_anim >= 0 && k > 0.0f) {
    const Transform u = sample_bone(clips, pose.blend_anim, pose.blend_time, pose.blend_loop, b);
    t = Transform{Vector3Lerp(t.translation, u.translation, k), QuaternionSlerp(t.rotation, u.rotation, k),
                  Vector3Lerp(t.scale, u.scale, k)};
  }
  return t;
}

// A bone the game placed (model_pose::bones), in the space model_bone_pose()
// returns: the model's own transform included.
Matrix bone_matrix(const bone_pose3d &p) {
  Matrix k = MatrixIdentity();
  k.m0 = p.x_axis.x, k.m1 = p.x_axis.y, k.m2 = p.x_axis.z;
  k.m4 = p.y_axis.x, k.m5 = p.y_axis.y, k.m6 = p.y_axis.z;
  k.m8 = p.z_axis.x, k.m9 = p.z_axis.y, k.m10 = p.z_axis.z;
  k.m12 = p.position.x, k.m13 = p.position.y, k.m14 = p.position.z;
  return k;
}

bool pose_bones(const render3d_state &s, const model_slot &m, const model_slot &clips, const model_pose &pose,
                u32 &first, u32 &count) {
  const bool blending = pose.blend_anim >= 0 && pose.blend > 0.0f;
  if (!m.skinned || (pose.anim < 0 && !blending && pose.bones == nullptr))
    return false;
  const i32 bones = m.model.skeleton.boneCount;
  first = (u32)s.bones.size();
  count = (u32)bones;
  if (pose.bones != nullptr) {
    const Matrix unroot = MatrixInvert(m.model.transform);
    for (i32 b = 0; b < bones; b++)
      s.bones.push_back(MatrixMultiply(m.inv_bind[(usize)b], MatrixMultiply(bone_matrix(pose.bones[b]), unroot)));
    return true;
  }
  for (i32 b = 0; b < bones; b++)
    s.bones.push_back(MatrixMultiply(m.inv_bind[(usize)b], pose_matrix(posed_bone(clips, pose, b))));
  return true;
}

// The local box of a built-in primitive mesh (render3d_state::cube...), or
// false for a mesh of the game's own.
bool primitive_box(const render3d_state &s, const Mesh *mesh, vec3 &mid, vec3 &half) {
  if (mesh == &s.cube) {
    mid = {}, half = {0.5f, 0.5f, 0.5f};
  } else if (mesh == &s.sphere || mesh == &s.sphere_low) {
    mid = {}, half = {1.0f, 1.0f, 1.0f};
  } else if (mesh == &s.plane) {
    mid = {}, half = {0.5f, 0.0f, 0.5f};
  } else if (mesh == &s.cylinder || mesh == &s.cylinder_low) {
    mid = {0.0f, 0.5f, 0.0f}, half = {1.0f, 0.5f, 1.0f};
  } else {
    return false;
  }
  return true;
}

// Boxes for the primitives and SDF shapes of the pass (models got theirs when
// recorded, place_model()), and whether each is out of the camera's view.
void cull_boxes(const render3d_state &s) {
  vec4 blend_a[sdf_blend_max], blend_b[sdf_blend_max];
  f32 blend_k[sdf_blend_max];
  for (draw3d_cmd &c : s.cmds) {
    if (c.world != world3d_none || c.buffer.id != 0 || c.boxed)
      continue;
    if (c.is_shape) {
      const shape_frame f = c.blend_count > 0 ? blend_frame(s, c, blend_a, blend_b, blend_k) : frame_of(c.shape);
      // The same margin draw_shape() gives the box it draws.
      place_box(c, f.to_world, {}, f.bounds * 1.02f + vec3{1e-3f, 1e-3f, 1e-3f});
    } else if (vec3 mid, half; c.mesh != nullptr && primitive_box(s, c.mesh, mid, half)) {
      place_box(c, c.transform, mid, half);
    } else {
      continue;
    }
    c.culled = box_outside(s.frustum, c.box_center, c.box_extent);
  }
}

// The model's box as the draw places it, against the pass's frustum (`culled`),
// and how much of the screen's height it covers, for the level of detail.
// `lod_scale` is graphics_quality::lod_scale.
void place_model(const render3d_state &s, const model_slot &m, draw3d_cmd &c, f32 lod_scale) {
  const Matrix t = MatrixMultiply(m.model.transform, c.transform);
  const Vector3 lo = m.bounds.min, hi = m.bounds.max;
  const vec3 mid{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
  const vec3 half{(hi.x - lo.x) * 0.5f, (hi.y - lo.y) * 0.5f, (hi.z - lo.z) * 0.5f};
  place_box(c, t, mid, half);
  // The box is the rest pose's: an arm swung out may leave it.
  if (c.bone_count > 0)
    c.box_extent = c.box_extent +
                   vec3{1.0f, 1.0f, 1.0f} * (std::max({c.box_extent.x, c.box_extent.y, c.box_extent.z}) * 0.3f);
  const vec3 center = c.box_center, extent = c.box_extent;
  if (box_outside(s.frustum, center, extent)) {
    c.culled = true;
    return;
  }
  if (m.lods.empty())
    return;
  const f32 radius = length(extent);
  const f32 dist = length(center - s.camera.position);
  if (dist <= radius)
    return;
  const f32 share = s.camera.projection == camera3d_orthographic
                        ? radius / std::max(s.camera.ortho_height * 0.5f, 1e-4f)
                        : radius / (dist * s.tan_half_fovy);
  f32 below = m.lod_screen * lod_scale;
  while (c.lod < m.lods.size() && share < below) {
    c.lod++;
    below *= 0.5f;
  }
}

void record_model(const context &ctx, model_handle handle, const transform3d &transform, const model_pose *pose,
                  rgba tint, const fx3d &fx, shader_handle shader, const model_recolor *recolors = nullptr,
                  u32 recolor_count = 0, u64 entity = 0) {
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  const model_slot *m = model_slot_of(ctx_ref<model_store>(ctx), handle);
  if (!s.active || m == nullptr)
    return;
  const u64 key = entity != 0 ? mix_key(2, entity) : next_motion_key(s, mix_key(4, handle.id));
  draw3d_cmd c{.is_shape = false,
               .shape = {},
               .mesh = nullptr,
               .model = handle,
               .transform = transform_matrix(transform),
               .color = tint,
               .shader = shader,
               .fx = fx,
               .material = s.material,
               .buffer = {},
               .first = 0,
               .count = 0,
               .bone_first = 0,
               .bone_count = 0};
  if (recolors != nullptr && recolor_count > 0) {
    c.recolor_first = (u32)s.recolors.size();
    c.recolor_count = recolor_count;
    s.recolors.insert(s.recolors.end(), recolors, recolors + recolor_count);
  }
  if (pose != nullptr && s.skin_ok)
    pose_bones(s, *m, model_anim_owner(ctx_ref<model_store>(ctx), *m), *pose, c.bone_first, c.bone_count);
  if (!m->morphs.empty()) {
    thread_local std::vector<f32> weights;
    model_morph_eval(ctx_ref<model_store>(ctx), *m, pose, weights);
    c.morph_first = (u32)s.morphs.size();
    c.morph_count = (u32)weights.size();
    s.morphs.insert(s.morphs.end(), weights.begin(), weights.end());
  }
  c.motion_key = key;
  place_model(s, *m, c, ctx_ref<quality_state>(ctx)->q.lod_scale);
  s.cmds.push_back(c);
}

// The 3D components (njin_3d.h) into the open pass: models, SDF shapes and
// lights, each at its entity's transform3d.
void record_entities(context &ctx) {
  render3d_state &s = ctx_ref<render3d_state>(ctx);
  entt::registry &reg = world(ctx);
  for (auto [e, t, m] : reg.view<const transform3d, const model3d>().each())
    if (m.visible)
      record_model(ctx, m.model, t, &m.pose, m.tint, m.fx, {}, nullptr, 0, (u64)entt::to_integral(e) + 1u);
  for (auto [e, t, r] : reg.view<const transform3d, const shape3d_render>().each()) {
    if (!r.visible)
      continue;
    shape3d shape = r.shape;
    shape.position = t.position;
    shape.rotation = t.rotation;
    s.cmds.push_back(draw3d_cmd{.is_shape = true,
                                .shape = shape,
                                .mesh = nullptr,
                                .model = {},
                                .transform = {},
                                .color = r.color,
                                .shader = {},
                                .fx = r.fx,
                                .material = r.material,
                                .buffer = {},
                                .first = 0,
                                .count = 0,
                                .bone_first = 0,
                                .bone_count = 0});
  }
  for (auto [e, l] : reg.view<const light3d_source>().each()) {
    if ((i32)s.lights.size() >= light3d_max)
      break;
    light3d_source light = l;
    if (const transform3d *t = reg.try_get<transform3d>(e))
      light.position = t->position;
    s.lights.push_back(light);
  }
}

void advance_models(context &ctx) {
  const f32 dt = delta(ctx);
  for (auto [e, m] : world(ctx).view<model3d>().each()) {
    m.pose.time += dt * m.speed;
    m.pose.blend_time += dt * m.speed;
  }
}

void setup(context &ctx) {
  ecs_register(ctx, phase_update, advance_models, "model3d_anim");
  ecs_register(ctx, phase_update, post3d_update, "decal3d_age");
}
} // namespace

mod_desc render3d_module() { return mod_desc{.name = "njin.render3d", .setup = setup}; }

render3d_state::~render3d_state() {
  env_free(env);
  free_shadow(shadow);
  free_shadow(lamp);
  free_shadow(cover);
  for (Shader *sh : {&mask, &mask_instanced, &mask_skinned, &velocity, &velocity_skinned})
    if (IsShaderValid(*sh))
      UnloadShader(*sh);
  if (prev_bones_tex != 0)
    rlUnloadTexture(prev_bones_tex);
  if (!ready)
    return;
  if (IsShaderValid(lit_skinned))
    UnloadShader(lit_skinned);
  if (IsShaderValid(depth_skinned))
    UnloadShader(depth_skinned);
  UnloadMesh(cube);
  UnloadMesh(sphere);
  UnloadMesh(plane);
  UnloadMesh(cylinder);
  UnloadMesh(sphere_low);
  UnloadMesh(cylinder_low);
  UnloadShader(lit);
  UnloadShader(sdf);
  UnloadShader(lit_instanced);
  UnloadShader(depth_instanced);
  UnloadShader(depth);
}

namespace {
bool can_begin(context &ctx) {
  render3d_state &s = ctx_ref<render3d_state>(ctx);
  if (s.active) {
    NJIN_WARN_LIMITED("3d: begin_3d called twice without end_3d, ignored");
    return false;
  }
  if (ctx_ref<view_state>(ctx)->world_depth == 0) {
    NJIN_WARN_LIMITED("3d: begin_3d outside phase_render, ignored");
    return false;
  }
  return ensure_ready(s);
}

void start_pass(context &ctx, const camera3d &camera) {
  render3d_state &s = ctx_ref<render3d_state>(ctx);
  // The world pass's projection comes back at end_3d.
  rlMatrixMode(RL_PROJECTION);
  rlPushMatrix();
  // A pass into a render texture is another eye: the screen's shake is not its.
  s.camera = s.target_fbo != 0 ? camera : shaken(ctx, camera);
  s.jitter = s.target_fbo != 0 ? vec2{} : post3d_taa_jitter(ctx);
  load_camera(ctx, s.camera);
  set_frustum(s);
  s.cmds.clear();
  s.bones.clear();
  s.recolors.clear();
  s.morphs.clear();
  s.blend_parts.clear();
  s.lights.clear();
  s.fx = fx3d{};
  s.material = material3d{};
  s.motion_seen.clear();
  s.next_motion_id = 0;
  s.entities = camera.entities;
  s.active = true;
}
} // namespace

void begin_3d(context &ctx, const camera3d &camera) {
  if (!can_begin(ctx))
    return;
  rlDrawRenderBatchActive();
  ctx_ref<render3d_state>(ctx)->target_fbo = 0;
  audio3d_note_camera(ctx_ref<audio_store>(ctx), camera.position, camera.target, camera.up); // unshaken
  start_pass(ctx, camera);
}

void begin_3d(context &ctx, const camera3d &camera, render_texture_handle target, rgba clear) {
  const render_texture_slot *slot = render_texture_slot_of(ctx_ref<render_texture_store>(ctx), target);
  if (slot == nullptr) {
    NJIN_WARN_LIMITED("3d: begin_3d into an invalid render texture, ignored");
    return;
  }
  if (!can_begin(ctx))
    return;
  render3d_state &s = ctx_ref<render3d_state>(ctx);
  rlDrawRenderBatchActive();
  s.target_fbo = slot->target.id;
  s.target_size = {(f32)slot->target.texture.width, (f32)slot->target.texture.height};
  bind_pass_target(ctx);
  rlClearColor((u8)(clear.r * 255.0f), (u8)(clear.g * 255.0f), (u8)(clear.b * 255.0f), (u8)(clear.a * 255.0f));
  rlClearScreenBuffers();
  start_pass(ctx, camera);
}

void end_3d(context &ctx) {
  render3d_state &s = ctx_ref<render3d_state>(ctx);
  if (!s.active)
    return;
  rlDrawRenderBatchActive();
  const bool offscreen = s.target_fbo != 0;
  if (s.entities)
    record_entities(ctx);
  cull_boxes(s);
  if (ctx_ref<debug_state>(ctx)->running && !offscreen)
    render3d_capture_debug(ctx);
  // light3d::environment's maps, made when it changed (into their own framebuffers).
  const bool env_drew = env_update(ctx, s.env, s.light);

  bool shadows = false;
  Matrix light_vp = MatrixIdentity();
  // graphics_quality caps what light3d_set() asked for, never raises it.
  const graphics_quality &q = ctx_ref<quality_state>(ctx)->q;
  i32 sun_size = std::clamp(s.light.shadow_size, 256, 8192);
  if (q.shadow_size_max > 0)
    sun_size = std::max(256, std::min(sun_size, q.shadow_size_max));
  if (s.light.shadows && q.shadows && !s.cmds.empty() && ensure_shadow(s.shadow, sun_size, sun_size)) {
    light_vp = render_shadow(ctx);
    shadows = true;
  }
  const bool lamps = render_lamp_shadows(ctx);
  // The cover map is drawn again every few passes (things move under it) or
  // when the camera has gone 2 m from where it was drawn; the rain box reaches
  // 16 m round the camera and the map 18 m, so in between it still covers it.
  bool cover_now = false;
  s.cover_drawn = false;
  if (!offscreen && cover_wanted(ctx) && ensure_shadow(s.cover, cover_size, cover_size)) {
    const vec2 at{s.camera.position.x, s.camera.position.z};
    const vec2 moved{at.x - s.cover_at.x, at.y - s.cover_at.y};
    if (s.cover_age < 0 || s.cover_age >= 8 || moved.x * moved.x + moved.y * moved.y > 4.0f) {
      s.cover_vp = render_cover(ctx);
      s.cover_at = at;
      s.cover_age = 0;
      cover_now = true;
    } else {
      s.cover_age++;
    }
    s.cover_drawn = true;
  } else if (!offscreen) {
    s.cover_age = -1;
  }
  if (shadows || lamps || cover_now || env_drew) {
    bind_pass_target(ctx);
    load_camera(ctx, s.camera);
  }

  rlEnableDepthTest();
  // Only the world target's depth feeds post_fx (depth of field).
  if (!offscreen) {
    const vec2 planes = render3d_depth_planes(s.camera);
    s.depth_near = planes.x;
    s.depth_far = planes.y;
    s.depth_inv_view_proj = MatrixInvert(MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection()));
    s.depth_drawn = true;
  }
  set_pass_uniforms(s, s.lit, s.locs, shadows, light_vp, lamps);
  set_pass_uniforms(s, s.sdf, s.sdf_locs, shadows, light_vp, lamps);
  set_pass_uniforms(s, s.lit_instanced, s.instanced_locs, shadows, light_vp, lamps);
  if (s.skin_ok)
    set_pass_uniforms(s, s.lit_skinned, s.skinned_locs, shadows, light_vp, lamps);
  world3d_pass_uniforms(ctx, shadows, light_vp, lamps);
  const Matrix view_proj = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
  if (shadows) {
    rlActiveTextureSlot(shadow_unit);
    rlEnableTexture(s.shadow.depth);
  }
  if (lamps) {
    rlActiveTextureSlot(lamp_unit);
    rlEnableTexture(s.lamp.depth);
  }
  env_bind(s.env);
  rlActiveTextureSlot(0);
  // The environment as the sky, behind everything of the pass.
  if (s.light.environment_visible)
    env_draw_sky(s.env, s.light, s.camera, view_proj);
  for (const draw3d_cmd &c : s.cmds)
    draw_main(ctx, c, view_proj);
  // Decals, SSAO and SSR work on the opaque image, before glass and water.
  const bool post3d = !offscreen && post3d_wanted(ctx);
  if (post3d) {
    post3d_after_opaque(ctx);
    bind_pass_target(ctx);
    load_camera(ctx, s.camera);
    rlEnableDepthTest();
    if (shadows) {
      rlActiveTextureSlot(shadow_unit);
      rlEnableTexture(s.shadow.depth);
    }
    if (lamps) {
      rlActiveTextureSlot(lamp_unit);
      rlEnableTexture(s.lamp.depth);
    }
    env_bind(s.env);
    rlActiveTextureSlot(0);
  }
  // Glass after everything it shows, tested against the depth but not
  // writing it, so panes behind panes still show.
  s.translucent_pass = true;
  rlDrawRenderBatchActive();
  rlDisableDepthMask();
  for (const draw3d_cmd &c : s.cmds)
    draw_main(ctx, c, view_proj);
  rlDrawRenderBatchActive();
  rlEnableDepthMask();
  s.translucent_pass = false;
  for (i32 unit : {shadow_unit, lamp_unit, env_unit}) {
    rlActiveTextureSlot(unit);
    rlDisableTexture();
  }
  rlActiveTextureSlot(0);
  particles3d_draw(ctx, s.camera);
  if (post3d) {
    post3d_after_pass(ctx);
    // Gizmos go over the resolved image, unshifted.
    s.jitter = {};
    bind_pass_target(ctx);
    load_camera(ctx, s.camera);
    rlEnableDepthTest();
  }
  s.jitter = {};
  if (!offscreen)
    gizmo_draw_3d(ctx, s.camera);

  s.cmds.clear();
  s.bones.clear();
  s.recolors.clear();
  s.morphs.clear();
  s.blend_parts.clear();
  s.lights.clear();
  s.active = false;
  world3d_pass_end(ctx);
  rlDrawRenderBatchActive();
  if (offscreen) {
    s.target_fbo = 0;
    world_target_rebind(ctx);
  }
  rlMatrixMode(RL_PROJECTION);
  rlPopMatrix();
  // Back to the world pass exactly as begin_world_space left it.
  rlMatrixMode(RL_MODELVIEW);
  rlLoadIdentity();
  rlMultMatrixf(MatrixToFloat(GetCameraMatrix2D(ctx_ref<camera_post>(ctx)->world_camera)));
  rlDisableDepthTest();
}

namespace {
vec3 translation(const Matrix &m) { return {m.m12, m.m13, m.m14}; }

// Where a model's bounding box centre ends up under `transform`.
vec3 model_center(const model_slot &m, const Matrix &transform) {
  const Vector3 lo = m.bounds.min;
  const Vector3 hi = m.bounds.max;
  const Vector3 c{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
  const Vector3 w = Vector3Transform(c, MatrixMultiply(m.model.transform, transform));
  return {w.x, w.y, w.z};
}

i32 mesh_kind(const render3d_state &s, const Mesh *mesh) {
  return mesh == &s.sphere || mesh == &s.sphere_low       ? debug3d_sphere
         : mesh == &s.plane                                 ? debug3d_plane
         : mesh == &s.cylinder || mesh == &s.cylinder_low   ? debug3d_cylinder
                                                            : debug3d_cube;
}

// The instances of a draw_instanced3d call, at the position instance0 gives
// them (njin_3d.h). The upload is kept on the CPU only while debugging.
void capture_instances(const context &ctx, const draw3d_cmd &c, debug3d_frame &f, usize max_items) {
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  const instance_slot *slot = instance_slot_of(const_cast<instance_store &>(*ctx_ref<instance_store>(ctx)), c.buffer);
  if (slot == nullptr || slot->cpu.empty())
    return;
  const u32 floats = slot->floats;
  const u32 end = std::min(c.first + c.count, (u32)(slot->cpu.size() / floats));
  const i32 kind = c.mesh == nullptr ? debug3d_model : mesh_kind(s, c.mesh);
  for (u32 i = c.first; i < end && f.items.size() < max_items; i++) {
    const f32 *v = &slot->cpu[(usize)i * floats];
    const rgba color = floats >= 8 ? rgba{v[4], v[5], v[6], v[7]} : colors::white;
    f.items.push_back({kind, {v[0], v[1], v[2]}, color});
  }
}
} // namespace

void render3d_capture_debug(context &ctx) {
  // A frame with thousands of draws is cut: the inspector only needs to see
  // where things are.
  constexpr usize max_items = 8192;
  render3d_state &s = ctx_ref<render3d_state>(ctx);
  debug3d_frame &f = s.debug;
  f.time = ctx_ref<time_state>(ctx).elapsed;
  f.camera = s.camera;
  f.sun = normalize(s.light.direction);
  f.lights = s.lights;
  f.items.clear();
  f.instanced = 0;
  for (const draw3d_cmd &c : s.cmds) {
    if (f.items.size() >= max_items)
      break;
    if (c.buffer.id != 0) {
      f.instanced += c.count;
      capture_instances(ctx, c, f, max_items);
    } else if (c.is_shape && c.blend_count > 0) {
      // A blended shape shows as a capsule at its first part.
      f.items.push_back({debug3d_shape + (i32)shape3d_capsule, s.blend_parts[c.blend_first].a, c.color});
    } else if (c.is_shape) {
      f.items.push_back({debug3d_shape + (i32)c.shape.kind, c.shape.position, c.color});
    } else if (c.mesh != nullptr) {
      f.items.push_back({mesh_kind(s, c.mesh), translation(c.transform), c.color});
    } else if (const model_slot *m = model_slot_of(ctx_ref<model_store>(ctx), c.model)) {
      f.items.push_back({debug3d_model, model_center(*m, c.transform), c.color});
    }
  }
}

shape_frame shape3d_frame(const shape3d &shape) { return frame_of(shape); }

const char *render3d_lighting_glsl() { return lighting_glsl; }

render3d_locations render3d_find_locations(Shader shader) { return find_locations(shader); }

void render3d_set_pass_uniforms(const render3d_state &s, Shader sh, const render3d_locations &l, bool shadows,
                                const Matrix &light_vp, bool lamps) {
  set_pass_uniforms(s, sh, l, shadows, light_vp, lamps);
}

void render3d_set_draw_uniforms(Shader sh, const render3d_locations &l, const material3d &m) {
  set_draw_uniforms(sh, l, fx3d{}, m, colors::white, false, false);
}

void render3d_record_world(const context &ctx, u8 kind, u32 id) {
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  if (!s.active)
    return;
  draw3d_cmd c;
  c.world = kind;
  c.world_id = id;
  c.color = colors::white;
  c.material = s.material;
  s.cmds.push_back(c);
  s.next_motion_id = 0;
}

bool render3d_box_visible(const render3d_state &s, vec3 lo, vec3 hi) {
  for (const vec4 &p : s.frustum) {
    // The corner furthest along the plane's normal.
    const vec3 far{p.x >= 0.0f ? hi.x : lo.x, p.y >= 0.0f ? hi.y : lo.y, p.z >= 0.0f ? hi.z : lo.z};
    if (p.x * far.x + p.y * far.y + p.z * far.z + p.w < 0.0f)
      return false;
  }
  return true;
}

namespace {
bool ensure_mask(render3d_state &s) {
  if (s.mask_ready || s.mask_failed)
    return s.mask_ready;
  const std::string head = "#version 330\n";
  const std::string instanced_vs = head + instanced_glsl + depth_instanced_vs_main;
  s.mask = LoadShaderFromMemory(depth_vs, mask_fs);
  s.mask_instanced = LoadShaderFromMemory(instanced_vs.c_str(), mask_fs);
  if (!IsShaderValid(s.mask) || !IsShaderValid(s.mask_instanced)) {
    NJIN_WARN("3d: the reflection mask shaders failed to compile, post3d::ssr is off");
    s.mask_failed = true;
    return false;
  }
  if (s.skin_ok) {
    const std::string skinned_vs = head + skin_glsl + depth_skinned_vs_main;
    s.mask_skinned = LoadShaderFromMemory(skinned_vs.c_str(), mask_fs);
    if (IsShaderValid(s.mask_skinned))
      s.mask_skinned_bones = GetShaderLocation(s.mask_skinned, "boneMatrices");
  }
  s.mask_ready = true;
  return true;
}

void set_mask_uniforms(Shader sh, vec2 size, vec2 planes) {
  if (!IsShaderValid(sh))
    return;
  set_i32(sh, GetShaderLocation(sh, "sceneDepth"), mask_depth_unit);
  set_vec2(sh, GetShaderLocation(sh, "sceneSize"), size);
  set_vec2(sh, GetShaderLocation(sh, "planes"), planes);
}

void set_mask_value(Shader sh, f32 v) { set_f32(sh, GetShaderLocation(sh, "maskValue"), clamp(v, 0.0f, 1.0f)); }

bool model_reflects(const context &ctx, model_handle handle) {
  const model_slot *m = model_slot_of(ctx_ref<model_store>(ctx), handle);
  if (m == nullptr)
    return false;
  for (const model_material &mm : m->materials)
    if (mm.surface.reflect > 0.0f)
      return true;
  return false;
}
} // namespace

bool render3d_draw_reflectors(context &ctx, u32 depth, vec2 size, vec2 planes) {
  render3d_state &s = ctx_ref<render3d_state>(ctx);
  if (!ensure_mask(s))
    return false;
  for (Shader sh : {s.mask, s.mask_instanced, s.mask_skinned})
    set_mask_uniforms(sh, size, planes);
  rlActiveTextureSlot(mask_depth_unit);
  rlEnableTexture(depth);
  rlActiveTextureSlot(0);
  // draw_instanced_cmd's depth pass draws through depth_instanced.
  const Shader depth_instanced = s.depth_instanced;
  s.depth_instanced = s.mask_instanced;
  map_set maps = s.maps;
  Material mesh_mat{};
  mesh_mat.shader = s.mask;
  mesh_mat.maps = maps.data();
  Material skinned_mat = mesh_mat;
  skinned_mat.shader = s.mask_skinned;
  const bool skinned_ok = IsShaderValid(s.mask_skinned);
  for (const draw3d_cmd &c : s.cmds) {
    if (c.world != world3d_none || c.is_shape || c.color.a < 1.0f || c.fx.dissolve > 0.0f)
      continue;
    if (c.buffer.id != 0) {
      if (c.mesh != nullptr ? c.material.reflect > 0.0f : model_reflects(ctx, c.model))
        draw_instanced_cmd(ctx, c, true);
      continue;
    }
    if (c.mesh != nullptr) {
      if (c.material.reflect > 0.0f) {
        set_mask_value(s.mask, c.material.reflect);
        DrawMesh(*c.mesh, mesh_mat, c.transform);
      }
      continue;
    }
    if (c.culled || !model_reflects(ctx, c.model))
      continue;
    bool bones_set = false;
    for_each_model_mesh(ctx, c, [&](const Mesh &mesh, map_set &, const model_material &mm, const Matrix &transform,
                                    bool posed) {
      const bool skin = posed && skinned_ok;
      const Shader sh = skin ? s.mask_skinned : s.mask;
      if (skin && !bones_set) {
        set_bones(s, sh, s.mask_skinned_bones, c);
        bones_set = true;
      }
      // Every part is drawn, so a part that does not reflect clears what is behind it.
      set_mask_value(sh, mm.surface.reflect);
      DrawMesh(mesh, skin ? skinned_mat : mesh_mat, transform);
    });
  }
  rlDrawRenderBatchActive();
  s.depth_instanced = depth_instanced;
  rlActiveTextureSlot(mask_depth_unit);
  rlDisableTexture();
  rlActiveTextureSlot(0);
  return true;
}

namespace {
bool ensure_velocity(render3d_state &s) {
  if (s.velocity_ready || s.velocity_failed)
    return s.velocity_ready;
  s.velocity = LoadShaderFromMemory(velocity_vs, velocity_fs);
  if (!IsShaderValid(s.velocity)) {
    NJIN_WARN("3d: the motion vector shaders failed to compile; TAA and motion blur follow the camera only");
    s.velocity_failed = true;
    return false;
  }
  if (s.skin_ok) {
    const std::string vs = std::string("#version 330\n") + skin_glsl + velocity_skinned_vs_main;
    s.velocity_skinned = LoadShaderFromMemory(vs.c_str(), velocity_fs);
    if (IsShaderValid(s.velocity_skinned)) {
      s.prev_bones_tex = rlLoadTexture(nullptr, skin_max_bones * 4, 1, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1);
      rlTextureParameters(s.prev_bones_tex, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
      rlTextureParameters(s.prev_bones_tex, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);
    }
  }
  s.velocity_ready = true;
  return true;
}

// The bone matrices of a draw's last motion pass into prevBones; false when it
// had none or another number of bones.
bool upload_prev_bones(const render3d_state &s, const motion_prev *last, u32 count) {
  if (last == nullptr || s.prev_bones_tex == 0 || last->bone_count != count || count == 0 ||
      count > (u32)skin_max_bones)
    return false;
  thread_local std::vector<f32> texels;
  texels.resize((usize)count * 16);
  for (u32 b = 0; b < count; b++) {
    const float16 m = MatrixToFloatV(s.motion_bones[last->bone_first + b]);
    std::copy(m.v, m.v + 16, texels.begin() + (std::ptrdiff_t)b * 16);
  }
  rlUpdateTexture(s.prev_bones_tex, 0, 0, (i32)count * 4, 1, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, texels.data());
  return true;
}

// Draws that get motion vectors: opaque meshes and models, one transform each.
bool has_motion(const draw3d_cmd &c) {
  return c.motion_key != 0 && c.world == world3d_none && !c.is_shape && c.buffer.id == 0 && c.color.a >= 1.0f &&
         c.fx.dissolve <= 0.0f;
}
} // namespace

bool render3d_draw_velocity(context &ctx, u32 depth, vec2 size, vec2 planes, const Matrix &cur_vp,
                            const Matrix &prev_vp, bool has_last) {
  render3d_state &s = ctx_ref<render3d_state>(ctx);
  if (!ensure_velocity(s))
    return false;
  const bool skinned_ok = IsShaderValid(s.velocity_skinned);
  for (Shader sh : {s.velocity, s.velocity_skinned}) {
    if (!IsShaderValid(sh))
      continue;
    set_mask_uniforms(sh, size, planes);
    SetShaderValueMatrix(sh, GetShaderLocation(sh, "curVP"), cur_vp);
    SetShaderValueMatrix(sh, GetShaderLocation(sh, "prevVP"), prev_vp);
  }
  const i32 mesh_prev = GetShaderLocation(s.velocity, "prevModel");
  const i32 skin_prev = skinned_ok ? GetShaderLocation(s.velocity_skinned, "prevModel") : -1;
  const i32 skin_count = skinned_ok ? GetShaderLocation(s.velocity_skinned, "prevBoneCount") : -1;
  const i32 skin_bones = skinned_ok ? GetShaderLocation(s.velocity_skinned, "boneMatrices") : -1;
  if (skinned_ok)
    set_i32(s.velocity_skinned, GetShaderLocation(s.velocity_skinned, "prevBones"), prev_bones_unit);
  rlActiveTextureSlot(mask_depth_unit);
  rlEnableTexture(depth);
  rlActiveTextureSlot(prev_bones_unit);
  rlEnableTexture(s.prev_bones_tex);
  rlActiveTextureSlot(0);
  map_set maps = s.maps;
  Material mesh_mat{};
  mesh_mat.shader = s.velocity;
  mesh_mat.maps = maps.data();
  Material skinned_mat = mesh_mat;
  skinned_mat.shader = s.velocity_skinned;
  for (const draw3d_cmd &c : s.cmds) {
    if (!has_motion(c))
      continue;
    const motion_prev *last = nullptr;
    if (has_last) {
      const auto it = s.motion_last.find(c.motion_key);
      if (it != s.motion_last.end())
        last = &it->second;
    }
    const Matrix prev_transform = last != nullptr ? last->transform : c.transform;
    if (c.mesh != nullptr) {
      SetShaderValueMatrix(s.velocity, mesh_prev, prev_transform);
      DrawMesh(*c.mesh, mesh_mat, c.transform);
      continue;
    }
    const model_slot *slot = model_slot_of(ctx_ref<model_store>(ctx), c.model);
    if (c.culled || slot == nullptr)
      continue;
    const Matrix prev_mesh = MatrixMultiply(slot->model.transform, prev_transform);
    bool bones_set = false;
    for_each_model_mesh(ctx, c, [&](const Mesh &mesh, map_set &, const model_material &, const Matrix &transform,
                                    bool posed) {
      const bool skin = posed && skinned_ok;
      if (skin && !bones_set) {
        set_bones(s, s.velocity_skinned, skin_bones, c);
        set_i32(s.velocity_skinned, skin_count, upload_prev_bones(s, last, c.bone_count) ? (i32)c.bone_count : 0);
        bones_set = true;
      }
      const Shader sh = skin ? s.velocity_skinned : s.velocity;
      SetShaderValueMatrix(sh, skin ? skin_prev : mesh_prev, prev_mesh);
      DrawMesh(mesh, skin ? skinned_mat : mesh_mat, transform);
    });
  }
  rlDrawRenderBatchActive();
  rlActiveTextureSlot(prev_bones_unit);
  rlDisableTexture();
  rlActiveTextureSlot(mask_depth_unit);
  rlDisableTexture();
  rlActiveTextureSlot(0);
  return true;
}

void render3d_motion_commit(context &ctx) {
  render3d_state &s = ctx_ref<render3d_state>(ctx);
  s.motion_last.clear();
  s.motion_bones.clear();
  for (const draw3d_cmd &c : s.cmds) {
    if (!has_motion(c))
      continue;
    motion_prev p{.transform = c.transform, .bone_first = (u32)s.motion_bones.size(), .bone_count = c.bone_count};
    s.motion_bones.insert(s.motion_bones.end(), s.bones.begin() + c.bone_first,
                          s.bones.begin() + c.bone_first + c.bone_count);
    s.motion_last[c.motion_key] = p;
  }
}

void draw3d_motion_id(const context &ctx, u64 id) {
  const render3d_state *s = open_pass(ctx);
  if (s != nullptr)
    s->next_motion_id = id;
}

void render3d_close(context &ctx) {
  if (!ctx_ref<render3d_state>(ctx)->active)
    return;
  NJIN_WARN_LIMITED("3d: begin_3d without end_3d, closed at the end of phase_render");
  end_3d(ctx);
}

void light3d_set(context &ctx, const light3d &light) { ctx_ref<render3d_state>(ctx)->light = light; }

light3d light3d_get(const context &ctx) { return ctx_ref<render3d_state>(ctx)->light; }

void light3d_add(context &ctx, const light3d_source &light) {
  render3d_state &s = ctx_ref<render3d_state>(ctx);
  if (!s.active)
    return;
  if ((i32)s.lights.size() >= light3d_max) {
    NJIN_WARN_LIMITED("3d: more than %d lights in one pass, the rest are ignored", light3d_max);
    return;
  }
  s.lights.push_back(light);
}

void material3d_set(context &ctx, const material3d &material) { ctx_ref<render3d_state>(ctx)->material = material; }

void fx3d_set(context &ctx, const fx3d &fx) { ctx_ref<render3d_state>(ctx)->fx = fx; }

void draw_cube3d(const context &ctx, vec3 center, vec3 size, rgba color) {
  if (const render3d_state *s = open_pass(ctx))
    record(ctx, &s->cube, {}, scale_then_move(size, center), color);
}

void draw_sphere3d(const context &ctx, vec3 center, f32 radius, rgba color) {
  if (const render3d_state *s = open_pass(ctx))
    record(ctx, &s->sphere, {}, scale_then_move({radius, radius, radius}, center), color);
}

void draw_plane3d(const context &ctx, vec3 center, vec2 size, rgba color) {
  if (const render3d_state *s = open_pass(ctx))
    record(ctx, &s->plane, {}, scale_then_move({size.x, 1.0f, size.y}, center), color);
}

void draw_cylinder3d(const context &ctx, vec3 from, vec3 to, f32 radius, rgba color) {
  const render3d_state *s = open_pass(ctx);
  Matrix m{};
  if (s != nullptr && cylinder_transform(from, to, radius, m))
    record(ctx, &s->cylinder, {}, m, color);
}

void draw_capsule3d(const context &ctx, vec3 from, vec3 to, f32 radius, rgba color) {
  if (open_pass(ctx) == nullptr)
    return;
  draw_cylinder3d(ctx, from, to, radius, color);
  draw_sphere3d(ctx, from, radius, color);
  draw_sphere3d(ctx, to, radius, color);
}

namespace {
void record_instanced(const context &ctx, const Mesh *mesh, model_handle model, instance_buffer_handle buffer,
                      u32 first, u32 count, shader_handle shader) {
  const render3d_state &s = ctx_ref<render3d_state>(ctx);
  s.cmds.push_back(draw3d_cmd{.is_shape = false,
                              .shape = {},
                              .mesh = mesh,
                              .model = model,
                              .transform = {},
                              .color = colors::white,
                              .shader = shader,
                              .fx = s.fx,
                              .material = s.material,
                              .buffer = buffer,
                              .first = first,
                              .count = count,
                              .bone_first = 0,
                              .bone_count = 0});
  s.next_motion_id = 0; // instances get no motion of their own
}
} // namespace

void draw_instanced3d(const context &ctx, mesh3d_kind mesh, instance_buffer_handle buffer, u32 first, u32 count,
                      shader_handle shader) {
  const render3d_state *s = open_pass(ctx);
  if (s == nullptr || buffer.id == 0)
    return;
  const Mesh *meshes[] = {&s->cube, &s->sphere, &s->plane, &s->cylinder, &s->sphere_low, &s->cylinder_low};
  constexpr i32 last = static_cast<i32>(std::size(meshes)) - 1;
  record_instanced(ctx, meshes[std::clamp((i32)mesh, 0, last)], {}, buffer, first, count, shader);
}

void draw_instanced3d(const context &ctx, model_handle model, instance_buffer_handle buffer, u32 first, u32 count,
                      shader_handle shader) {
  if (open_pass(ctx) == nullptr || buffer.id == 0 || model_slot_of(ctx_ref<model_store>(ctx), model) == nullptr)
    return;
  record_instanced(ctx, nullptr, model, buffer, first, count, shader);
}

void draw_instanced3d(const context &ctx, model_handle model, instance_buffer_handle buffer, u32 first, u32 count,
                      const model_pose &pose, shader_handle shader) {
  const render3d_state *s = open_pass(ctx);
  const model_slot *m = model_slot_of(ctx_ref<model_store>(ctx), model);
  if (s == nullptr || buffer.id == 0 || m == nullptr)
    return;
  record_instanced(ctx, nullptr, model, buffer, first, count, shader);
  if (m->morphs.empty())
    return;
  thread_local std::vector<f32> weights;
  model_morph_eval(ctx_ref<model_store>(ctx), *m, &pose, weights);
  draw3d_cmd &c = s->cmds.back();
  c.morph_first = (u32)s->morphs.size();
  c.morph_count = (u32)weights.size();
  s->morphs.insert(s->morphs.end(), weights.begin(), weights.end());
}

void draw_sdf_blend(const context &ctx, const sdf_part *parts, u32 count, f32 blend, rgba color) {
  const render3d_state *s = open_pass(ctx);
  if (s == nullptr || parts == nullptr || count == 0)
    return;
  if (count > sdf_blend_max) {
    NJIN_WARN_LIMITED("draw_sdf_blend: %u parts, at most %u: the rest are dropped", count, sdf_blend_max);
    count = sdf_blend_max;
  }
  const u32 first = (u32)s->blend_parts.size();
  s->blend_parts.insert(s->blend_parts.end(), parts, parts + count);
  s->cmds.push_back(draw3d_cmd{.is_shape = true,
                               .shape = {},
                               .mesh = nullptr,
                               .model = {},
                               .transform = {},
                               .color = color,
                               .shader = {},
                               .fx = s->fx,
                               .material = s->material,
                               .buffer = {},
                               .first = 0,
                               .count = 0,
                               .bone_first = 0,
                               .bone_count = 0,
                               .blend_first = first,
                               .blend_count = count,
                               .blend_k = std::max(blend, 0.0f)});
  s->next_motion_id = 0; // SDF shapes get no motion of their own
}

void draw_shape3d(const context &ctx, const shape3d &shape, rgba color) {
  const render3d_state *s = open_pass(ctx);
  if (s == nullptr)
    return;
  s->cmds.push_back(draw3d_cmd{.is_shape = true,
                               .shape = shape,
                               .mesh = nullptr,
                               .model = {},
                               .transform = {},
                               .color = color,
                               .shader = {},
                               .fx = s->fx,
                               .material = s->material,
                               .buffer = {},
                               .first = 0,
                               .count = 0,
                               .bone_first = 0,
                               .bone_count = 0});
  s->next_motion_id = 0;
}

void draw_model(const context &ctx, model_handle handle, const transform3d &transform, rgba tint) {
  record_model(ctx, handle, transform, nullptr, tint, ctx_ref<render3d_state>(ctx)->fx, ctx_ref<shader_store>(ctx)->active);
}

void draw_model_anim(const context &ctx, model_handle handle, const transform3d &transform, const model_pose &pose,
                     rgba tint) {
  record_model(ctx, handle, transform, &pose, tint, ctx_ref<render3d_state>(ctx)->fx, ctx_ref<shader_store>(ctx)->active);
}

void draw_model_anim(const context &ctx, model_handle handle, const transform3d &transform, const model_pose &pose,
                     rgba tint, const model_recolor &recolor) {
  record_model(ctx, handle, transform, &pose, tint, ctx_ref<render3d_state>(ctx)->fx, ctx_ref<shader_store>(ctx)->active, &recolor, 1);
}

void draw_model_anim(const context &ctx, model_handle handle, const transform3d &transform, const model_pose &pose,
                     rgba tint, const model_recolor *recolors, u32 count) {
  record_model(ctx, handle, transform, &pose, tint, ctx_ref<render3d_state>(ctx)->fx, ctx_ref<shader_store>(ctx)->active, recolors, count);
}

namespace {
// The slot whose clips `handle` plays (model_create_skinned borrows them).
const model_slot *clips_of(const context &ctx, model_handle handle) {
  const model_slot *m = model_slot_of(ctx_ref<model_store>(ctx), handle);
  return m != nullptr ? &model_anim_owner(ctx_ref<model_store>(ctx), *m) : nullptr;
}
} // namespace

i32 model_anim_count(const context &ctx, model_handle handle) {
  const model_slot *m = clips_of(ctx, handle);
  return m != nullptr ? model_clip_count(*m) : 0;
}

const char *model_anim_name(const context &ctx, model_handle handle, i32 index) {
  const model_slot *m = clips_of(ctx, handle);
  if (m == nullptr || index < 0 || index >= model_clip_count(*m))
    return "";
  return m->anim_kept > 0 ? m->anims[index].name : m->morph_clips[(usize)index].name.c_str();
}

i32 model_anim_find(const context &ctx, model_handle handle, const char *name) {
  const model_slot *m = clips_of(ctx, handle);
  if (m == nullptr || name == nullptr)
    return -1;
  if (m->anim_kept == 0) {
    for (usize i = 0; i < m->morph_clips.size(); i++)
      if (m->morph_clips[i].name == name)
        return (i32)i;
    return -1;
  }
  for (i32 i = 0; i < m->anim_kept; i++)
    if (std::strncmp(m->anims[i].name, name, sizeof(m->anims[i].name)) == 0)
      return i;
  return -1;
}

f32 model_anim_duration(const context &ctx, model_handle handle, i32 index) {
  const model_slot *m = clips_of(ctx, handle);
  if (m == nullptr || index < 0 || index >= model_clip_count(*m))
    return 0.0f;
  if (m->anim_kept == 0)
    return m->morph_clips[(usize)index].duration;
  return (f32)std::max(m->anims[index].keyframeCount - 1, 0) / anim_fps;
}

i32 model_morph_count(const context &ctx, model_handle handle) {
  const model_slot *m = model_slot_of(ctx_ref<model_store>(ctx), handle);
  return m != nullptr ? (i32)m->morph_names.size() : 0;
}

const char *model_morph_name(const context &ctx, model_handle handle, i32 morph) {
  const model_slot *m = model_slot_of(ctx_ref<model_store>(ctx), handle);
  if (m == nullptr || morph < 0 || morph >= (i32)m->morph_names.size())
    return "";
  return m->morph_names[(usize)morph].c_str();
}

i32 model_morph_find(const context &ctx, model_handle handle, const char *name) {
  const model_slot *m = model_slot_of(ctx_ref<model_store>(ctx), handle);
  if (m == nullptr || name == nullptr)
    return -1;
  for (usize i = 0; i < m->morph_names.size(); i++)
    if (m->morph_names[i] == name)
      return (i32)i;
  return -1;
}

i32 model_morph_weights(const context &ctx, model_handle handle, const model_pose &pose, f32 *out, i32 count) {
  const model_slot *m = model_slot_of(ctx_ref<model_store>(ctx), handle);
  if (m == nullptr)
    return 0;
  std::vector<f32> weights;
  model_morph_eval(ctx_ref<model_store>(ctx), *m, &pose, weights);
  if (out != nullptr)
    std::copy_n(weights.begin(), std::min<usize>(weights.size(), (usize)std::max(count, 0)), out);
  return (i32)weights.size();
}

i32 model_bone_count(const context &ctx, model_handle handle) {
  const model_slot *m = model_slot_of(ctx_ref<model_store>(ctx), handle);
  return m != nullptr ? m->model.skeleton.boneCount : 0;
}

const char *model_bone_name(const context &ctx, model_handle handle, i32 bone) {
  const model_slot *m = model_slot_of(ctx_ref<model_store>(ctx), handle);
  if (m == nullptr || bone < 0 || bone >= m->model.skeleton.boneCount)
    return "";
  return m->model.skeleton.bones[bone].name;
}

i32 model_bone_find(const context &ctx, model_handle handle, const char *name) {
  const model_slot *m = model_slot_of(ctx_ref<model_store>(ctx), handle);
  if (m == nullptr || name == nullptr)
    return -1;
  for (i32 b = 0; b < m->model.skeleton.boneCount; b++)
    if (std::strncmp(m->model.skeleton.bones[b].name, name, sizeof(m->model.skeleton.bones[b].name)) == 0)
      return b;
  return -1;
}

i32 model_bone_parent(const context &ctx, model_handle handle, i32 bone) {
  const model_slot *m = model_slot_of(ctx_ref<model_store>(ctx), handle);
  if (m == nullptr || bone < 0 || bone >= m->model.skeleton.boneCount)
    return -1;
  return m->model.skeleton.bones[bone].parent;
}

bone_pose3d model_bone_pose(const context &ctx, model_handle handle, const model_pose &pose, i32 bone);

namespace {
// Calls `use` with each skin vertex that `bone` (or, with `children`, a bone
// below it) pulls hardest, along the model's axes from the bone's rest origin.
template <class F> void each_bone_vertex(const context &ctx, model_handle handle, i32 bone, bool children, F use) {
  const model_slot *m = model_slot_of(ctx_ref<model_store>(ctx), handle);
  const i32 bones = m != nullptr ? m->model.skeleton.boneCount : 0;
  if (m == nullptr || bone < 0 || bone >= bones)
    return;
  const BoneInfo *info = m->model.skeleton.bones;
  const auto counts = [&](i32 b) {
    for (i32 guard = 0; b >= 0 && guard < bones; guard++, b = info[b].parent)
      if (b == bone)
        return true;
      else if (!children)
        return false;
    return false;
  };
  const vec3 origin = model_bone_pose(ctx, handle, model_pose{}, bone).position;
  for (i32 k = 0; k < m->model.meshCount; k++) {
    const Mesh &mesh = m->model.meshes[k];
    if (mesh.vertices == nullptr || mesh.boneIndices == nullptr || mesh.boneWeights == nullptr)
      continue;
    for (i32 v = 0; v < mesh.vertexCount; v++) {
      i32 best = 0;
      for (i32 w = 1; w < 4; w++)
        if (mesh.boneWeights[v * 4 + w] > mesh.boneWeights[v * 4 + best])
          best = w;
      if (!counts(mesh.boneIndices[v * 4 + best]))
        continue;
      const Vector3 q = Vector3Transform({mesh.vertices[v * 3], mesh.vertices[v * 3 + 1], mesh.vertices[v * 3 + 2]},
                                         m->model.transform);
      use(vec3{q.x, q.y, q.z} - origin);
    }
  }
}
} // namespace

f32 model_volume(const context &ctx, model_handle handle) {
  const model_slot *m = model_slot_of(ctx_ref<model_store>(ctx), handle);
  if (m == nullptr)
    return 0.0f;
  // Each triangle with the origin makes a tetrahedron; their signed volumes
  // add up to what the surface encloses.
  f64 total = 0.0;
  for (i32 k = 0; k < m->model.meshCount; k++) {
    const Mesh &mesh = m->model.meshes[k];
    if (mesh.vertices == nullptr)
      continue;
    const auto at = [&](i32 v) {
      return Vector3Transform({mesh.vertices[v * 3], mesh.vertices[v * 3 + 1], mesh.vertices[v * 3 + 2]}, m->model.transform);
    };
    const i32 corners = mesh.indices != nullptr ? mesh.triangleCount * 3 : mesh.vertexCount;
    for (i32 c = 0; c + 2 < corners; c += 3) {
      const i32 i0 = mesh.indices != nullptr ? mesh.indices[c] : c;
      const i32 i1 = mesh.indices != nullptr ? mesh.indices[c + 1] : c + 1;
      const i32 i2 = mesh.indices != nullptr ? mesh.indices[c + 2] : c + 2;
      const Vector3 a = at(i0), b = at(i1), d = at(i2);
      total += (f64)Vector3DotProduct(a, Vector3CrossProduct(b, d)) / 6.0;
    }
  }
  return (f32)std::fabs(total);
}

bool model_bounds(const context &ctx, model_handle handle, vec3 *min, vec3 *max) {
  const model_slot *m = model_slot_of(ctx_ref<model_store>(ctx), handle);
  if (m == nullptr)
    return false;
  if (min != nullptr)
    *min = {m->bounds.min.x, m->bounds.min.y, m->bounds.min.z};
  if (max != nullptr)
    *max = {m->bounds.max.x, m->bounds.max.y, m->bounds.max.z};
  return true;
}

bool model_bone_bounds(const context &ctx, model_handle handle, i32 bone, bool children, vec3 *min, vec3 *max) {
  vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
  bool any = false;
  each_bone_vertex(ctx, handle, bone, children, [&](vec3 l) {
    lo = {std::min(lo.x, l.x), std::min(lo.y, l.y), std::min(lo.z, l.z)};
    hi = {std::max(hi.x, l.x), std::max(hi.y, l.y), std::max(hi.z, l.z)};
    any = true;
  });
  if (!any || min == nullptr || max == nullptr)
    return any;
  *min = lo;
  *max = hi;
  return true;
}

i32 model_bone_points(const context &ctx, model_handle handle, i32 bone, bool children, vec3 *out, i32 count) {
  i32 n = 0;
  each_bone_vertex(ctx, handle, bone, children, [&](vec3 l) {
    if (out != nullptr && n < count)
      out[n] = l;
    n++;
  });
  return n;
}

bone_pose3d model_bone_pose(const context &ctx, model_handle handle, const model_pose &pose, i32 bone) {
  const model_slot *m = model_slot_of(ctx_ref<model_store>(ctx), handle);
  if (m == nullptr || bone < 0 || bone >= m->model.skeleton.boneCount)
    return {};
  if (pose.bones != nullptr)
    return pose.bones[bone];
  const Transform t = posed_bone(model_anim_owner(ctx_ref<model_store>(ctx), *m), pose, bone);
  // The model's own transform (a file's root), as the draw applies it.
  const Matrix k = MatrixMultiply(pose_matrix(t), m->model.transform);
  const auto axis = [](f32 x, f32 y, f32 z) {
    const vec3 v{x, y, z};
    return length_sq(v) > 1e-12f ? normalize(v) : v;
  };
  return {.position = {k.m12, k.m13, k.m14},
          .x_axis = axis(k.m0, k.m1, k.m2),
          .y_axis = axis(k.m4, k.m5, k.m6),
          .z_axis = axis(k.m8, k.m9, k.m10)};
}
} // namespace njin
