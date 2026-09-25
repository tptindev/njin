#pragma once

#include "_types.h"
namespace njin {
struct transform {
  vec2 pos{};
  f32 rot = 0.0f;
  f32 scale = 1.0f;
};

// Linear velocity in world units per second. Core integrates this into
// transform during phase_post_update.
struct velocity {
  vec2 value{};
};

struct camera_2d {
  vec2 offset{};
  f32 zoom = 1.0f;
};

struct camera_on {};
} // namespace njin
