#include "game.h"

int main() {
  using namespace njin;
  context *ctx = create({.title = "Thành Trì Bình Minh",
                               .width = 1280.0f,
                               .height = 720.0f,
                               .target_fps = 60.0f,
                               .clear_bg_color = {0.055f, 0.08f, 0.1f, 1.0f},
                               .exit_key = key_none,
                               .resizable = true,
                               .app_name = "ThanhTriBinhMinh",
                               .virtual_size = {1280.0f, 720.0f},
                               .integer_scale = false,
                               .smooth_ui = true});
  mod_register(*ctx, defense::module());
#ifndef NDEBUG
  debug_server_start(*ctx);
#endif
  run(*ctx);
  destroy(ctx);
}
