#include <njin.h>

int main() {
  const njin::njin_cfg cfg{
      .title = "njin sandbox",
      .width = 1280.0f,
      .height = 720.0f,
      .target_fps = 60.0f,
      .clear_bg_color = {.r = 1.0, .g = 1.0, .b = 1.0, .a = 1.0}};

  njin::njin_ctx *ctx = njin::njin_create(cfg);
  njin::njin_run(*ctx);
  njin::njin_destroy(ctx);

  return 0;
}
