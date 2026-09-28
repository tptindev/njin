#include "lighting_internal.h"
#include "_comps.h"
#include "njin_ctx_impl.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>
#include "njin_light.h"

namespace njin {
namespace light_impl {
namespace {
u64 pack(i32 x, i32 y) { return ((u64)(u32)(x + 0x40000000) << 32) | (u64)(u32)(y + 0x40000000); }
vec2 unpack(u64 key) {
  return {(f32)((i32)(u32)(key >> 32) - 0x40000000), (f32)((i32)(u32)(key & 0xffffffffu) - 0x40000000)};
}


f32 distance_to_line(vec2 p, vec2 a, vec2 b) {
  const f32 dx = b.x - a.x, dy = b.y - a.y;
  const f32 len = std::hypot(dx, dy);
  if (len < 1e-6f)
    return std::hypot(p.x - a.x, p.y - a.y);
  return std::abs((p.x - a.x) * dy - (p.y - a.y) * dx) / len;
}

// Douglas-Peucker on an open run of points: marks the ones to keep.
void rdp(const std::vector<vec2> &p, usize lo, usize hi, f32 tol, std::vector<bool> &keep) {
  if (hi <= lo + 1)
    return;
  f32 worst = 0.0f;
  usize at = lo;
  for (usize i = lo + 1; i < hi; i++) {
    const f32 d = distance_to_line(p[i], p[lo], p[hi]);
    if (d > worst)
      worst = d, at = i;
  }
  if (worst > tol) {
    keep[at] = true;
    rdp(p, lo, at, tol, keep);
    rdp(p, at, hi, tol, keep);
  }
}

// A closed loop with fewer points, none of the removed ones farther than `tol`
// from the result.
std::vector<vec2> simplify_loop(const std::vector<vec2> &p, f32 tol) {
  if (tol <= 0.0f || p.size() <= 4)
    return p;
  usize far = 0;
  f32 best = -1.0f;
  for (usize i = 1; i < p.size(); i++) {
    const f32 d = std::hypot(p[i].x - p[0].x, p[i].y - p[0].y);
    if (d > best)
      best = d, far = i;
  }
  std::vector<vec2> first(p.begin(), p.begin() + (isize)far + 1);
  std::vector<vec2> second(p.begin() + (isize)far, p.end());
  second.push_back(p[0]);
  std::vector<bool> keep_a(first.size(), false), keep_b(second.size(), false);
  keep_a.front() = keep_a.back() = keep_b.front() = keep_b.back() = true;
  rdp(first, 0, first.size() - 1, tol, keep_a);
  rdp(second, 0, second.size() - 1, tol, keep_b);
  std::vector<vec2> out;
  for (usize i = 0; i + 1 < first.size(); i++) {
    if (keep_a[i])
      out.push_back(first[i]);
  }
  for (usize i = 0; i + 1 < second.size(); i++) {
    if (keep_b[i])
      out.push_back(second[i]);
  }
  return out;
}
} // namespace

f32 signed_area(const std::vector<vec2> &p) {
  f32 a = 0.0f;
  for (usize i = 0; i < p.size(); i++) {
    const vec2 &u = p[i], &v = p[(i + 1) % p.size()];
    a += u.x * v.y - v.x * u.y;
  }
  return a * 0.5f;
}

// The outlines of a set of cells, as closed loops in cell units: one around each
// connected block and one around each hole in it, the latter marked. Straight
// runs are merged into one edge; `simplify` (cells) then smooths what is left.
std::vector<silhouette_loop> outline_cells(const cell_list &cells, f32 simplify) {
  std::unordered_set<u64> solid;
  solid.reserve(cells.size() * 2);
  for (const cell &c : cells)
    solid.insert(pack(c.x, c.y));
  const auto has = [&solid](i32 x, i32 y) { return solid.contains(pack(x, y)); };

  // Every cell side that borders an empty cell, running so that the outside of
  // a block is to its right: an outer loop comes out with a positive area.
  std::unordered_map<u64, std::vector<u64>> next;
  for (const cell &c : cells) {
    const i32 x = c.x, y = c.y;
    if (!has(x, y - 1))
      next[pack(x, y)].push_back(pack(x + 1, y));
    if (!has(x + 1, y))
      next[pack(x + 1, y)].push_back(pack(x + 1, y + 1));
    if (!has(x, y + 1))
      next[pack(x + 1, y + 1)].push_back(pack(x, y + 1));
    if (!has(x - 1, y))
      next[pack(x, y + 1)].push_back(pack(x, y));
  }

  std::vector<silhouette_loop> loops;
  while (!next.empty()) {
    const u64 start = next.begin()->first;
    std::vector<vec2> pts;
    u64 at = start;
    do {
      const auto it = next.find(at);
      if (it == next.end())
        break;
      pts.push_back(unpack(at));
      const u64 to = it->second.back();
      it->second.pop_back();
      if (it->second.empty())
        next.erase(it);
      at = to;
    } while (at != start);
    // Merge straight runs.
    std::vector<vec2> corners;
    for (usize i = 0; i < pts.size(); i++) {
      const vec2 &prev = pts[(i + pts.size() - 1) % pts.size()], &cur = pts[i], &nxt = pts[(i + 1) % pts.size()];
      const f32 cross = (cur.x - prev.x) * (nxt.y - cur.y) - (cur.y - prev.y) * (nxt.x - cur.x);
      if (cross != 0.0f)
        corners.push_back(cur);
    }
    if (corners.size() < 3)
      continue;
    const f32 area = signed_area(corners);
    std::vector<vec2> final_points = simplify_loop(corners, simplify);
    if (final_points.size() < 3)
      continue;
    loops.push_back({std::move(final_points), area < 0.0f});
  }
  return loops;
}
} // namespace light_impl

std::vector<light_occluder> light_occluders_from_tiles(const tilemap &map, const std::function<bool(i32 tile)> &solid,
                                                       f32 simplify) {
  light_impl::cell_list cells;
  for (const auto &[key, chunk] : map.chunks) {
    const cell origin = tile_chunk_coord(key);
    for (i32 i = 0; i < tile_chunk_size * tile_chunk_size; i++) {
      const i32 id = tile_id(chunk.tiles[(usize)i]);
      if (id >= 0 && solid(id))
        cells.push_back({origin.x * tile_chunk_size + i % tile_chunk_size, origin.y * tile_chunk_size + i / tile_chunk_size});
    }
  }
  std::vector<light_occluder> out;
  for (const silhouette_loop &loop : light_impl::outline_cells(cells, simplify)) {
    light_occluder o;
    o.closed = true;
    o.hole = loop.hole;
    for (const vec2 &p : loop.points) {
      o.points.push_back({p.x * map.tile_size.x, p.y * map.tile_size.y});
      o.reach = std::max(o.reach, std::hypot(o.points.back().x, o.points.back().y));
    }
    out.push_back(std::move(o));
  }
  return out;
}
} // namespace njin
