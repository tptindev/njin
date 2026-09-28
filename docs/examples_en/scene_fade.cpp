#include <njin.h>

namespace {
njin::scene_handle menu{}, level{};
njin::texture_handle level_art{};

void draw_loading(njin::njin_ctx &ctx) {
  const njin::vec2 screen = njin::screen_size(ctx);
  njin::draw_text(ctx, "Loading...", {screen.x - 220.0f, screen.y - 60.0f}, 32.0f,
                  njin::colors::white);
}

void enter_level(njin::njin_ctx &ctx) {
  // Do the heavy loading here: the player sees the loading screen, not a frozen window.
  level_art = njin::texture_load(ctx, "assets/level1.png");
}

void exit_level(njin::njin_ctx &ctx) { njin::texture_unload(ctx, level_art); }

void startup(njin::njin_ctx &ctx) {
  menu = njin::scene_register(ctx, {.name = "menu"});
  level = njin::scene_register(ctx, {.name = "level", .on_enter = enter_level,
                                     .on_exit = exit_level});
  njin::scene_set(ctx, menu);
}

void menu_input(njin::njin_ctx &ctx) {
  // Ignore keys during a transition, so it cannot be triggered twice.
  if (!njin::scene_transitioning(ctx) && njin::key_pressed(ctx, njin::key_enter))
    njin::scene_fade(ctx, level, {.fade_out = 0.4f, .hold = 0.3f, .fade_in = 0.6f,
                                  .draw_loading = draw_loading});
}

void level_input(njin::njin_ctx &ctx) {
  if (njin::key_pressed(ctx, njin::key_escape))
    njin::scene_fade(ctx, menu, {.color = {1.0f, 1.0f, 1.0f, 1.0f}}); // fade to white
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_update, {.fnc = menu_input, .scene = menu});
  njin::ecs_register(ctx, njin::phase_update, {.fnc = level_input, .scene = level});
}
} // namespace

njin::mod_desc scene_fade_module() { return {.name = "scene_fade", .setup = setup}; }
