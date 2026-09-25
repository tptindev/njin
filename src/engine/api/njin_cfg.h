#pragma once
#include "_types.h"

namespace njin {
struct njin_ctx;
struct njin_cfg {
  const char *title;
  f32 width;
  f32 height;
  f32 target_fps;
  rgba clear_bg_color = { .r = 1.0, .g = 1.0, .b = 1.0, .a = 1.0 };
};

f32 fps(const njin_ctx &ctx);
vec2 screen_size(const njin_ctx &ctx);
}
