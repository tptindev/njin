#pragma once
#include "types.h"

namespace njin {
struct njin_ctx;
struct njin_cfg {
  const char *title;
  f32 width;
  f32 height;
  f32 target_fps;
};

void get_fps(const njin_ctx &ctx, f32 &fps);
void get_screen_size(const njin_ctx &ctx, vec2 &screen_size);
}
