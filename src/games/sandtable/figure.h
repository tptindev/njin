#pragma once

#include "types.h"

namespace sandtable {

// A man in 3D: a clay figure of blended SDF parts (draw_sdf_blend), posed
// afresh every frame from what he is doing. A round head on a thin neck, a
// body wider at the hips than the chest with sloping shoulders, and limbs
// that curve through elbow and knee, all melted into one piece.

// What the body is doing, from the soldier (or corpse) it draws.
struct figure_state {
  f32 time = 0.0f;   // a running clock, for breathing and looking about
  f32 stride = 0.0f; // distance walked, world units: the steps follow it
  f32 pace = 0.0f;   // world units a second: still, walking or running
  f32 act = 0.0f;    // time left in a blow
  i32 act_kind = 0;  // 0 left punch, 1 right punch, 2 kick
  f32 hurt = 0.0f;   // time left flinching
  f32 down = 0.0f;   // time left lying knocked down
  f32 rise = 0.0f;   // time left getting up
  f32 dead = -1.0f;  // seconds since he fell dead; below 0 alive
  bool fighting = false; // squared up to someone: fists up, knees bent
  u32 seed = 0;      // who he is: each man stands, breathes and moves a little his own way
};

// A man's pose from the frame before: the new one eases toward what he is
// doing rather than jumping to it, so the body carries its weight.
struct figure_memory {
  f32 joint[24]{};
  bool set = false;
};

struct figure_pose {
  sdf_part parts[sdf_blend_max];
  u32 count = 0;
  f32 blend = 0.0f;  // the softness of the parts that do not set their own
  vec3 eyes[2]{};
  f32 eye_radius = 0.0f;
  vec3 hand{};       // the right hand, where a torch is held
};

// The figure standing at `feet` (3D), facing `facing` on the table, `height`
// 3D units tall. With `memory`, the pose eases from the last one over `dt`
// seconds; without, it is the pose itself.
figure_pose pose_figure(const figure_state &st, vec3 feet, vec2 facing, f32 height, figure_memory *memory = nullptr,
                        f32 dt = 0.0f);

} // namespace sandtable
