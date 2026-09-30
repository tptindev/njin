#pragma once

#include "types.h"

namespace sandtable {

// The one battle on the table.
const level_def &current_level();

// --- Terrain -----------------------------------------------------------------
//
// Each level's table is a grid of tiles made when the level loads: noise for
// hills, mountains and woods, the level's own features stamped on top, a
// river and streams carved through. For now troops move on the low ground
// only: hills and mountains are in the way, the river is crossed only at
// fords. They find their way round with a nav grid built from it.

enum class terrain : u8 { plain = 0, hill, mountain, river, stream, forest, ford };

inline constexpr f32 tile_world = 32.0f; // world units per tile (8 pixels of 4 units)
inline constexpr i32 tiles_x = static_cast<i32>(world_width / tile_world);
inline constexpr i32 tiles_y = static_cast<i32>(world_height / tile_world);

// Builds the current level's terrain. load_level() calls it.
void build_terrain();
// Goes up by one each time the terrain is built, so a drawing of it knows to
// build itself again.
u32 terrain_version();

terrain terrain_at(vec2 pos);
terrain terrain_cell(i32 x, i32 y);
// Whether men can be at `pos`: on the low ground, fords and streams.
bool walkable(vec2 pos);
// The nav grid men find their way on.
const nav_grid &terrain_nav();

// How fast men move on it, 1 on open plain.
f32 terrain_speed(terrain t);
const char *terrain_name(terrain t);

} // namespace sandtable
