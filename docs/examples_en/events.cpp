#include <njin.h>

namespace {
// An event is any struct.
struct hit {
  entt::entity target;
  njin::f32 damage;
};

// The receiver: an ordinary function that takes the event.
void on_hit(const hit &event) {
  NJIN_INFO("entity %u took %.1f damage", (unsigned)event.target,
            event.damage);
}

void deal_damage(njin::context &ctx) {
  entt::registry &registry = njin::world(ctx);
  auto view = registry.view<njin::transform>();
  for (const entt::entity entity : view) {
    // Queued: on_hit runs right after this frame's phase_post_update.
    njin::events(ctx).enqueue<hit>(entity, 10.0f);
  }
}

void setup(njin::context &ctx) {
  njin::events(ctx).sink<hit>().connect<&on_hit>();
  njin::ecs_register(ctx, njin::phase_update, deal_damage);
}
} // namespace

njin::mod_desc combat_module() {
  return {.name = "combat", .setup = setup};
}
