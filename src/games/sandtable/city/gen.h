#pragma once

// Inside the city generator: shared by the gen_*.cpp files, one per group of
// stages. Games use city.h, not this.

#include "city.h"
#include "geometry.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace sandtable::city {

// --- How each kind of district is built ------------------------------------------

struct profile {
  f32 block_area;     // blocks bigger than this get a street through them
  f32 street_w, sidewalk;
  f32 organic;        // 0 ruler-straight, 1 wandering
  f32 alley_spacing;  // 0 no alleys
  i32 alley_depth;    // how many times alleys branch
  f32 alley_loop;     // chance an alley joins another instead of ending blind
  f32 front_min, front_max, depth_min, depth_max;
  i32 floors_min, floors_max;
  f32 interior;       // how full the inside of blocks gets with small houses
  f32 business;       // share of street-front houses that are businesses
};

inline constexpr profile profiles[] = {
    // old quarter: tiny blocks of narrow deep tube houses
    {230.0f * 190.0f, 30.0f, 6.0f, 0.5f, 170.0f, 1, 0.35f, 22.0f, 32.0f, 70.0f, 120.0f, 2, 4, 1.0f, 0.62f},
    // market
    {300.0f * 240.0f, 36.0f, 8.0f, 0.4f, 150.0f, 2, 0.3f, 26.0f, 36.0f, 60.0f, 100.0f, 2, 4, 0.8f, 0.5f},
    // residential: big blocks, the alleys do the rest
    {540.0f * 420.0f, 36.0f, 7.0f, 0.7f, 120.0f, 3, 0.25f, 30.0f, 42.0f, 56.0f, 96.0f, 2, 4, 1.0f, 0.28f},
    // nightlife
    {320.0f * 260.0f, 40.0f, 9.0f, 0.3f, 190.0f, 1, 0.3f, 30.0f, 46.0f, 60.0f, 100.0f, 3, 6, 0.6f, 0.62f},
    // docks
    {500.0f * 380.0f, 44.0f, 8.0f, 0.2f, 0.0f, 0, 0.0f, 40.0f, 70.0f, 60.0f, 110.0f, 1, 3, 0.0f, 0.22f},
    // industrial
    {540.0f * 420.0f, 44.0f, 8.0f, 0.15f, 260.0f, 1, 0.1f, 44.0f, 80.0f, 60.0f, 110.0f, 1, 3, 0.0f, 0.2f},
    // new town: straight grid at an angle
    {400.0f * 320.0f, 48.0f, 12.0f, 0.0f, 0.0f, 0, 0.0f, 36.0f, 60.0f, 50.0f, 90.0f, 4, 9, 0.0f, 0.36f},
};

inline const profile &prof(district_kind k) { return profiles[static_cast<i32>(k)]; }

constexpr f32 avenue_w = 60.0f, avenue_side = 14.0f;
constexpr f32 alley_w = 18.0f;
constexpr f32 min_block_area = 110.0f * 100.0f;
constexpr f32 min_block_thick = 80.0f;
constexpr f32 floor_h = floor_height;

// Lots in whole bays of the building kit (2 m), at least `min_bays`: so
// every facade is whole modules and the kit's rules can lay the inside out.
constexpr f32 bay_w = 2.0f * units_per_metre;
inline f32 in_bays(f32 len, i32 min_bays) {
  return std::max(static_cast<f32>(min_bays), std::round(len / bay_w)) * bay_w;
}
// The kit's rules hold a building to 8 floors (building_rules.json).
constexpr i32 max_floors = 8;

// --- Name lists (names.cpp) ------------------------------------------------------

extern const char *const street_names[];
extern const i32 street_name_count;
extern const char *const district_names[][6];
extern const char *const owner_names[];
extern const i32 owner_name_count;
extern const char *const food_names[];
extern const i32 food_name_count;

// --- The generator ------------------------------------------------------------------

struct generator {
  city_map &m;
  const city_desc &d;
  f32 cs = 8.0f;

  std::vector<obb> footprints; // every building and spot, for overlap tests
  std::vector<std::vector<i32>> buckets;
  i32 bcols = 0, brows = 0;
  static constexpr f32 bucket = 64.0f;

  std::vector<i32> label;   // per cell, which free region
  std::vector<i32> stamp;   // per cell, scratch marks
  i32 stamp_gen = 0;

  std::vector<i32> name_order;
  i32 next_name = 0;
  // Per road: its street name and alley number path ("45/12"), for addresses.
  std::vector<std::string> road_base;
  std::vector<std::string> road_number;

  generator(city_map &map, const city_desc &desc) : m(map), d(desc), cs(desc.cell) {}

  rng stage_rng(u32 salt) const;
  noise_desc noise(u32 salt, f32 freq, i32 octaves = 3) const;

  i32 idx(i32 x, i32 y) const { return y * m.cols + x; }
  bool cell_of(vec2 p, i32 &x, i32 &y) const;

  // --- Roads -----------------------------------------------------------------

  static i32 prio(road_kind k);

  void raster_road(i32 id);

  i32 add_road(road r);

  std::string take_street_name();

  // --- Footprints ----------------------------------------------------------------

  void bucket_range(const obb &b, i32 &bx0, i32 &by0, i32 &bx1, i32 &by1) const;

  void add_footprint(const obb &b);

  bool overlaps_footprint(const obb &b) const;

  template <typename F> void for_cells_in(const obb &b, F &&fn) const {
    f32 minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
    for (i32 i = 0; i < 4; ++i) {
      const vec2 c = b.corner(i);
      minx = std::min(minx, c.x);
      maxx = std::max(maxx, c.x);
      miny = std::min(miny, c.y);
      maxy = std::max(maxy, c.y);
    }
    const i32 x0 = static_cast<i32>(std::floor(minx / cs)), x1 = static_cast<i32>(std::floor(maxx / cs));
    const i32 y0 = static_cast<i32>(std::floor(miny / cs)), y1 = static_cast<i32>(std::floor(maxy / cs));
    for (i32 y = y0; y <= y1; ++y)
      for (i32 x = x0; x <= x1; ++x)
        if (b.contains({(x + 0.5f) * cs, (y + 0.5f) * cs}))
          fn(x, y);
  }

  bool fits(const obb &b, i32 blk) const;

  bool off_roads(vec2 p, i32 x, i32 y) const;

  i32 add_building(const obb &b, building_kind kind, i32 floors, i32 road_id, u32 look);

  i32 add_spot(const obb &b, spot_kind kind, ground g, bool footprint = true);

  bool front_box(i32 road_id, const polyline_walk &walk, f32 s, i32 side, f32 w, f32 depth, f32 gap, obb &out) const;

  // --- Stage 1: the ground ----------------------------------------------------------

  void init();

  void make_river();

  // Calls fn once for every cell whose centre is within `r` of the polyline.
  template <typename F> void for_cells_near(const std::vector<vec2> &pts, f32 r, F &&fn) {
    ++stamp_gen;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
      const vec2 a = pts[i], b = pts[i + 1];
      const i32 x0 = std::max(0, static_cast<i32>((std::min(a.x, b.x) - r) / cs) - 1);
      const i32 x1 = std::min(m.cols - 1, static_cast<i32>((std::max(a.x, b.x) + r) / cs) + 1);
      const i32 y0 = std::max(0, static_cast<i32>((std::min(a.y, b.y) - r) / cs) - 1);
      const i32 y1 = std::min(m.rows - 1, static_cast<i32>((std::max(a.y, b.y) + r) / cs) + 1);
      for (i32 y = y0; y <= y1; ++y)
        for (i32 x = x0; x <= x1; ++x) {
          i32 &st = stamp[static_cast<size_t>(idx(x, y))];
          if (st != stamp_gen && seg_dist(m.center_of(x, y), a, b) <= r) {
            st = stamp_gen;
            fn(x, y);
          }
        }
    }
  }

  f32 river_y(f32 x) const;

  // --- Stage 2: districts --------------------------------------------------------------

  void make_districts();

  // --- Stage 3: avenues ----------------------------------------------------------------

  road avenue(std::vector<vec2> pts);

  void make_avenues();

  // --- Stage 4: a lake with a park and a road round it ---------------------------------

  static constexpr f32 lake_park = 36.0f, lake_ring_w = 36.0f, lake_ring_side = 8.0f;
  static f32 lake_ring_at(const lake_shape &lk, f32 degrees);

  void keep_off_lake(std::vector<vec2> &pts) const;

  void make_lake();

  // --- Stage 5: a roundabout where the boulevard meets an avenue ------------------------

  void make_roundabout();

  // --- Stage 6: streets, splitting blocks until they are the size their district wants ----

  struct region {
    std::vector<i32> cells;
  };

  void label_free(std::vector<region> &out);

  struct shape_stats {
    vec2 c{};
    f32 angle = 0.0f;
    f32 umin = 0, umax = 0, vmin = 0, vmax = 0;
    f32 len() const { return umax - umin; }
    f32 wid() const { return vmax - vmin; }
  };

  shape_stats stats(const std::vector<i32> &cells) const;


  i32 majority_district(const std::vector<i32> &cells) const;

  bool march(vec2 start, f32 heading, i32 rid, f32 organic, f32 phase, std::vector<vec2> &out, i32 &hit_cell, bool &hit_edge);

  bool try_split(const region &rg, i32 rid, const shape_stats &st, const district &dist, rng &r, bool big);

  f32 region_factor(i32 first_cell) const;

  void make_streets();
  i32 car_pieces(std::vector<i32> &comp, std::vector<i32> &sizes) const;
  void connect_streets();

  // --- Stage 7: blocks ----------------------------------------------------------------------

  void make_blocks();

  // --- Stage 8: landmarks, reserved before anything else is built ------------------------------

  struct front_spot {
    i32 road;
    f32 s;
    i32 side;
  };

  std::vector<front_spot> frontage_in(i32 district_id, bool avenues_only, rng &r) const;

  bool place_landmark(i32 district_id, bool avenue, f32 w, f32 depth, f32 apron, obb &box, obb &front, rng &r, i32 &road_id);

  void make_landmarks();

  // --- Stage 9: alleys (hẻm), growing into the blocks from the streets ---------------------------

  bool alley_clear(vec2 p, f32 clear, i32 blk, vec2 origin, f32 origin_r) const;

  struct alley_seed {
    vec2 mouth;   // on the parent's centre line
    vec2 start;   // first point inside the block
    f32 heading;
    i32 parent;
    i32 depth;
    f32 parent_s;
    i32 parent_side;
  };

  void grow_alley(const alley_seed &seed, i32 blk, const profile &pf, rng &r, std::vector<alley_seed> &more);

  void make_alleys();

  // --- Stage 10: houses along every road, then inside the blocks ------------------------------

  void front_houses(i32 road_id, rng &r);

  void make_houses();

  void fill_interior(i32 bi, rng &r);

  // --- Stage 11: what is left open becomes a place ------------------------------------------------

  void make_open_spots();

  // --- Stage 12: the road graph ----------------------------------------------------------------

  void make_graph();

  // --- Stage 13: nav grids and doors ----------------------------------------------------------

  void make_nav();

  // --- Stage 14: businesses -------------------------------------------------------------------------

  struct weight {
    business_kind k;
    f32 w;
  };

  static business_kind pick(const std::vector<weight> &table, rng &r);

  static std::vector<weight> table_for(district_kind k);

  static i32 base_income(business_kind k);

  void add_business(i32 bi, business_kind kind, rng &r);

  void make_businesses();

  // --- Stage 15: street furniture ------------------------------------------------------------------

  bool on_sidewalk(vec2 p) const;

  void make_props();
  void avoid_props(); // make_props: foot nav round the props

  // --- Stage 16: how the pieces touch, and where gangs could start --------------------------------

  void make_links();

  void run();
};

} // namespace sandtable::city
