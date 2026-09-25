#pragma once

#include "_types.h"
namespace njin {
struct transform {
  vec2 pos;
  f32 rot;
  f32 scale;
};

struct camera_2d {
  vec2 offset;
  f32 zoom = 1.0f;
};

struct camera_on {};
} // namespace njin
