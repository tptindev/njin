#include "fps.h"
#include <njin.h>

int main() {
  const njin::config cfg{.title = "njin fps",
                           .width = 1280.0f,
                           .height = 720.0f,
                           .target_fps = 144.0f,
                           .clear_bg_color = {0.17f, 0.17f, 0.18f, 1.0f},
                           .resizable = true,
                           .app_name = "njin fps"};
  njin::context *ctx = njin::create(cfg);
  njin::mod_register(*ctx, fps::fps_module());
#ifndef NDEBUG
  // njin_inspector, in a debug build: its World panel shows the 3D pass.
  njin::debug_server_start(*ctx);
#endif
  njin::run(*ctx);
  njin::destroy(ctx);
  return 0;
}
