#include <njin.h>

int main() {
  const njin::njin_cfg cfg{.title = "njin",
                           .width = 1280.0f,
                           .height = 720.0f,
                           .target_fps = 60.0f};

  njin::njin_ctx *ctx = njin::njin_create(cfg);
  njin::njin_run(*ctx);
  njin::njin_destroy(ctx);
  return 0;
}
