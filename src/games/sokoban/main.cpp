#include "sokoban.h"
#include <njin.h>

int main() {
  const njin::njin_cfg cfg{.title = "Đẩy thùng",
                           .width = 1280.0f,
                           .height = 720.0f,
                           .target_fps = 144.0f,
                           .clear_bg_color = {0.13f, 0.15f, 0.21f, 1.0f},
                           .resizable = true,
                           .app_name = "njin sokoban"};
  njin::njin_ctx *ctx = njin::njin_create(cfg);
  njin::njin_mod_register(*ctx, sokoban::sokoban_module());
#ifndef NDEBUG
  // njin_inspector, in a debug build: its World panel shows the 3D pass.
  njin::debug_server_start(*ctx);
#endif
  njin::njin_run(*ctx);
  njin::njin_destroy(ctx);
  return 0;
}
