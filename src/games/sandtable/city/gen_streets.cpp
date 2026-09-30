#include "gen.h"

#include <algorithm>
#include <cmath>

// Stages 6 and 7: streets that split the ground into blocks of the size
// each district wants, then the blocks themselves.

namespace sandtable::city {

// Labels the connected free ground (4-neighbours), in scan order.
void generator::label_free(std::vector<region> &out) {
  out.clear();
  label.assign(m.cells.size(), -1);
  std::vector<i32> queue;
  for (i32 i = 0; i < static_cast<i32>(m.cells.size()); ++i) {
    if (label[static_cast<size_t>(i)] >= 0 || m.cells[static_cast<size_t>(i)].g != ground::free)
      continue;
    const i32 id = static_cast<i32>(out.size());
    out.push_back({});
    queue.clear();
    queue.push_back(i);
    label[static_cast<size_t>(i)] = id;
    for (size_t h = 0; h < queue.size(); ++h) {
      const i32 c = queue[h];
      out.back().cells.push_back(c);
      const i32 x = c % m.cols, y = c / m.cols;
      const i32 nx[4] = {x + 1, x - 1, x, x};
      const i32 ny[4] = {y, y, y + 1, y - 1};
      for (i32 k = 0; k < 4; ++k) {
        if (!m.inside(nx[k], ny[k]))
          continue;
        const i32 n = idx(nx[k], ny[k]);
        if (label[static_cast<size_t>(n)] < 0 && m.cells[static_cast<size_t>(n)].g == ground::free) {
          label[static_cast<size_t>(n)] = id;
          queue.push_back(n);
        }
      }
    }
  }
}

generator::shape_stats generator::stats(const std::vector<i32> &cells) const {
  shape_stats s;
  if (cells.empty())
    return s;
  f64 sx = 0, sy = 0;
  for (const i32 c : cells) {
    const vec2 p = m.center_of(c % m.cols, c / m.cols);
    sx += p.x;
    sy += p.y;
  }
  const f64 n = static_cast<f64>(cells.size());
  s.c = {static_cast<f32>(sx / n), static_cast<f32>(sy / n)};
  f64 cxx = 0, cyy = 0, cxy = 0;
  for (const i32 c : cells) {
    const vec2 p = m.center_of(c % m.cols, c / m.cols) - s.c;
    cxx += p.x * p.x;
    cyy += p.y * p.y;
    cxy += p.x * p.y;
  }
  s.angle = static_cast<f32>(0.5 * std::atan2(2.0 * cxy, cxx - cyy)) * (180.0f / pi);
  const vec2 u = from_angle(s.angle), v = perp(u);
  s.umin = s.vmin = 1e30f;
  s.umax = s.vmax = -1e30f;
  for (const i32 c : cells) {
    const vec2 p = m.center_of(c % m.cols, c / m.cols) - s.c;
    s.umin = std::min(s.umin, dot(p, u));
    s.umax = std::max(s.umax, dot(p, u));
    s.vmin = std::min(s.vmin, dot(p, v));
    s.vmax = std::max(s.vmax, dot(p, v));
  }
  s.umin -= cs * 0.5f;
  s.vmin -= cs * 0.5f;
  s.umax += cs * 0.5f;
  s.vmax += cs * 0.5f;
  return s;
}

i32 generator::majority_district(const std::vector<i32> &cells) const {
  i32 votes[16] = {};
  for (const i32 c : cells)
    ++votes[std::min<i32>(m.cells[static_cast<size_t>(c)].district, 15)];
  i32 best = 0;
  for (i32 i = 1; i < 16; ++i)
    if (votes[i] > votes[best])
      best = i;
  return best;
}

// From `start`, walks one way until it leaves region `rid`. The walk bends
// gently in organic districts.
bool generator::march(vec2 start, f32 heading, i32 rid, f32 organic, f32 phase, std::vector<vec2> &out, i32 &hit_cell, bool &hit_edge) {
  out.clear();
  vec2 p = start;
  const f32 step = cs * 0.5f;
  hit_cell = -1;
  hit_edge = false;
  for (i32 i = 0; i < 1200; ++i) {
    const f32 h = heading + organic * 16.0f * std::sin(static_cast<f32>(i) * step / 150.0f + phase);
    const vec2 next = p + from_angle(h) * step;
    i32 x, y;
    if (!cell_of(next, x, y)) {
      hit_edge = true;
      out.push_back(next + from_angle(h) * 40.0f);
      return true;
    }
    if (label[static_cast<size_t>(idx(x, y))] != rid) {
      hit_cell = idx(x, y);
      out.push_back(next);
      return true;
    }
    p = next;
    if (i % 6 == 5)
      out.push_back(p);
  }
  ++m.report.caps_hit;
  return false;
}

bool generator::try_split(const region &rg, i32 rid, const shape_stats &st, const district &dist, rng &r, bool big) {
  const profile &pf = prof(dist.kind);
  // Across the long way.
  f32 heading = st.angle + 90.0f;
  if (dist.kind == district_kind::new_urban) {
    // Snap to the grain of the new town's grid.
    const f32 g0 = dist.grain, g1 = dist.grain + 90.0f;
    heading = std::fabs(wrap_deg(heading - g0)) < 45.0f || std::fabs(wrap_deg(heading - g0 - 180.0f)) < 45.0f
                  ? g0
                  : g1;
  } else {
    heading += r.range(-6.0f, 6.0f) + r.range(-12.0f, 12.0f) * pf.organic;
  }
  const vec2 u = from_angle(st.angle);
  vec2 start = st.c + u * (r.range(-0.2f, 0.2f) * st.len());
  i32 sx, sy;
  if (!cell_of(start, sx, sy) || label[static_cast<size_t>(idx(sx, sy))] != rid) {
    // A bent block: start from its cell nearest the centre.
    f32 bd = 1e30f;
    for (const i32 c : rg.cells) {
      const f32 dd = length_sq(m.center_of(c % m.cols, c / m.cols) - start);
      if (dd < bd) {
        bd = dd;
        sx = c % m.cols;
        sy = c / m.cols;
      }
    }
    start = m.center_of(sx, sy);
  }
  const f32 phase = r.range(0.0f, 6.28f);
  std::vector<vec2> a, b;
  i32 ha, hb;
  bool ea, eb;
  if (!march(start, heading + 180.0f, rid, pf.organic, phase + pi, a, ha, ea) ||
      !march(start, heading, rid, pf.organic, phase, b, hb, eb))
    return false;
  std::vector<vec2> pts(a.rbegin(), a.rend());
  pts.push_back(start);
  pts.insert(pts.end(), b.begin(), b.end());
  // A street that ends on a road ends at its middle.
  auto snap = [&](vec2 &end, i32 hit) {
    if (hit < 0)
      return;
    const cell_info &ci = m.cells[static_cast<size_t>(hit)];
    if ((ci.g != ground::road && ci.g != ground::bridge) || ci.road_id < 0)
      return;
    vec2 q;
    if (closest_on(m.roads[static_cast<size_t>(ci.road_id)].pts, end, q) < 90.0f)
      end = q;
  };
  snap(pts.front(), ha);
  snap(pts.back(), hb);
  // Streets do not end in water or a park: those are for alleys and walks.
  auto dead = [&](i32 hit, bool edge) {
    if (edge || hit < 0)
      return false;
    const ground g = m.cells[static_cast<size_t>(hit)].g;
    return g != ground::road && g != ground::bridge;
  };
  if (dead(ha, ea) && dead(hb, eb))
    return false;

  road rd;
  rd.kind = road_kind::street;
  rd.width = pf.street_w + (big ? 8.0f : 0.0f);
  rd.sidewalk = pf.sidewalk + (big ? 2.0f : 0.0f);
  rd.pts = pts;

  // Would it leave two good blocks? Mark what it covers, count the pieces.
  const f32 lim = rd.reach() + cs * 0.5f;
  ++stamp_gen;
  const i32 cut = stamp_gen;
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    const vec2 a = pts[i], b = pts[i + 1];
    const i32 x0 = std::max(0, static_cast<i32>((std::min(a.x, b.x) - lim) / cs) - 1);
    const i32 x1 = std::min(m.cols - 1, static_cast<i32>((std::max(a.x, b.x) + lim) / cs) + 1);
    const i32 y0 = std::max(0, static_cast<i32>((std::min(a.y, b.y) - lim) / cs) - 1);
    const i32 y1 = std::min(m.rows - 1, static_cast<i32>((std::max(a.y, b.y) + lim) / cs) + 1);
    for (i32 y = y0; y <= y1; ++y)
      for (i32 x = x0; x <= x1; ++x)
        if (label[static_cast<size_t>(idx(x, y))] == rid && seg_dist(m.center_of(x, y), a, b) <= lim)
          stamp[static_cast<size_t>(idx(x, y))] = cut;
  }
  const i32 min_cells = static_cast<i32>(std::ceil(min_block_area / (cs * cs)));
  i32 good = 0;
  std::vector<i32> piece, queue;
  for (const i32 c0 : rg.cells) {
    if (stamp[static_cast<size_t>(c0)] >= cut)
      continue;
    ++stamp_gen;
    const i32 mark = stamp_gen;
    piece.clear();
    queue.assign(1, c0);
    stamp[static_cast<size_t>(c0)] = mark;
    for (size_t h = 0; h < queue.size(); ++h) {
      const i32 c = queue[h];
      piece.push_back(c);
      const i32 x = c % m.cols, y = c / m.cols;
      const i32 nx[4] = {x + 1, x - 1, x, x};
      const i32 ny[4] = {y, y, y + 1, y - 1};
      for (i32 k = 0; k < 4; ++k) {
        if (!m.inside(nx[k], ny[k]))
          continue;
        const i32 n = idx(nx[k], ny[k]);
        if (label[static_cast<size_t>(n)] == rid && stamp[static_cast<size_t>(n)] < cut) {
          stamp[static_cast<size_t>(n)] = mark;
          queue.push_back(n);
        }
      }
    }
    if (static_cast<i32>(piece.size()) < 16)
      continue; // a crumb at the end of the cut: left as a gap
    if (static_cast<i32>(piece.size()) < min_cells || stats(piece).wid() < min_block_thick)
      return false;
    ++good;
  }
  if (good < 2)
    return false;
  add_road(std::move(rd));
  return true;
}

// A fixed number per region, from where it starts: the same region always
// wants the same size, however many passes it takes.
f32 generator::region_factor(i32 first_cell) const {
  u32 h = static_cast<u32>(first_cell) * 2654435761u ^ (d.seed * 97u);
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  return 0.75f + static_cast<f32>(h % 1000u) / 1000.0f * 0.6f;
}

void generator::make_streets() {
  rng r = stage_rng(6);
  std::vector<u8> done(m.cells.size(), 0);
  std::vector<region> regions;
  for (i32 pass = 0; pass < 40; ++pass) {
    label_free(regions);
    bool any = false;
    for (i32 rid = 0; rid < static_cast<i32>(regions.size()); ++rid) {
      const region &rg = regions[static_cast<size_t>(rid)];
      const i32 first = rg.cells.front();
      if (done[static_cast<size_t>(first)])
        continue;
      const f32 area = static_cast<f32>(rg.cells.size()) * cs * cs;
      const i32 di = majority_district(rg.cells);
      const district &dist = m.districts[static_cast<size_t>(std::min(di, static_cast<i32>(m.districts.size()) - 1))];
      const f32 target = prof(dist.kind).block_area * d.density * region_factor(first);
      if (area <= target || area < min_block_area * 2.2f) {
        done[static_cast<size_t>(first)] = 1;
        continue;
      }
      const shape_stats st = stats(rg.cells);
      bool ok = false;
      for (i32 attempt = 0; attempt < 8 && !ok; ++attempt)
        ok = try_split(rg, rid, st, dist, r, area > target * 2.6f);
      if (ok)
        any = true;
      else
        done[static_cast<size_t>(first)] = 1;
    }
    if (!any)
      return;
  }
  ++m.report.caps_hit;
}

void generator::make_blocks() {
  std::vector<region> regions;
  label_free(regions);
  const i32 min_cells = static_cast<i32>(std::ceil(min_block_area / (cs * cs)));
  for (const region &rg : regions) {
    const shape_stats st = stats(rg.cells);
    if (static_cast<i32>(rg.cells.size()) < min_cells || st.wid() < min_block_thick * 0.6f) {
      // Too small to build on: a patch of green.
      for (const i32 c : rg.cells)
        m.cells[static_cast<size_t>(c)].g = ground::park;
      continue;
    }
    block bk;
    bk.district = majority_district(rg.cells);
    bk.cells = static_cast<i32>(rg.cells.size());
    bk.centroid = st.c;
    bk.axis = st.angle;
    vec2 lo{1e30f, 1e30f}, hi{-1e30f, -1e30f};
    const i32 id = static_cast<i32>(m.blocks.size());
    for (const i32 c : rg.cells) {
      cell_info &ci = m.cells[static_cast<size_t>(c)];
      ci.block = id;
      ci.district = static_cast<u8>(bk.district);
      const vec2 p = m.center_of(c % m.cols, c / m.cols);
      lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
      hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
    }
    bk.bounds = {lo - vec2{cs * 0.5f, cs * 0.5f}, hi - lo + vec2{cs, cs}};
    m.blocks.push_back(std::move(bk));
  }
}

// The driving network in connected pieces (carriageways of avenues and
// streets, 4-neighbours). Returns the biggest piece.
i32 generator::car_pieces(std::vector<i32> &comp, std::vector<i32> &sizes) const {
  comp.assign(m.cells.size(), -1);
  sizes.clear();
  auto drive = [&](i32 c) {
    const cell_info &ci = m.cells[static_cast<size_t>(c)];
    return (ci.g == ground::road || ci.g == ground::bridge) && ci.carriage && ci.road != road_kind::alley;
  };
  std::vector<i32> queue;
  i32 main = -1;
  for (i32 i = 0; i < static_cast<i32>(m.cells.size()); ++i) {
    if (comp[static_cast<size_t>(i)] >= 0 || !drive(i))
      continue;
    const i32 id = static_cast<i32>(sizes.size());
    queue.assign(1, i);
    comp[static_cast<size_t>(i)] = id;
    for (size_t h = 0; h < queue.size(); ++h) {
      const i32 c = queue[h], x = c % m.cols, y = c / m.cols;
      const i32 nx[4] = {x + 1, x - 1, x, x}, ny[4] = {y, y, y + 1, y - 1};
      for (i32 k = 0; k < 4; ++k) {
        if (!m.inside(nx[k], ny[k]))
          continue;
        const i32 n = idx(nx[k], ny[k]);
        if (comp[static_cast<size_t>(n)] < 0 && drive(n)) {
          comp[static_cast<size_t>(n)] = id;
          queue.push_back(n);
        }
      }
    }
    sizes.push_back(static_cast<i32>(queue.size()));
    if (main < 0 || sizes.back() > sizes[static_cast<size_t>(main)])
      main = id;
  }
  return main;
}

// A piece of the street network can end up cut off: a lake road whose
// streets all run off the table, say. Join each such piece to the main one
// by the shortest straight street, square off one of its roads, that crosses
// only open ground.
void generator::connect_streets() {
  std::vector<i32> comp, sizes;
  for (i32 round = 0; round < 16; ++round) {
    const i32 main = car_pieces(comp, sizes);
    f32 best = 1e30f;
    vec2 from{}, to{};
    i32 to_road = -1;
    for (i32 ri = 0; ri < static_cast<i32>(m.roads.size()); ++ri) {
      const road &rd = m.roads[static_cast<size_t>(ri)];
      if (rd.kind == road_kind::alley)
        continue;
      const polyline_walk walk(rd.pts);
      for (f32 s = 12.0f; s < walk.length() - 12.0f; s += 24.0f) {
        vec2 p, t;
        walk.at(s, p, t);
        i32 x, y;
        if (!cell_of(p, x, y))
          continue;
        const i32 piece = comp[static_cast<size_t>(idx(x, y))];
        if (piece < 0 || piece == main || sizes[static_cast<size_t>(piece)] < 20)
          continue;
        for (const f32 side : {-1.0f, 1.0f}) {
          const vec2 dir = perp(t) * side;
          for (f32 k = rd.reach(); k < 480.0f && k < best; k += 4.0f) {
            const vec2 q = p + dir * k;
            if (!cell_of(q, x, y))
              break;
            const cell_info &ci = m.at(x, y);
            const i32 c = comp[static_cast<size_t>(idx(x, y))];
            if (c == main) {
              best = k;
              from = p;
              to = q;
              to_road = ci.road_id;
              break;
            }
            if (c == piece || ci.g == ground::free || ci.g == ground::road)
              continue;
            break; // water, park, plaza, another cut-off piece
          }
        }
      }
    }
    if (to_road < 0)
      return; // one piece, or nothing to join by
    vec2 q;
    closest_on(m.roads[static_cast<size_t>(to_road)].pts, to, q);
    road rd;
    rd.kind = road_kind::street;
    rd.width = 36.0f;
    rd.sidewalk = 8.0f;
    rd.pts = {from, q};
    rd.name = take_street_name();
    add_road(std::move(rd));
  }
  ++m.report.caps_hit;
}

} // namespace sandtable::city
