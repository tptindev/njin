#include <njin.h>

namespace {
njin::action_handle jump;
bool open_settings = true;

void startup(njin::context &ctx) {
  jump = njin::action_register(ctx, "jump");
  njin::action_bind_key(ctx, jump, njin::key_space);
  njin::action_bind_pad(ctx, jump, njin::pad_face_down);
  // After all actions are registered and the language is loaded: the keys the player
  // changed, volume, fullscreen and language replace the default values.
  njin::settings_load(ctx);
}

void settings_menu(njin::context &ctx) {
  if (!open_settings)
    return;
  njin::ui_begin(ctx, {.id = "settings", .title = "Settings"});

  // Per-channel volume. The master channel multiplies into all of them.
  njin::f32 music = njin::audio_bus_volume(ctx, njin::bus_music);
  if (njin::ui_slider(ctx, "Music", music, 0.0f, 1.0f, 0.1f, true))
    njin::audio_set_bus_volume(ctx, njin::bus_music, music);

  // Rebinding keys: click the row, then press the new key. The second row is for the gamepad.
  njin::ui_keybind(ctx, "Jump", jump);
  njin::ui_keybind(ctx, "Jump##pad", jump, true);

  // Do not treat Esc as "close menu" while waiting for a key.
  if (njin::ui_button(ctx, "Done") || (!njin::ui_keybind_listening(ctx) && njin::ui_back(ctx))) {
    njin::settings_save(ctx); // writes to the user's game save folder
    open_settings = false;
  }
  njin::ui_end(ctx);
}

// Rumble the gamepad on a hit.
void on_hit(njin::context &ctx) { njin::pad_rumble(ctx, 0, 0.6f, 0.8f, 0.25f); }

// Smooth music switch between two tracks, on real time so it also runs while the game is paused.
void enter_boss(njin::context &ctx, njin::music_handle boss) { njin::music_crossfade(ctx, boss, 1.5f); }

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_post_render, settings_menu);
}
} // namespace

int main() {
  njin::context *ctx = njin::create({.title = "Settings", .width = 1280, .height = 720, .target_fps = 60});
  njin::mod_register(*ctx, {.name = "game", .setup = setup});
  (void)on_hit;
  (void)enter_boss;
  njin::run(*ctx);
  njin::destroy(ctx);
}
