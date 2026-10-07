#include <njin.h>

namespace {
using namespace njin;

video_handle intro; // opening cutscene, full screen
video_handle tv;    // a looping video on the TV in the room
bool playing_intro = true;

void load(context &ctx) {
  // The file must be MPEG-1 (.mpg); see the ffmpeg command at njin::video_desc.
  intro = video_open(ctx, {.path = "videos/intro.mpg"});
  // The TV's sound on the effects bus, quieter; not playing until the cutscene ends.
  tv = video_open(ctx, {.path = "videos/news.mpg", .loop = true, .play = false, .bus = bus_sfx, .volume = 0.4f});
}

void update(context &ctx) {
  if (!playing_intro)
    return;
  // The video ended, or the player pressed Enter to skip: into the game.
  if (video_finished(ctx, intro) || key_pressed(ctx, key_enter)) {
    video_close(ctx, intro);
    playing_intro = false;
    video_play(ctx, tv);
  }
}

void render(context &ctx) {
  if (playing_intro) {
    // Fitted to the screen, aspect kept, black bars at the sides.
    video_draw_fit(ctx, intro, {{0.0f, 0.0f}, window_size(ctx)});
    return;
  }
  begin_3d(ctx, camera3d{.position = {0.0f, 1.6f, 4.0f}, .target = {0.0f, 1.2f, 0.0f}});
  draw_cube3d(ctx, {0.0f, 0.4f, 0.0f}, {2.2f, 0.8f, 0.6f}, rgba{0.3f, 0.2f, 0.15f, 1.0f}); // stand
  // The screen: the current frame on the front of a thin box, unlit.
  material3d screen{};
  screen.unlit = true;
  screen.texture = video_texture(ctx, tv);
  material3d_set(ctx, screen);
  draw_cube3d(ctx, {0.0f, 1.45f, 0.0f}, {1.6f, 0.9f, 0.05f}, colors::white);
  material3d_set(ctx, {});
  end_3d(ctx);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, load, "load");
  ecs_register(ctx, phase_update, update, "update");
  ecs_register(ctx, phase_render, render, "render");
}
} // namespace

mod_desc cinema_module() { return {.name = "cinema", .setup = setup}; }
