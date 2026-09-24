#pragma once
#include "njin_cfg.h"
#include "types.h"

namespace njin {
struct njin_input;

struct njin_ctx {
  f32 dt;
  f32 elapsed;
  njin_cfg cfg;
  njin_input *input;
};

// Time
void get_delta(const njin_ctx &ctx, f32 &delta);
void get_elapsed(const njin_ctx &ctx, f32 &time);

// Coordinates
void world_to_screen(const njin_ctx &ctx, vec2 world, vec2 &screen);
void screen_to_world(const njin_ctx &ctx, vec2 screen, vec2 &world);

// Keys
bool key_pressed(const njin_ctx &ctx, key_code key);
bool key_held(const njin_ctx &ctx, key_code key);
bool key_released(const njin_ctx &ctx, key_code key);

// Actions
action_handle action_register(njin_ctx &ctx, const char *name);
action_handle action_find(const njin_ctx &ctx, const char *name);
void action_bind_key(njin_ctx &ctx, action_handle handle, key_code key);
bool action_pressed(const njin_ctx &ctx, action_handle handle);
bool action_held(const njin_ctx &ctx, action_handle handle);
bool action_released(const njin_ctx &ctx, action_handle handle);
} // namespace njin
