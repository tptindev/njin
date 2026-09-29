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
#include <raymath.h>
#include <rlgl.h>
#include <string>
#include <vector>

namespace njin {
namespace {
// Past raylib's material map slots (0..11), so DrawMesh never rebinds it.
constexpr i32 shadow_unit = 14;

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

float sdf(vec3 p) {
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
  vec3 n = normalize(mat3(shapeToWorld) * sdf_normal(local, eps));
  finalColor = vec4(shade(colDiffuse.rgb, n, pos, cut, vec3(0.0)), colDiffuse.a);
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
  l.shape_kind = loc("shapeKind");
  l.shape_dims = loc("shapeDims");
  l.shape_bounds = loc("shapeBounds");
  l.shape_to_local = loc("shapeToLocal");
  l.shape_to_world = loc("shapeToWorld");
  l.mat_vp = loc("matVP");
  l.ray_ortho = loc("rayOrtho");
  l.ray_dir = loc("rayDir");
  l.depth_only = loc("depthOnly");
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
  // DrawMesh binds the emission map through the emission slot's location.
  s.lit.locs[SHADER_LOC_MAP_EMISSION] = GetShaderLocation(s.lit, "emissionMap");
  // Enough segments that a sphere or capsule filling a good part of the
  // screen still reads as round.
  s.cube = GenMeshCube(1.0f, 1.0f, 1.0f);
  s.sphere = GenMeshSphere(1.0f, 32, 48);
  s.plane = GenMeshPlane(1.0f, 1.0f, 1, 1);
  s.cylinder = GenMeshCylinder(1.0f, 1.0f, 48);
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

bool ensure_shadow(shadow_target &t, i32 size) {
  size = std::clamp(size, 256, 8192);
  if (t.fbo != 0 && t.size == size)
    return true;
  free_shadow(t);
  t.fbo = rlLoadFramebuffer();
  t.color = rlLoadTexture(nullptr, size, size, RL_PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, 1);
  t.depth = rlLoadTextureDepth(size, size, false);
  rlFramebufferAttach(t.fbo, t.color, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
  rlFramebufferAttach(t.fbo, t.depth, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
  if (!rlFramebufferComplete(t.fbo)) {
    NJIN_WARN("3d: shadow map framebuffer incomplete, shadows are off");
    free_shadow(t);
    return false;
  }
  t.size = size;
  return true;
}

// camera_shake() for a 3D camera: the 2D shake's screen offset becomes a turn
// of the view by the same share of the screen, its roll a tilt of `up`.
camera3d shaken(const njin_ctx &ctx, camera3d camera) {
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
void load_camera(const njin_ctx &ctx, const camera3d &camera) {
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

// Pass state for a draw call: only inside begin_3d/end_3d.
const render3d_state *open_pass(const njin_ctx &ctx) {
  const render3d_state &s = ctx.render3d;
  return s.active ? &s : nullptr;
}

void record(const njin_ctx &ctx, const Mesh *mesh, model_handle model, const Matrix &transform, rgba color) {
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
                              .count = 0});
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
bool mesh_texture(const njin_ctx &ctx, texture_handle handle, Texture2D &out) {
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
  set_vec4(sh, l.emission, v4(m.emission));
  set_vec4(sh, l.rim, v4(m.rim));
  set_vec4(sh, l.emission_color, v4(emission_color));
  set_i32(sh, l.unlit, m.unlit ? 1 : 0);
  set_i32(sh, l.use_normal_map, normal_map ? 1 : 0);
  set_i32(sh, l.use_emission_map, emission_map ? 1 : 0);
}

// The shader for a draw recorded with `handle` bound: the game's, given the
// sun under the built-in names, or false for the built-in one.
bool game_shader(const njin_ctx &ctx, shader_handle handle, Shader &out) {
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
template <typename Fn> void for_each_model_mesh(const njin_ctx &ctx, const draw3d_cmd &c, Fn &&fn) {
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
    fn(model.meshes[i], maps, mm, transform);
  }
}

void draw_shape(const render3d_state &s, const draw3d_cmd &c, const Matrix &view_proj, bool ortho, vec3 ray_dir,
                bool depth_only);
void draw_instanced_cmd(njin_ctx &ctx, const draw3d_cmd &c, bool depth_only);

// Depth of every opaque shadow caster, seen from the sun, into the shadow map.
// Returns the sun's view-projection, for the lit shader to look up.
Matrix render_shadow(njin_ctx &ctx) {
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

  map_set maps = s.maps;
  Material depth{};
  depth.shader = s.depth;
  depth.maps = maps.data();
  for (const draw3d_cmd &c : s.cmds) {
    if (!casts(c))
      continue;
    if (c.is_shape) {
      if (c.material.cast_shadows && !c.material.unlit)
        draw_shape(s, c, light_vp, true, dir, true);
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
    for_each_model_mesh(ctx, c, [&](const Mesh &mesh, map_set &, const model_material &mm, const Matrix &transform) {
      if (mm.surface.cast_shadows)
        DrawMesh(mesh, depth, transform);
    });
  }
  rlDisableFramebuffer();
  return light_vp;
}

void set_pass_uniforms(const render3d_state &s, Shader sh, const render3d_locations &l, bool shadows,
                       const Matrix &light_vp) {
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
void draw_shape(const render3d_state &s, const draw3d_cmd &c, const Matrix &view_proj, bool ortho, vec3 ray_dir,
                bool depth_only) {
  const shape_frame f = frame_of(c.shape);
  const render3d_locations &l = s.sdf_locs;
  // A hair of margin so the surface never touches the box's faces.
  const vec3 b = f.bounds * 1.02f + vec3{1e-3f, 1e-3f, 1e-3f};
  const Matrix box = MatrixMultiply(MatrixScale(b.x * 2.0f, b.y * 2.0f, b.z * 2.0f), f.to_world);
  set_i32(s.sdf, l.shape_kind, (i32)c.shape.kind);
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
void draw_instanced_cmd(njin_ctx &ctx, const draw3d_cmd &c, bool depth_only) {
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

void draw_main(njin_ctx &ctx, const draw3d_cmd &c, const Matrix &view_proj) {
  const render3d_state &s = ctx.render3d;
  if (c.is_shape) {
    draw_shape(s, c, view_proj, false, {}, false);
    return;
  }
  if (c.buffer.id != 0) {
    draw_instanced_cmd(ctx, c, false);
    return;
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
  for_each_model_mesh(ctx, c, [&](const Mesh &mesh, map_set &maps, const model_material &mm, const Matrix &transform) {
    Shader shader = s.lit;
    if (!game_shader(ctx, mm.shader, shader) && own)
      shader = custom;
    if (shader.id == s.lit.id)
      set_draw_uniforms(s.lit, s.locs, c.fx, mm.surface, mm.emission_color, maps[MATERIAL_MAP_NORMAL].texture.id > 0,
                        maps[MATERIAL_MAP_EMISSION].texture.id > 0);
    Material material{};
    material.shader = shader;
    material.maps = maps.data();
    DrawMesh(mesh, material, transform);
  });
}
} // namespace

render3d_state::~render3d_state() {
  free_shadow(shadow);
  if (!ready)
    return;
  UnloadMesh(cube);
  UnloadMesh(sphere);
  UnloadMesh(plane);
  UnloadMesh(cylinder);
  UnloadShader(lit);
  UnloadShader(sdf);
  UnloadShader(lit_instanced);
  UnloadShader(depth_instanced);
  UnloadShader(depth);
}

void begin_3d(njin_ctx &ctx, const camera3d &camera) {
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
  s.cmds.clear();
  s.lights.clear();
  s.fx = fx3d{};
  s.material = material3d{};
  s.active = true;
}

void end_3d(njin_ctx &ctx) {
  render3d_state &s = ctx.render3d;
  if (!s.active)
    return;
  rlDrawRenderBatchActive();
  if (ctx.debug.running)
    render3d_capture_debug(ctx);

  bool shadows = false;
  Matrix light_vp = MatrixIdentity();
  if (s.light.shadows && !s.cmds.empty() && ensure_shadow(s.shadow, s.light.shadow_size)) {
    light_vp = render_shadow(ctx);
    shadows = true;
    world_target_rebind(ctx);
    load_camera(ctx, s.camera);
  }

  rlEnableDepthTest();
  set_pass_uniforms(s, s.lit, s.locs, shadows, light_vp);
  set_pass_uniforms(s, s.sdf, s.sdf_locs, shadows, light_vp);
  set_pass_uniforms(s, s.lit_instanced, s.instanced_locs, shadows, light_vp);
  const Matrix view_proj = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
  if (shadows) {
    rlActiveTextureSlot(shadow_unit);
    rlEnableTexture(s.shadow.depth);
    rlActiveTextureSlot(0);
  }
  for (const draw3d_cmd &c : s.cmds)
    draw_main(ctx, c, view_proj);
  if (shadows) {
    rlActiveTextureSlot(shadow_unit);
    rlDisableTexture();
    rlActiveTextureSlot(0);
  }
  particles3d_draw(ctx, s.camera);
  gizmo_draw_3d(ctx, s.camera);

  s.cmds.clear();
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
  return mesh == &s.sphere     ? debug3d_sphere
         : mesh == &s.plane    ? debug3d_plane
         : mesh == &s.cylinder ? debug3d_cylinder
                               : debug3d_cube;
}

// The instances of a draw_instanced3d call, at the position instance0 gives
// them (njin_3d.h). The upload is kept on the CPU only while debugging.
void capture_instances(const njin_ctx &ctx, const draw3d_cmd &c, debug3d_frame &f, usize max_items) {
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

void render3d_capture_debug(njin_ctx &ctx) {
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

void render3d_close(njin_ctx &ctx) {
  if (!ctx.render3d.active)
    return;
  NJIN_WARN("3d: begin_3d without end_3d, closed at the end of phase_render");
  end_3d(ctx);
}

void light3d_set(njin_ctx &ctx, const light3d &light) { ctx.render3d.light = light; }

light3d light3d_get(const njin_ctx &ctx) { return ctx.render3d.light; }

void light3d_add(njin_ctx &ctx, const light3d_source &light) {
  render3d_state &s = ctx.render3d;
  if (!s.active)
    return;
  if ((i32)s.lights.size() >= light3d_max) {
    NJIN_WARN("3d: more than %d lights in one pass, the rest are ignored", light3d_max);
    return;
  }
  s.lights.push_back(light);
}

void material3d_set(njin_ctx &ctx, const material3d &material) { ctx.render3d.material = material; }

void fx3d_set(njin_ctx &ctx, const fx3d &fx) { ctx.render3d.fx = fx; }

void draw_cube3d(const njin_ctx &ctx, vec3 center, vec3 size, rgba color) {
  if (const render3d_state *s = open_pass(ctx))
    record(ctx, &s->cube, {}, scale_then_move(size, center), color);
}

void draw_sphere3d(const njin_ctx &ctx, vec3 center, f32 radius, rgba color) {
  if (const render3d_state *s = open_pass(ctx))
    record(ctx, &s->sphere, {}, scale_then_move({radius, radius, radius}, center), color);
}

void draw_plane3d(const njin_ctx &ctx, vec3 center, vec2 size, rgba color) {
  if (const render3d_state *s = open_pass(ctx))
    record(ctx, &s->plane, {}, scale_then_move({size.x, 1.0f, size.y}, center), color);
}

void draw_cylinder3d(const njin_ctx &ctx, vec3 from, vec3 to, f32 radius, rgba color) {
  const render3d_state *s = open_pass(ctx);
  Matrix m{};
  if (s != nullptr && cylinder_transform(from, to, radius, m))
    record(ctx, &s->cylinder, {}, m, color);
}

void draw_capsule3d(const njin_ctx &ctx, vec3 from, vec3 to, f32 radius, rgba color) {
  if (open_pass(ctx) == nullptr)
    return;
  draw_cylinder3d(ctx, from, to, radius, color);
  draw_sphere3d(ctx, from, radius, color);
  draw_sphere3d(ctx, to, radius, color);
}

namespace {
void record_instanced(const njin_ctx &ctx, const Mesh *mesh, model_handle model, instance_buffer_handle buffer,
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
                              .count = count});
}
} // namespace

void draw_instanced3d(const njin_ctx &ctx, mesh3d_kind mesh, instance_buffer_handle buffer, u32 first, u32 count,
                      shader_handle shader) {
  const render3d_state *s = open_pass(ctx);
  if (s == nullptr || buffer.id == 0)
    return;
  const Mesh *meshes[] = {&s->cube, &s->sphere, &s->plane, &s->cylinder};
  record_instanced(ctx, meshes[std::clamp((i32)mesh, 0, 3)], {}, buffer, first, count, shader);
}

void draw_instanced3d(const njin_ctx &ctx, model_handle model, instance_buffer_handle buffer, u32 first, u32 count,
                      shader_handle shader) {
  if (open_pass(ctx) == nullptr || buffer.id == 0 || model_slot_of(ctx.model, model) == nullptr)
    return;
  record_instanced(ctx, nullptr, model, buffer, first, count, shader);
}

void draw_shape3d(const njin_ctx &ctx, const shape3d &shape, rgba color) {
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
                               .count = 0});
}

void draw_model(const njin_ctx &ctx, model_handle handle, const transform3d &transform, rgba tint) {
  if (open_pass(ctx) == nullptr || model_slot_of(ctx.model, handle) == nullptr)
    return;
  const vec3 r = transform.rotation * (PI / 180.0f);
  Matrix m = MatrixScale(transform.scale.x, transform.scale.y, transform.scale.z);
  m = MatrixMultiply(m, MatrixRotateZ(r.z));
  m = MatrixMultiply(m, MatrixRotateX(r.x));
  m = MatrixMultiply(m, MatrixRotateY(r.y));
  m = MatrixMultiply(m, MatrixTranslate(transform.position.x, transform.position.y, transform.position.z));
  record(ctx, nullptr, handle, m, tint);
}
} // namespace njin
