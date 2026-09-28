#include <njin.h>

namespace {
// The game's own component: velocity, in world units per second.
struct velocity {
  njin::vec2 value{};
};

void spawn(njin::njin_ctx &ctx) {
  entt::registry &registry = njin::world(ctx);
  const entt::entity entity = registry.create();
  registry.emplace<njin::transform>(entity);
  registry.emplace<velocity>(entity, velocity{.value = {50.0f, 0.0f}});
}

void move(njin::njin_ctx &ctx) {
  const njin::f32 dt = njin::delta(ctx);
  auto view = njin::world(ctx).view<njin::transform, const velocity>();
  for (auto [entity, tr, vel] : view.each()) {
    tr.pos.x += vel.value.x * dt;
    tr.pos.y += vel.value.y * dt;
  }
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, spawn);
  njin::ecs_register(ctx, njin::phase_update, move);
}
} // namespace

njin::mod_desc movement_module() {
  return {.name = "movement", .setup = setup};
}
