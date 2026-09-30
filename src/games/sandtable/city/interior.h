#pragma once

#include "city.h"

#include <vector>

// A building's inside, laid out on a grid once it is cut open to look into
// (render_cutaway.cpp draws it with the PSX interior kit,
// assets/models/interior/, a curated slice of a modular house/interior pack).
// Pure data: no window, no context; the same building always lays out the
// same way, seeded from building::look.

namespace sandtable::city {

inline constexpr f32 interior_cell = 22.0f;         // world units, one room-grid cell (~4x4 m of the kit)
inline constexpr f32 kit_unit = interior_cell / 4.0f; // world units per kit file unit
inline constexpr f32 cut_height = 9.0f;             // walls of a cut-open building stop here (world units)
inline constexpr f32 door_width = 9.0f;             // a doorway's gap in a wall (world units)

enum class room_kind : u8 {
  living,
  shop,
  bedroom,
  kitchen,
  bathroom,
  stair,
  corridor,
  hotel_room,
  apartment_unit,
  classroom,
  hall_market,
  hall_temple,
  hall_storage,
  office,
  altar, // phòng thờ: the ancestors' room, up at the front of a tube house's top floor
  // A gang's headquarters (build_interior of a building set_gang_hqs() names):
  lounge,      // sảnh anh em: where the men sit about, ground floor
  meeting,     // phòng họp: a long table, the floors between
  boss_office, // văn phòng đại ca: the top floor
  count
};

enum class floor_finish : u8 { wood, tiles };

enum class wall_kind : u8 {
  none,  // open to the next cell: the same room, or a shopfront left open
  solid, // a full wall, no way through
  door,  // a wall with a doorway in it
};

struct interior_room {
  room_kind kind = room_kind::living;
  floor_finish floor = floor_finish::wood;
};

// One grid cell: which room it is part of, and the wall on its own two sides
// (west, toward -u; south, toward -v, the building's front). The wall between
// this cell and its neighbour to the west (or south) is drawn once, from
// here; a cell's east and north only matter (and are drawn) on the last
// column or row, where they are the building's own outer wall. A door's
// place along its wall is a fraction from the wall's low end (-u or -v).
struct grid_cell {
  i32 room = -1;
  wall_kind west = wall_kind::solid;
  wall_kind south = wall_kind::solid;
  wall_kind east = wall_kind::none;
  wall_kind north = wall_kind::none;
  f32 west_door = 0.5f, south_door = 0.5f, east_door = 0.5f, north_door = 0.5f;
};

// A piece of furniture as the renderer draws it: `piece` is a base file name
// under assets/models/interior/, `pos` where the model's own origin goes
// (table coordinates), `angle` its turn (njin::obb::angle convention),
// `scale` on top of kit_unit, `lift` world units off the floor (something
// set on a table or a shelf).
struct furn_item {
  const char *piece = nullptr;
  vec2 pos{};
  f32 angle = 0.0f;
  f32 scale = 1.0f;
  f32 lift = 0.0f;
};

// The stairs of a stair room, from this floor up to the next: the flight's
// top is at `top` (table coordinates), it runs down toward `angle`+180;
// `sx`/`sy`/`sz` fit the kit's flight (2 x 4 x 8 file units) to the room and
// to one storey's height.
struct stair_flight {
  vec2 top{};
  f32 angle = 0.0f;
  f32 sx = 1.0f, sy = 1.0f, sz = 1.0f;
};

struct interior_layout {
  i32 nx = 0, nz = 0; // columns (along the front), rows (front to back)
  // World units a cell actually is, along u and v: the building's own width
  // and depth divided evenly by nx and nz, so the grid fills its footprint
  // exactly. Close to interior_cell but not always equal to it.
  f32 cell_x = interior_cell, cell_z = interior_cell;
  // A tube house's walkway: the side (-1 along -u, +1 along +u, 0 none) where
  // every door between its rooms lines up, kept clear of furniture front to
  // back.
  i32 passage = 0;
  std::vector<interior_room> rooms;
  std::vector<grid_cell> cells; // nx * nz, row-major: cells[z * nx + x]
  std::vector<furn_item> furniture;
  std::vector<stair_flight> stairs;

  const grid_cell &cell(i32 x, i32 z) const { return cells[static_cast<size_t>(z * nx + x)]; }
  grid_cell &cell(i32 x, i32 z) { return cells[static_cast<size_t>(z * nx + x)]; }
};

// The inside of `b` on floor `floor` (0 the ground floor, up to
// building::floors - 1): each floor its own rooms, the stairs in the same
// place on every floor.
interior_layout build_interior(const building &b, i32 floor = 0);

// The buildings that are gangs' headquarters (gameplay decides, gang.cpp):
// they are laid out as one, whatever they were built as. The ground floor is
// the men's lounge, the floors between meeting rooms, the top floor the
// boss's office (with one floor, the office walled off at the back of the
// lounge).
void set_gang_hqs(std::vector<const building *> hqs);
bool is_gang_hq(const building &b);

// Puts the furniture into the rooms of a laid-out floor
// (interior_furnish.cpp; build_interior() calls it).
void furnish_interior(const building &b, interior_layout &L, rng &r);

} // namespace sandtable::city
