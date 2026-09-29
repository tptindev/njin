#include "game.h"

#include <string>

int main(int argc, char **argv) {
  using namespace njin;

  bool test_mode = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--test" || arg == "--capture" || arg == "-t") {
      test_mode = true;
    }
  }

  context *ctx = create({
      .title = "Chiến Trận Cờ Tướng - Xiangqi RTS",
      .width = 1280.0f,
      .height = 720.0f,
      .target_fps = 60.0f,
      .clear_bg_color = {0.08f, 0.09f, 0.11f, 1.0f},
      .exit_key = key_none,
      .resizable = true,
      .app_name = "XiangqiRTS",
      .virtual_size = {1280.0f, 720.0f},
      .integer_scale = false,
      .smooth_ui = true
  });

  mod_register(*ctx, xiangqi::module(test_mode));

#ifndef NDEBUG
  debug_server_start(*ctx);
#endif

  run(*ctx);
  destroy(ctx);
  return 0;
}
