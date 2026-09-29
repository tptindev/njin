#include "platformer3d.h"
#include <njin.h>

int main() {
  const njin::njin_cfg cfg{.title = "Nhảy qua mây",
                           .width = 1280.0f,
                           .height = 720.0f,
                           .target_fps = 144.0f,
                           .clear_bg_color = {0.55f, 0.68f, 0.85f, 1.0f},
                           .resizable = true,
                           .app_name = "njin platformer3d"};
  njin::njin_ctx *ctx = njin::njin_create(cfg);
  njin::njin_mod_register(*ctx, platformer3d::platformer3d_module());
#ifndef NDEBUG
  // njin_inspector, in a debug build: its World panel shows the 3D pass.
  njin::debug_server_start(*ctx);
#endif
  njin::njin_run(*ctx);
  njin::njin_destroy(ctx);
  return 0;
}
