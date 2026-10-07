#pragma once
#include "njin_internal_only.h"

#include "njin_physics3d.h"
#include <memory>

namespace njin {
struct context;

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
void physics3d_step(context &ctx, f32 dt);

// A static height field (terrain3d): `count` x `count` world heights (row z,
// then column x), `spacing` metres apart, the first at (origin.x, origin.z).
// Room is kept from `lo` to `hi` (and some way past) for later edits.
body3d_handle physics3d_heightfield_create(context &ctx, const f32 *heights, i32 count, vec3 origin, f32 spacing,
                                           f32 lo, f32 hi, f32 friction, u64 user);
// Copies samples [x0, x1) x [z0, z1) of `heights` (the same layout as at
// creation) into the height field of `body`, and wakes what rests there.
void physics3d_heightfield_set(context &ctx, body3d_handle body, const f32 *heights, i32 count, i32 x0, i32 z0,
                               i32 x1, i32 z1);

// Where the water is under `at` (its surface point and normal), or false
// where there is none.
using water_surface_fn = bool (*)(const context &ctx, u32 water, vec3 at, vec3 &point, vec3 &normal);
// Each step `body` takes the buoyancy of water `water` (water3d_float). Once
// per body and water; again replaces the settings.
void physics3d_float(context &ctx, body3d_handle body, u32 water, f32 buoyancy, f32 linear_drag, f32 angular_drag,
                     vec3 flow, water_surface_fn surface);
// Stops it; `water` 0 = from every water (the water was destroyed: body 0
// with it stops every body on it).
void physics3d_unfloat(context &ctx, body3d_handle body, u32 water);
} // namespace njin
