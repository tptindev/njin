#include "../kungfu_preview.h"
#include <string_view>

int main(int argc, char **argv) {
  using namespace njin;
  bool capture = false;
  for (int i = 1; i < argc; ++i)
    capture |= std::string_view(argv[i]) == "--test";
  context *ctx = create({.title = "Sandtable - Boxing + kicks",
                         .width = 1280,
                         .height = 720,
                         .target_fps = 60,
                         .clear_bg_color = {.80f, .75f, .64f, 1},
                         .exit_key = key_escape,
                         .app_name = "SandtableKungfu"});
  mod_register(*ctx, sandtable::kungfu_module(capture));
  run(*ctx);
  destroy(ctx);
}
