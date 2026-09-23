#pragma once

#include "types.h"

namespace njin {
struct njin_ctx;
void get_fps(const njin_ctx &ctx, f32 &fps);
void get_delta(const njin_ctx &ctx, f32 &delta);
void get_screen_size(const njin_ctx &ctx, vec2 &screen_size);
void world_to_screen(const njin_ctx &ctx, vec2 world, vec2 &screen);
void screen_to_world(const njin_ctx &ctx, vec2 screen, vec2 &world);
}
