#include "gen.h"

#include <algorithm>
#include <cmath>

// Stages 1 to 5: the ground, river, districts, lake, avenues and roundabout.

namespace sandtable::city {

void generator::init() {
  m.cols = static_cast<i32>(std::ceil(d.width / cs));
  m.rows = static_cast<i32>(std::ceil(d.height / cs));
  m.cells.assign(static_cast<size_t>(m.cols * m.rows), cell_info{});
  bcols = static_cast<i32>(std::ceil(d.width / bucket)) + 1;
  brows = static_cast<i32>(std::ceil(d.height / bucket)) + 1;
  buckets.assign(static_cast<size_t>(bcols * brows), {});
  stamp.assign(m.cells.size(), 0);
  rng r = stage_rng(1);
  const i32 n = street_name_count;
  for (i32 i = 0; i < n; ++i)
    name_order.push_back(i);
  for (i32 i = n - 1; i > 0; --i)
    std::swap(name_order[static_cast<size_t>(i)], name_order[static_cast<size_t>(r.range(0, i))]);
}

// A river across the long way of the table, meandering.
void generator::make_river() {
  rng r = stage_rng(2);
  const bool on = d.river < 0 ? r.chance(0.8f) : d.river > 0;
  if (!on)
    return;
  river_shape &rv = m.river;
  rv.on = true;
  rv.width = r.range(96.0f, 130.0f);
  const f32 base = d.height * r.range(0.45f, 0.6f);
  const f32 tilt = r.range(-220.0f, 220.0f);
  const noise_desc nd = noise(21, 0.0014f, 2);
  for (f32 x = -80.0f; x <= d.width + 80.0f; x += 40.0f) {
    const f32 y = base + (x / d.width - 0.5f) * tilt + (noise_1d(nd, x) - 0.5f) * 2.0f * 260.0f;
    rv.pts.push_back({x, y});
  }
  for_cells_near(rv.pts, rv.width * 0.5f, [&](i32 x, i32 y) { m.at(x, y).g = ground::water; });
}

f32 generator::river_y(f32 x) const {
  const std::vector<vec2> &p = m.river.pts;
  for (size_t i = 0; i + 1 < p.size(); ++i)
    if (x >= p[i].x && x <= p[i + 1].x)
      return lerp(p[i].y, p[i + 1].y, (x - p[i].x) / (p[i + 1].x - p[i].x));
  return p.back().y;
}

void generator::make_districts() {
  rng r = stage_rng(3);
  const i32 n = d.districts > 0 ? std::clamp(d.districts, 3, 12) : r.range(7, 9);
  std::vector<vec2> sites;
  for (i32 i = 0; i < n; ++i) {
    vec2 best{};
    f32 best_d = -1.0f;
    for (i32 k = 0; k < 16; ++k) {
      const vec2 c{r.range(d.width * 0.08f, d.width * 0.92f), r.range(d.height * 0.1f, d.height * 0.9f)};
      f32 md = 1e30f;
      for (const vec2 s : sites)
        md = std::min(md, distance(c, s));
      if (md > best_d) {
        best_d = md;
        best = c;
      }
    }
    sites.push_back(best);
  }
  // Who is what: the old quarter in the middle, the market beside it, the
  // docks by the river, the new town and the works far off, nightlife near
  // the old centre, and the rest the people's alleys.
  std::vector<i32> kind(static_cast<size_t>(n), -1);
  const vec2 mid{d.width * 0.5f, d.height * 0.5f};
  auto pick = [&](auto score) {
    i32 best = -1;
    f32 bs = 1e30f;
    for (i32 i = 0; i < n; ++i)
      if (kind[static_cast<size_t>(i)] < 0) {
        const f32 s = score(sites[static_cast<size_t>(i)]);
        if (s < bs) {
          bs = s;
          best = i;
        }
      }
    return best;
  };
  auto set = [&](i32 i, district_kind k) {
    if (i >= 0)
      kind[static_cast<size_t>(i)] = static_cast<i32>(k);
  };
  const i32 old = pick([&](vec2 s) { return distance(s, mid); });
  set(old, district_kind::old_quarter);
  const vec2 oq = sites[static_cast<size_t>(old)];
  if (m.river.on)
    set(pick([&](vec2 s) { return std::fabs(s.y - river_y(s.x)); }), district_kind::docks);
  set(pick([&](vec2 s) { return distance(s, oq); }), district_kind::market);
  set(pick([&](vec2 s) { return -distance(s, oq); }), district_kind::new_urban);
  set(pick([&](vec2 s) { return distance(s, oq) + r.range(0.0f, 200.0f); }), district_kind::nightlife);
  set(pick([&](vec2 s) { return -distance(s, oq) + r.range(0.0f, 300.0f); }), district_kind::industrial);
  for (i32 i = 0; i < n; ++i)
    if (kind[static_cast<size_t>(i)] < 0)
      kind[static_cast<size_t>(i)] = static_cast<i32>(district_kind::residential);

  i32 used[static_cast<i32>(district_kind::count)] = {};
  const i32 name_offset = r.range(0, 5);
  for (i32 i = 0; i < n; ++i) {
    district ds;
    ds.kind = static_cast<district_kind>(kind[static_cast<size_t>(i)]);
    ds.site = sites[static_cast<size_t>(i)];
    const i32 k = static_cast<i32>(ds.kind);
    ds.name = district_names[k][(name_offset + used[k]++) % 6];
    ds.grain = ds.kind == district_kind::new_urban ? r.range(8.0f, 24.0f) * (r.chance(0.5f) ? 1.0f : -1.0f)
                                                   : r.range(-8.0f, 8.0f);
    m.districts.push_back(ds);
  }

  // Each cell goes to the nearest site, through a warp so borders wander.
  const noise_desc wx = noise(31, 0.0025f, 2), wy = noise(37, 0.0025f, 2);
  for (i32 y = 0; y < m.rows; ++y)
    for (i32 x = 0; x < m.cols; ++x) {
      vec2 p = m.center_of(x, y);
      p += vec2{noise_2d(wx, p.x, p.y) - 0.5f, noise_2d(wy, p.x, p.y) - 0.5f} * 520.0f;
      i32 best = 0;
      f32 bd = 1e30f;
      for (i32 i = 0; i < n; ++i) {
        const f32 dd = length_sq(p - sites[static_cast<size_t>(i)]);
        if (dd < bd) {
          bd = dd;
          best = i;
        }
      }
      m.at(x, y).district = static_cast<u8>(best);
    }
}

road generator::avenue(std::vector<vec2> pts) {
  keep_off_lake(pts);
  road rd;
  rd.kind = road_kind::avenue;
  rd.width = avenue_w;
  rd.sidewalk = avenue_side;
  rd.pts = std::move(pts);
  rd.name = take_street_name();
  return rd;
}

void generator::make_avenues() {
  rng r = stage_rng(4);
  const f32 W = d.width, H = d.height;
  const f32 keep = m.river.on ? m.river.width * 0.5f + 40.0f + 2.0f * 30.0f + avenue_w * 0.5f + 130.0f : 0.0f;

  // Across: roughly with the river, one on each side of it.
  std::vector<f32> bases;
  if (m.river.on) {
    const f32 mid = river_y(W * 0.5f);
    bases.push_back(mid - r.range(420.0f, 560.0f));
    bases.push_back(mid + r.range(400.0f, 520.0f));
  } else {
    bases.push_back(H * r.range(0.26f, 0.34f));
    bases.push_back(H * r.range(0.62f, 0.72f));
  }
  const noise_desc nh = noise(41, 0.0012f, 2);
  for (size_t i = 0; i < bases.size(); ++i) {
    const f32 base = bases[i];
    if (base < 170.0f || base > H - 170.0f)
      continue;
    std::vector<vec2> pts;
    for (f32 x = -40.0f; x <= W + 40.0f; x += 40.0f) {
      f32 y = base + (noise_1d(nh, x + static_cast<f32>(i) * 977.0f) - 0.5f) * 2.0f * 90.0f;
      if (m.river.on) {
        // Follow the river's bends a little, and keep clear of it.
        const f32 ry = river_y(x);
        y = lerp(y, ry + (base - river_y(W * 0.5f)), 0.45f);
        if (std::fabs(y - ry) < keep)
          y = ry + (y > ry ? keep : -keep);
      }
      pts.push_back({x, clamp(y, 120.0f, H - 120.0f)});
    }
    add_road(avenue(std::move(pts)));
  }

  // Down: two or three, crossing the river on bridges.
  const i32 downs = r.chance(0.55f) ? 3 : 2;
  const noise_desc nv = noise(43, 0.0015f, 2);
  for (i32 i = 0; i < downs; ++i) {
    const f32 base = W * (static_cast<f32>(i) + 1.0f) / (static_cast<f32>(downs) + 1.0f) + r.range(-110.0f, 110.0f);
    const f32 tilt = r.range(-140.0f, 140.0f);
    std::vector<vec2> pts;
    for (f32 y = -40.0f; y <= H + 40.0f; y += 40.0f) {
      const f32 x = base + (y / H - 0.5f) * tilt + (noise_1d(nv, y + static_cast<f32>(i) * 1311.0f) - 0.5f) * 2.0f * 70.0f;
      pts.push_back({x, y});
    }
    add_road(avenue(std::move(pts)));
  }

  // A diagonal boulevard, cutting the grid into triangles.
  if (r.chance(0.7f)) {
    const bool flip = r.chance(0.5f);
    vec2 a{W * r.range(0.04f, 0.3f), -40.0f}, b{W * r.range(0.6f, 0.92f), H + 40.0f};
    if (flip) {
      a.x = W - a.x;
      b.x = W - b.x;
    }
    const vec2 bend = perp(normalize(b - a)) * r.range(-140.0f, 140.0f);
    std::vector<vec2> pts;
    for (i32 k = 0; k <= 40; ++k) {
      const f32 t = static_cast<f32>(k) / 40.0f;
      pts.push_back(lerp(a, b, t) + bend * (4.0f * t * (1.0f - t)));
    }
    add_road(avenue(std::move(pts)));
  }

  // Along both banks of the river.
  if (m.river.on) {
    const std::vector<vec2> &rp = m.river.pts;
    const f32 street_w = 40.0f, side = 10.0f;
    for (const f32 sgn : {-1.0f, 1.0f}) {
      std::vector<vec2> pts;
      for (size_t i = 0; i < rp.size(); ++i) {
        const vec2 t = normalize(rp[std::min(i + 1, rp.size() - 1)] - rp[i == 0 ? 0 : i - 1]);
        pts.push_back(rp[i] + perp(t) * (sgn * (m.river.width * 0.5f + 20.0f + street_w * 0.5f + side)));
      }
      road rd;
      rd.kind = road_kind::street;
      rd.width = street_w;
      rd.sidewalk = side;
      rd.pts = std::move(pts);
      rd.name = "Bến " + take_street_name();
      add_road(std::move(rd));
    }
    // The strip between road and water is the embankment walk.
    for_cells_near(rp, m.river.width * 0.5f + 34.0f, [&](i32 x, i32 y) {
      cell_info &ci = m.at(x, y);
      if (ci.g == ground::free)
        ci.g = ground::plaza;
    });
  }
}

f32 generator::lake_ring_at(const lake_shape &lk, f32 degrees) {
  return lk.radius_at(degrees) + lake_park + lake_ring_w * 0.5f + lake_ring_side;
}

// An avenue that would cross the lake bends round it along the lake road.
void generator::keep_off_lake(std::vector<vec2> &pts) const {
  if (!m.lake.on)
    return;
  for (vec2 &p : pts) {
    const vec2 dv = p - m.lake.center;
    const f32 a = angle_of(dv);
    const f32 keep = lake_ring_at(m.lake, a);
    if (length(dv) < keep)
      p = m.lake.center + from_angle(a) * keep;
  }
}

void generator::make_lake() {
  rng r = stage_rng(5);
  const bool on = d.lake < 0 ? r.chance(0.6f) : d.lake > 0;
  if (!on)
    return;
  const f32 park = lake_park, ring_w = lake_ring_w, ring_side = lake_ring_side;
  for (i32 attempt = 0; attempt < 60; ++attempt) {
    lake_shape lk;
    lk.radius = r.range(100.0f, 150.0f);
    lk.center = {r.range(d.width * 0.12f, d.width * 0.88f), r.range(d.height * 0.15f, d.height * 0.85f)};
    const noise_desc nd = noise(51 + static_cast<u32>(attempt), 0.8f, 2);
    for (i32 k = 0; k < 48; ++k) {
      const f32 a = static_cast<f32>(k) / 48.0f * 2.0f * pi;
      lk.r.push_back(lk.radius * (1.0f + (noise_2d(nd, std::cos(a) * 2.0f, std::sin(a) * 2.0f) - 0.5f) * 0.7f) *
                     (1.0f + 0.25f * std::cos(a * 2.0f)));
    }
    f32 rmax = 0.0f;
    for (const f32 v : lk.r)
      rmax = std::max(rmax, v);
    const f32 outer = rmax + park + ring_w + ring_side * 2.0f + 30.0f;
    if (lk.center.x - outer < 0.0f || lk.center.y - outer < 0.0f || lk.center.x + outer > d.width ||
        lk.center.y + outer > d.height)
      continue;
    bool clear = true;
    for (i32 y = 0; y < m.rows && clear; ++y)
      for (i32 x = 0; x < m.cols; ++x)
        if (distance(m.center_of(x, y), lk.center) < outer && m.at(x, y).g != ground::free) {
          clear = false;
          break;
        }
    if (!clear)
      continue;
    lk.on = true;
    for (i32 y = 0; y < m.rows; ++y)
      for (i32 x = 0; x < m.cols; ++x) {
        const vec2 c = m.center_of(x, y);
        const f32 dd = distance(c, lk.center);
        if (dd > outer)
          continue;
        const f32 rr = lk.radius_at(angle_of(c - lk.center));
        if (dd < rr)
          m.at(x, y).g = ground::water;
        else if (dd < rr + park)
          m.at(x, y).g = ground::park;
      }
    road ring;
    ring.kind = road_kind::street;
    ring.width = ring_w;
    ring.sidewalk = ring_side;
    ring.name = "Bờ Hồ";
    for (i32 k = 0; k <= 48; ++k) {
      const f32 a = static_cast<f32>(k % 48) / 48.0f * 360.0f;
      ring.pts.push_back(lk.center + from_angle(a) * lake_ring_at(lk, a));
    }
    add_road(std::move(ring));
    add_spot({lk.center, {rmax + park, rmax + park}, 0.0f}, spot_kind::park, ground::free, false);
    m.lake = std::move(lk);
    return;
  }
  ++m.report.caps_hit;
}

void generator::make_roundabout() {
  // The crossing of two avenues nearest the middle.
  vec2 best{};
  f32 bd = 1e30f;
  const vec2 mid{d.width * 0.5f, d.height * 0.5f};
  for (size_t i = 0; i < m.roads.size(); ++i)
    for (size_t j = i + 1; j < m.roads.size(); ++j) {
      const road &a = m.roads[i], &b = m.roads[j];
      if (a.kind != road_kind::avenue || b.kind != road_kind::avenue)
        continue;
      for (size_t p = 0; p + 1 < a.pts.size(); ++p)
        for (size_t q = 0; q + 1 < b.pts.size(); ++q) {
          f32 t, u;
          if (!seg_cross(a.pts[p], a.pts[p + 1], b.pts[q], b.pts[q + 1], t, u))
            continue;
          const vec2 c = lerp(a.pts[p], a.pts[p + 1], t);
          if (c.x < 200.0f || c.y < 200.0f || c.x > d.width - 200.0f || c.y > d.height - 200.0f)
            continue;
          const f32 dd = distance(c, mid);
          if (dd < bd) {
            bd = dd;
            best = c;
          }
        }
    }
  if (bd > 1e29f)
    return;
  // No roundabout on a bridge.
  const cell_info *c = m.cell_at(best);
  if (!c || c->g == ground::bridge)
    return;
  const f32 radius = 78.0f, ring_w = 40.0f;
  road ring;
  ring.kind = road_kind::ring;
  ring.width = ring_w;
  ring.sidewalk = 8.0f;
  ring.name = "Bùng binh " + take_street_name();
  for (i32 k = 0; k <= 32; ++k)
    ring.pts.push_back(best + from_angle(static_cast<f32>(k % 32) / 32.0f * 360.0f) * radius);
  add_road(std::move(ring));
  // The island in the middle: roads pass under the drawing, cars go round.
  const f32 island = radius - ring_w * 0.5f - 2.0f;
  for (i32 y = 0; y < m.rows; ++y)
    for (i32 x = 0; x < m.cols; ++x)
      if (distance(m.center_of(x, y), best) < island) {
        cell_info &ci = m.at(x, y);
        ci.g = ground::plaza;
        ci.carriage = false;
        ci.road_id = -1;
      }
  add_spot({best, {island, island}, 0.0f}, spot_kind::roundabout, ground::free, false);
  m.props.push_back({prop_kind::monument, best, 0.0f, 1.0f, 0});
}

} // namespace sandtable::city
