#pragma once

#include "_types.h"
namespace njin {
struct transform {
  vec2 pos{};
  // Degrees, clockwise.
  f32 rot = 0.0f;
  f32 scale = 1.0f;
};

// 2D camera. Needs a transform on the same entity: transform.pos is the world
// point the camera looks at, transform.rot its rotation. scale is ignored.
struct camera_2d {
  // Screen position (pixels) where transform.pos is drawn. Use half the
  // screen size to keep the target centered.
  vec2 offset{};
  // 1 = no zoom, 2 = everything twice as big. Values <= 0 are treated as 1.
  f32 zoom = 1.0f;
};

// Tag: marks the camera used for rendering and w2scr/scr2w. If several
// entities have it, the first one found wins. With none, world == screen.
struct camera_on {};
} // namespace njin
