#pragma once
#include "njin_internal_only.h"

#include "_types.h"
#include "njin_cfg.h"
#include "njin_pace.h"
#include <string>
#include <vector>

namespace njin {
struct context;

// Opens the window on construction and closes it on destruction.
struct window_guard {
  explicit window_guard(const config &cfg);
  ~window_guard();
  window_guard(const window_guard &) = delete;
  window_guard &operator=(const window_guard &) = delete;
  frame_pacer pacer; // config::low_power only
};

// Frame timing. `dt` is what delta() returns outside phase_fixed_update:
// dt_real scaled by `scale`, or 0 while paused.
struct time_state {
  f32 dt = 0.0f;
  f32 dt_real = 0.0f;
  f32 elapsed = 0.0f;
  f32 scale = 1.0f;
  bool paused = false;
  f32 fixed_dt = 1.0f / 60.0f;
  f32 fixed_accum = 0.0f;
  f32 fixed_alpha = 0.0f;
  f32 fixed_rate = 1.0f; // time_set_fixed_rate: fixed steps run this much faster than time
  bool in_fixed = false; // delta() answers fixed_dt while this is set
};

// What the frame loop is asked to do.
struct frame_state {
  bool quit = false;
  // window_set_close_intercept(): closing the window only sets
  // close_requested, for one frame, instead of ending the loop.
  bool close_intercept = false;
  bool close_requested = false;
  // frame_skip_draw(): this frame updates but draws nothing (low_power only).
  bool skip_draw = false;
  // Paths requested by screenshot() this frame, taken at its end.
  std::vector<std::string> screenshots;
};

// Saves every screenshot requested this frame. Called after post_render and
// before EndDrawing, while the back buffer still holds the finished frame.
void take_pending_screenshots(context &ctx);
// Draws the text queued for window resolution (view_state::text_layer) onto the
// window, over the scaled virtual image. Called right after view_draw_end.
void text_layer_flush(context &ctx);
} // namespace njin
