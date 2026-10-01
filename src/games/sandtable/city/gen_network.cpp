#include "gen.h"

#include <algorithm>
#include <cmath>

// Stages 12, 13 and 16: the road graph, the nav grids and doors, and how
// blocks and districts touch; where gangs could set up.

namespace sandtable::city {

void generator::make_graph() {
  struct cut {
    f32 s;
    vec2 p;
  };
  const size_t n = m.roads.size();
  std::vector<std::vector<cut>> cuts(n);
  std::vector<polyline_walk> walks;
  std::vector<rect> boxes;
  walks.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    const road &rd = m.roads[i];
    walks.emplace_back(rd.pts);
    vec2 lo{1e30f, 1e30f}, hi{-1e30f, -1e30f};
    for (const vec2 p : rd.pts) {
      lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
      hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
    }
    boxes.push_back({lo - vec2{4, 4}, hi - lo + vec2{8, 8}});
    cuts[i].push_back({0.0f, rd.pts.front()});
    cuts[i].push_back({walks[i].length(), rd.pts.back()});
  }
  auto overlap = [](const rect &a, const rect &b) {
    return a.pos.x <= b.pos.x + b.size.x && b.pos.x <= a.pos.x + a.size.x && a.pos.y <= b.pos.y + b.size.y &&
           b.pos.y <= a.pos.y + a.size.y;
  };
  for (size_t i = 0; i < n; ++i)
    for (size_t j = i + 1; j < n; ++j) {
      if (!overlap(boxes[i], boxes[j]))
        continue;
      const road &a = m.roads[i], &b = m.roads[j];
      for (size_t p = 0; p + 1 < a.pts.size(); ++p)
        for (size_t q = 0; q + 1 < b.pts.size(); ++q) {
          f32 t, u;
          if (!seg_cross(a.pts[p], a.pts[p + 1], b.pts[q], b.pts[q + 1], t, u))
            continue;
          t = clamp(t, 0.0f, 1.0f);
          u = clamp(u, 0.0f, 1.0f);
          const vec2 at = lerp(a.pts[p], a.pts[p + 1], t);
          cuts[i].push_back({walks[i].acc[p] + distance(a.pts[p], a.pts[p + 1]) * t, at});
          cuts[j].push_back({walks[j].acc[q] + distance(b.pts[q], b.pts[q + 1]) * u, at});
        }
      // Ends that stop on the other road (T-junctions), either way round.
      for (const auto &[x, y] : {std::pair{i, j}, std::pair{j, i}}) {
        const road &e = m.roads[x];
        for (const vec2 end : {e.pts.front(), e.pts.back()}) {
          vec2 q;
          f32 s;
          if (closest_on(m.roads[y].pts, end, q, &s) < 3.0f)
            cuts[y].push_back({s, q});
        }
      }
    }
  // Nodes: every cut, points within a few units merged.
  auto node_at = [&](vec2 p) {
    for (i32 k = 0; k < static_cast<i32>(m.nodes.size()); ++k)
      if (distance(m.nodes[static_cast<size_t>(k)].pos, p) < 6.0f)
        return k;
    m.nodes.push_back({p, {}, false});
    return static_cast<i32>(m.nodes.size()) - 1;
  };
  for (size_t i = 0; i < n; ++i) {
    std::vector<cut> &cs_ = cuts[i];
    std::sort(cs_.begin(), cs_.end(), [](const cut &a, const cut &b) { return a.s < b.s; });
    i32 prev = -1;
    f32 prev_s = 0.0f;
    for (const cut &c : cs_) {
      const i32 nd = node_at(c.p);
      if (prev >= 0 && nd != prev && c.s - prev_s > 2.0f) {
        road_edge e;
        e.road = static_cast<i32>(i);
        e.a = prev;
        e.b = nd;
        e.pts = walks[i].slice(prev_s, c.s);
        e.length = c.s - prev_s;
        m.edges.push_back(std::move(e));
        const i32 eid = static_cast<i32>(m.edges.size()) - 1;
        m.nodes[static_cast<size_t>(prev)].edges.push_back(eid);
        m.nodes[static_cast<size_t>(nd)].edges.push_back(eid);
      }
      if (nd != prev) {
        prev = nd;
        prev_s = c.s;
      }
    }
  }
  for (road_node &nd : m.nodes)
    for (const spot &s : m.spots)
      if (s.kind == spot_kind::roundabout && distance(nd.pos, s.box.center) < s.box.half.x + 50.0f)
        nd.roundabout = true;
}

void generator::make_nav() {
  m.foot = nav_grid_make({0.0f, 0.0f}, {cs, cs}, m.cols, m.rows, 0);
  m.car = nav_grid_make({0.0f, 0.0f}, {cs, cs}, m.cols, m.rows, 0);
  for (i32 y = 0; y < m.rows; ++y)
    for (i32 x = 0; x < m.cols; ++x) {
      const cell_info &ci = m.at(x, y);
      // On foot any open ground is as good as any other: sidewalk, street,
      // square, yard or park.
      u8 foot = 0;
      switch (ci.g) {
      case ground::road:
      case ground::bridge:
      case ground::plaza:
      case ground::free:
      case ground::lot:
      case ground::park: foot = 1; break;
      default: break;
      }
      nav_set_cost(m.foot, {x, y}, foot);
      const bool drive = (ci.g == ground::road || ci.g == ground::bridge) && ci.carriage &&
                         ci.road != road_kind::alley;
      nav_set_cost(m.car, {x, y}, drive ? 1 : 0);
    }
  // Doors open only onto ground joined to the rest of the town: not a yard
  // walled in by houses.
  std::vector<i32> comp(m.cells.size(), -1), queue;
  i32 main = -1, main_size = 0;
  for (i32 i = 0; i < static_cast<i32>(m.cells.size()); ++i) {
    if (comp[static_cast<size_t>(i)] >= 0 || m.foot.cost[static_cast<size_t>(i)] == 0)
      continue;
    queue.assign(1, i);
    comp[static_cast<size_t>(i)] = i;
    for (size_t h = 0; h < queue.size(); ++h) {
      const i32 c = queue[h], x = c % m.cols, y = c / m.cols;
      const i32 nx[4] = {x + 1, x - 1, x, x};
      const i32 ny[4] = {y, y, y + 1, y - 1};
      for (i32 k = 0; k < 4; ++k) {
        if (!m.inside(nx[k], ny[k]))
          continue;
        const i32 n = idx(nx[k], ny[k]);
        if (comp[static_cast<size_t>(n)] < 0 && m.foot.cost[static_cast<size_t>(n)] != 0) {
          comp[static_cast<size_t>(n)] = i;
          queue.push_back(n);
        }
      }
    }
    if (static_cast<i32>(queue.size()) > main_size) {
      main_size = static_cast<i32>(queue.size());
      main = i;
    }
  }
  for (building &b : m.buildings) {
    const vec2 fwd = b.front();
    const vec2 side = b.box.axis_x();
    const vec2 tries[4] = {b.box.center + fwd * (b.box.half.y + 5.0f), b.box.center + side * (b.box.half.x + 5.0f),
                           b.box.center - side * (b.box.half.x + 5.0f), b.box.center - fwd * (b.box.half.y + 5.0f)};
    for (const vec2 p : tries) {
      i32 x, y;
      if (!cell_of(p, x, y))
        continue;
      const cell_info *c = &m.at(x, y);
      if (c->g != ground::building && c->g != ground::water && comp[static_cast<size_t>(idx(x, y))] == main) {
        b.door = p;
        b.door_ok = true;
        break;
      }
    }
    if (!b.door_ok)
      b.door = tries[0];
  }
}

void generator::make_links() {
  // Blocks facing each other across a street are neighbours.
  const i32 nb = static_cast<i32>(m.blocks.size());
  std::vector<std::vector<u8>> adj(static_cast<size_t>(nb), std::vector<u8>(static_cast<size_t>(nb), 0));
  for (i32 y = 0; y < m.rows; ++y)
    for (i32 x = 0; x < m.cols; ++x) {
      const cell_info &ci = m.at(x, y);
      if (ci.block < 0 || ci.g == ground::road)
        continue;
      // Across the road to the right and downward.
      for (const auto &[dx, dy] : {std::pair{1, 0}, std::pair{0, 1}}) {
        i32 xx = x + dx, yy = y + dy, steps = 0;
        while (m.inside(xx, yy) && steps < 16) {
          const cell_info &n = m.at(xx, yy);
          if (n.g != ground::road && n.g != ground::bridge)
            break;
          xx += dx;
          yy += dy;
          ++steps;
        }
        if (steps == 0 || !m.inside(xx, yy))
          continue;
        const i32 other = m.at(xx, yy).block;
        if (other >= 0 && other != ci.block)
          adj[static_cast<size_t>(ci.block)][static_cast<size_t>(other)] =
              adj[static_cast<size_t>(other)][static_cast<size_t>(ci.block)] = 1;
      }
    }
  for (i32 a = 0; a < nb; ++a)
    for (i32 b = 0; b < nb; ++b)
      if (adj[static_cast<size_t>(a)][static_cast<size_t>(b)])
        m.blocks[static_cast<size_t>(a)].neighbours.push_back(b);

  for (i32 i = 0; i < static_cast<i32>(m.buildings.size()); ++i) {
    const building &b = m.buildings[static_cast<size_t>(i)];
    if (b.block >= 0)
      m.blocks[static_cast<size_t>(b.block)].buildings.push_back(i);
  }
  for (i32 i = 0; i < static_cast<i32>(m.businesses.size()); ++i) {
    const business &b = m.businesses[static_cast<size_t>(i)];
    if (b.block >= 0)
      m.blocks[static_cast<size_t>(b.block)].businesses.push_back(i);
  }

  // Bridges: where a road crosses the river, a place to hold.
  for (const road &rd : m.roads) {
    if (rd.kind == road_kind::alley)
      continue;
    const polyline_walk walk(rd.pts);
    f32 start = -1.0f;
    for (f32 s = 0.0f; s <= walk.length(); s += 4.0f) {
      vec2 p, t;
      walk.at(s, p, t);
      const cell_info *c = m.cell_at(p);
      const bool wet = c && c->g == ground::bridge;
      if (wet && start < 0.0f)
        start = s;
      if ((!wet || s + 4.0f > walk.length()) && start >= 0.0f) {
        if (s - start > 20.0f) {
          vec2 mid, tm;
          walk.at((start + s) * 0.5f, mid, tm);
          add_spot({mid, {(s - start) * 0.5f, rd.reach()}, angle_of(tm)}, spot_kind::bridge, ground::free, false);
        }
        start = -1.0f;
      }
    }
  }
  for (i32 i = 0; i < static_cast<i32>(m.spots.size()); ++i) {
    const spot &s = m.spots[static_cast<size_t>(i)];
    if (s.block >= 0)
      m.blocks[static_cast<size_t>(s.block)].spots.push_back(i);
  }

  // Districts: their blocks, size, middle, and who borders whom.
  for (district &ds : m.districts) {
    ds.blocks.clear();
    ds.cells = 0;
  }
  std::vector<vec2> sum(m.districts.size(), vec2{});
  for (i32 bi = 0; bi < nb; ++bi) {
    const block &bk = m.blocks[static_cast<size_t>(bi)];
    district &ds = m.districts[static_cast<size_t>(bk.district)];
    ds.blocks.push_back(bi);
    ds.cells += bk.cells;
    sum[static_cast<size_t>(bk.district)] += bk.centroid * static_cast<f32>(bk.cells);
  }
  for (size_t i = 0; i < m.districts.size(); ++i) {
    district &ds = m.districts[i];
    ds.centroid = ds.cells > 0 ? sum[i] / static_cast<f32>(ds.cells) : ds.site;
    for (const i32 bi : ds.blocks)
      for (const i32 nb2 : m.blocks[static_cast<size_t>(bi)].neighbours) {
        const i32 od = m.blocks[static_cast<size_t>(nb2)].district;
        if (od != static_cast<i32>(i) &&
            std::find(ds.neighbours.begin(), ds.neighbours.end(), od) == ds.neighbours.end())
          ds.neighbours.push_back(od);
      }
    std::sort(ds.neighbours.begin(), ds.neighbours.end());
  }

  // Gang seats: plain houses with a door, spread as far apart as they go.
  std::vector<i32> cand;
  for (i32 i = 0; i < static_cast<i32>(m.buildings.size()); ++i) {
    const building &b = m.buildings[static_cast<size_t>(i)];
    if (b.door_ok && b.business < 0 && b.road >= 0 &&
        (b.kind == building_kind::tube_house || b.kind == building_kind::workshop ||
         b.kind == building_kind::warehouse))
      cand.push_back(i);
  }
  if (cand.empty())
    return;
  rng r = stage_rng(16);
  std::vector<i32> used_district;
  m.hq_sites.push_back(cand[static_cast<size_t>(r.range(0, static_cast<i32>(cand.size()) - 1))]);
  used_district.push_back(m.buildings[static_cast<size_t>(m.hq_sites.back())].district);
  while (m.hq_sites.size() < 8) {
    i32 best = -1;
    f32 bd = -1.0f;
    for (const i32 c : cand) {
      const building &b = m.buildings[static_cast<size_t>(c)];
      f32 md = 1e30f;
      for (const i32 h : m.hq_sites)
        md = std::min(md, distance(b.box.center, m.buildings[static_cast<size_t>(h)].box.center));
      if (std::find(used_district.begin(), used_district.end(), b.district) != used_district.end())
        md *= 0.5f;
      if (md > bd) {
        bd = md;
        best = c;
      }
    }
    if (best < 0 || bd < 200.0f)
      break;
    m.hq_sites.push_back(best);
    used_district.push_back(m.buildings[static_cast<size_t>(best)].district);
  }
}


} // namespace sandtable::city
