#pragma once
#include "njin_internal_only.h"

#include "njin_physics3d.h"
#include <memory>

namespace njin {
struct njin_ctx;

// 3D physics (njin_physics3d.h) on Jolt Physics. The Jolt world lives behind
// `world` (defined in njin_physics3d.cpp only), so no other runtime file
// includes Jolt. It is made on the first body3d_create or
// character3d_create: a game without 3D physics never starts Jolt.
struct physics3d_world;

struct physics3d_state {
  std::unique_ptr<physics3d_world> world;
  vec3 gravity{0.0f, -9.81f, 0.0f};

  physics3d_state();
  ~physics3d_state();
  physics3d_state(const physics3d_state &) = delete;
  physics3d_state &operator=(const physics3d_state &) = delete;
};

// One fixed step: kinematic targets, then characters, then every body.
// Called by the main loop right after each run of phase_fixed_update, so what
// the game set in that phase moves in the same step.
void physics3d_step(njin_ctx &ctx, f32 dt);
} // namespace njin
