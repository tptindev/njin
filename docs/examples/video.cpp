#include <njin.h>

namespace {
using namespace njin;

video_handle intro; // cutscene đầu game, toàn màn hình
video_handle tv;    // video lặp trên màn hình TV trong phòng
bool playing_intro = true;

void load(context &ctx) {
  // File phải là MPEG-1 (.mpg); xem lệnh ffmpeg ở njin::video_desc.
  intro = video_open(ctx, {.path = "videos/intro.mpg"});
  // Tiếng TV ở kênh hiệu ứng, nhỏ hơn; chưa phát cho tới khi hết cutscene.
  tv = video_open(ctx, {.path = "videos/news.mpg", .loop = true, .play = false, .bus = bus_sfx, .volume = 0.4f});
}

void update(context &ctx) {
  if (!playing_intro)
    return;
  // Hết video, hay người chơi bấm Enter để bỏ qua: vào game.
  if (video_finished(ctx, intro) || key_pressed(ctx, key_enter)) {
    video_close(ctx, intro);
    playing_intro = false;
    video_play(ctx, tv);
  }
}

void render(context &ctx) {
  if (playing_intro) {
    // Vừa khít màn hình, giữ tỉ lệ, dải đen hai bên.
    video_draw_fit(ctx, intro, {{0.0f, 0.0f}, window_size(ctx)});
    return;
  }
  begin_3d(ctx, camera3d{.position = {0.0f, 1.6f, 4.0f}, .target = {0.0f, 1.2f, 0.0f}});
  draw_cube3d(ctx, {0.0f, 0.4f, 0.0f}, {2.2f, 0.8f, 0.6f}, rgba{0.3f, 0.2f, 0.15f, 1.0f}); // kệ
  // Màn hình: frame hiện tại dán lên mặt trước của một hộp mỏng, không nhận ánh sáng.
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
