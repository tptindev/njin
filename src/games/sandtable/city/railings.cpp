#include "railings.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sandtable::city {
namespace {
using polygon = std::vector<vec2>;

polygon offset(const polygon &p, f32 width) {
  polygon out;
  for (size_t i = 0; i < p.size(); ++i) {
    const vec2 prev = normalize(p[i == 0 ? 1 : i] - p[i == 0 ? 0 : i - 1]);
    const vec2 next = normalize(p[i + 1 < p.size() ? i + 1 : i] - p[i + 1 < p.size() ? i : i - 1]);
    const vec2 n0{-prev.y, prev.x}, n1{-next.y, next.x};
    const vec2 bisector = n0 + n1;
    const f32 divisor = dot(bisector, n1);
    // Miter the joint so adjacent straight pieces share one endpoint.
    out.push_back(p[i] + (divisor > 0.05f ? bisector * (width / divisor) : n1 * width));
  }
  return out;
}
polygon strip(const polygon &p, f32 width) {
  polygon result = offset(p, width), other = offset(p, -width);
  result.insert(result.end(), other.rbegin(), other.rend());
  return result;
}
bool inside(const polygon &p, vec2 q) {
  bool hit = false;
  for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
    const vec2 a = p[i], b = p[j];
    if ((a.y > q.y) != (b.y > q.y) &&
        q.x < (b.x - a.x) * (q.y - a.y) / (b.y - a.y) + a.x)
      hit = !hit;
  }
  return hit;
}

// Exact edge intersections, rather than raster cells or bridge bounding boxes.
std::vector<std::pair<vec2, vec2>> clip(vec2 a, vec2 b, const std::vector<polygon> &areas, bool keep_inside) {
  std::vector<f32> cuts{0, 1};
  const vec2 d = b - a;
  for (const polygon &p : areas)
    for (size_t i = 0; i < p.size(); ++i) {
      const vec2 c = p[i], e = p[(i + 1) % p.size()] - c;
      const f32 den = cross(d, e);
      if (std::fabs(den) < 1e-6f) continue;
      const f32 t = cross(c - a, e) / den, u = cross(c - a, d) / den;
      if (t > 0 && t < 1 && u >= 0 && u <= 1) cuts.push_back(t);
    }
  std::sort(cuts.begin(), cuts.end());
  cuts.erase(std::unique(cuts.begin(), cuts.end(), [](f32 x, f32 y) { return std::fabs(x - y) < 1e-5f; }), cuts.end());
  std::vector<std::pair<vec2, vec2>> result;
  for (size_t i = 0; i + 1 < cuts.size(); ++i) {
    const vec2 mid = a + d * ((cuts[i] + cuts[i + 1]) * 0.5f);
    const bool hit = std::any_of(areas.begin(), areas.end(), [&](const polygon &p) { return inside(p, mid); });
    if (hit == keep_inside) result.emplace_back(a + d * cuts[i], a + d * cuts[i + 1]);
  }
  return result;
}
} // namespace

std::vector<railing_edge> railing_layout(const city_map &map) {
  std::vector<railing_edge> out;
  if (!map.river.on || map.river.pts.size() < 2) return out;
  const f32 bank = map.river.width * 0.5f + 2.0f; // landward of the rendered water
  const polygon waterfront = strip(map.river.pts, bank);
  const std::vector<polygon> water{waterfront};
  std::vector<polygon> entrances;
  std::vector<const road *> bridges;
  for (const road &r : map.roads) {
    if (r.kind == road_kind::alley || r.pts.size() < 2) continue;
    bool crossing = false;
    for (size_t i = 0; i + 1 < r.pts.size(); ++i)
      crossing = crossing || !clip(r.pts[i], r.pts[i + 1], water, true).empty();
    if (crossing) {
      bridges.push_back(&r);
      entrances.push_back(strip(r.pts, r.reach() - railing_thickness * 0.5f));
    }
  }
  const std::vector<polygon> bounds{{{0, 0}, {map.desc.width, 0}, {map.desc.width, map.desc.height}, {0, map.desc.height}}};
  const auto add = [&](vec2 a, vec2 b, bool bridge) {
    for (const auto &[x, y] : clip(a, b, bounds, true))
      if (distance(x, y) > 0.01f) out.push_back({x, y, bridge});
  };
  for (const f32 side : {-1.0f, 1.0f}) {
    const polygon p = offset(map.river.pts, side * bank);
    for (size_t i = 0; i + 1 < p.size(); ++i)
      for (const auto &[a, b] : clip(p[i], p[i + 1], entrances, false)) add(a, b, false);
    for (const road *r : bridges) {
      const polygon edge = offset(r->pts, side * (r->reach() - railing_thickness * 0.5f));
      std::vector<polygon> other_roads;
      for (size_t j = 0; j < bridges.size(); ++j)
        if (bridges[j] != r) other_roads.push_back(entrances[j]);
      for (size_t i = 0; i + 1 < edge.size(); ++i)
        for (const auto &[a, b] : clip(edge[i], edge[i + 1], water, true))
          for (const auto &[x, y] : clip(a, b, other_roads, false)) add(x, y, true);
    }
  }
  return out;
}

i32 run_railing_check(i32 seeds) {
  i32 failed = 0;
  // A skew bridge over a bent river: the edge layout must leave both land
  // entrances open and every terminal must meet a bank/bridge junction.
  city_map fixture;
  fixture.desc.width = fixture.desc.height = 400;
  fixture.river = {true, 60, {{-40, 160}, {180, 180}, {440, 140}}};
  road r;
  r.pts = {{120, 0}, {220, 400}};
  r.width = 32; r.sidewalk = 8;
  fixture.roads.push_back(r);
  for (i32 seed = 0; seed <= seeds; ++seed) {
    city_map map;
    if (seed == 0) map = fixture;
    else { city_desc d; d.seed = static_cast<u32>(seed); d.river = 1; generate(map, d); }
    const auto edges = railing_layout(map);
    i32 banks = 0, bridges = 0;
    for (const railing_edge &e : edges) {
      (e.bridge ? bridges : banks)++;
      for (const vec2 p : {e.a, e.b}) {
        if (p.x < -0.01f || p.y < -0.01f || p.x > map.desc.width + 0.01f || p.y > map.desc.height + 0.01f) ++failed;
        if (p.x < 0.01f || p.y < 0.01f || p.x > map.desc.width - 0.01f || p.y > map.desc.height - 0.01f) continue;
        bool joined = false;
        for (const railing_edge &other : edges)
          if (&other != &e && (distance(p, other.a) < 0.03f || distance(p, other.b) < 0.03f)) joined = true;
        if (!joined) { ++failed; std::printf("[railings] open seed %d at %.3f %.3f bridge=%d\n", seed, p.x, p.y, e.bridge); }
      }
    }
    if (banks == 0 || bridges == 0) ++failed;
    std::printf("[railings] seed %d: %d bank edges, %d bridge edges\n", seed, banks, bridges);
  }
  std::printf("[railings] %s (%d failures)\n", failed ? "FAIL" : "PASS", failed);
  return failed;
}
} // namespace sandtable::city
