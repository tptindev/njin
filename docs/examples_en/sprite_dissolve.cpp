#include <njin.h>

namespace {
njin::texture_handle sheet;
entt::entity enemy = entt::null;

entt::entity spawn_enemy(njin::context &ctx) {
  entt::registry &reg = njin::world(ctx);
  const entt::entity e = reg.create();
  reg.emplace<njin::transform>(e, njin::transform{.pos = {480.0f, 270.0f}, .scale = 6.0f});
  reg.emplace<njin::sprite>(e, njin::sprite{.texture = sheet, .source = {{64.0f, 96.0f}, {32.0f, 32.0f}}});
  return e;
}

void startup(njin::context &ctx) {
  sheet = njin::texture_load(ctx, "assets/sprites.png");
  njin::texture_set_filter(ctx, sheet, njin::filter_nearest);
  enemy = spawn_enemy(ctx);
}

void update(njin::context &ctx) {
  entt::registry &reg = njin::world(ctx);
  if (njin::key_pressed(ctx, njin::key_space) && reg.valid(enemy)) {
    // The enemy dies: flashes white then dissolves away, with an orange burning edge.
    njin::sprite_flash(ctx, enemy, {1.0f, 1.0f, 1.0f, 1.0f}, 0.15f);
    njin::sprite_dissolve(ctx, enemy, 0.8f);
  }
  if (njin::key_pressed(ctx, njin::key_r)) {
    // Fade back in gradually: same component, set `reverse`. When done the component removes itself.
    if (reg.valid(enemy))
      reg.destroy(enemy);
    enemy = spawn_enemy(ctx);
    reg.emplace<njin::dissolve_fx>(enemy, njin::dissolve_fx{.duration = 0.8f, .reverse = true});
  }
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_update, update);
}
} // namespace

int main() {
  njin::context *ctx = njin::create({.title = "Dissolve", .width = 960, .height = 540, .target_fps = 60,
                                           .clear_bg_color = {0.37f, 0.80f, 0.89f, 1.0f}});
  njin::mod_register(*ctx, {.name = "game", .setup = setup});
  njin::run(*ctx);
  njin::destroy(ctx);
}
