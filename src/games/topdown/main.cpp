// Old Stone Forest: a small top-down adventure built on njin. It shows what a
// top-down game needs: a Tiled map with animated water, an 8-way body with a
// dash, a sword, enemies that chase by A* around walls and trees (and lose you
// out of sight), things sorted by y so the hero walks behind trees, a camera
// that follows and clamps, dialogue, and settings with rebinding.
#include "../shared/settings_menu.h"
#include "game.h"

namespace td {
game_state g;

namespace {
void startup(context &ctx) {
  g.sprites = texture_load(ctx, "assets/sprites.png");
  texture_set_filter(ctx, g.sprites, filter_nearest);
  g.s_swing = sound_load(ctx, "assets/sounds/swing.wav");
  g.s_hit = sound_load(ctx, "assets/sounds/hit.wav");
  g.s_hurt = sound_load(ctx, "assets/sounds/hurt.wav");
  g.s_dash = sound_load(ctx, "assets/sounds/dash.wav");
  g.s_blip = sound_load(ctx, "assets/sounds/blip.wav");
  g.s_select = sound_load(ctx, "assets/sounds/select.wav");
  g.s_win = sound_load(ctx, "assets/sounds/win.wav");
  for (const sound_handle s : {g.s_blip, g.s_select})
    sound_set_bus(ctx, s, bus_ui);
  g.m_forest = music_load(ctx, "assets/music/forest.wav");
  music_set_volume(ctx, g.m_forest, 0.7f);

  i18n_load(ctx, "vi", "assets/lang/vi.json");
  i18n_load(ctx, "en", "assets/lang/en.json");

  g.move_x = axis_define(ctx, "move_x", {{key_left, key_right}, {key_a, key_d}}, {pad_axis_left_x});
  g.move_y = axis_define(ctx, "move_y", {{key_up, key_down}, {key_w, key_s}}, {pad_axis_left_y});
  g.attack = action_define(ctx, "attack", {key_j, key_space, pad_face_down});
  g.dash = action_define(ctx, "dash", {key_left_shift, key_k, pad_face_right});
  g.interact = action_define(ctx, "interact", {key_e, pad_face_left});
  g.pause = action_define(ctx, "pause", {key_p, pad_start});
  settings_load(ctx);

  shared::apply_style(ctx, "assets/fonts/BeVietnamPro-Bold.ttf");
  dialog_load("assets/dialog/elder.json", g.elder);
  dialog_portrait(ctx, "elder", g.sprites, rect{{64.0f, 96.0f}, {32.0f, 32.0f}});

  // Everything standing on the map is drawn in y order.
  draw_set_y_sort(ctx, draw_things, true);
  register_prefabs(ctx);
  scene_set(ctx, g.title);
}
} // namespace
} // namespace td

int main() {
  using namespace njin;
  using namespace td;
  context *ctx = create({.title = "Old Stone Forest",
                               .width = 1280,
                               .height = 720,
                               .target_fps = 60,
                               .clear_bg_color = {0.23f, 0.49f, 0.27f, 1.0f},
                               .exit_key = key_none,
                               .resizable = true,
                               .app_name = "OldStoneForest",
                               .virtual_size = {640.0f, 360.0f},
                               .integer_scale = false,
                               // The world is pixels; the UI is drawn smooth, at the window's
                               // resolution.
                               .smooth_ui = true});
  g.title = scene_register(*ctx, {.name = "title", .on_enter = title_enter});
  g.play = scene_register(*ctx, {.name = "play", .on_enter = play_enter, .on_exit = play_exit});
  g.win = scene_register(*ctx, {.name = "win", .on_enter = end_enter});
  g.over = scene_register(*ctx, {.name = "over", .on_enter = end_enter});
  mod_register(*ctx, {.name = "td.startup", .setup = [](context &c) {
                             ecs_register(c, phase_startup, startup, "startup");
                           }});
  mod_register(*ctx, {play_module(), menus_module()});
#ifndef NDEBUG
  debug_server_start(*ctx);
#endif
  run(*ctx);
  destroy(ctx);
}
