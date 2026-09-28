#include <njin.h>

namespace {
njin::sound_handle click;
njin::sound_handle hit;
njin::sound_handle engine_hum;
njin::music_handle theme;
njin::action_handle fire;
njin::action_handle pause_music;
bool paused = false;

void load(njin::njin_ctx &ctx) {
  // Sound: short, held entirely in memory. Music: long, streamed from disk.
  click = njin::sound_load(ctx, "assets/click.wav");
  hit = njin::sound_load(ctx, "assets/hit.wav");
  engine_hum = njin::sound_load(ctx, "assets/hum.ogg");
  theme = njin::music_load(ctx, "assets/theme.ogg");

  // A failed load (missing file, no speakers) returns a handle with id 0 and logs it.
  // Every function that takes that handle does nothing, so there is no need to check everywhere.

  njin::sound_set_volume(ctx, engine_hum, 0.4f);
  njin::music_set_volume(ctx, theme, 0.7f);
  njin::music_play(ctx, theme); // loops by default
  njin::sound_play_loop(ctx, engine_hum);

  fire = njin::action_register(ctx, "fire");
  njin::action_bind_key(ctx, fire, njin::key_space);
  pause_music = njin::action_register(ctx, "pause_music");
  njin::action_bind_key(ctx, pause_music, njin::key_p);
}

void update(njin::njin_ctx &ctx) {
  // Even rapid presses let you hear each click: every time it cuts the previous one and plays again.
  if (njin::key_pressed(ctx, njin::key_enter))
    njin::sound_play_restart(ctx, click);

  // Impact sound: when several things trigger it at once, they overlap (up to 8 copies).
  // A bigger object sounds lower and louder: the same recording serves every size.
  if (njin::action_pressed(ctx, fire)) {
    const njin::f32 size = 2.0f; // example
    njin::sound_play_once_at(ctx, hit, 1.0f / size, 0.5f + 0.25f * size);
  }

  // Pausing keeps the position; playback resumes from there.
  if (njin::action_pressed(ctx, pause_music)) {
    paused = !paused;
    if (paused)
      njin::music_pause(ctx, theme);
    else
      njin::music_resume(ctx, theme);
  }
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, load);
  njin::ecs_register(ctx, njin::phase_update, update);
}
} // namespace

njin::mod_desc audio_demo_module() {
  return {.name = "audio_demo", .setup = setup};
}
