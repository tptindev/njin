#include "particles.h"
#include "njin2rl.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_scene.h"
#include <algorithm>
#include <raylib.h>
#include <vector>

namespace njin {
namespace {
// Continuous emission after a long stall (breakpoint, window drag) would
// spawn one huge clump; cap what one frame may emit.
constexpr f32 max_emit_per_frame = 1000.0f;

f32 pick(rng &random, f32_range range) {
  return range.max > range.min ? random.range(range.min, range.max) : range.min;
}

void spawn(rng &random, particle_emitter &em, const transform &tr) {
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
  em.particles.push_back(p);
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
    simulate(em, dt);

    for (; em.pending_burst > 0; em.pending_burst--)
      spawn(random, em, tr);

    const bool continuous = em.emitting && em.rate > 0.0f;
    if (continuous) {
      em.emit_accum = std::min(em.emit_accum + em.rate * dt, max_emit_per_frame);
      for (; em.emit_accum >= 1.0f; em.emit_accum -= 1.0f)
        spawn(random, em, tr);
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

void particles_draw(const njin_ctx &ctx, const transform &tr,
                    const particle_emitter &em) {
  if (em.particles.empty())
    return;
  const texture_slot *slot = texture_slot_of(ctx.texture, em.texture);
  vec2 source_size{};
  if (slot != nullptr) {
    const bool whole = em.source.size.x == 0.0f || em.source.size.y == 0.0f;
    source_size = whole ? vec2{(f32)slot->texture.width, (f32)slot->texture.height}
                        : em.source.size;
  }
  const f32 longest = std::max(source_size.x, source_size.y);

  blend_begin(ctx, em.blend);
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
  em.emit_accum = 0.0f;
  em.pending_burst = 0;
  particles_burst(em, count);
  if (const scene_handle scene = scene_current(ctx); scene.id != 0)
    registry.emplace<scene_owned>(entity, scene_owned{scene});
  return entity;
}
} // namespace njin
