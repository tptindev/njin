#include "pong.h"
#include <njin.h>

int main() {
  const njin::njin_cfg cfg{.title = "njin pong",
                           .width = 960.0f,
                           .height = 540.0f,
                           .target_fps = 60.0f,
                           .clear_bg_color = {0.06f, 0.07f, 0.1f, 1.0f},
                           // Esc opens the pause menu instead of closing.
                           .exit_key = njin::key_none,
                           .resizable = true,
                           .app_name = "njin pong"};

  njin::njin_ctx *ctx = njin::njin_create(cfg);
  njin::njin_mod_register(*ctx, pong::pong_module());
  njin::njin_run(*ctx);
  njin::njin_destroy(ctx);
  return 0;
}
