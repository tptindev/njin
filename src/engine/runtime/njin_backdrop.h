#pragma once
#include "njin_internal_only.h"

#include "_types.h"
#include <raylib.h>

// draw_backdrop_blur(): what is drawn under a rectangle, copied out of the
// framebuffer, blurred at half size and put back cut to the rectangle's
// rounded corners. The shaders and the scratch targets are made on first use.

namespace njin {

struct backdrop_state {
  bool tried = false; // the shaders were loaded once (they may have failed)
  Shader blur{}, mask{};
  i32 blur_dir = -1, blur_area = -1;
  i32 mask_shape = -1, mask_uv = -1;
  // `grab` as big as the framebuffer copied from (a multisampled one is only
  // resolved into a rectangle of the same place and size), `half_*` half it.
  RenderTexture2D grab{}, half_a{}, half_b{};

  backdrop_state() = default;
  ~backdrop_state();
  backdrop_state(const backdrop_state &) = delete;
  backdrop_state &operator=(const backdrop_state &) = delete;
};

} // namespace njin
