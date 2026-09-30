#include "render3d.h"
#include "camera.h"
#include "fx.h"
#include "gizmo.h"
#include "njin2rl.h"
#include "njin_cfg.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_log.h"
#include "njin_instance.h"
#include "njin_model.h"
#include "njin_shader.h"
#include "njin_texture.h"
#include "particles3d.h"
#include <algorithm>
#include <cmath>
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
// The shadow atlas of the point and spot lights: one row per shadowed light,
// one tile per face (a point light has 6, a spot light 1).
constexpr i32 lamp_faces = 6;

const char *const lit_vs = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out vec3 fragPos;
void main() {
  fragTexCoord = vertexTexCoord;
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
uniform vec4 edgeColor;
// Shadows of point and spot lights: the atlas row of each light (-1 = none),
// each face's view-projection, and per row near, far, the size of a texel
// one unit away, and 1 / tile size.
uniform sampler2D lampShadowMap;
uniform int lightShadow[16];
uniform mat4 lampVP[24];
uniform vec4 lampParams[4];
out vec4 finalColor;

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
      float z = texture(lampShadowMap, (cell + q) / vec2(6.0, 4.0)).r;
      float linear = p.x * p.y / (p.y - z * (p.y - p.x));
      lit += depth - bias <= linear ? 1.0 : 0.0;
    }
  return lit / 9.0;
}

vec3 shade(vec3 base, vec3 n, vec3 pos, float cut, vec3 glow) {
  vec3 color = base;
  vec3 v = normalize(viewPos - pos);
  if (unlit == 0) {
    vec3 diffuse = ambient;
    vec3 spec = vec3(0.0);
    vec3 l = -lightDir;
    float nl = max(dot(n, l), 0.0);
    if (nl > 0.0) {
      float sun = shadowOn == 1 ? sunlight(pos, n, l) : 1.0;
      diffuse += lightColor * nl * sun;
      spec += lightColor * pow(max(dot(n, normalize(l + v)), 0.0), surface.y) * sun;
    }
    for (int i = 0; i < lightCount; i++) {
      vec3 d = lightPos[i].xyz - pos;
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
      float nli = max(dot(n, li), 0.0);
      if (nli > 0.0 && att > 0.0 && lightShadow[i] >= 0)
        att *= lamplight(lightShadow[i], lightSpot[i].w < -1.5, lightPos[i].xyz, pos, n, li);
      diffuse += lightColors[i].rgb * nli * att;
      if (nli > 0.0)
        spec += lightColors[i].rgb * pow(max(dot(n, normalize(li + v)), 0.0), surface.y) * att;
    }
    color = base * diffuse + spec * surface.x;
    color += rim.rgb * rim.a * pow(1.0 - max(dot(n, v), 0.0), 3.0);
  }
  color += emission.rgb * emission.a + glow;
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
in vec4 fragColor;
in vec3 fragNormal;
in vec3 fragPos;
uniform sampler2D texture0;    // albedo
uniform sampler2D texture2;    // normal map
uniform sampler2D emissionMap;
uniform vec4 emissionColor;    // tint of the emission map
uniform int useNormalMap;
uniform int useEmissionMap;

// Normal mapping without tangents: the frame comes from screen derivatives
// (Christian Schueler, "Normal Mapping Without Precomputed Tangents").
vec3 mapped_normal(vec3 n) {
  vec3 dp1 = dFdx(fragPos);
  vec3 dp2 = dFdy(fragPos);
  vec2 duv1 = dFdx(fragTexCoord);
  vec2 duv2 = dFdy(fragTexCoord);
  vec3 dp2perp = cross(dp2, n);
  vec3 dp1perp = cross(n, dp1);
  vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
  vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
  float invmax = inversesqrt(max(max(dot(t, t), dot(b, b)), 1e-12));
  vec3 m = texture(texture2, fragTexCoord).xyz * 2.0 - 1.0;
  return normalize(mat3(t * invmax, b * invmax, n) * m);
}

void main() {
  vec4 base = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
  float cut = dissolve_cut(fragPos);
  vec3 n = normalize(fragNormal);
  if (useNormalMap == 1)
    n = mapped_normal(n);
  vec3 glow = useEmissionMap == 1 ? texture(emissionMap, fragTexCoord).rgb * emissionColor.rgb : vec3(0.0);
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


// Continuous value noise; texture coordinates follow the closest articulated
// capsule instead of world space, so moving a person does not swim through grain.
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
  vec3 anchor = vec3(0), axis = vec3(0,1,0);
  float radius = max(length(shapeBounds) * 0.2, 0.0001), nearest = 1e10;
  if (shapeKind == 5) {
    for (int i=0; i<blendCount; ++i) {
      float d = round_cone(p, blendA[i].xyz, blendB[i].xyz, blendA[i].w, blendB[i].w);
      if (d < nearest) {
        nearest = d;
        anchor = blendA[i].xyz;
        vec3 segment = blendB[i].xyz - anchor;
        axis = dot(segment,segment)>1e-9 ? normalize(segment) : vec3(0,1,0);
        radius = max((blendA[i].w+blendB[i].w)*0.5, 0.0001);
      }
    }
  }
  vec3 tangent = normalize(cross(axis, abs(axis.z)<0.9 ? vec3(0,0,1) : vec3(1,0,0)));
  mat3 basis = mat3(tangent, axis, cross(tangent,axis));
  vec3 q = transpose(basis) * (p-anchor) / radius * claySurface.y;
  // Fade grain below pixel resolution to avoid sparkly noise in an RTS crowd.
  float footprint = max(length(dFdx(q)),length(dFdy(q)));
  float amount = claySurface.x * (1.0-smoothstep(0.35,1.4,footprint));
  vec3 e=vec3(0.08,0,0);
  vec3 g=vec3(clay_noise(q+e.xyy)-clay_noise(q-e.xyy),
              clay_noise(q+e.yxy)-clay_noise(q-e.yxy),
              clay_noise(q+e.yyx)-clay_noise(q-e.yyx))/0.16;
  g=basis*g;
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
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out vec3 fragPos;
void main() {
  mat3 r = instance_rotation();
  vec3 k = instance_scale();
  vec3 world = r * (vertexPosition * k) + instance0.xyz;
  fragTexCoord = vertexTexCoord;
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
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
out vec3 fragPos;
void main() {
  mat4 k = skin_matrix();
  vec4 p = k * vec4(vertexPosition, 1.0);
  fragTexCoord = vertexTexCoord;
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
  l.shadow_on = loc("shadowOn");
  l.shadow_map = loc("shadowMap");
  l.light_vp = loc("lightVP");
  l.shadow_params = loc("shadowParams");
  l.flash = loc("flash");
  l.dissolve = loc("dissolve");
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
  l.lamp_map = loc("lampShadowMap");
  l.bones = loc("boneMatrices");
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
  } else {
    NJIN_WARN("3d: the skinning shader failed to compile, animated models are drawn in their rest pose");
  }
  // DrawMesh binds the emission map through the emission slot's location.
  s.lit.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(s.lit, "emissionMap");
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
  const f32 per_pixel = screen.y > 0.0f ? camera.fovy / screen.y : 0.0f;
  const vec3 forward = normalize(camera.target - camera.position);
  const vec3 right = normalize(cross(forward, camera.up));
  const vec3 up = cross(right, forward);
  Vector3 f = rl3(forward);
  f = Vector3RotateByAxisAngle(f, rl3(up), -offset.x * per_pixel * DEG2RAD);
  f = Vector3RotateByAxisAngle(f, rl3(right), -offset.y * per_pixel * DEG2RAD);
  const Vector3 u = Vector3RotateByAxisAngle(rl3(up), f, roll * DEG2RAD);
  const f32 dist = distance(camera.position, camera.target);
  camera.target = camera.position + vec3{f.x, f.y, f.z} * dist;
  camera.up = {u.x, u.y, u.z};
  return camera;
}

// Projection and view of `camera` on the current matrices.
void load_camera(const context &ctx, const camera3d &camera) {
  // BeginMode3D takes the aspect from the window framebuffer, which is wrong
  // on the virtual image; the logical screen size is the right one.
  const vec2 screen = screen_size(ctx);
  const f64 aspect = screen.y > 0.0f ? (f64)screen.x / (f64)screen.y : 1.0;
  const f64 top = (f64)camera.near_plane * std::tan((f64)camera.fovy * 0.5 * (f64)DEG2RAD);
  const f64 right = top * aspect;
  rlMatrixMode(RL_PROJECTION);
  rlLoadIdentity();
  rlFrustum(-right, right, -top, top, camera.near_plane, camera.far_plane);
  rlMatrixMode(RL_MODELVIEW);
  rlLoadIdentity();
  rlMultMatrixf(MatrixToFloat(MatrixLookAt(rl3(camera.position), rl3(camera.target), rl3(camera.up))));
}

// The frustum of the matrices load_camera() set, as six planes. Row i of the
// view-projection is (m[i], m[i + 4], m[i + 8], m[i + 12]) in raylib's layout.
void set_frustum(render3d_state &s) {
  const Matrix m = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
  const vec4 r0{m.m0, m.m4, m.m8, m.m12}, r1{m.m1, m.m5, m.m9, m.m13}, r2{m.m2, m.m6, m.m10, m.m14},
      r3{m.m3, m.m7, m.m11, m.m15};
  const auto add = [](vec4 a, vec4 b, f32 k) { return vec4{a.x + b.x * k, a.y + b.y * k, a.z + b.z * k, a.w + b.w * k}; };
  s.frustum = {add(r3, r0, 1.0f), add(r3, r0, -1.0f), add(r3, r1, 1.0f),
               add(r3, r1, -1.0f), add(r3, r2, 1.0f), add(r3, r2, -1.0f)};
  s.tan_half_fovy = std::tan(s.camera.fovy * 0.5f * DEG2RAD);
}

// Pass state for a draw call: only inside begin_3d/end_3d.
const render3d_state *open_pass(const context &ctx) {
  const render3d_state &s = ctx.render3d;
  return s.active ? &s : nullptr;
}

void record(const context &ctx, const Mesh *mesh, model_handle model, const Matrix &transform, rgba color) {
  const render3d_state &s = ctx.render3d;
  s.cmds.push_back(draw3d_cmd{.is_shape = false,
                              .shape = {},
                              .mesh = mesh,
                              .model = model,
                              .transform = transform,
                              .color = color,
                              .shader = ctx.shader.active,
                              .fx = s.fx,
                              .material = s.material,
                              .buffer = {},
                              .first = 0,
                              .count = 0,
                              .bone_first = 0,
                              .bone_count = 0});
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
  const texture_slot *slot = texture_slot_of(ctx.texture, handle);
  if (slot == nullptr || slot->packed)
    return false;
  out = slot->texture;
  return true;
}

// Uniforms of the built-in shader for one draw.
void set_draw_uniforms(Shader sh, const render3d_locations &l, const fx3d &fx, const material3d &m,
                       rgba emission_color, bool normal_map, bool emission_map) {
  set_vec4(sh, l.flash, v4(fx.flash));
  set_vec4(sh, l.dissolve,
           {clamp(fx.dissolve, 0.0f, 1.0f), fx.edge_width, fx.grain > 0.0f ? fx.grain : 0.1f, fx.seed});
  set_vec4(sh, l.edge_color, v4(fx.edge_color));
  set_vec2(sh, l.surface, {m.specular, std::max(m.shininess, 1.0f)});
  set_vec2(sh, l.clay_surface, {clamp(m.clay, 0.0f, 1.0f), std::max(m.clay_detail, 0.1f)});
  set_vec4(sh, l.emission, v4(m.emission));
  set_vec4(sh, l.rim, v4(m.rim));
  set_vec4(sh, l.emission_color, v4(emission_color));
  set_i32(sh, l.unlit, m.unlit ? 1 : 0);
  set_i32(sh, l.use_normal_map, normal_map ? 1 : 0);
  set_i32(sh, l.use_emission_map, emission_map ? 1 : 0);
}

// The shader for a draw recorded with `handle` bound: the game's, given the
// sun under the built-in names, or false for the built-in one.
bool game_shader(const context &ctx, shader_handle handle, Shader &out) {
  const shader_slot *slot = shader_slot_of(ctx.shader, handle);
  if (slot == nullptr)
    return false;
  const render3d_state &s = ctx.render3d;
  shader_slot_set_optional_vec3(*slot, "lightDir", normalize(s.light.direction));
  shader_slot_set_optional_vec3(*slot, "lightColor", rgb(s.light.color));
  shader_slot_set_optional_vec3(*slot, "ambient", rgb(s.light.ambient));
  shader_slot_set_optional_vec3(*slot, "viewPos", s.camera.position);
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
  const model_slot *slot = model_slot_of(ctx.model, c.model);
  if (slot == nullptr)
    return;
  const Model &model = slot->model;
  const Matrix transform = MatrixMultiply(model.transform, c.transform);
  for (i32 i = 0; i < model.meshCount; i++) {
    const i32 index = model.meshMaterial[i];
    const Material &file = model.materials[index];
    const model_material &mm = slot->materials[(usize)index];
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
    if (maps[MATERIAL_MAP_DIFFUSE].texture.id == 0)
      maps[MATERIAL_MAP_DIFFUSE].texture = default_texture();
    const rgba color{mm.color.r * c.color.r, mm.color.g * c.color.g, mm.color.b * c.color.b,
                     mm.color.a * c.color.a};
    to_raylib(color, maps[MATERIAL_MAP_DIFFUSE].color);
    const model_lod_mesh *lod = model_lod_of(*slot, c.lod, i);
    const bool posed = c.bone_count > 0 && ctx.render3d.skin_ok &&
                       (lod != nullptr ? lod->bone_vbo != 0
                                       : (usize)i < slot->bone_vbo.size() && slot->bone_vbo[(usize)i] != 0);
    fn(lod != nullptr ? lod->mesh : model.meshes[i], maps, mm, transform, posed);
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
void draw_casters(context &ctx, const Matrix &view_proj, bool ortho, vec3 dir);

// Depth of every opaque shadow caster into the bound target, seen through the
// current matrices (`view_proj`): along `dir` for the sun (orthographic), from
// the SDF shader's viewPos for a lamp.
void draw_casters(context &ctx, const Matrix &view_proj, bool ortho, vec3 dir) {
  const render3d_state &s = ctx.render3d;
  map_set maps = s.maps;
  Material depth{};
  depth.shader = s.depth;
  depth.maps = maps.data();
  Material skinned = depth;
  skinned.shader = s.depth_skinned;
  for (const draw3d_cmd &c : s.cmds) {
    if (!casts(c))
      continue;
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
  render3d_state &s = ctx.render3d;
  const light3d &sun = s.light;
  const f32 range = std::max(sun.shadow_range, 0.1f);
  const vec3 dir = normalize(sun.direction);
  const vec3 forward = normalize(s.camera.target - s.camera.position);
  vec3 focus = s.camera.position + forward * (range * 0.5f);
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

// Depth of the point and spot lights with `shadows` into the lamp atlas: a
// row per light, a 90 degree face per cube side for a point light, one face
// covering the cone for a spot light. Fills s.lamps; false when none casts.
bool render_lamp_shadows(context &ctx) {
  render3d_state &s = ctx.render3d;
  lamp_shadows &ls = s.lamps;
  ls.row.fill(-1);
  ls.rows = 0;
  const i32 count = std::min((i32)s.lights.size(), light3d_max);
  for (i32 i = 0; i < count && ls.rows < light3d_shadow_max; i++)
    if (s.lights[(usize)i].shadows && s.lights[(usize)i].radius > 0.0f)
      ls.row[(usize)i] = ls.rows++;
  if (ls.rows == 0 || s.cmds.empty())
    return false;
  const i32 tile = std::clamp(s.light.source_shadow_size, 64, 2048);
  if (!ensure_shadow(s.lamp, tile * lamp_faces, tile * light3d_shadow_max)) {
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

  vec4 pos[light3d_max]{};
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
    spot[i] = {d.x, d.y, d.z, src.kind == light3d_spot ? std::cos(half) : -2.0f};
  }
  set_i32(sh, l.light_count, count);
  if (count > 0) {
    SetShaderValueV(sh, l.light_pos, pos, SHADER_UNIFORM_VEC4, count);
    SetShaderValueV(sh, l.light_colors, colors, SHADER_UNIFORM_VEC4, count);
    SetShaderValueV(sh, l.light_spot, spot, SHADER_UNIFORM_VEC4, count);
  }

  const lamp_shadows &ls = s.lamps;
  i32 rows[light3d_max];
  for (i32 i = 0; i < light3d_max; i++)
    rows[i] = lamps && i < count ? ls.row[(usize)i] : -1;
  SetShaderValueV(sh, l.light_shadow, rows, SHADER_UNIFORM_INT, light3d_max);
  set_i32(sh, l.lamp_map, lamp_unit);
  if (lamps) {
    SetShaderValueV(sh, l.lamp_params, ls.params.data(), SHADER_UNIFORM_VEC4, ls.rows);
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
  render3d_state &s = ctx.render3d;
  instance_slot *slot = instance_slot_of(ctx.instances, c.buffer);
  if (slot == nullptr || slot->vbo == 0 || c.first >= slot->count)
    return;
  const u32 count = std::min(c.count, slot->count - c.first);
  if (count == 0)
    return;
  Shader shader = depth_only ? s.depth_instanced : s.lit_instanced;
  const bool own = !depth_only && game_shader(ctx, c.shader, shader);
  const bool built_in = !depth_only && !own;

  // The meshes to draw, each with its albedo texture and colour.
  struct part {
    const Mesh *mesh;
    Texture2D texture;
    rgba color;
    const material3d *surface;
  };
  std::vector<part> parts;
  Texture2D texture = default_texture();
  if (c.mesh != nullptr) {
    mesh_texture(ctx, c.material.texture, texture);
    parts.push_back({c.mesh, texture, colors::white, &c.material});
  } else if (const model_slot *m = model_slot_of(ctx.model, c.model)) {
    for (i32 i = 0; i < m->model.meshCount; i++) {
      const i32 index = m->model.meshMaterial[i];
      const model_material &mm = m->materials[(usize)index];
      Texture2D t = m->model.materials[index].maps[MATERIAL_MAP_DIFFUSE].texture;
      if (!mesh_texture(ctx, mm.albedo, t) && t.id == 0)
        t = default_texture();
      parts.push_back({&m->model.meshes[i], t, mm.color, &mm.surface});
    }
  }

  ctx.stats.instanced_calls++;
  const Matrix mvp = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
  const i32 floats = (i32)slot->floats;
  const i32 stride = (i32)(slot->floats * sizeof(f32));
  for (const part &p : parts) {
    if (built_in)
      set_draw_uniforms(shader, s.instanced_locs, c.fx, *p.surface, colors::white, false, false);
    rlEnableShader(shader.id);
    rlSetUniformMatrix(rlGetLocationUniform(shader.id, "mvp"), mvp);
    rlSetUniform(rlGetLocationUniform(shader.id, "instanceFloats"), &floats, RL_SHADER_UNIFORM_INT, 1);
    if (!depth_only) {
      const vec4 color = v4(p.color);
      rlSetUniform(rlGetLocationUniform(shader.id, "colDiffuse"), &color, RL_SHADER_UNIFORM_VEC4, 1);
      const i32 unit = 0;
      rlActiveTextureSlot(0);
      rlEnableTexture(p.texture.id);
      rlSetUniform(rlGetLocationUniform(shader.id, "texture0"), &unit, RL_SHADER_UNIFORM_INT, 1);
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
    if (p.mesh->indices != nullptr)
      rlDrawVertexArrayElementsInstanced(0, p.mesh->triangleCount * 3, nullptr, (i32)count);
    else
      rlDrawVertexArrayInstanced(0, p.mesh->vertexCount, (i32)count);
    for (i32 i = 0; i < 4; i++) {
      if (locs[i] < 0 || i >= floats / 4)
        continue;
      rlSetVertexAttributeDivisor((u32)locs[i], 0);
      restore_mesh_attribute(*p.mesh, locs[i]);
    }
    rlDisableVertexArray();
  }
  if (!depth_only)
    rlDisableTexture();
  rlDisableShader();
}

void draw_main(context &ctx, const draw3d_cmd &c, const Matrix &view_proj) {
  const render3d_state &s = ctx.render3d;
  if (c.is_shape) {
    draw_shape(s, c, view_proj, false, {}, false);
    return;
  }
  if (c.buffer.id != 0) {
    draw_instanced_cmd(ctx, c, false);
    return;
  }
  if (c.mesh == nullptr) {
    if (c.culled) {
      ctx.stats.models3d_culled++;
      return;
    }
    ctx.stats.models3d++;
  }
  Shader custom{};
  const bool own = game_shader(ctx, c.shader, custom);
  if (c.mesh != nullptr) {
    map_set maps = s.maps;
    Texture2D texture{};
    if (mesh_texture(ctx, c.material.texture, texture))
      maps[MATERIAL_MAP_DIFFUSE].texture = texture;
    to_raylib(c.color, maps[MATERIAL_MAP_DIFFUSE].color);
    if (!own)
      set_draw_uniforms(s.lit, s.locs, c.fx, c.material, colors::white, false, false);
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
                        maps[MATERIAL_MAP_NORMAL].texture.id > 0, maps[MATERIAL_MAP_EMISSION].texture.id > 0);
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
// the keyframes; the rest pose for anim < 0.
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
bool pose_bones(const render3d_state &s, const model_slot &m, const model_pose &pose, u32 &first, u32 &count) {
  const f32 k = clamp(pose.blend, 0.0f, 1.0f);
  const bool blending = pose.blend_anim >= 0 && k > 0.0f;
  if (!m.skinned || (pose.anim < 0 && !blending))
    return false;
  const i32 bones = m.model.skeleton.boneCount;
  first = (u32)s.bones.size();
  count = (u32)bones;
  for (i32 b = 0; b < bones; b++) {
    Transform t = sample_bone(m, pose.anim, pose.time, pose.loop, b);
    if (blending) {
      const Transform u = sample_bone(m, pose.blend_anim, pose.blend_time, pose.blend_loop, b);
      t = Transform{Vector3Lerp(t.translation, u.translation, k), QuaternionSlerp(t.rotation, u.rotation, k),
                    Vector3Lerp(t.scale, u.scale, k)};
    }
    s.bones.push_back(MatrixMultiply(m.inv_bind[(usize)b], pose_matrix(t)));
  }
  return true;
}

// The model's box as the draw places it, against the pass's frustum (`culled`),
// and how much of the screen's height it covers, for the level of detail.
void place_model(const render3d_state &s, const model_slot &m, draw3d_cmd &c) {
  const Matrix t = MatrixMultiply(m.model.transform, c.transform);
  const Vector3 lo = m.bounds.min, hi = m.bounds.max;
  const vec3 mid{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
  const vec3 half{(hi.x - lo.x) * 0.5f, (hi.y - lo.y) * 0.5f, (hi.z - lo.z) * 0.5f};
  const vec3 center{t.m0 * mid.x + t.m4 * mid.y + t.m8 * mid.z + t.m12,
                    t.m1 * mid.x + t.m5 * mid.y + t.m9 * mid.z + t.m13,
                    t.m2 * mid.x + t.m6 * mid.y + t.m10 * mid.z + t.m14};
  vec3 extent{std::abs(t.m0) * half.x + std::abs(t.m4) * half.y + std::abs(t.m8) * half.z,
              std::abs(t.m1) * half.x + std::abs(t.m5) * half.y + std::abs(t.m9) * half.z,
              std::abs(t.m2) * half.x + std::abs(t.m6) * half.y + std::abs(t.m10) * half.z};
  // The box is the rest pose's: an arm swung out may leave it.
  if (c.bone_count > 0)
    extent = extent + vec3{1.0f, 1.0f, 1.0f} * (std::max({extent.x, extent.y, extent.z}) * 0.3f);
  for (const vec4 &p : s.frustum) {
    const f32 reach = std::abs(p.x) * extent.x + std::abs(p.y) * extent.y + std::abs(p.z) * extent.z;
    if (p.x * center.x + p.y * center.y + p.z * center.z + p.w + reach < 0.0f) {
      c.culled = true;
      return;
    }
  }
  if (m.lods.empty())
    return;
  const f32 radius = length(extent);
  const f32 dist = length(center - s.camera.position);
  if (dist <= radius)
    return;
  const f32 share = radius / (dist * s.tan_half_fovy);
  f32 below = m.lod_screen;
  while (c.lod < m.lods.size() && share < below) {
    c.lod++;
    below *= 0.5f;
  }
}

void record_model(const context &ctx, model_handle handle, const transform3d &transform, const model_pose *pose,
                  rgba tint, const fx3d &fx, shader_handle shader) {
  const render3d_state &s = ctx.render3d;
  const model_slot *m = model_slot_of(ctx.model, handle);
  if (!s.active || m == nullptr)
    return;
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
  if (pose != nullptr && s.skin_ok)
    pose_bones(s, *m, *pose, c.bone_first, c.bone_count);
  place_model(s, *m, c);
  s.cmds.push_back(c);
}

// The 3D components (njin_3d.h) into the open pass: models, SDF shapes and
// lights, each at its entity's transform3d.
void record_entities(context &ctx) {
  render3d_state &s = ctx.render3d;
  entt::registry &reg = world(ctx);
  for (auto [e, t, m] : reg.view<const transform3d, const model3d>().each())
    if (m.visible)
      record_model(ctx, m.model, t, &m.pose, m.tint, m.fx, {});
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

void setup(context &ctx) { ecs_register(ctx, phase_update, advance_models, "model3d_anim"); }
} // namespace

mod_desc render3d_module() { return mod_desc{.name = "njin.render3d", .setup = setup}; }

render3d_state::~render3d_state() {
  free_shadow(shadow);
  free_shadow(lamp);
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

void begin_3d(context &ctx, const camera3d &camera) {
  render3d_state &s = ctx.render3d;
  if (s.active) {
    NJIN_WARN("3d: begin_3d called twice without end_3d, ignored");
    return;
  }
  if (ctx.view.world_depth == 0) {
    NJIN_WARN("3d: begin_3d outside phase_render, ignored");
    return;
  }
  if (!ensure_ready(s))
    return;
  rlDrawRenderBatchActive();
  // The world pass's projection comes back at end_3d.
  rlMatrixMode(RL_PROJECTION);
  rlPushMatrix();
  s.camera = shaken(ctx, camera);
  load_camera(ctx, s.camera);
  set_frustum(s);
  s.cmds.clear();
  s.bones.clear();
  s.blend_parts.clear();
  s.lights.clear();
  s.fx = fx3d{};
  s.material = material3d{};
  s.entities = camera.entities;
  s.active = true;
}

void end_3d(context &ctx) {
  render3d_state &s = ctx.render3d;
  if (!s.active)
    return;
  rlDrawRenderBatchActive();
  if (s.entities)
    record_entities(ctx);
  if (ctx.debug.running)
    render3d_capture_debug(ctx);

  bool shadows = false;
  Matrix light_vp = MatrixIdentity();
  const i32 sun_size = std::clamp(s.light.shadow_size, 256, 8192);
  if (s.light.shadows && !s.cmds.empty() && ensure_shadow(s.shadow, sun_size, sun_size)) {
    light_vp = render_shadow(ctx);
    shadows = true;
  }
  const bool lamps = render_lamp_shadows(ctx);
  if (shadows || lamps) {
    world_target_rebind(ctx);
    load_camera(ctx, s.camera);
  }

  rlEnableDepthTest();
  s.depth_near = s.camera.near_plane;
  s.depth_far = s.camera.far_plane;
  s.depth_drawn = true;
  set_pass_uniforms(s, s.lit, s.locs, shadows, light_vp, lamps);
  set_pass_uniforms(s, s.sdf, s.sdf_locs, shadows, light_vp, lamps);
  set_pass_uniforms(s, s.lit_instanced, s.instanced_locs, shadows, light_vp, lamps);
  if (s.skin_ok)
    set_pass_uniforms(s, s.lit_skinned, s.skinned_locs, shadows, light_vp, lamps);
  const Matrix view_proj = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
  if (shadows) {
    rlActiveTextureSlot(shadow_unit);
    rlEnableTexture(s.shadow.depth);
  }
  if (lamps) {
    rlActiveTextureSlot(lamp_unit);
    rlEnableTexture(s.lamp.depth);
  }
  rlActiveTextureSlot(0);
  for (const draw3d_cmd &c : s.cmds)
    draw_main(ctx, c, view_proj);
  for (i32 unit : {shadow_unit, lamp_unit}) {
    rlActiveTextureSlot(unit);
    rlDisableTexture();
  }
  rlActiveTextureSlot(0);
  particles3d_draw(ctx, s.camera);
  gizmo_draw_3d(ctx, s.camera);

  s.cmds.clear();
  s.bones.clear();
  s.blend_parts.clear();
  s.lights.clear();
  s.active = false;
  rlDrawRenderBatchActive();
  rlMatrixMode(RL_PROJECTION);
  rlPopMatrix();
  // Back to the world pass exactly as begin_world_space left it.
  rlMatrixMode(RL_MODELVIEW);
  rlLoadIdentity();
  rlMultMatrixf(MatrixToFloat(GetCameraMatrix2D(ctx.post.world_camera)));
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
  const render3d_state &s = ctx.render3d;
  const instance_slot *slot = instance_slot_of(const_cast<instance_store &>(ctx.instances), c.buffer);
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
  render3d_state &s = ctx.render3d;
  debug3d_frame &f = s.debug;
  f.time = ctx.time.elapsed;
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
    } else if (const model_slot *m = model_slot_of(ctx.model, c.model)) {
      f.items.push_back({debug3d_model, model_center(*m, c.transform), c.color});
    }
  }
}

shape_frame shape3d_frame(const shape3d &shape) { return frame_of(shape); }

void render3d_close(context &ctx) {
  if (!ctx.render3d.active)
    return;
  NJIN_WARN("3d: begin_3d without end_3d, closed at the end of phase_render");
  end_3d(ctx);
}

void light3d_set(context &ctx, const light3d &light) { ctx.render3d.light = light; }

light3d light3d_get(const context &ctx) { return ctx.render3d.light; }

void light3d_add(context &ctx, const light3d_source &light) {
  render3d_state &s = ctx.render3d;
  if (!s.active)
    return;
  if ((i32)s.lights.size() >= light3d_max) {
    NJIN_WARN("3d: more than %d lights in one pass, the rest are ignored", light3d_max);
    return;
  }
  s.lights.push_back(light);
}

void material3d_set(context &ctx, const material3d &material) { ctx.render3d.material = material; }

void fx3d_set(context &ctx, const fx3d &fx) { ctx.render3d.fx = fx; }

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
  const render3d_state &s = ctx.render3d;
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
  if (open_pass(ctx) == nullptr || buffer.id == 0 || model_slot_of(ctx.model, model) == nullptr)
    return;
  record_instanced(ctx, nullptr, model, buffer, first, count, shader);
}

void draw_sdf_blend(const context &ctx, const sdf_part *parts, u32 count, f32 blend, rgba color) {
  const render3d_state *s = open_pass(ctx);
  if (s == nullptr || parts == nullptr || count == 0)
    return;
  if (count > sdf_blend_max) {
    NJIN_WARN("draw_sdf_blend: %u parts, at most %u: the rest are dropped", count, sdf_blend_max);
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
}

void draw_model(const context &ctx, model_handle handle, const transform3d &transform, rgba tint) {
  record_model(ctx, handle, transform, nullptr, tint, ctx.render3d.fx, ctx.shader.active);
}

void draw_model_anim(const context &ctx, model_handle handle, const transform3d &transform, const model_pose &pose,
                     rgba tint) {
  record_model(ctx, handle, transform, &pose, tint, ctx.render3d.fx, ctx.shader.active);
}

i32 model_anim_count(const context &ctx, model_handle handle) {
  const model_slot *m = model_slot_of(ctx.model, handle);
  return m != nullptr ? m->anim_kept : 0;
}

i32 model_anim_find(const context &ctx, model_handle handle, const char *name) {
  const model_slot *m = model_slot_of(ctx.model, handle);
  if (m == nullptr || name == nullptr)
    return -1;
  for (i32 i = 0; i < m->anim_kept; i++)
    if (std::strncmp(m->anims[i].name, name, sizeof(m->anims[i].name)) == 0)
      return i;
  return -1;
}

const char *model_anim_name(const context &ctx, model_handle handle, i32 index) {
  const model_slot *m = model_slot_of(ctx.model, handle);
  if (m == nullptr || index < 0 || index >= m->anim_kept)
    return "";
  return m->anims[index].name;
}

f32 model_anim_duration(const context &ctx, model_handle handle, i32 index) {
  const model_slot *m = model_slot_of(ctx.model, handle);
  if (m == nullptr || index < 0 || index >= m->anim_kept)
    return 0.0f;
  return (f32)std::max(m->anims[index].keyframeCount - 1, 0) / anim_fps;
}
} // namespace njin
