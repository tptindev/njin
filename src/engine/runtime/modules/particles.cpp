#include "particles.h"
#include "njin2rl.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_scene.h"
#include "particles_gpu.h"
#include "_collide.h"
#include <algorithm>
#include <cmath>
#include <raylib.h>
#include <rlgl.h>
#include <vector>

namespace njin {
namespace {
// Continuous emission after a long stall (breakpoint, window drag) would
// spawn one huge clump; cap what one frame may emit.
constexpr f32 max_emit_per_frame = 1000.0f;

f32 pick(rng &random, f32_range range) {
  return range.max > range.min ? random.range(range.min, range.max) : range.min;
}

// `birth` is the value for `particle::age` at spawn: 0 on the CPU, where age
// counts up from there, and the emitter clock on the GPU, where it is the
// moment of birth.
void spawn(rng &random, particle_emitter &em, particle_extent &extent, const transform &tr,
           f32 birth) {
  if ((i32)em.particles.size() >= em.max_particles)
    return;
  particle p{};
  const vec2 offset{random.range(-0.5f, 0.5f) * em.area.x,
                    random.range(-0.5f, 0.5f) * em.area.y};
  p.pos = em.local_space ? offset : tr.pos + offset;
  const f32 half = em.spread * 0.5f;
  const f32 angle = em.angle + tr.rot + (half > 0.0f ? random.range(-half, half) : 0.0f);
  p.velocity = from_angle(angle) * pick(random, em.speed);
  p.life = std::max(pick(random, em.life), 0.001f);
  p.spin = pick(random, em.spin);
  // Spinning particles start at a random angle so they do not all line up.
  p.rot = p.spin != 0.0f ? random.range(0.0f, 360.0f) : 0.0f;
  p.size = pick(random, em.size_jitter);
  p.age = birth;
  em.particles.push_back(p);

  // Straight-line travel is an upper bound: drag only shortens it.
  const f32 travel = length(p.velocity) * p.life + 0.5f * length(em.gravity) * p.life * p.life;
  if (!extent.any) {
    extent = particle_extent{.any = true, .lo = p.pos, .hi = p.pos};
  } else {
    extent.lo = {std::min(extent.lo.x, p.pos.x), std::min(extent.lo.y, p.pos.y)};
    extent.hi = {std::max(extent.hi.x, p.pos.x), std::max(extent.hi.y, p.pos.y)};
  }
  extent.reach = std::max(extent.reach, travel);
  extent.life = std::max(extent.life, p.life);
  extent.size = std::max(extent.size, p.size);
}

void simulate(particle_emitter &em, f32 dt) {
  // Exponential decay: independent of frame rate, never reverses direction.
  const f32 keep = em.drag > 0.0f ? std::exp(-em.drag * dt) : 1.0f;
  for (particle &p : em.particles) {
    p.age += dt;
    p.velocity += em.gravity * dt;
    p.velocity *= keep;
    p.pos += p.velocity * dt;
    p.rot += p.spin * dt;
  }
  std::erase_if(em.particles, [](const particle &p) { return p.age >= p.life; });
}

void update(njin_ctx &ctx) {
  const f32 dt = delta(ctx);
  entt::registry &registry = world(ctx);
  rng &random = njin::random(ctx);
  std::vector<entt::entity> done;
  for (auto [entity, tr, em] : registry.view<const transform, particle_emitter>().each()) {
    // The GPU keeps a clock and works out the ages itself (particles_gpu.h).
    if (em.gpu) {
      if (particle_gpu_buffer *buffer = registry.try_get<particle_gpu_buffer>(entity))
        particles_gpu_step(em, *buffer, dt);
    } else {
      simulate(em, dt);
    }
    // Only an emitter with no particles can change where it runs: the two
    // paths keep different state, so particles in flight cannot move across.
    if (em.particles.empty())
      em.gpu = particles_gpu_wanted(ctx);
    f32 birth = 0.0f;
    if (em.gpu) {
      birth = registry.get_or_emplace<particle_gpu_buffer>(entity).clock;
    } else if (registry.all_of<particle_gpu_buffer>(entity)) {
      registry.remove<particle_gpu_buffer>(entity);
    }
    particle_extent &extent = registry.get_or_emplace<particle_extent>(entity);
    if (em.particles.empty())
      extent = particle_extent{};

    for (; em.pending_burst > 0; em.pending_burst--)
      spawn(random, em, extent, tr, birth);

    const bool continuous = em.emitting && em.rate > 0.0f;
    if (continuous) {
      em.emit_accum = std::min(em.emit_accum + em.rate * dt, max_emit_per_frame);
      for (; em.emit_accum >= 1.0f; em.emit_accum -= 1.0f)
        spawn(random, em, extent, tr, birth);
    } else {
      em.emit_accum = 0.0f;
    }

    if (em.destroy_when_done && !continuous && em.particles.empty())
      done.push_back(entity);
  }
  registry.destroy(done.begin(), done.end());
}

void setup(njin_ctx &ctx) { ecs_register(ctx, phase_post_update, update, "update"); }
} // namespace

mod_desc particles_module() {
  return mod_desc{.name = "njin.particles", .setup = setup};
}

bool particles_on_screen(const transform &tr, const particle_emitter &em,
                         const particle_extent &extent, const rect &view) {
  if (!extent.any)
    return true;
  // Settings that change while particles fly (gravity, size) can carry them
  // further than they were spawned to go, so add what the current ones allow.
  const f32 reach = extent.reach + 0.5f * length(em.gravity) * extent.life * extent.life +
                    std::max(em.size_start, em.size_end) * extent.size * 1.5f;
  rect box;
  if (em.local_space) {
    const f32 local = std::hypot(std::max(std::abs(extent.lo.x), std::abs(extent.hi.x)),
                                 std::max(std::abs(extent.lo.y), std::abs(extent.hi.y)));
    const f32 radius = (local + reach) * std::abs(tr.scale);
    box = rect{tr.pos - vec2{radius, radius}, {2.0f * radius, 2.0f * radius}};
  } else {
    box = rect{extent.lo - vec2{reach, reach}, (extent.hi - extent.lo) + vec2{2.0f * reach, 2.0f * reach}};
  }
  return rects_overlap(box, view);
}

void particles_draw(njin_ctx &ctx, entt::entity entity, const transform &tr,
                    const particle_emitter &em) {
  if (em.particles.empty())
    return;
  if (em.gpu) {
    particles_gpu_draw(ctx, entity, tr, em);
    return;
  }
  const texture_slot *slot = texture_slot_of(ctx.texture, em.texture);
  vec2 source_size{};
  if (slot != nullptr) {
    const bool whole = em.source.size.x == 0.0f || em.source.size.y == 0.0f;
    source_size = whole ? vec2{texture_area(*slot).width, texture_area(*slot).height}
                        : em.source.size;
  }
  const f32 longest = std::max(source_size.x, source_size.y);

  blend_begin(ctx, em.blend);
  render_stats &stats = ctx.stats;
  stats.emitters++;
  stats.note_draw(slot != nullptr ? slot->texture.id : rlGetTextureIdDefault(), (i32)em.blend);
  for (const particle &p : em.particles) {
    const f32 t = p.age / p.life;
    const f32 size = lerp(em.size_start, em.size_end, t) * p.size;
    if (size <= 0.0f)
      continue;
    const rgba tint = lerp(em.color_start, em.color_end, t);
    if (tint.a <= 0.0f)
      continue;
    Color color{};
    to_raylib(tint, color);
    const vec2 pos = em.local_space ? transform_combine(tr, {.pos = p.pos}).pos : p.pos;
    stats.particles++;
    if (slot != nullptr && longest > 0.0f) {
      const f32 scale = size / longest;
      texture_store_draw_ex(ctx.texture, em.texture,
                            texture_draw_desc{.pos = pos,
                                              .source = em.source,
                                              .scale = {scale, scale},
                                              .origin = {0.5f, 0.5f},
                                              .rotation = p.rot,
                                              .tint = tint});
    } else if (em.shape == particle_square) {
      DrawRectanglePro(Rectangle{pos.x, pos.y, size, size},
                       Vector2{size * 0.5f, size * 0.5f}, p.rot, color);
    } else {
      DrawCircleV(Vector2{pos.x, pos.y}, size * 0.5f, color);
    }
  }
  blend_end(ctx);
}

entt::entity particles_spawn(njin_ctx &ctx, const particle_emitter &preset,
                             vec2 pos, i32 count) {
  entt::registry &registry = world(ctx);
  const entt::entity entity = registry.create();
  registry.emplace<transform>(entity, transform{.pos = pos});
  particle_emitter &em = registry.emplace<particle_emitter>(entity, preset);
  em.emitting = false;
  em.destroy_when_done = true;
  em.particles.clear();
  em.gpu = false;
  em.emit_accum = 0.0f;
  em.pending_burst = 0;
  particles_burst(em, count);
  if (const scene_handle scene = scene_current(ctx); scene.id != 0)
    registry.emplace<scene_owned>(entity, scene_owned{scene});
  return entity;
}
} // namespace njin
