#include <njin.h>

int main() {
  const njin::config cfg{.title = "njin",
                           .width = 1280.0f,
                           .height = 720.0f,
                           .target_fps = 60.0f};

  njin::context *ctx = njin::create(cfg);
  njin::run(*ctx);
  njin::destroy(ctx);
  return 0;
}
