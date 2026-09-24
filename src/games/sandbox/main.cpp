#include <njin.h>
#include "njin_ctx.h"

int main() {
  njin::njin_ctx ctx{};

  const njin::njin_cfg cfg{
      .title = "njin sandbox",
      .width = 1280.0f,
      .height = 720.0f,
      .target_fps = 60.0f,
  };

  njin::njin_init(ctx, cfg);
  njin::njin_run(ctx);
  njin::njin_shutdown(ctx);

  return 0;
}
