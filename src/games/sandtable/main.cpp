#include "game.h"

#include <cstdlib>
#include <string>

int main(int argc, char **argv) {
  using namespace njin;

  bool test_mode = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--test" || arg == "-t")
      test_mode = true;
  }

  context *ctx = create({
      .title = "Sa Bàn Chiến Trận",
      .width = 1280.0f,
      .height = 720.0f,
      .target_fps = 60.0f,
      .clear_bg_color = {0.05f, 0.05f, 0.07f, 1.0f},
      .exit_key = key_none,
      .resizable = true,
      .app_name = "SandTable",
      // The HUD is laid out in a virtual screen of half the window (fit_view
      // keeps it so as the window changes) and drawn smooth at the window's
      // resolution; the 3D table is drawn at twice the virtual size, so at
      // the window's own resolution too.
      .virtual_size = {640.0f, 360.0f},
      .smooth_ui = true,
      .render_scale = 2,
  });

  mod_register(*ctx, sandtable::module(test_mode));

#ifndef NDEBUG
  debug_server_start(*ctx);
#endif

  run(*ctx);
  destroy(ctx);
  return 0;
}
