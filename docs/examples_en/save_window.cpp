#include <njin.h>
#include <string>

namespace {
int high_score = 0;
std::string save_file;

void load(njin::context &ctx) {
  // %APPDATA%/<app_name>/highscore.txt on Windows. The folder is created automatically.
  save_file = njin::save_path(ctx, "highscore.txt");
  std::string text;
  if (njin::file_read(save_file.c_str(), text))
    high_score = std::stoi(text);
}

void update(njin::context &ctx) {
  if (njin::key_pressed(ctx, njin::key_f11))
    njin::window_set_fullscreen(ctx, !njin::window_fullscreen(ctx));
  if (njin::window_resized(ctx)) {
    const njin::vec2 size = njin::screen_size(ctx);
    NJIN_INFO("new window: %.0f x %.0f", size.x, size.y);
  }
  if (njin::key_pressed(ctx, njin::key_q))
    njin::quit(ctx); // phase_shutdown still runs
}

void save(njin::context &) {
  // Write to a temp file then rename: even a sudden shutdown leaves the old file intact.
  njin::file_write(save_file.c_str(), std::to_string(high_score));
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, load);
  njin::ecs_register(ctx, njin::phase_update, update);
  njin::ecs_register(ctx, njin::phase_shutdown, save);
}
} // namespace

int main() {
  const njin::config cfg{.title = "My Game",
                           .width = 1280,
                           .height = 720,
                           .target_fps = 60,
                           .exit_key = njin::key_none, // Esc is for the menu
                           .resizable = true,
                           .app_name = "my_game"};
  njin::context *ctx = njin::create(cfg);
  njin::mod_register(*ctx, {.name = "save_window", .setup = setup});
  njin::run(*ctx);
  njin::destroy(ctx);
}
