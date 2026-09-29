#include "crowd.h"
#include <cstdlib>
#include <cstring>
#include <njin.h>

int main(int argc, char **argv) {
  const njin::config cfg{.title = "njin crowd",
                           .width = 1280.0f,
                           .height = 720.0f,
                           .target_fps = 144.0f,
                           .clear_bg_color = {0.94f, 0.94f, 0.92f, 1.0f},
                           .resizable = true,
                           .app_name = "njin crowd"};

  // --gallery: open on the gallery of every pose. --save-sheets: save the
  // baked sheets as PNG (to the save folder) at startup. --people N: crowd size.
  crowd::options opts;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--gallery") == 0)
      opts.gallery = true;
    else if (std::strcmp(argv[i], "--save-sheets") == 0)
      opts.save_sheets = true;
    else if (std::strcmp(argv[i], "--people") == 0 && i + 1 < argc)
      opts.people = (njin::u32)std::strtoul(argv[++i], nullptr, 10);
  }

  njin::context *ctx = njin::create(cfg);
  njin::mod_register(*ctx, crowd::crowd_module(opts));
#ifndef NDEBUG
  // njin_inspector, in a debug build. Every person is an entity: room for the
  // default crowd, the gallery and its groups (a bigger crowd is cut, the
  // inspector says so).
  njin::debug_server_start(*ctx, {.max_entities = 12000});
#endif
  njin::run(*ctx);
  njin::destroy(ctx);
  return 0;
}
