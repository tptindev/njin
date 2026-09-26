#include <njin.h>

namespace {
njin::texture_handle sheet;
entt::entity enemy = entt::null;

entt::entity spawn_enemy(njin::njin_ctx &ctx) {
  entt::registry &reg = njin::world(ctx);
  const entt::entity e = reg.create();
  reg.emplace<njin::transform>(e, njin::transform{.pos = {480.0f, 270.0f}, .scale = 6.0f});
  reg.emplace<njin::sprite>(e, njin::sprite{.texture = sheet, .source = {{64.0f, 96.0f}, {32.0f, 32.0f}}});
  return e;
}

void startup(njin::njin_ctx &ctx) {
  sheet = njin::texture_load(ctx, "assets/sprites.png");
  njin::texture_set_filter(ctx, sheet, njin::filter_nearest);
  enemy = spawn_enemy(ctx);
}

void update(njin::njin_ctx &ctx) {
  entt::registry &reg = njin::world(ctx);
  if (njin::key_pressed(ctx, njin::key_space) && reg.valid(enemy)) {
    // Kẻ địch chết: nháy trắng rồi tan biến, viền cháy màu cam.
    njin::sprite_flash(ctx, enemy, {1.0f, 1.0f, 1.0f, 1.0f}, 0.15f);
    njin::sprite_dissolve(ctx, enemy, 0.8f);
  }
  if (njin::key_pressed(ctx, njin::key_r)) {
    // Hiện lại dần: cùng component, đặt `reverse`. Xong thì component tự được gỡ.
    if (reg.valid(enemy))
      reg.destroy(enemy);
    enemy = spawn_enemy(ctx);
    reg.emplace<njin::dissolve_fx>(enemy, njin::dissolve_fx{.duration = 0.8f, .reverse = true});
  }
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_update, update);
}
} // namespace

int main() {
  njin::njin_ctx *ctx = njin::njin_create({.title = "Tan biến", .width = 960, .height = 540, .target_fps = 60,
                                           .clear_bg_color = {0.37f, 0.80f, 0.89f, 1.0f}});
  njin::njin_mod_register(*ctx, {.name = "game", .setup = setup});
  njin::njin_run(*ctx);
  njin::njin_destroy(ctx);
}
