#pragma once

#include "types.h"

namespace sandtable {

// A procedural articulated clay person, ray-marched through draw_sdf_blend.
// Rounded limbs fuse at joints; tint applies to clothes, not skin/shoes.

enum class act : u8 {
  idle = 0,
  talk,
  walk,
  jog,
  sprint,
  jab,
  cross,  // a right punch
  hit,    // taking a blow
  death,
  sit,
  crouch,
  aim,    // pistol raised
  shoot,
  count
};

const char *act_name(act a);
// Seconds one go of the motion takes (a loop's length).
f32 act_duration(act a);
// Whether it repeats (walking) or plays once and holds (a punch, dying).
bool act_loops(act a);

struct person_draw {
  vec2 at{};          // table coordinates
  f32 facing = 0.0f;  // degrees, table angle (0 is +x, 90 toward the player)
  act now = act::idle;
  f32 time = 0.0f;    // seconds into `now`
  // Easing out of the previous motion: `blend` 1 is all `was`, 0 all `now`.
  act was = act::idle;
  f32 was_time = 0.0f;
  f32 blend = 0.0f;
  rgba tint{1.0f, 1.0f, 1.0f, 1.0f};
  f32 lift = 0.0f;    // 3D units above the sand (sidewalks, floors)
  u32 identity = 0;   // Stable body/tone seed; 0 = reference proportions.
};

void person_init(context &ctx);
void person_cleanup(context &ctx);
bool person_ready();
// Only between begin_3d() and end_3d().
void draw_person(context &ctx, const person_draw &p);

} // namespace sandtable
