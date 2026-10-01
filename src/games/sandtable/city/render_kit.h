#pragma once

// The buildings from outside, put together from the game's own procedural
// building kit (assets/models/procedural_building, pbk.h): 105 modules in
// three styles, a 2 m bay wide and a 3 m storey high, chosen and placed by
// the IDs of its export manifest. Each side of a building is cut into bays
// (stretched along the wall to fit its length; the rigged street door never
// stretched); the front gets shopfronts, the street door, windows and
// balconies, the corners columns, the top a parapet or a roof of the kit's
// slopes, ridges and gables, laid as the kit's own generator lays them. Water tanks and the pagoda are boxes of our own.
//
// The houses the kit's rules can lay out in full (render_pbk.cpp) are drawn
// from their plan instead and skipped here; every other building of the town
// is drawn here, chunk by chunk as render_lod culls them; the cutaway
// (render_cutaway.cpp) builds the floors under an open one from the same
// modules.

#include "render_facade.h"
#include "render_lod.h"

#include <string>
#include <vector>

namespace sandtable::city {

// The kit's modules, in the manifest's order (kit_init loads them all).
i32 kit_module_count();
const std::string &kit_module_id(i32 i);
model_handle kit_model(i32 i);

// Where a building's modules go: one batch per module, and the boxes of what
// the kit has not got (tanks, the pagoda, a flat roof's slab) with the facade parts.
struct kit_sink {
  std::vector<instances *> parts; // kit_module_count() of them
  facade_batches extras;
};

// Loads the modules once (view_init), and lets them go at the end.
void kit_init(context &ctx);
void kit_shutdown(context &ctx);

// Building `b` from the kit into `out`: its storeys below `floors` (all of
// them for the city; the ones under the open floor for the cutaway), and,
// with `roof`, its parapet and roof.
void kit_building(const building &b, const city_map &map, i32 floors, bool roof, kit_sink &out);

// Every building, laid out chunk by chunk (view_build), and drawn where the
// chunk is in view, less the buildings open in the cutaway and those the
// procedural plans draw.
void kit_build(context &ctx, const city_map &map);
void kit_draw(context &ctx, const view_options &opt);
void kit_cleanup(context &ctx);

} // namespace sandtable::city
