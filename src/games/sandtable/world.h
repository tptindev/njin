#pragma once

#include "city/city.h"
#include "city/render.h"
#include "types.h"

namespace sandtable {

// The city the game is played on, and how it is shown. Gameplay reads the
// map; nothing but world_generate() changes it.

const city::city_map &world();
city::view_options &world_view();

// Makes the city of `seed` (state.seed), validates it, logs what came out and
// rebuilds its drawing.
void world_generate(context &ctx, u32 seed);

// Debug keys: N/B the next or previous seed, R a random one, F1 the ground
// overlay, F2 labels, F3 the road graph, F4 pins, T jumps to night (21:00) or
// back to day (12:00). Looking inside: a left click on a building focuses it
// (world_focus), a click beside it or Esc lets go (world_unfocus); C cuts
// open every building round the middle of the view when close and nothing is
// in focus. PgUp/PgDn (or ] and [) go up and down the floors of what is open.
void world_input(context &ctx);

// Focuses building `id`: it alone is cut open, and the camera turns to its
// front, looks steeply down and comes close, centred on it. Only one building
// is in focus at a time: another replaces it. The camera as it was before
// the first focus is kept for world_unfocus().
void world_focus(i32 id);
// Lets go of the building in focus, and the camera goes back to where it was.
void world_unfocus();

// Works out which buildings are drawn cut open this frame (after the camera
// has moved).
void world_update_view();

// Cutting open everything round the middle of the view (the C key).
void world_cut_around(bool on);
bool world_cut_around_on();

} // namespace sandtable
