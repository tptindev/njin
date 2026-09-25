#pragma once
#include "_mod.h"
#include "njin_collision.h"
#include <utility>
#include <vector>

namespace njin {
// Core module. In phase_post_update, after the hierarchy module has placed
// child entities, finds every overlapping pair of box and circle colliders
// through a uniform grid and enqueues enter/stay/exit events. In
// phase_render, draws collider outlines when debug drawing is on. Registered
// after the sprite module so the outlines land on top of sprites.
mod_desc collision_module();

struct collision_state {
  f32 cell_size = 64.0f;
  bool debug = false;
  // Pairs overlapping at the last check, sorted by pair key (both entity ids
  // packed, lower first), with whether either side was a trigger (the exit
  // event repeats it).
  std::vector<std::pair<u64, bool>> touching;
  // Scratch buffers kept between frames so detection does not reallocate.
  std::vector<std::pair<u64, i32>> cells;
  std::vector<u64> candidates;
};
} // namespace njin
