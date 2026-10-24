#pragma once
#include "../njin_internal_only.h"
#include "njin_3d.h"
#include <vector>

namespace njin {
// What the inspector's 3D world view shows of the last pass (debug.cpp): one
// point per draw (per instance for draw_instanced3d), where it is and in its
// colour. Kept only while the debug server runs.
struct debug3d_item {
  i32 kind = 0; // debug3d_kind
  vec3 pos{};
  rgba color{};
};

// Kinds of debug3d_item; the inspector (panel_world.cpp) reads the same numbers.
enum debug3d_kind {
  debug3d_cube = 0,
  debug3d_sphere = 1,
  debug3d_plane = 2,
  debug3d_cylinder = 3,
  debug3d_model = 4, // the centre of the model's bounding box
  debug3d_shape = 10, // + shape3d_kind
};

struct debug3d_frame {
  f32 time = -1.0f; // elapsed() when captured; old frames are not sent
  camera3d camera;
  vec3 sun{};
  std::vector<light3d_source> lights;
  std::vector<debug3d_item> items;
  u32 instanced = 0; // instances drawn by draw_instanced3d (their positions stay on the GPU)
};
} // namespace njin
