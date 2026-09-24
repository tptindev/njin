#pragma once
#include "types.h"
#include "njin_cfg.h"

namespace njin {
struct njin_input;
struct njin_ctx {
  f32 dt;
  f32 elapsed; 
  njin_cfg cfg;
  njin_input *input;
};

void get_delta(const njin_ctx &ctx, f32 &delta);
void get_elapsed(const njin_ctx &ctx, f32 &time);
void world_to_screen(const njin_ctx &ctx, vec2 world, vec2 &screen);
void screen_to_world(const njin_ctx &ctx, vec2 screen, vec2 &world);
}
