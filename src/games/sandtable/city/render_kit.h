#pragma once

// The buildings from outside, put together from Quaternius' Downtown City
// MegaKit (CC0, assets/models/city, copied in by tools/make_city_kit.py):
// painted plaster fronts, red brick, glass and metal. Each side of a
// building is cut into panels of the kit's width (2 or 4 m, stretched to fit)
// and one storey high; the front gets windows, a shop or a door, the top a
// cornice. The Vietnamese street things stay: balconies, awnings and signs,
// tiled roofs, water tanks.
//
// Every building of the town, drawn chunk by chunk as render_lod culls them;
// the cutaway (render_cutaway.cpp) builds the floors under an open one from
// the same pieces.

#include "render_facade.h"
#include "render_lod.h"

#include <array>

namespace sandtable::city {

enum class piece : u8 {
  trim_plain, trim_window, trim_shop, trim_ground, trim_column, trim_guard, trim_cornice, trim_door_frame,
  brick_plain, brick_window, brick_window_trim, brick_arches, brick_ground, brick_top, brick_corner,
  brick_cornice, brick_door_frame,
  metal_plain, metal_window_half, metal_window_full, metal_window_wide, metal_shop, metal_ground,
  metal_column, metal_cornice, metal_door_frame,
  door_wood, door_glass, ac_unit, bollard, planter, manhole,
  count
};
inline constexpr i32 piece_count = static_cast<i32>(piece::count);

// Where a building's pieces go: one batch per piece, and the boxes of what
// the kit has not got (balconies, signs, roofs) with the facade parts.
struct kit_sink {
  std::array<instances *, piece_count> parts{};
  facade_batches extras;
};

// Loads the pieces once (view_init), and lets them go at the end.
void kit_init(context &ctx);
void kit_shutdown(context &ctx);

// Building `b` from the kit into `out`: its storeys below `floors` (all of
// them for the city; the ones under the open floor for the cutaway), and,
// with `roof`, its cornice and roof.
void kit_building(const building &b, const city_map &map, i32 floors, bool roof, kit_sink &out);

// Every building, laid out chunk by chunk (view_build), and drawn where the
// chunk is detailed, less the buildings open in the cutaway.
void kit_build(context &ctx, const city_map &map);
void kit_draw(context &ctx, const view_options &opt);
void kit_cleanup(context &ctx);

// The model of each piece, for drawing a sink of one's own (the cutaway).
model_handle kit_model(piece p);

} // namespace sandtable::city
