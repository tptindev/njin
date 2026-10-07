#pragma once
#include "njin_internal_only.h"

#include "_types.h"
#include "njin_anim3d.h"
#include <vector>

namespace njin {
// Spring bones and retargeting (njin_anim3d.h). Same handle rules as the other
// stores: id N maps to slots[N - 1], id 0 is invalid, slots are never reused.

struct spring_joint {
  i32 bone = -1;
  vec3 axis{};      // tail direction in the bone's frame, at rest, unit
  f32 length = 0.0f; // rest distance to the tail, model units
  f32 stiffness = 1.0f, drag = 0.4f, gravity = 0.0f, radius = 0.02f;
  vec3 gravity_dir{0.0f, -1.0f, 0.0f};
  vec3 tail{}, prev{}; // tail now and one step ago, world
};

struct spring_slot {
  bool alive = false;
  model_handle model{};
  std::vector<i32> order;    // every bone, parents before children
  std::vector<i32> joint_of; // per bone, its joint or -1
  std::vector<spring_joint> joints;
  std::vector<spring3d_collider> colliders;
  bool fresh = true; // the tails start at the pose on the next update
};

struct retarget_slot {
  bool alive = false;
  model_handle source{}, target{};
  std::vector<i32> source_of; // per target bone, its source bone or -1
  std::vector<i32> order;     // target bones, parents before children
  i32 hips = -1;              // target bone moved with the source's hips
  f32 scale = 1.0f;           // target hips height over the source's
};

struct anim3d_store {
  std::vector<spring_slot> springs;
  std::vector<retarget_slot> retargets;
};
} // namespace njin
