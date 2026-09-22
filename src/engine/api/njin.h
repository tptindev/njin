#pragma once 
#include "types.h"

namespace njin {
struct njin_cfg {
  const char *title;
  f32 width;
  f32 height;
  f32 target_fps;
};

struct njin_ctx {
  njin_cfg cfg;
};

void njin_init(njin_ctx &ctx, const njin_cfg &cfg);
void njin_run(njin_ctx &ctx);
void njin_shutdown(njin_ctx &ctx);
}

