#include <njin.h>

namespace {
njin::scene_handle menu, play;

// Runs once when entering the "play" scene: creates the level's world.
void enter_play(njin::njin_ctx &ctx) {
  entt::registry &reg = njin::world(ctx);
  for (int i = 0; i < 10; i++) {
    const entt::entity e = reg.create();
    reg.emplace<njin::transform>(e, njin::transform{.pos = {i * 40.0f, 100}});
    // When leaving the "play" scene the engine destroys this entity automatically.
    reg.emplace<njin::scene_owned>(e, njin::scene_owned{play});
  }
}

void menu_update(njin::njin_ctx &ctx) {
  if (njin::key_pressed(ctx, njin::key_enter))
    njin::scene_set(ctx, play); // switches at the start of the next frame
}

void play_update(njin::njin_ctx &ctx) {
  if (njin::key_pressed(ctx, njin::key_escape))
    njin::scene_set(ctx, menu);
}

void draw_menu(njin::njin_ctx &ctx) {
  njin::draw_text(ctx, "Press Enter", {20, 20}, 30, njin::colors::white);
}

void setup(njin::njin_ctx &ctx) {
  // Register the scenes first, so the handles can be used in sys_desc.
  menu = njin::scene_register(ctx, {.name = "menu"});
  play = njin::scene_register(ctx, {.name = "play", .on_enter = enter_play});

  // Each system only runs in its own scene.
  njin::ecs_register(ctx, njin::phase_update, njin::sys_desc{.fnc = menu_update, .scene = menu});
  njin::ecs_register(ctx, njin::phase_update, njin::sys_desc{.fnc = play_update, .scene = play});
  njin::ecs_register(ctx, njin::phase_post_render, njin::sys_desc{.fnc = draw_menu, .scene = menu});
  njin::ecs_register(ctx, njin::phase_startup,
                     [](njin::njin_ctx &c) { njin::scene_set(c, menu); });
}
} // namespace

njin::mod_desc scenes_module() { return {.name = "scenes", .setup = setup}; }
