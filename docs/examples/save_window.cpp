#include <njin.h>
#include <string>

namespace {
int high_score = 0;
std::string save_file;

void load(njin::njin_ctx &ctx) {
  // %APPDATA%/<app_name>/highscore.txt trên Windows. Thư mục tự được tạo.
  save_file = njin::save_path(ctx, "highscore.txt");
  std::string text;
  if (njin::file_read(save_file.c_str(), text))
    high_score = std::stoi(text);
}

void update(njin::njin_ctx &ctx) {
  if (njin::key_pressed(ctx, njin::key_f11))
    njin::window_set_fullscreen(ctx, !njin::window_fullscreen(ctx));
  if (njin::window_resized(ctx)) {
    const njin::vec2 size = njin::screen_size(ctx);
    NJIN_INFO("cửa sổ mới: %.0f x %.0f", size.x, size.y);
  }
  if (njin::key_pressed(ctx, njin::key_q))
    njin::njin_quit(ctx); // phase_shutdown vẫn chạy
}

void save(njin::njin_ctx &) {
  // Ghi vào file tạm rồi đổi tên: tắt ngang cũng không hỏng file cũ.
  njin::file_write(save_file.c_str(), std::to_string(high_score));
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, load);
  njin::ecs_register(ctx, njin::phase_update, update);
  njin::ecs_register(ctx, njin::phase_shutdown, save);
}
} // namespace

int main() {
  const njin::njin_cfg cfg{.title = "My Game",
                           .width = 1280,
                           .height = 720,
                           .target_fps = 60,
                           .exit_key = njin::key_none, // Esc dành cho menu
                           .resizable = true,
                           .app_name = "my_game"};
  njin::njin_ctx *ctx = njin::njin_create(cfg);
  njin::njin_mod_register(*ctx, {.name = "save_window", .setup = setup});
  njin::njin_run(*ctx);
  njin::njin_destroy(ctx);
}
