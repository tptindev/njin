#include "interior.h"

#include <algorithm>
#include <cmath>

// Lays a building's inside out on a grid of cells: which rooms it has, the
// walls between them and where their doors are, and the stairs. The
// furniture is interior_furnish.cpp's; render_cutaway.cpp draws it all.

namespace sandtable::city {

namespace {

i32 cells_across(f32 size, i32 lo, i32 hi) {
  return std::clamp(static_cast<i32>(std::lround(size / interior_cell)), lo, hi);
}

// nx by nz cells filling the building's footprint exactly.
void grid(const building &b, interior_layout &L, i32 nx, i32 nz) {
  L.nx = nx;
  L.nz = nz;
  L.cell_x = b.box.half.x * 2.0f / static_cast<f32>(nx);
  L.cell_z = b.box.half.y * 2.0f / static_cast<f32>(nz);
  L.cells.assign(static_cast<size_t>(nx * nz), {});
}

void close_edges(interior_layout &L) {
  for (i32 z = 0; z < L.nz; ++z)
    L.cell(L.nx - 1, z).east = wall_kind::solid;
  for (i32 x = 0; x < L.nx; ++x)
    L.cell(x, L.nz - 1).north = wall_kind::solid;
}

// Where a door by the walkway sits in its cell's wall: close to the side
// wall, leaving it room for a frame.
f32 passage_door(const interior_layout &L, i32 side) {
  const f32 off = std::min(0.45f, (door_width * 0.5f + 2.0f) / L.cell_x);
  return side < 0 ? off : 1.0f - off;
}

// A flight of stairs up the far side of a stair row, from the front of the
// row up to its back, fitted to the room.
void add_stairs(const building &b, interior_layout &L, i32 row) {
  const f32 room_w = static_cast<f32>(L.nx) * L.cell_x;
  const f32 walkway = L.passage != 0 ? door_width + 4.0f : 0.0f;
  const f32 flight_w = std::clamp(room_w - walkway - 1.0f, 5.0f, 11.0f);
  const f32 flight_l = L.cell_z * 0.9f;
  stair_flight s;
  s.sx = flight_w / (2.0f * kit_unit);
  s.sz = flight_l / (8.0f * kit_unit);
  s.sy = floor_height / (4.0f * kit_unit); // up one storey
  // The far side from the walkway (or the left, with none).
  const f32 a = L.passage < 0 ? room_w - flight_w * 0.5f - 0.5f : flight_w * 0.5f + 0.5f;
  const f32 top_v = (static_cast<f32>(row) + 1.0f) * L.cell_z - 0.5f;
  const f32 gw = room_w, gd = static_cast<f32>(L.nz) * L.cell_z;
  s.top = b.box.center + b.box.axis_x() * (a - gw * 0.5f) + b.box.axis_y() * (top_v - gd * 0.5f);
  s.angle = b.box.angle; // the flight runs down toward the front
  L.stairs.push_back(s);
}

// --- Layout: a line of rooms front to back (nhà ống), one room a row ------------------

// What each row of a tube house is, front to back, on floor `floor`: the
// shop or the living room and the kitchen on the ground; bedrooms and a
// bathroom above; the altar room at the front of the top floor. The stair row
// is the same on every floor.
std::vector<room_kind> row_purposes(const building &b, i32 nz, i32 floor, i32 &stair_row) {
  std::vector<room_kind> purpose(static_cast<size_t>(nz), room_kind::bedroom);
  stair_row = b.floors > 1 && nz >= 3 ? 1 : -1;
  const bool top = floor == b.floors - 1 && floor > 0;
  if (floor == 0) {
    purpose[0] = b.business >= 0 ? room_kind::shop : room_kind::living;
    if (nz >= 2)
      purpose[static_cast<size_t>(nz - 1)] = room_kind::kitchen;
    const i32 bath_row = nz - 2;
    if (nz >= 4 && bath_row != stair_row && bath_row > 0)
      purpose[static_cast<size_t>(bath_row)] = room_kind::bathroom;
  } else {
    purpose[0] = top ? room_kind::altar : room_kind::bedroom;
    if (nz >= 3 && nz - 1 != stair_row)
      purpose[static_cast<size_t>(nz - 1)] = room_kind::bathroom;
  }
  if (stair_row >= 0)
    purpose[static_cast<size_t>(stair_row)] = room_kind::stair;
  return purpose;
}

void build_rows(const building &b, interior_layout &L, i32 floor) {
  grid(b, L, cells_across(b.box.half.x * 2.0f, 1, 3), cells_across(b.box.half.y * 2.0f, 1, 6));
  const bool shop = floor == 0 && b.business >= 0;
  // The same side on every floor, so the stairs and doors stack up.
  L.passage = (b.look >> 7) & 1u ? -1 : 1;
  i32 stair_row = -1;
  const std::vector<room_kind> purpose = row_purposes(b, L.nz, floor, stair_row);

  // Every door is on the walkway's side: one column, one place in it.
  const i32 door_col = L.passage < 0 ? 0 : L.nx - 1;
  const f32 door_at = passage_door(L, L.passage);
  for (i32 z = 0; z < L.nz; ++z) {
    const room_kind k = purpose[static_cast<size_t>(z)];
    const i32 room_id = static_cast<i32>(L.rooms.size());
    const bool wet = k == room_kind::kitchen || k == room_kind::bathroom || k == room_kind::shop;
    L.rooms.push_back({k, wet ? floor_finish::tiles : floor_finish::wood});
    for (i32 x = 0; x < L.nx; ++x) {
      grid_cell &c = L.cell(x, z);
      c.room = room_id;
      c.west = x == 0 ? wall_kind::solid : wall_kind::none;
      if (z == 0 && shop)
        c.south = wall_kind::none; // the shutters rolled up: the whole front open
      else
        c.south = x == door_col ? wall_kind::door : wall_kind::solid;
      c.south_door = door_at;
      if (z == 0 && floor > 0) {
        // Upstairs the front is a door onto the balcony, in the middle.
        c.south = x == L.nx / 2 ? wall_kind::door : wall_kind::solid;
        c.south_door = 0.5f;
      }
    }
  }
  close_edges(L);
  // A flight up from every floor but the top.
  if (stair_row >= 0 && floor < b.floors - 1)
    add_stairs(b, L, stair_row);
}

// A narrow building of the same room, over and over (a fallback for
// apartments and the like too narrow for a corridor).
void build_rows_uniform(const building &b, interior_layout &L, room_kind unit, i32 floor) {
  grid(b, L, cells_across(b.box.half.x * 2.0f, 1, 2), cells_across(b.box.half.y * 2.0f, 1, 6));
  for (i32 z = 0; z < L.nz; ++z) {
    const room_kind k = z == 0 ? room_kind::corridor : unit;
    const i32 room_id = static_cast<i32>(L.rooms.size());
    L.rooms.push_back({k, k == room_kind::corridor ? floor_finish::tiles : floor_finish::wood});
    for (i32 x = 0; x < L.nx; ++x) {
      grid_cell &c = L.cell(x, z);
      c.room = room_id;
      c.west = x == 0 ? wall_kind::solid : wall_kind::none;
      c.south = x == 0 && (z > 0 || floor == 0) ? wall_kind::door : wall_kind::solid;
      c.south_door = 0.5f;
    }
  }
  close_edges(L);
}

// --- Layout: a corridor down the middle, a room either side (flats, hotels, schools) ----

void build_corridor(const building &b, interior_layout &L, room_kind unit, i32 floor) {
  const i32 nx = cells_across(b.box.half.x * 2.0f, 1, 5);
  if (nx < 3) {
    build_rows_uniform(b, L, unit, floor);
    return;
  }
  grid(b, L, nx | 1, cells_across(b.box.half.y * 2.0f, 2, 8)); // odd: a middle column
  const i32 cc = L.nx / 2;
  const i32 corridor_room = static_cast<i32>(L.rooms.size());
  L.rooms.push_back({room_kind::corridor, floor_finish::tiles});
  std::vector<i32> side_room(static_cast<size_t>(L.nz * 2)); // [z*2 + 0] left, [z*2 + 1] right
  for (i32 z = 0; z < L.nz; ++z)
    for (i32 side = 0; side < 2; ++side) {
      side_room[static_cast<size_t>(z * 2 + side)] = static_cast<i32>(L.rooms.size());
      L.rooms.push_back({unit, unit == room_kind::classroom ? floor_finish::tiles : floor_finish::wood});
    }
  for (i32 z = 0; z < L.nz; ++z)
    for (i32 x = 0; x < L.nx; ++x) {
      grid_cell &c = L.cell(x, z);
      c.room = x == cc ? corridor_room : side_room[static_cast<size_t>(z * 2 + (x < cc ? 0 : 1))];
      // Into each room from the corridor, near the room's front.
      c.west = x == 0 ? wall_kind::solid : (x == cc || x == cc + 1) ? wall_kind::door : wall_kind::none;
      c.west_door = 0.3f;
      // The street door on the ground floor only; upstairs a plain wall.
      c.south = x == cc ? (z == 0 ? (floor == 0 ? wall_kind::door : wall_kind::solid) : wall_kind::none)
                        : wall_kind::solid;
      c.south_door = 0.5f;
    }
  close_edges(L);
  // The stairs up, at the back of the corridor.
  if (floor < b.floors - 1 && L.nz >= 2) {
    stair_flight s;
    const f32 w = std::min(L.cell_x - 2.0f, 10.0f);
    s.sx = w / (2.0f * kit_unit);
    s.sz = L.cell_z * 0.9f / (8.0f * kit_unit);
    s.sy = floor_height / (4.0f * kit_unit);
    const f32 gw = static_cast<f32>(L.nx) * L.cell_x, gd = static_cast<f32>(L.nz) * L.cell_z;
    const f32 a = (static_cast<f32>(cc) + 0.5f) * L.cell_x;
    s.top = b.box.center + b.box.axis_x() * (a - gw * 0.5f) + b.box.axis_y() * (gd * 0.5f - 0.5f);
    s.angle = b.box.angle;
    L.stairs.push_back(s);
  }
}

// --- Layout: one open hall, maybe an office walled off in the back corner --------------

void build_open(const building &b, interior_layout &L, room_kind kind, bool office, bool open_front,
                i32 floor) {
  grid(b, L, cells_across(b.box.half.x * 2.0f, 1, 5), cells_across(b.box.half.y * 2.0f, 1, 5));
  const i32 hall = static_cast<i32>(L.rooms.size());
  L.rooms.push_back({kind, kind == room_kind::hall_market ? floor_finish::tiles : floor_finish::wood});
  for (grid_cell &c : L.cells)
    c.room = hall;
  i32 office_room = -1;
  if (office && L.nx >= 2 && L.nz >= 2) {
    office_room = static_cast<i32>(L.rooms.size());
    L.rooms.push_back({room_kind::office, floor_finish::wood});
    L.cell(L.nx - 1, L.nz - 1).room = office_room;
  }
  for (i32 z = 0; z < L.nz; ++z)
    for (i32 x = 0; x < L.nx; ++x) {
      grid_cell &c = L.cell(x, z);
      c.west = x == 0 ? wall_kind::solid : wall_kind::none;
      c.south = z == 0 ? (open_front ? wall_kind::none : wall_kind::solid) : wall_kind::none;
      if (c.room == office_room) {
        if (x > 0 && L.cell(x - 1, z).room != office_room)
          c.west = wall_kind::door;
        if (z > 0 && L.cell(x, z - 1).room != office_room)
          c.south = wall_kind::door;
      }
    }
  if (floor > 0)
    for (i32 x = 0; x < L.nx; ++x)
      L.cell(x, 0).south = wall_kind::solid; // upstairs: no way out to the street
  else if (!open_front)
    L.cell(L.nx / 2, 0).south = wall_kind::door; // the one way in
  close_edges(L);
  // The stairs up, along the left wall at the back.
  if (floor < b.floors - 1) {
    stair_flight s;
    const f32 w = std::min(L.cell_x * 0.5f, 10.0f);
    s.sx = w / (2.0f * kit_unit);
    s.sz = std::min(L.cell_z * 0.9f, 30.0f) / (8.0f * kit_unit);
    s.sy = floor_height / (4.0f * kit_unit);
    const f32 gw = static_cast<f32>(L.nx) * L.cell_x, gd = static_cast<f32>(L.nz) * L.cell_z;
    s.top = b.box.center + b.box.axis_x() * (w * 0.5f + 0.5f - gw * 0.5f) + b.box.axis_y() * (gd * 0.5f - 0.5f);
    s.angle = b.box.angle;
    L.stairs.push_back(s);
  }
}

} // namespace

std::vector<const building *> gang_hqs;

void set_gang_hqs(std::vector<const building *> hqs) { gang_hqs = std::move(hqs); }

bool is_gang_hq(const building &b) { return std::find(gang_hqs.begin(), gang_hqs.end(), &b) != gang_hqs.end(); }

interior_layout build_interior(const building &b, i32 floor) {
  interior_layout L;
  floor = std::clamp(floor, 0, std::max(0, b.floors - 1));
  rng r(static_cast<u64>(b.look) * 31u + static_cast<u64>(floor) * 7919u);
  const bool up = floor > 0;
  if (is_gang_hq(b)) {
    // One hall a floor, the stairs at the back; the office walled off at the
    // back of the lounge when there is no floor above for it.
    const bool top = floor == b.floors - 1;
    const room_kind kind = floor == 0 ? room_kind::lounge : top ? room_kind::boss_office : room_kind::meeting;
    build_open(b, L, kind, b.floors == 1, false, floor);
    for (interior_room &room : L.rooms)
      if (room.kind == room_kind::office)
        room.kind = room_kind::boss_office;
    furnish_interior(b, L, r);
    return L;
  }
  switch (b.kind) {
  case building_kind::apartment: build_corridor(b, L, room_kind::apartment_unit, floor); break;
  case building_kind::hotel: build_corridor(b, L, room_kind::hotel_room, floor); break;
  case building_kind::school: build_corridor(b, L, room_kind::classroom, floor); break;
  case building_kind::market_hall:
    build_open(b, L, up ? room_kind::hall_storage : room_kind::hall_market, false, !up, floor);
    break;
  case building_kind::pagoda: build_open(b, L, room_kind::hall_temple, false, false, floor); break;
  case building_kind::warehouse:
  case building_kind::workshop:
    build_open(b, L, up ? room_kind::office : room_kind::hall_storage, !up, false, floor);
    break;
  default: build_rows(b, L, floor); break; // tube_house, house
  }
  furnish_interior(b, L, r);
  return L;
}

} // namespace sandtable::city
