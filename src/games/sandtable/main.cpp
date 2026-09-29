#include "game.h"

#include <cstdlib>
#include <string>

int main(int argc, char **argv) {
  using namespace njin;

  bool test_mode = false;
  i32 test_level = 0;
  const char *test_plan = nullptr;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--test" || arg == "-t")
      test_mode = true;
    else if (arg == "--level" && i + 1 < argc)
      test_level = std::atoi(argv[++i]) - 1;
    else if (arg == "--plan" && i + 1 < argc)
      test_plan = argv[++i];
  }

  context *ctx = create({
      .title = "Sa Bàn Chiến Trận",
      .width = 1280.0f,
      .height = 720.0f,
      .target_fps = 60.0f,
      .clear_bg_color = {0.08f, 0.07f, 0.06f, 1.0f},
      .exit_key = key_none,
      .resizable = true,
      .app_name = "SandTable",
      // Pixel art: everything, text included, is drawn in the 640 x 360 image
      // and scaled up by whole numbers with the nearest filter.
      .virtual_size = {640.0f, 360.0f},
      .integer_scale = true,
      .crisp_text = false,
  });

  mod_register(*ctx, sandtable::module(test_mode, test_level, test_plan));

#ifndef NDEBUG
  debug_server_start(*ctx);
#endif

  run(*ctx);
  destroy(ctx);
  return 0;
}
