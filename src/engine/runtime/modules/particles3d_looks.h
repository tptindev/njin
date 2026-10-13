#pragma once
#include "../njin_internal_only.h"
#include "njin_3d.h"
#include <raylib.h>

namespace njin {
struct context;

// The built-in looks of 3D particles (particles3d_desc::style), compiled on
// first use. particles3d_draw() wraps a burst in particle3d_look_begin() and
// EndShaderMode().
inline constexpr i32 particle3d_look_count = (i32)particle3d_flame + 1;

struct particle3d_looks {
  Shader shaders[particle3d_look_count]{};
  bool tried[particle3d_look_count]{};

  particle3d_looks() = default;
  ~particle3d_looks();
  particle3d_looks(const particle3d_looks &) = delete;
  particle3d_looks &operator=(const particle3d_looks &) = delete;
};

// Binds the look's shader with its uniforms (time, the sun of light3d_set());
// false for particle3d_flat or if it did not compile (drawn plain then).
bool particle3d_look_begin(context &ctx, particle3d_looks &looks, particle3d_style style);
} // namespace njin
