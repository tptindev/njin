#include "particles3d.h"
#include "particles_gpu.h"
#include "render3d.h"
#include "njin2rl.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "render_stats.h"
#include "njin_quality_impl.h"
#include "njin_draw_shapes.h"
#include "njin_render.h"
#include "njin_texture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>

namespace njin {
namespace {
// Bounds what one careless spawn call can cost.
constexpr usize max_particles = 16384;

f32 pick(rng &random, f32_range range) {
  return range.max > range.min ? random.range(range.min, range.max) : range.min;
}

// Any two unit vectors that, with `axis`, make a right-handed basis.
void basis(vec3 axis, vec3 &u, vec3 &v) {
  const vec3 helper = std::fabs(axis.y) < 0.99f ? vec3{0.0f, 1.0f, 0.0f} : vec3{1.0f, 0.0f, 0.0f};
  u = normalize(cross(helper, axis));
  v = cross(axis, u);
}

// A unit vector inside the cone of half angle `half` (radians) around `axis`,
// uniform over the cap of the sphere it cuts.
vec3 in_cone(rng &random, vec3 axis, f32 half) {
  const f32 z = random.range(std::cos(half), 1.0f);
  const f32 a = random.range(0.0f, 2.0f * pi);
  const f32 r = std::sqrt(std::max(0.0f, 1.0f - z * z));
  const vec3 local{r * std::cos(a), r * std::sin(a), z};
  // Any basis with `axis` as its third vector.
  vec3 u, v;
  basis(axis, u, v);
  return u * local.x + v * local.y + axis * local.z;
}

usize live_count(const particles3d_state &s) {
  usize n = 0;
  for (const particle3d_burst &b : s.bursts)
    n += b.particles.size();
  return n;
}

void update(context &ctx) {
  const f32 dt = delta(ctx);
  if (dt <= 0.0f)
    return;
  for (particle3d_burst &b : ctx_ref<particles3d_state>(ctx)->bursts) {
    const particle_emitter &em = b.look;
    const vec3 gravity{em.gravity.x * b.scale, -em.gravity.y * b.scale, 0.0f};
    // Same exponential drag as the 2D particles.
    const f32 keep = em.drag > 0.0f ? std::exp(-em.drag * dt) : 1.0f;
    for (particle3d &p : b.particles) {
      p.age += dt;
      p.velocity += gravity * dt;
      p.velocity *= keep;
      p.pos += p.velocity * dt;
      p.rotation += p.spin * dt;
    }
    std::erase_if(b.particles, [](const particle3d &p) { return p.age >= p.life; });
  }
  std::erase_if(ctx_ref<particles3d_state>(ctx)->bursts, [](const particle3d_burst &b) { return b.particles.empty(); });
}

void setup(context &ctx) { ecs_register(ctx, phase_post_update, update, "update"); }

// A white disc with a one-pixel soft edge, so small particles stay round.
Texture2D make_disc() {
  constexpr i32 size = 64;
  Image image = GenImageColor(size, size, BLANK);
  const f32 c = (f32)size * 0.5f;
  for (i32 y = 0; y < size; y++) {
    for (i32 x = 0; x < size; x++) {
      const f32 d = std::hypot((f32)x + 0.5f - c, (f32)y + 0.5f - c);
      const f32 a = clamp(c - d, 0.0f, 1.0f);
      ImageDrawPixel(&image, x, y, Color{255, 255, 255, (unsigned char)(a * 255.0f)});
    }
  }
  Texture2D texture = LoadTextureFromImage(image);
  UnloadImage(image);
  SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
  return texture;
}
} // namespace

mod_desc particles3d_module() { return mod_desc{.name = "njin.particles3d", .setup = setup}; }

particles3d_state::~particles3d_state() {
  if (IsTextureValid(disc))
    UnloadTexture(disc);
}

namespace {
// A spot of `area` and the unit direction from its centre out to it (zero at
// the centre and for a point).
vec3 area_spot(rng &random, const particle3d_area &area, vec3 &out) {
  out = {};
  if (area.kind == particle3d_area_point || area.radius <= 0.0f)
    return area.center;
  const vec3 normal = length_sq(area.normal) > 0.0f ? normalize(area.normal) : vec3{0.0f, 1.0f, 0.0f};
  vec3 u, v;
  basis(normal, u, v);
  const f32 t = random.range(0.0f, 2.0f * pi);
  // Uniform over the disc: the share of it within d grows as d squared.
  const f32 d = area.kind == particle3d_area_ring ? area.radius : area.radius * std::sqrt(random.range(0.0f, 1.0f));
  out = u * std::cos(t) + v * std::sin(t);
  return area.center + out * d;
}

// Adds a burst of `n` particles of `emitter`, the i-th spawned where `place`
// says (position and the axis of its cone of flight). `n` is already cut to
// the quality share.
template <class Place>
void add_burst(context &ctx, const particle_emitter &emitter, usize n, const particles3d_desc &desc, Place place) {
  particles3d_state &s = ctx_ref<particles3d_state>(ctx);
  const usize room = max_particles - std::min(max_particles, live_count(s));
  n = std::min(room, n);
  if (n == 0)
    return;
  particle3d_burst burst;
  burst.look = emitter;
  burst.look.particles.clear();
  burst.scale = desc.scale;
  burst.stretch = std::max(desc.stretch, 0.0f);
  burst.shader = desc.shader;
  burst.style = desc.style;
  burst.particles.reserve(n);
  rng &random = njin::random(ctx);
  const f32 half = clamp(emitter.spread, 0.0f, 360.0f) * 0.5f * (pi / 180.0f);
  const vec3 area{emitter.area.x * desc.scale, emitter.area.y * desc.scale, emitter.area.x * desc.scale};
  for (usize i = 0; i < n; i++) {
    vec3 pos, axis;
    place(random, pos, axis);
    particle3d p;
    p.pos = pos + vec3{random.range(-0.5f, 0.5f) * area.x, random.range(-0.5f, 0.5f) * area.y,
                       random.range(-0.5f, 0.5f) * area.z};
    p.velocity = in_cone(random, axis, half) * (pick(random, emitter.speed) * desc.scale);
    p.life = std::max(pick(random, emitter.life), 0.001f);
    p.spin = pick(random, emitter.spin);
    p.rotation = p.spin != 0.0f ? random.range(0.0f, 360.0f) : 0.0f;
    p.size = pick(random, emitter.size_jitter);
    burst.particles.push_back(p);
  }
  s.bursts.push_back(std::move(burst));
}

vec3 axis_of(const particles3d_desc &desc) {
  return length_sq(desc.direction) > 0.0f ? normalize(desc.direction) : vec3{0.0f, 1.0f, 0.0f};
}
} // namespace

void particles3d_spawn(context &ctx, const particle_emitter &emitter, vec3 pos, i32 count,
                       const particles3d_desc &desc) {
  // graphics_quality::particles keeps that share of the burst, at least one
  // particle of a burst that asked for some (unless the share is 0).
  const f32 share = std::clamp(ctx_ref<quality_state>(ctx)->q.particles, 0.0f, 1.0f);
  if (share < 1.0f && count > 0)
    count = share > 0.0f ? std::max(1, (i32)std::lround((f32)count * share)) : 0;
  const vec3 axis = axis_of(desc);
  add_burst(ctx, emitter, (usize)std::max(count, 0), desc, [&](rng &, vec3 &at, vec3 &dir) {
    at = pos;
    dir = axis;
  });
}

i32 particles3d_due(context &ctx, particle_emitter &emitter) {
  if (!emitter.emitting || emitter.rate <= 0.0f) {
    emitter.emit_accum = 0.0f;
    return 0;
  }
  // graphics_quality::particles thins the stream itself: a share of every
  // frame's particles would round back up to one each frame.
  const f32 share = std::clamp(ctx_ref<quality_state>(ctx)->q.particles, 0.0f, 1.0f);
  emitter.emit_accum += emitter.rate * share * delta(ctx);
  // A long stall (breakpoint, window drag) would spawn one huge clump.
  emitter.emit_accum = std::min(emitter.emit_accum, (f32)max_particles);
  const i32 n = (i32)emitter.emit_accum;
  emitter.emit_accum -= (f32)n;
  return n;
}

void particles3d_emit(context &ctx, particle_emitter &emitter, const particle3d_area &area,
                      const particles3d_desc &desc) {
  const i32 n = particles3d_due(ctx, emitter);
  if (n <= 0)
    return;
  const vec3 axis = axis_of(desc);
  add_burst(ctx, emitter, (usize)n, desc, [&](rng &random, vec3 &at, vec3 &dir) {
    vec3 out;
    at = area_spot(random, area, out);
    dir = area.outward != 0.0f && length_sq(out) > 0.0f ? normalize(axis + out * area.outward) : axis;
  });
}

vec3 particles3d_area_point(context &ctx, const particle3d_area &area) {
  vec3 out;
  return area_spot(njin::random(ctx), area, out);
}

void particles3d_clear(context &ctx) { ctx_ref<particles3d_state>(ctx)->bursts.clear(); }

i32 particles3d_count(const context &ctx) { return (i32)live_count(ctx_ref<particles3d_state>(ctx)); }

void particles3d_draw(context &ctx, const camera3d &view) {
  particles3d_state &s = ctx_ref<particles3d_state>(ctx);
  if (s.bursts.empty())
    return;
  if (!IsTextureValid(s.disc))
    s.disc = make_disc();
  Camera3D camera{};
  to_raylib(view.position, camera.position);
  to_raylib(view.target, camera.target);
  to_raylib(view.up, camera.up);
  camera.fovy = view.fovy;
  camera.projection = CAMERA_PERSPECTIVE;
  const Texture2D white{.id = rlGetTextureIdDefault(), .width = 1, .height = 1, .mipmaps = 1,
                        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};

  // Tested against the depth of the solid shapes, but not written: particles
  // do not hide each other, so their order does not matter.
  rlDrawRenderBatchActive();
  rlDisableDepthMask();
  render_stats &stats = ctx_ref<render_stats>(ctx);
  for (const particle3d_burst &b : s.bursts) {
    const particle_emitter &em = b.look;
    const texture_slot *slot = texture_slot_of(ctx_ref<texture_store>(ctx), em.texture);
    Texture2D texture = em.shape == particle_square ? white
                        : em.shape == particle_soft ? particle_soft_texture(ctx)
                                                    : s.disc;
    Rectangle source{0.0f, 0.0f, (f32)texture.width, (f32)texture.height};
    if (slot != nullptr) {
      texture = slot->texture;
      const Rectangle area = texture_area(*slot);
      const bool whole = em.source.size.x == 0.0f || em.source.size.y == 0.0f;
      source = whole ? area
                     : Rectangle{area.x + em.source.pos.x, area.y + em.source.pos.y, em.source.size.x,
                                 em.source.size.y};
    }
    const f32 longest = std::max(source.width, source.height);
    const f32 u0 = source.x / (f32)texture.width, u1 = (source.x + source.width) / (f32)texture.width;
    const f32 v0 = source.y / (f32)texture.height, v1 = (source.y + source.height) / (f32)texture.height;
    blend_begin(ctx, em.blend);
    // The game's shader, or the look of the burst's style.
    bool look = false;
    if (b.shader.id != 0) {
      rlDrawRenderBatchActive();
      shader_begin(ctx, b.shader);
    } else if (b.style != particle3d_flat) {
      rlDrawRenderBatchActive();
      look = particle3d_look_begin(ctx, s.looks, b.style);
    }
    stats.emitters++;
    // Streaks are seen from either side (which way round their corners go
    // depends on the flight): culling is off for the whole burst, not toggled
    // round each one, which flushed the batch once per particle.
    if (b.stretch > 0.0f) {
      rlDrawRenderBatchActive();
      rlDisableBackfaceCulling();
    }
    const std::array<vec4, 6> &frustum = ctx_ref<render3d_state>(ctx)->frustum;
    for (const particle3d &p : b.particles) {
      const f32 t = p.age / p.life;
      const f32 size = lerp(em.size_start, em.size_end, t) * p.size * b.scale;
      const rgba tint = lerp(em.color_start, em.color_end, t);
      if (size <= 0.0f || tint.a <= 0.0f)
        continue;
      // Off camera: a sphere round the particle (its streak included) wholly
      // outside a plane of the pass's frustum.
      const f32 reach = size + b.stretch * length(p.velocity);
      bool outside = false;
      for (const vec4 &pl : frustum)
        outside = outside || pl.x * p.pos.x + pl.y * p.pos.y + pl.z * p.pos.z + pl.w < -reach;
      if (outside)
        continue;
      Color color{};
      to_raylib(tint, color);
      // The longer side of the image is the particle's size.
      const Vector2 extent{size * source.width / longest, size * source.height / longest};
      stats.particles++;
      if (b.stretch > 0.0f && length_sq(p.velocity) > 1e-8f) {
        // A streak: along the flight as the camera sees it, from where the
        // particle was `stretch` seconds ago to a little past where it is.
        const vec3 eye = view.projection == camera3d_orthographic ? normalize(view.target - view.position)
                                                                  : normalize(p.pos - view.position);
        vec3 along = p.velocity - eye * dot(p.velocity, eye);
        const f32 run = length(along);
        if (run > 1e-6f) {
          along = along * (1.0f / run);
          const vec3 side = normalize(cross(along, eye)) * (extent.x * 0.5f);
          const vec3 head = p.pos + along * (extent.y * 0.5f);
          const vec3 tail = p.pos - along * (extent.y * 0.5f + run * b.stretch);
          rlSetTexture(texture.id);
          rlBegin(RL_QUADS);
          rlColor4ub(color.r, color.g, color.b, color.a);
          rlTexCoord2f(u0, v1);
          rlVertex3f(tail.x - side.x, tail.y - side.y, tail.z - side.z);
          rlTexCoord2f(u1, v1);
          rlVertex3f(tail.x + side.x, tail.y + side.y, tail.z + side.z);
          rlTexCoord2f(u1, v0);
          rlVertex3f(head.x + side.x, head.y + side.y, head.z + side.z);
          rlTexCoord2f(u0, v0);
          rlVertex3f(head.x - side.x, head.y - side.y, head.z - side.z);
          rlEnd();
          rlSetTexture(0);
          continue;
        }
      }
      Vector3 pos{};
      to_raylib(p.pos, pos);
      DrawBillboardPro(camera, texture, source, pos, camera.up, extent,
                       Vector2{extent.x * 0.5f, extent.y * 0.5f}, p.rotation, color);
    }
    if (b.stretch > 0.0f) {
      rlDrawRenderBatchActive();
      rlEnableBackfaceCulling();
    }
    if (b.shader.id != 0) {
      rlDrawRenderBatchActive();
      shader_end(ctx);
    } else if (look) {
      rlDrawRenderBatchActive();
      EndShaderMode();
    }
    blend_end(ctx);
  }
  rlDrawRenderBatchActive();
  rlEnableDepthMask();
}
} // namespace njin
