#pragma once

#include "types.h"

namespace sandtable {

// The townsfolk: passers-by walking the sidewalks and alleys on the city's
// foot nav grid, stopping to talk, and customers sitting on the plastic
// stools of the street eateries. Background life, until the gangs' men walk
// the same streets.

// Puts a new crowd on the current city (world()), the same for the same seed:
// each walker a physics character (physics.h), each seated customer an
// obstacle. After physics_build().
void crowd_spawn(context &ctx, u32 seed);
// Each fixed step: where each walker has got to (the physics moved him), and
// the velocity he wants next, toward the next point of his path.
void crowd_step(context &ctx, f32 dt);
// Each frame: the motions play on.
void crowd_update(f32 dt);

// How well they walk, for the test run: walkers standing in one another or
// in a building (should be none), and those given up on as stuck.
struct crowd_report {
  i32 walkers = 0;
  i32 overlapping = 0;
  i32 in_buildings = 0;
  i32 repaths = 0;
};
crowd_report crowd_check();
// Draws those near the camera. Between begin_3d() and end_3d().
void crowd_draw(context &ctx);
i32 crowd_size();

} // namespace sandtable
