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
// Whether troops can be at `pos`: on the low ground, or for boats on water.
bool walkable(vec2 pos, bool boat = false);
// The nav grid for troops on foot, or for boats (water only).
const nav_grid &terrain_nav(bool boat = false);
// The middle of the water tile nearest `pos` (river, ford or stream), and
// how far it is; no water at all gives `pos` and a huge distance.
vec2 nearest_water(vec2 pos, f32 *dist = nullptr);

// How fast troops move on it, 1 on open plain (or open river for boats).
f32 terrain_speed(terrain t, bool boat = false);
// Arrows lose half their damage on troops standing in it.
bool terrain_covers(terrain t);
const char *terrain_name(terrain t);

} // namespace sandtable
