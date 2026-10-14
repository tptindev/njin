#pragma once
#include "njin_internal_only.h"

#include "_math.h"
#include "_types.h"
#include <raylib.h>

// Rounded rectangles drawn by their signed distance field instead of
// raylib's triangle fans: one quad, its pixels' coverage worked out in a
// fragment shader, so the edges and the corners are anti-aliased at any
// scale. Off unless the game asks (draw_set_smooth_shapes()); used by
// draw_rect_rounded(), draw_rect_rounded_lines() and the UI's flat skins.

namespace njin {
struct context;

struct sdf2d_state {
  bool on = false;    // draw_set_smooth_shapes()
  bool tried = false; // the shader was loaded once (it may have failed)
  Shader shader{};
  i32 shape_loc = -1, quad_loc = -1;

  sdf2d_state() = default;
  ~sdf2d_state();
  sdf2d_state(const sdf2d_state &) = delete;
  sdf2d_state &operator=(const sdf2d_state &) = delete;
};

// `r` with corners of `radius` pixels, filled in `color` when `thickness` is
// 0, else a line `thickness` thick inside `r`. False when it was not drawn:
// smooth shapes off, another shader on, or no shader; the caller draws it the
// usual way then.
bool sdf2d_rounded_rect(const context &ctx, rect r, f32 radius, f32 thickness, rgba color);

} // namespace njin
