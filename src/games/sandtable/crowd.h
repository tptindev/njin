#pragma once

#include "types.h"

namespace sandtable {

// The townsfolk: passers-by walking the sidewalks and alleys on the city's
// foot nav grid, stopping to talk, and customers sitting on the plastic
// stools of the street eateries. Background life, until the gangs' men walk
// the same streets.

// Puts a new crowd on the current city (world()), the same for the same seed.
void crowd_spawn(u32 seed);
void crowd_update(f32 dt);
// Draws those near the camera. Between begin_3d() and end_3d().
void crowd_draw(context &ctx);
i32 crowd_size();

} // namespace sandtable
