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
// overlay, F2 labels, F3 the road graph, F4 pins. Looking inside: a left
// click picks a building and cuts it open (Esc lets go), C cuts open every
// building round the middle of the view when close. PgUp/PgDn (or ] and [)
// go up and down the floors of what is open.
void world_input(context &ctx);

// Works out which buildings are drawn cut open this frame (after the camera
// has moved).
void world_update_view();

// Cutting open everything round the middle of the view (the C key).
void world_cut_around(bool on);

} // namespace sandtable
