#pragma once

#include "types.h"

namespace sandtable {

// The townsfolk, living by the town's clock (clock.h). Each has a home, most
// a job at one of the town's businesses with a shift to keep, and the hours
// he sleeps. Awake and free, he goes for a meal at mealtimes, out for the
// evening (karaoke, a bar, a café), on an errand to an open shop, home for a
// while, or for a stroll. Indoors he is not on the table; the streets fill in
// the morning and empty at night, but for the night owls. Customers sit on
// the plastic stools of the street eateries while they are open.

// Puts a new crowd on the current city (world()), the same for the same seed:
// each walker a physics character (physics.h), each seated customer an
// obstacle. After physics_build().
void crowd_spawn(context &ctx, u32 seed);
// Each fixed step, after the clock: who leaves where he is, where each walker
// has got to (the physics moved him), and the velocity he wants next, toward
// the next point of his path.
void crowd_step(context &ctx, f32 dt);
// Each frame: the motions play on.
void crowd_update(f32 dt);

// How well they walk, for the test run: walkers standing in one another or
// in a building (should be none), and those given up on as stuck.
struct crowd_report {
  i32 residents = 0; // all the townsfolk but the seated customers
  i32 asleep = 0;    // at home, in the night
  i32 at_work = 0;
  i32 seated = 0;    // on the stools now
  i32 walkers = 0;   // out on the street
  i32 overlapping = 0;
  i32 in_buildings = 0;
  i32 repaths = 0;
};
crowd_report crowd_check();
// Draws those near the camera. Between begin_3d() and end_3d().
void crowd_draw(context &ctx);
// Draws those within `range` world units of `at`, for a second eye (feeds.h).
void crowd_draw_around(context &ctx, vec2 at, f32 range);
i32 crowd_size();

} // namespace sandtable
