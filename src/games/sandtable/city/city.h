#pragma once

#include <njin.h>

#include <string>
#include <vector>

// The city on the table, made from a seed.
//
// generate() turns a seed and a few settings into a whole town: rivers and a
// lake, avenues, streets and alleys (hẻm), the blocks they close, districts,
// houses and landmarks, the businesses that pay for protection, the open
// places where gangs meet, street furniture, and the nav grids men and
// vehicles find their way on. It is plain data: no window, no context, so it
// runs in the batch check (--citycheck) as well as in the game, and the same
// seed and settings always give the same city (city_map::hash).
//
// Coordinates are table coordinates (world units, view.h): x to the right,
// y toward the player, about 7 units to the metre. Angles are degrees from
// +x toward +y.
//
// Nothing here is drawn: city_render.* draws it, gameplay reads it.

namespace sandtable::city {
using namespace njin;

// --- Scale ------------------------------------------------------------------

// The scale of the whole city: world units a metre. A storey of the city kit
// (render_kit.cpp) is 3 m, the interior kit's room cell about 4 m.
inline constexpr f32 units_per_metre = 6.0f;
inline constexpr f32 person_height = 1.68f * units_per_metre; // a man, 1.68 m
inline constexpr f32 floor_height = 3.0f * units_per_metre;   // a storey, 3 m

// --- Settings ---------------------------------------------------------------

struct city_desc {
  u32 seed = 1;
  f32 width = 4800.0f;  // world units (800 m)
  f32 height = 3072.0f; // 512 m
  f32 cell = 8.0f;      // size of a raster cell
  // < 0: the seed decides; 0 off, 1 on.
  i32 river = -1;
  i32 lake = -1;
  i32 districts = 0;    // 0: the seed decides (7 to 9)
  f32 density = 1.0f;   // scales block sizes: above 1 bigger blocks, fewer streets
};

// --- Kinds ------------------------------------------------------------------

enum class road_kind : u8 { avenue = 0, street, alley, ring };

enum class district_kind : u8 {
  old_quarter = 0, // phố cổ: tiny blocks, narrow tube houses, shops everywhere
  market,          // chợ: the market hall, stalls, eateries
  residential,     // xóm: big blocks full of alleys and small houses
  nightlife,       // ăn chơi: bars, karaoke, hotels
  docks,           // bến cảng: warehouses and container yards by the river
  industrial,      // xưởng: workshops and yards
  new_urban,       // khu mới: a straight grid at an angle, towers, banks
  count
};

enum class ground : u8 {
  free = 0, // inside a block, nothing on it (gaps, yards)
  road,     // carriageway or sidewalk of any road (see cell::road)
  water,
  bridge,   // road over water
  building,
  park,     // grass, trees
  plaza,    // paved open ground: squares, the roundabout island
  lot,      // open lot: vacant ground, parking, a yard (see spots)
};

enum class building_kind : u8 {
  tube_house = 0, // nhà ống: narrow and deep, fronting a street
  house,          // small house inside a block or on an alley
  apartment,      // tall block in the new town
  warehouse,
  workshop,
  market_hall,
  pagoda,
  school,
  hotel,
  count
};

enum class business_kind : u8 {
  cafe = 0,
  street_food,   // quán ăn / quán nhậu
  restaurant,
  grocery,       // tạp hóa
  pharmacy,
  gold_shop,     // tiệm vàng
  pawn_shop,     // tiệm cầm đồ
  karaoke,
  bar,
  billiards,
  massage,
  guest_house,   // nhà nghỉ
  hotel,
  bike_repair,   // sửa xe
  gas_station,
  market,
  warehouse,
  workshop,
  gambling_den,  // sòng bạc, hidden in an alley
  count
};

// Open places where things happen: meetings, fights, deals, hand-overs.
enum class spot_kind : u8 {
  vacant_lot = 0,
  parking,
  sports_field,
  market_square,
  container_yard,
  park,
  wharf,
  bridge,        // a chokepoint over the river
  dead_end,      // the blind end of an alley
  roundabout,
  count
};

enum class prop_kind : u8 {
  tree = 0,
  lamp,
  pole,        // electricity pole with its tangle of wires
  motorbike,
  stool,       // a plastic stool, set out by a street eatery
  stall,       // a market stall under an awning
  container,
  boat,
  bench,
  monument,
  count
};

const char *district_name(district_kind k);
const char *building_name(building_kind k);
const char *business_name(business_kind k);
const char *spot_name(spot_kind k);

// --- The parts --------------------------------------------------------------

struct road {
  road_kind kind = road_kind::street;
  f32 width = 36.0f;    // carriageway
  f32 sidewalk = 8.0f;  // each side
  std::vector<vec2> pts;
  std::string name;
  i32 parent = -1;      // an alley: the road its mouth opens onto
  f32 length = 0.0f;
  f32 reach() const { return width * 0.5f + sidewalk; }
};

// Where roads meet or end: the road graph for vehicles and for gameplay
// (checkpoints, ambushes at a junction).
struct road_node {
  vec2 pos{};
  std::vector<i32> edges;
  bool roundabout = false;
};

struct road_edge {
  i32 road = -1;
  i32 a = -1, b = -1;   // nodes
  std::vector<vec2> pts;
  f32 length = 0.0f;
};

struct district {
  district_kind kind = district_kind::residential;
  std::string name;
  vec2 site{};          // the seed point it grew from
  vec2 centroid{};
  i32 cells = 0;
  std::vector<i32> blocks;
  std::vector<i32> neighbours;
  f32 grain = 0.0f;     // the angle its streets tend to follow
};

// Ground closed by avenues and streets (alleys are inside blocks): the unit a
// gang holds.
struct block {
  i32 district = -1;
  i32 cells = 0;
  rect bounds{};
  vec2 centroid{};
  f32 axis = 0.0f;      // the long direction
  std::vector<i32> neighbours;
  std::vector<i32> buildings;
  std::vector<i32> businesses;
  std::vector<i32> spots;
};

// An oriented rectangle: `half.x` along `angle`, `half.y` across it.
struct obb {
  vec2 center{};
  vec2 half{};
  f32 angle = 0.0f;
  vec2 axis_x() const { return from_angle(angle); }
  vec2 axis_y() const { return from_angle(angle + 90.0f); }
  bool contains(vec2 p) const;
  vec2 corner(i32 i) const;
};

struct building {
  building_kind kind = building_kind::house;
  obb box;              // footprint; axis_y points from the front to the back
  f32 height = 30.0f;   // world units
  i32 floors = 2;
  i32 block = -1;
  i32 district = -1;
  i32 road = -1;        // the road it fronts, -1 inside a block
  i32 number = 0;       // house number on that road
  i32 business = -1;
  vec2 door{};          // just outside the front, where men go in
  bool door_ok = false; // the door opens on walkable ground
  u32 look = 0;         // colours, roof, water tank: city_render picks from this
  vec2 front() const { return -box.axis_y(); }
};

struct business {
  business_kind kind = business_kind::cafe;
  i32 building = -1;
  i32 block = -1;
  i32 district = -1;
  vec2 door{};
  std::string name;
  std::string address;
  i32 income = 0;       // a day's takings, in thousands of đồng
  i32 protection = 0;   // what it can pay a gang a week
  i32 tier = 1;         // 1 small, 2 middling, 3 big
};

struct spot {
  spot_kind kind = spot_kind::vacant_lot;
  obb box;
  i32 block = -1;
  i32 district = -1;
};

struct prop {
  prop_kind kind = prop_kind::tree;
  vec2 pos{};
  f32 angle = 0.0f;
  f32 scale = 1.0f;
  u32 look = 0;
};

// One raster cell.
struct cell_info {
  ground g = ground::free;
  road_kind road = road_kind::street; // for road and bridge cells
  bool carriage = false;              // on the carriageway, not the sidewalk
  u8 district = 0;
  i32 block = -1;
  i32 building = -1;
  i32 road_id = -1;
};

struct lake_shape {
  bool on = false;
  vec2 center{};
  f32 radius = 0.0f;
  std::vector<f32> r; // radius every 360/r.size() degrees
  f32 radius_at(f32 degrees) const;
};

struct river_shape {
  bool on = false;
  f32 width = 0.0f;
  std::vector<vec2> pts;
};

struct city_report {
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
  f32 gen_ms = 0.0f;
  struct stage_time {
    const char *name;
    f32 ms;
  };
  std::vector<stage_time> stages; // how long each step of generate() took
  i32 caps_hit = 0; // loops that stopped at their safety cap
  bool ok() const { return errors.empty(); }
};

// --- The city ---------------------------------------------------------------

struct city_map {
  city_desc desc;
  i32 cols = 0, rows = 0;
  std::vector<cell_info> cells;

  river_shape river;
  lake_shape lake;
  std::vector<road> roads;
  std::vector<road_node> nodes;
  std::vector<road_edge> edges;
  std::vector<district> districts;
  std::vector<block> blocks;
  std::vector<building> buildings;
  std::vector<business> businesses;
  std::vector<spot> spots;
  std::vector<prop> props;
  // Buildings a gang could set up in, spread out over the map, best first.
  std::vector<i32> hq_sites;

  nav_grid foot; // men: roads, alleys, open ground
  nav_grid car;  // vehicles: carriageways of avenues and streets

  city_report report;
  u64 hash = 0;

  bool inside(i32 x, i32 y) const { return x >= 0 && y >= 0 && x < cols && y < rows; }
  const cell_info *cell_at(vec2 p) const;
  cell_info *cell_at(vec2 p);
  const cell_info &at(i32 x, i32 y) const { return cells[static_cast<size_t>(y * cols + x)]; }
  cell_info &at(i32 x, i32 y) { return cells[static_cast<size_t>(y * cols + x)]; }
  vec2 center_of(i32 x, i32 y) const { return {(x + 0.5f) * desc.cell, (y + 0.5f) * desc.cell}; }

  i32 block_at(vec2 p) const;
  i32 district_at(vec2 p) const;
  i32 building_at(vec2 p) const;
  i32 road_at(vec2 p) const;
  bool walkable(vec2 p) const;
  // Whether `p` is on a road's carriageway or sidewalk (measured on the road's
  // centre line, not the raster), by more than `slack` units.
  bool on_road(vec2 p, f32 slack = 0.0f) const;
};

// Makes the whole city. Never loops forever: every search has a cap, and a
// cap reached is counted in report.caps_hit.
void generate(city_map &out, const city_desc &desc);

// Checks what generate() promises: no overlaps, every business reachable on
// foot, the street network in one piece, no scrap blocks. Fills
// map.report.errors / warnings.
void validate(city_map &map);

// A hash of everything generated, to check that a seed is reproducible.
u64 city_hash(const city_map &map);

// --citycheck: generates `count` seeds from `first`, validates each, checks two
// runs of a seed agree, prints a line per seed. Returns the number of seeds
// that failed.
i32 run_city_check(u32 first, i32 count, bool verbose);

// --citymap: writes the raster of a seed as a picture (PPM), one pixel a cell.
bool write_city_ppm(const city_map &map, const char *path);

} // namespace sandtable::city
