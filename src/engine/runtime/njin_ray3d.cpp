// 3D picking (njin_3d.h): rays from the camera, the camera's projection of a
// point, and ray tests against boxes, spheres, planes, SDF shapes and models.
// Boxes, spheres and model triangles use raylib's tests; SDF shapes are
// sphere-traced with the same distance functions the SDF shader draws with.
#include "njin_3d.h"
#include "modules/render3d.h"
#include "njin2rl.h"
#include "njin_cfg.h"
#include "njin_ctx_impl.h"
#include "njin_model.h"
#include <algorithm>
#include <cmath>
#include <raymath.h>

namespace njin {
namespace {
Vector3 rl(vec3 v) { return Vector3{v.x, v.y, v.z}; }
vec3 nj(Vector3 v) { return vec3{v.x, v.y, v.z}; }

ray3d_hit from_raylib(const RayCollision &c) {
  if (!c.hit)
    return ray3d_hit{};
  return ray3d_hit{.hit = true, .distance = c.distance, .point = nj(c.point), .normal = nj(c.normal)};
}

Ray to_ray(const ray3d &r) { return Ray{rl(r.origin), rl(normalize(r.direction))}; }

// The camera's basis: forward, right, up.
void basis(const camera3d &c, vec3 &f, vec3 &r, vec3 &u) {
  f = normalize(c.target - c.position);
  r = normalize(cross(f, c.up));
  u = cross(r, f);
}

// The SDF shader's distance functions, in shape space (render3d.cpp).
f32 sdf(i32 kind, const vec4 &d, vec3 p) {
  switch (kind) {
  case shape3d_box: {
    const vec3 q{std::fabs(p.x) - d.x + d.w, std::fabs(p.y) - d.y + d.w, std::fabs(p.z) - d.z + d.w};
    const vec3 out{std::max(q.x, 0.0f), std::max(q.y, 0.0f), std::max(q.z, 0.0f)};
    return length(out) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f) - d.w;
  }
  case shape3d_capsule:
    p.y -= clamp(p.y, -d.y, d.y);
    return length(p) - d.x;
  case shape3d_cylinder: {
    const vec2 q{std::sqrt(p.x * p.x + p.z * p.z) - d.x + d.w, std::fabs(p.y) - d.y + d.w};
    return std::min(std::max(q.x, q.y), 0.0f) + length(vec2{std::max(q.x, 0.0f), std::max(q.y, 0.0f)}) - d.w;
  }
  case shape3d_torus: {
    const vec2 q{std::sqrt(p.x * p.x + p.z * p.z) - d.x, p.y};
    return length(q) - d.y;
  }
  case shape3d_sphere:
  default:
    return length(p) - d.x;
  }
}
} // namespace

ray3d camera3d_ray(const njin_ctx &ctx, const camera3d &camera, vec2 screen) {
  const vec2 size = screen_size(ctx);
  vec3 f, r, u;
  basis(camera, f, r, u);
  const f32 h = std::tan(camera.fovy * 0.5f * DEG2RAD);
  const f32 aspect = size.y > 0.0f ? size.x / size.y : 1.0f;
  const f32 x = size.x > 0.0f ? 2.0f * screen.x / size.x - 1.0f : 0.0f;
  const f32 y = size.y > 0.0f ? 1.0f - 2.0f * screen.y / size.y : 0.0f;
  return ray3d{.origin = camera.position, .direction = normalize(f + r * (x * h * aspect) + u * (y * h))};
}

vec2 camera3d_to_screen(const njin_ctx &ctx, const camera3d &camera, vec3 point, bool *visible) {
  const vec2 size = screen_size(ctx);
  vec3 f, r, u;
  basis(camera, f, r, u);
  const vec3 d = point - camera.position;
  const f32 depth = dot(d, f);
  if (visible != nullptr)
    *visible = depth > 0.0f;
  if (depth <= 0.0f)
    return vec2{0.0f, 0.0f};
  const f32 h = std::tan(camera.fovy * 0.5f * DEG2RAD);
  const f32 aspect = size.y > 0.0f ? size.x / size.y : 1.0f;
  const f32 x = dot(d, r) / (depth * h * aspect);
  const f32 y = dot(d, u) / (depth * h);
  return vec2{(x + 1.0f) * 0.5f * size.x, (1.0f - y) * 0.5f * size.y};
}

ray3d_hit ray3d_box(const ray3d &ray, vec3 center, vec3 size) {
  const vec3 h = size * 0.5f;
  return from_raylib(GetRayCollisionBox(to_ray(ray), BoundingBox{rl(center - h), rl(center + h)}));
}

ray3d_hit ray3d_sphere(const ray3d &ray, vec3 center, f32 radius) {
  return from_raylib(GetRayCollisionSphere(to_ray(ray), rl(center), radius));
}

ray3d_hit ray3d_plane(const ray3d &ray, vec3 point, vec3 normal) {
  const vec3 n = normalize(normal);
  const vec3 d = normalize(ray.direction);
  const f32 denom = dot(n, d);
  if (std::fabs(denom) < 1e-8f)
    return ray3d_hit{};
  const f32 t = dot(point - ray.origin, n) / denom;
  if (t < 0.0f)
    return ray3d_hit{};
  return ray3d_hit{.hit = true, .distance = t, .point = ray.origin + d * t, .normal = denom < 0.0f ? n : -n};
}

ray3d_hit ray3d_shape(const ray3d &ray, const shape3d &shape) {
  const shape_frame f = shape3d_frame(shape);
  const Matrix to_local = MatrixInvert(f.to_world);
  const vec3 dir = normalize(ray.direction);
  const vec3 ro = nj(Vector3Transform(rl(ray.origin), to_local));
  // Directions turn but do not move: the rotation part only.
  const vec3 rd = normalize(nj(Vector3Subtract(Vector3Transform(rl(dir), to_local),
                                               Vector3Transform(Vector3{0, 0, 0}, to_local))));
  // Where the ray is inside the shape's box, as the shader does.
  const vec3 b = f.bounds * 1.02f + vec3{1e-3f, 1e-3f, 1e-3f};
  f32 t0 = 0.0f, t1 = 1e30f;
  for (f32 vec3::*axis : {&vec3::x, &vec3::y, &vec3::z}) {
    const f32 o = ro.*axis, d = rd.*axis, lo = -(b.*axis), hi = b.*axis;
    if (std::fabs(d) < 1e-9f) {
      if (o < lo || o > hi)
        return ray3d_hit{};
      continue;
    }
    f32 a = (lo - o) / d, c = (hi - o) / d;
    if (a > c)
      std::swap(a, c);
    t0 = std::max(t0, a);
    t1 = std::min(t1, c);
    if (t0 > t1)
      return ray3d_hit{};
  }
  const i32 kind = (i32)shape.kind;
  const f32 eps = 0.0004f * length(f.bounds);
  f32 t = t0;
  for (i32 i = 0; i < 128 && t <= t1; i++) {
    const vec3 p = ro + rd * t;
    const f32 d = sdf(kind, f.dims, p);
    if (d < eps) {
      // The gradient of the distance is the normal.
      const f32 e = std::max(eps, 1e-4f);
      const vec3 n{sdf(kind, f.dims, p + vec3{e, 0, 0}) - sdf(kind, f.dims, p - vec3{e, 0, 0}),
                   sdf(kind, f.dims, p + vec3{0, e, 0}) - sdf(kind, f.dims, p - vec3{0, e, 0}),
                   sdf(kind, f.dims, p + vec3{0, 0, e}) - sdf(kind, f.dims, p - vec3{0, 0, e})};
      const vec3 world = nj(Vector3Transform(rl(p), f.to_world));
      const vec3 turned = nj(Vector3Subtract(Vector3Transform(rl(normalize(n)), f.to_world),
                                             Vector3Transform(Vector3{0, 0, 0}, f.to_world)));
      return ray3d_hit{
          .hit = true, .distance = distance(ray.origin, world), .point = world, .normal = normalize(turned)};
    }
    t += d;
  }
  return ray3d_hit{};
}

ray3d_hit ray3d_model(const njin_ctx &ctx, const ray3d &ray, model_handle handle, const transform3d &transform) {
  const model_slot *slot = model_slot_of(ctx.model, handle);
  if (slot == nullptr)
    return ray3d_hit{};
  // The same matrix draw_model builds.
  const vec3 r = transform.rotation * (PI / 180.0f);
  Matrix m = MatrixScale(transform.scale.x, transform.scale.y, transform.scale.z);
  m = MatrixMultiply(m, MatrixRotateZ(r.z));
  m = MatrixMultiply(m, MatrixRotateX(r.x));
  m = MatrixMultiply(m, MatrixRotateY(r.y));
  m = MatrixMultiply(m, MatrixTranslate(transform.position.x, transform.position.y, transform.position.z));
  m = MatrixMultiply(slot->model.transform, m);
  const Ray rr = to_ray(ray);
  RayCollision best{};
  best.distance = 1e30f;
  for (i32 i = 0; i < slot->model.meshCount; i++) {
    const RayCollision c = GetRayCollisionMesh(rr, slot->model.meshes[i], m);
    if (c.hit && c.distance < best.distance)
      best = c;
  }
  return best.hit ? from_raylib(best) : ray3d_hit{};
}
} // namespace njin
