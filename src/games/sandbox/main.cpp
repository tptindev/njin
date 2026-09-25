#include "njin_ctx.h"
#include <njin.h>

int main() {
  njin::njin_ctx ctx{};

  const njin::njin_cfg cfg{
      .title = "njin sandbox",
      .width = 1280.0f,
      .height = 720.0f,
      .target_fps = 60.0f,
      .clear_bg_color = {.r = 1.0, .g = 1.0, .b = 1.0, .a = 1.0}};

  njin::njin_init(ctx, cfg);
  njin::njin_run(ctx);
  njin::njin_shutdown(ctx);

  return 0;
}
