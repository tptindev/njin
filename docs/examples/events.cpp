#include <njin.h>

namespace {
// Event là một struct bất kỳ.
struct hit {
  entt::entity target;
  njin::f32 damage;
};

// Người nhận: một hàm thường nhận event.
void on_hit(const hit &event) {
  NJIN_INFO("entity %u nhận %.1f sát thương", (unsigned)event.target,
            event.damage);
}

void deal_damage(njin::njin_ctx &ctx) {
  entt::registry &registry = njin::world(ctx);
  auto view = registry.view<njin::transform>();
  for (const entt::entity entity : view) {
    // Xếp hàng: on_hit chạy ngay sau phase_post_update của frame này.
    njin::events(ctx).enqueue<hit>(entity, 10.0f);
  }
}

void setup(njin::njin_ctx &ctx) {
  njin::events(ctx).sink<hit>().connect<&on_hit>();
  njin::ecs_register(ctx, njin::phase_update, deal_damage);
}
} // namespace

njin::mod_desc combat_module() {
  return {.name = "combat", .setup = setup};
}
