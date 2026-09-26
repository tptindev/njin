#pragma once
#include "njin_fx.h"
#include <raylib.h>

namespace njin {
// Screen and time effects (njin_fx.h). All timers run on real time, so they
// keep going through hitstop and pause.
struct fx_state {
  shake_config shake{};
  f32 trauma = 0.0f;
  f32 shake_time = 0.0f; // real seconds since start, drives the shake noise

  f32 hitstop = 0.0f; // real seconds of freeze left

  rgba flash_color{};
  f32 flash_duration = 0.0f;
  f32 flash_time = 0.0f;

  // Mixes a sprite's pixels toward a colour (flash_fx). Loaded by fx_warmup()
  // when the game starts, or on first use if that did not run; freed with the state.
  Shader flash_shader{};
  i32 flash_color_loc = -1;
  bool flash_loaded = false;

  fx_state() = default;
  ~fx_state();
  fx_state(const fx_state &) = delete;
  fx_state &operator=(const fx_state &) = delete;
};

// Called once per frame right after the frame's delta is computed: runs down
// hitstop (zeroing delta while it lasts), shake and the screen flash.
void fx_frame_begin(njin_ctx &ctx);

// Adds the current shake to the camera used for drawing the world.
void fx_apply_shake(const njin_ctx &ctx, Camera2D &camera);

// Draws the screen flash over the whole frame. Called after post_render.
void fx_draw_screen_flash(njin_ctx &ctx);

// Advances every flash_fx by delta() and removes finished ones.
void fx_update_sprite_flashes(njin_ctx &ctx);

// Compiles the flash shader now instead of on the first hit, when the driver
// would stall the frame for it. Safe to call again.
void fx_warmup(njin_ctx &ctx);

// Begins drawing with the flash shader for `flash`; returns false (and begins
// nothing) when the shader is unavailable or the flash is invisible.
bool fx_flash_begin(njin_ctx &ctx, const flash_fx &flash);
void fx_flash_end();
} // namespace njin
