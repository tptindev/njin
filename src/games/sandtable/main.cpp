#include "city/city.h"
#include "game.h"

#include <cstdio>

#include <cstdlib>
#include <string>

int main(int argc, char **argv) {
  using namespace njin;

  bool test_mode = false;
  u32 seed = 1;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool more = i + 1 < argc;
    if (arg == "--test" || arg == "-t")
      test_mode = true;
    else if (arg == "--seed" && more)
      seed = static_cast<u32>(std::strtoul(argv[++i], nullptr, 10));
    else if (arg == "--citycheck") {
      // --citycheck [first seed] [count]: generate and validate, no window.
      const u32 first = more ? static_cast<u32>(std::strtoul(argv[++i], nullptr, 10)) : 1u;
      const i32 count = i + 1 < argc ? std::atoi(argv[++i]) : 50;
      return sandtable::city::run_city_check(first, count, true) == 0 ? 0 : 1;
    } else if (arg == "--citymap" && i + 2 < argc) {
      // --citymap <seed> <file.ppm>: the raster of one city, a pixel a cell.
      sandtable::city::city_map map;
      sandtable::city::city_desc desc;
      desc.seed = static_cast<u32>(std::strtoul(argv[++i], nullptr, 10));
      sandtable::city::generate(map, desc);
      const bool ok = sandtable::city::write_city_ppm(map, argv[++i]);
      std::printf("[city] seed %u: %s\n", desc.seed, ok ? "written" : "could not write");
      return ok ? 0 : 1;
    }
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

  mod_register(*ctx, sandtable::module(test_mode, seed));

#ifndef NDEBUG
  debug_server_start(*ctx);
#endif

  run(*ctx);
  destroy(ctx);
  return 0;
}
