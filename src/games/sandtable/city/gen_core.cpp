#include "gen.h"

#include <algorithm>
#include <chrono>
#include <cmath>

// The generator's tools: laying roads on the raster, testing and placing
// footprints; and generate() itself, which runs the stages in order.

namespace sandtable::city {

rng generator::stage_rng(u32 salt) const {
  return rng((static_cast<u64>(d.seed) << 20) ^ (static_cast<u64>(salt) * 0x9E3779B97F4A7C15ull));
}

noise_desc generator::noise(u32 salt, f32 freq, i32 octaves) const {
  noise_desc n{};
  n.seed = d.seed * 131u + salt;
  n.frequency = freq;
  n.octaves = octaves;
  return n;
}

bool generator::cell_of(vec2 p, i32 &x, i32 &y) const {
  x = static_cast<i32>(std::floor(p.x / cs));
  y = static_cast<i32>(std::floor(p.y / cs));
  return m.inside(x, y);
}

i32 generator::prio(road_kind k) {
  switch (k) {
  case road_kind::avenue: return 0;
  case road_kind::ring: return 1;
  case road_kind::street: return 2;
  default: return 3;
  }
}

// Marks the cells whose centre is on a road: carriageway and sidewalks. The
// two ends are cut square (a street that ends on another stops at its
// middle). fits() measures exactly where a cell is only partly road.
void generator::raster_road(i32 id) {
  const road &rd = m.roads[static_cast<size_t>(id)];
  const f32 lim = rd.reach();
  const f32 half = rd.width * 0.5f;
  const size_t n = rd.pts.size();
  const bool closed = n > 2 && distance(rd.pts.front(), rd.pts.back()) < 0.5f;
  for (size_t i = 0; i + 1 < n; ++i) {
    const vec2 a = rd.pts[i], b = rd.pts[i + 1];
    const i32 x0 = std::max(0, static_cast<i32>((std::min(a.x, b.x) - lim) / cs) - 1);
    const i32 x1 = std::min(m.cols - 1, static_cast<i32>((std::max(a.x, b.x) + lim) / cs) + 1);
    const i32 y0 = std::max(0, static_cast<i32>((std::min(a.y, b.y) - lim) / cs) - 1);
    const i32 y1 = std::min(m.rows - 1, static_cast<i32>((std::max(a.y, b.y) + lim) / cs) + 1);
    for (i32 y = y0; y <= y1; ++y)
      for (i32 x = x0; x <= x1; ++x) {
        const vec2 c = m.center_of(x, y);
        f32 t;
        const f32 dd = seg_dist(c, a, b, &t);
        if (dd > lim)
          continue;
        if (!closed && ((i == 0 && t < 0.0f) || (i + 2 == n && t > 1.0f)))
          continue;
        cell_info &ci = m.at(x, y);
        if (ci.g == ground::building)
          continue;
        if (ci.g == ground::water) {
          if (rd.kind == road_kind::alley)
            continue;
          ci.g = ground::bridge;
        } else if (ci.g != ground::bridge) {
          ci.g = ground::road;
        }
        if (ci.road_id < 0 || prio(rd.kind) < prio(ci.road)) {
          ci.road = rd.kind;
          ci.road_id = id;
        }
        if (dd <= half + cs * 0.25f)
          ci.carriage = true;
      }
  }
}

i32 generator::add_road(road r) {
  // Drop points closer than a unit, which make zero-length segments.
  std::vector<vec2> clean;
  for (const vec2 p : r.pts)
    if (clean.empty() || distance(clean.back(), p) > 1.0f)
      clean.push_back(p);
  if (clean.size() < 2)
    return -1;
  r.pts = std::move(clean);
  r.length = polyline_walk(r.pts).length();
  m.roads.push_back(std::move(r));
  const i32 id = static_cast<i32>(m.roads.size()) - 1;
  raster_road(id);
  return id;
}

std::string generator::take_street_name() {
  if (next_name < static_cast<i32>(name_order.size()))
    return street_names[name_order[static_cast<size_t>(next_name++)]];
  char buf[32];
  std::snprintf(buf, sizeof(buf), "Đường số %d", ++next_name - static_cast<i32>(name_order.size()));
  return buf;
}

void generator::bucket_range(const obb &b, i32 &bx0, i32 &by0, i32 &bx1, i32 &by1) const {
  const f32 r = length(b.half);
  bx0 = std::clamp(static_cast<i32>((b.center.x - r) / bucket), 0, bcols - 1);
  bx1 = std::clamp(static_cast<i32>((b.center.x + r) / bucket), 0, bcols - 1);
  by0 = std::clamp(static_cast<i32>((b.center.y - r) / bucket), 0, brows - 1);
  by1 = std::clamp(static_cast<i32>((b.center.y + r) / bucket), 0, brows - 1);
}

void generator::add_footprint(const obb &b) {
  footprints.push_back(b);
  const i32 id = static_cast<i32>(footprints.size()) - 1;
  i32 bx0, by0, bx1, by1;
  bucket_range(b, bx0, by0, bx1, by1);
  for (i32 y = by0; y <= by1; ++y)
    for (i32 x = bx0; x <= bx1; ++x)
      buckets[static_cast<size_t>(y * bcols + x)].push_back(id);
}

bool generator::overlaps_footprint(const obb &b) const {
  i32 bx0, by0, bx1, by1;
  bucket_range(b, bx0, by0, bx1, by1);
  for (i32 y = by0; y <= by1; ++y)
    for (i32 x = bx0; x <= bx1; ++x)
      for (const i32 id : buckets[static_cast<size_t>(y * bcols + x)])
        if (obb_overlap(b, footprints[static_cast<size_t>(id)]))
          return true;
  return false;
}

// Whether a box can stand in `block`: every cell under it free ground of
// that block, its edges off roads and water, and no other box in the way.
bool generator::fits(const obb &b, i32 blk) const {
  if (blk < 0)
    return false;
  for (i32 i = 0; i < 4; ++i) {
    const vec2 c = b.corner(i);
    if (c.x < 1.0f || c.y < 1.0f || c.x > d.width - 1.0f || c.y > d.height - 1.0f)
      return false;
    // Quick no: a corner (pulled in a cell) on a building, or off the block.
    const cell_info *ci = m.cell_at(c + normalize(b.center - c) * (cs * 0.8f));
    if (!ci || ci->block != blk || ci->g == ground::building)
      return false;
  }
  bool ok = true;
  i32 count = 0;
  for_cells_in(b, [&](i32 x, i32 y) {
    ++count;
    if (!m.inside(x, y)) {
      ok = false;
      return;
    }
    const cell_info &ci = m.at(x, y);
    if (ci.g != ground::free || ci.block != blk)
      ok = false;
  });
  if (!ok || count == 0)
    return false;
  // The outline, every few units, a little inside.
  for (i32 side = 0; side < 4; ++side) {
    const vec2 a = b.corner(side), c = b.corner((side + 1) % 4);
    const vec2 inward = normalize(b.center - (a + c) * 0.5f) * 0.5f;
    const i32 steps = std::max(1, static_cast<i32>(distance(a, c) / 4.0f));
    for (i32 k = 0; k <= steps; ++k) {
      const vec2 p = lerp(a, c, static_cast<f32>(k) / static_cast<f32>(steps)) + inward;
      i32 x, y;
      if (!cell_of(p, x, y))
        return false;
      const cell_info &ci = m.at(x, y);
      if (ci.g == ground::water || ci.g == ground::park || ci.g == ground::plaza)
        return false;
      if (!off_roads(p, x, y))
        return false;
      if (ci.block != blk && ci.g != ground::road)
        return false;
    }
  }
  return !overlaps_footprint(b);
}

// Whether `p` (in cell x, y) is clear of the sidewalks of every road around.
bool generator::off_roads(vec2 p, i32 x, i32 y) const {
  i32 seen[4] = {-1, -1, -1, -1};
  i32 n = 0;
  for (i32 yy = y - 1; yy <= y + 1; ++yy)
    for (i32 xx = x - 1; xx <= x + 1; ++xx) {
      if (!m.inside(xx, yy))
        continue;
      const cell_info &c = m.at(xx, yy);
      if ((c.g != ground::road && c.g != ground::bridge) || c.road_id < 0)
        continue;
      bool dup = false;
      for (i32 k = 0; k < n; ++k)
        dup = dup || seen[k] == c.road_id;
      if (dup || n == 4)
        continue;
      seen[n++] = c.road_id;
      const road &rd = m.roads[static_cast<size_t>(c.road_id)];
      vec2 q;
      if (closest_on(rd.pts, p, q) < rd.reach() - 0.25f)
        return false;
    }
  return true;
}

i32 generator::add_building(const obb &b, building_kind kind, i32 floors, i32 road_id, u32 look) {
  building bd;
  bd.kind = kind;
  bd.box = b;
  bd.floors = floors;
  bd.height = static_cast<f32>(floors) * floor_h + 4.0f;
  bd.road = road_id;
  bd.look = look;
  const cell_info *c = m.cell_at(b.center);
  bd.block = c ? c->block : -1;
  bd.district = c ? c->district : -1;
  m.buildings.push_back(bd);
  const i32 id = static_cast<i32>(m.buildings.size()) - 1;
  for_cells_in(b, [&](i32 x, i32 y) {
    cell_info &ci = m.at(x, y);
    ci.g = ground::building;
    ci.building = id;
  });
  add_footprint(b);
  return id;
}

i32 generator::add_spot(const obb &b, spot_kind kind, ground g, bool footprint) {
  spot s;
  s.kind = kind;
  s.box = b;
  const cell_info *c = m.cell_at(b.center);
  s.block = c ? c->block : -1;
  s.district = c ? c->district : -1;
  m.spots.push_back(s);
  if (g != ground::free)
    for_cells_in(b, [&](i32 x, i32 y) {
      if (!m.inside(x, y))
        return;
      cell_info &ci = m.at(x, y);
      if (ci.g == ground::free)
        ci.g = g;
    });
  if (footprint)
    add_footprint(b);
  return static_cast<i32>(m.spots.size()) - 1;
}

// A box fronting a road: `w` along it from `s`, `depth` back from the
// sidewalk (plus `gap`), on `side` (+1 left of the road's direction).
bool generator::front_box(i32 road_id, const polyline_walk &walk, f32 s, i32 side, f32 w, f32 depth, f32 gap, obb &out) const {
  const road &rd = m.roads[static_cast<size_t>(road_id)];
  if (s + w > walk.length())
    return false;
  vec2 p, t;
  walk.at(s + w * 0.5f, p, t);
  const vec2 n = perp(t) * static_cast<f32>(side);
  out.center = p + n * (rd.reach() + gap + depth * 0.5f);
  out.half = {w * 0.5f, depth * 0.5f};
  out.angle = side > 0 ? angle_of(t) : angle_of(t) + 180.0f;
  return true;
}

void generator::run() {
  auto t = std::chrono::steady_clock::now();
  auto stage = [&](const char *name, void (generator::*fn)()) {
    (this->*fn)();
    const auto now = std::chrono::steady_clock::now();
    m.report.stages.push_back({name, std::chrono::duration<f32, std::milli>(now - t).count()});
    t = now;
  };
  stage("ground", &generator::init);
  stage("river", &generator::make_river);
  stage("districts", &generator::make_districts);
  stage("lake", &generator::make_lake);
  stage("avenues", &generator::make_avenues);
  stage("roundabout", &generator::make_roundabout);
  stage("streets", &generator::make_streets);
  stage("connect", &generator::connect_streets);
  stage("blocks", &generator::make_blocks);
  stage("landmarks", &generator::make_landmarks);
  stage("alleys", &generator::make_alleys);
  stage("houses", &generator::make_houses);
  stage("open", &generator::make_open_spots);
  stage("graph", &generator::make_graph);
  stage("nav", &generator::make_nav);
  stage("business", &generator::make_businesses);
  stage("props", &generator::make_props);
  stage("links", &generator::make_links);
}

namespace {

// FNV-1a over what was made.
struct hasher {
  u64 h = 1469598103934665603ull;
  void bytes(const void *p, size_t n) {
    const u8 *b = static_cast<const u8 *>(p);
    for (size_t i = 0; i < n; ++i) {
      h ^= b[i];
      h *= 1099511628211ull;
    }
  }
  void i(i64 v) { bytes(&v, sizeof(v)); }
  void f(f32 v) { i(static_cast<i64>(std::lround(v * 16.0f))); }
  void v(vec2 p) {
    f(p.x);
    f(p.y);
  }
  void s(const std::string &str) { bytes(str.data(), str.size()); }
};

} // namespace

u64 city_hash(const city_map &m) {
  hasher h;
  for (const cell_info &c : m.cells) {
    h.i(static_cast<i64>(c.g) | (static_cast<i64>(c.block + 1) << 8) | (static_cast<i64>(c.district) << 40));
  }
  for (const road &r : m.roads) {
    h.i(static_cast<i64>(r.kind));
    h.s(r.name);
    for (const vec2 p : r.pts)
      h.v(p);
  }
  for (const building &b : m.buildings) {
    h.v(b.box.center);
    h.v(b.box.half);
    h.f(b.box.angle);
    h.i(b.floors);
    h.i(static_cast<i64>(b.look));
  }
  for (const business &b : m.businesses) {
    h.i(static_cast<i64>(b.kind));
    h.i(b.income);
    h.s(b.name);
  }
  for (const spot &s : m.spots)
    h.v(s.box.center);
  for (const prop &p : m.props)
    h.v(p.pos);
  h.i(static_cast<i64>(m.nodes.size()));
  h.i(static_cast<i64>(m.edges.size()));
  return h.h;
}

void generate(city_map &out, const city_desc &desc) {
  const auto t0 = std::chrono::steady_clock::now();
  out = city_map{};
  out.desc = desc;
  generator g(out, out.desc);
  g.run();
  out.hash = city_hash(out);
  out.report.gen_ms =
      std::chrono::duration<f32, std::milli>(std::chrono::steady_clock::now() - t0).count();
  validate(out);
}

} // namespace sandtable::city
