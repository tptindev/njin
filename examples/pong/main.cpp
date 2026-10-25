#include "pong.h"
#include <njin.h>

int main() {
  const njin::config cfg{.title = "njin pong",
                           .width = 960.0f,
                           .height = 540.0f,
                           .target_fps = 60.0f,
                           .clear_bg_color = {0.06f, 0.07f, 0.1f, 1.0f},
                           // Esc opens the pause menu instead of closing.
                           .exit_key = njin::key_none,
                           .resizable = true,
                           .app_name = "njin pong"};

  njin::context *ctx = njin::create(cfg);
  njin::mod_register(*ctx, pong::pong_module());
#ifndef NDEBUG
  njin::debug_server_start(*ctx); // njin_inspector, in a debug build
#endif
  njin::run(*ctx);
  njin::destroy(ctx);
  return 0;
}
