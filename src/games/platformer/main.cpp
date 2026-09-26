// Sprout's Climb: a small platformer built on njin. It shows the pieces a
// platformer needs: Tiled levels with slopes, one-way ledges and moving
// platforms, a body controller with coyote time and wall jumps, a camera that
// follows and clamps to the level, a 320 x 180 virtual screen, dialogue,
// settings with rebinding, two languages, and a packaged release build.
#include "game.h"
#include "../shared/settings_menu.h"

namespace plat {
game_state g;

namespace {
void startup(njin_ctx &ctx) {
  g.sprites = texture_load(ctx, "assets/sprites.png");
  texture_set_filter(ctx, g.sprites, filter_nearest);
  g.s_jump = sound_load(ctx, "assets/sounds/jump.wav");
  g.s_coin = sound_load(ctx, "assets/sounds/coin.wav");
  g.s_stomp = sound_load(ctx, "assets/sounds/stomp.wav");
  g.s_hurt = sound_load(ctx, "assets/sounds/hurt.wav");
  g.s_land = sound_load(ctx, "assets/sounds/land.wav");
  g.s_check = sound_load(ctx, "assets/sounds/checkpoint.wav");
  g.s_blip = sound_load(ctx, "assets/sounds/blip.wav");
  g.s_select = sound_load(ctx, "assets/sounds/select.wav");
  g.s_win = sound_load(ctx, "assets/sounds/win.wav");
  for (const sound_handle s : {g.s_blip, g.s_select})
    sound_set_bus(ctx, s, bus_ui);
  g.m_title = music_load(ctx, "assets/music/title.wav");
  g.m_level = music_load(ctx, "assets/music/level.wav");
  music_set_volume(ctx, g.m_title, 0.7f);
  music_set_volume(ctx, g.m_level, 0.7f);

  // Strings, before anything reads them.
  i18n_load(ctx, "vi", "assets/lang/vi.json");
  i18n_load(ctx, "en", "assets/lang/en.json");

  // Controls. Defaults first; the player's own come from settings.json below.
  g.move = axis_register(ctx, "move");
  axis_bind_keys(ctx, g.move, key_left, key_right);
  axis_bind_keys(ctx, g.move, key_a, key_d);
  axis_bind_pad(ctx, g.move, pad_axis_left_x);
  g.jump = action_register(ctx, "jump");
  action_bind_key(ctx, g.jump, key_space);
  action_bind_key(ctx, g.jump, key_w);
  action_bind_key(ctx, g.jump, key_up);
  action_bind_pad(ctx, g.jump, pad_face_down);
  g.down = action_register(ctx, "down");
  action_bind_key(ctx, g.down, key_down);
  action_bind_key(ctx, g.down, key_s);
  action_bind_pad(ctx, g.down, pad_dpad_down);
  g.interact = action_register(ctx, "interact");
  action_bind_key(ctx, g.interact, key_e);
  action_bind_pad(ctx, g.interact, pad_face_left);
  g.pause = action_register(ctx, "pause");
  action_bind_key(ctx, g.pause, key_p);
  action_bind_pad(ctx, g.pause, pad_start);
  settings_load(ctx);

  shared::apply_style(ctx, "assets/fonts/BeVietnamPro-Bold.ttf");
  dialog_load("assets/dialog/owl.json", g.owl);
  const texture_handle sheet = g.sprites;
  dialog_portrait(ctx, "owl", sheet, rect{{64.0f, 96.0f}, {32.0f, 32.0f}});

  register_prefabs(ctx);
  scene_set(ctx, g.title);
}
} // namespace
} // namespace plat

int main() {
  using namespace njin;
  using namespace plat;
  njin_ctx *ctx = njin_create({.title = "Sprout's Climb",
                               .width = 1280,
                               .height = 720,
                               .target_fps = 60,
                               .clear_bg_color = {0.37f, 0.80f, 0.89f, 1.0f},
                               .exit_key = key_none,
                               .resizable = true,
                               .app_name = "SproutsClimb",
                               .virtual_size = {640.0f, 360.0f},
                               .integer_scale = false});
  g.title = scene_register(*ctx, {.name = "title", .on_enter = title_enter});
  scene_register(*ctx, {.name = "card", .on_enter = card_scene_enter});
  g.play = scene_register(*ctx, {.name = "play", .on_enter = play_enter, .on_exit = play_exit});
  g.win = scene_register(*ctx, {.name = "win", .on_enter = win_enter});
  njin_mod_register(*ctx, {.name = "plat.startup", .setup = [](njin_ctx &c) {
                             ecs_register(c, phase_startup, startup, "startup");
                           }});
  njin_mod_register(*ctx, {play_module(), menus_module()});
#ifndef NDEBUG
  debug_server_start(*ctx);
#endif
  njin_run(*ctx);
  njin_destroy(ctx);
}
