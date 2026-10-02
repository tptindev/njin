#pragma once

#include "types.h"

namespace sandtable {

// Shared skinned assets/models/person.glb with per-person animation and tint.

enum class act : u8 {
  idle = 0,
  talk,
  walk,
  jog,
  sprint,
  jab,
  cross, // a right punch
  hit,   // taking a blow
  death,
  sit,
  crouch,
  aim, // pistol raised
  shoot,
  boxing_guard,
  boxing_combo,
  boxing_hook,
  boxing_front_kick,
  boxing_round_kick,
  boxing_block,
  boxing_low_kick,
  count
};

const char *act_name(act a);
// Seconds one go of the motion takes (a loop's length).
f32 act_duration(act a);
// Whether it repeats (walking) or plays once and holds (a punch, dying).
bool act_loops(act a);

// Width scales the GLB silhouette. Other legacy shape fields remain for caller
// compatibility and do not deform the source skeleton.
struct person_style {
  f32 width = 1.0f;
  f32 head = 1.0f;
  f32 limb = 1.0f;
  f32 softness = 1.0f;
  f32 retro = 0.38f;
};

struct person_draw {
  vec2 at{};         // table coordinates
  f32 facing = 0.0f; // degrees, table angle (0 is +x, 90 toward the player)
  act now = act::idle;
  f32 time = 0.0f; // seconds into `now`
  // Easing out of the previous motion: `blend` 1 is all `was`, 0 all `now`.
  act was = act::idle;
  f32 was_time = 0.0f;
  f32 blend = 0.0f;
  rgba tint{1.0f, 1.0f, 1.0f, 1.0f};
  f32 lift = 0.0f;  // 3D units above the sand (sidewalks, floors)
  u32 identity = 0; // Stable body/tone seed; 0 = reference proportions.
  person_style style{};
};

void person_init(context &ctx);
void person_cleanup(context &ctx);
bool person_ready();
// Only between begin_3d() and end_3d().
void draw_person(context &ctx, const person_draw &p);

} // namespace sandtable
