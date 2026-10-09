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

struct foot_leg_state {
  foot3d_leg bones;
  f32 lift = 0.0f;              // the ground under the foot over the draw's origin, world, eased
  vec3 normal{0.0f, 1.0f, 0.0f}; // the ground's normal in model space, eased
};

struct foot_slot {
  bool alive = false;
  model_handle model{};
  std::vector<foot_leg_state> legs;
  f32 max_step = 0.5f, max_tilt = 35.0f, smoothing = 15.0f;
  vec3 knee_forward{0.0f, 0.0f, 1.0f};
  f32 hip = 0.0f;    // the hips' drop, world, eased
  bool fresh = true; // the next update places the feet at once
};

struct anim3d_store {
  std::vector<spring_slot> springs;
  std::vector<retarget_slot> retargets;
  std::vector<foot_slot> feet;
};
} // namespace njin
