#include "gen.h"

#include <algorithm>
#include <cmath>

// Stage 9: alleys (hẻm) growing into the blocks from the streets.

namespace sandtable::city {

// Whether an alley can pass `p`: nothing but free ground of its block within
// `clear`, except near where it started.
bool generator::alley_clear(vec2 p, f32 clear, i32 blk, vec2 origin, f32 origin_r) const {
  const i32 r = static_cast<i32>(std::ceil(clear / cs));
  i32 cx, cy;
  if (!cell_of(p, cx, cy))
    return false;
  for (i32 y = cy - r; y <= cy + r; ++y)
    for (i32 x = cx - r; x <= cx + r; ++x) {
      const vec2 c = m.center_of(x, y);
      if (distance(c, p) > clear)
        continue;
      if (!m.inside(x, y))
        return false;
      const cell_info &ci = m.at(x, y);
      if (ci.g == ground::free && ci.block == blk)
        continue;
      if (distance(c, origin) < origin_r)
        continue;
      return false;
    }
  return true;
}

void generator::grow_alley(const alley_seed &seed, i32 blk, const profile &pf, rng &r, std::vector<alley_seed> &more) {
  const f32 clear = alley_w * 0.5f + std::max(26.0f, pf.depth_min * 0.55f);
  const f32 origin_r = m.roads[static_cast<size_t>(seed.parent)].reach() + clear + cs;
  std::vector<vec2> pts{seed.mouth, seed.start};
  vec2 p = seed.start;
  f32 h = seed.heading;
  f32 run = 0.0f, total = 0.0f;
  f32 turn_at = r.range(70.0f, 170.0f);
  i32 last_turn = 0;
  bool joined = false;
  const f32 step = cs;
  const f32 max_len = r.range(160.0f, 420.0f);
  for (i32 i = 0; i < 200; ++i) {
    const vec2 next = p + from_angle(h) * step;
    if (!alley_clear(next, clear, blk, seed.mouth, origin_r)) {
      // Blocked: sometimes break through to the road or alley just ahead.
      if (r.chance(pf.alley_loop) && run > 24.0f) {
        for (f32 k = step; k <= clear + step * 2.0f; k += step * 0.5f) {
          const cell_info *c = m.cell_at(p + from_angle(h) * k);
          if (!c || c->block != blk)
            break;
          if (c->g == ground::road && c->road_id >= 0) {
            vec2 q;
            if (closest_on(m.roads[static_cast<size_t>(c->road_id)].pts, p + from_angle(h) * k, q) < clear) {
              pts.push_back(p);
              pts.push_back(q);
              joined = true;
            }
            break;
          }
          if (c->g != ground::free)
            break;
        }
      }
      if (joined)
        break;
      // Or turn a corner.
      if (run > 24.0f) {
        const i32 dir = last_turn != 0 ? -last_turn : (r.chance(0.5f) ? 1 : -1);
        const f32 nh = h + 90.0f * static_cast<f32>(dir);
        if (alley_clear(p + from_angle(nh) * step, clear, blk, seed.mouth, origin_r)) {
          pts.push_back(p);
          h = nh;
          last_turn = dir;
          run = 0.0f;
          turn_at = r.range(60.0f, 150.0f);
          continue;
        }
      }
      break;
    }
    p = next;
    run += step;
    total += step;
    if (total > max_len)
      break;
    if (run > turn_at && pf.organic > 0.0f) {
      const i32 dir = last_turn != 0 ? -last_turn : (r.chance(0.5f) ? 1 : -1);
      if (r.chance(0.35f + pf.organic * 0.4f)) {
        const f32 nh = h + 90.0f * static_cast<f32>(dir);
        if (alley_clear(p + from_angle(nh) * step, clear, blk, seed.mouth, origin_r)) {
          pts.push_back(p);
          // The other way off the corner, a branch.
          if (seed.depth < pf.alley_depth && r.chance(0.5f))
            more.push_back({p, p + from_angle(h) * step, h, -1, seed.depth + 1, total, 0});
          h = nh;
          last_turn = dir;
        }
      } else if (seed.depth < pf.alley_depth && r.chance(0.45f)) {
        // A side branch off a straight run.
        more.push_back({p, p + from_angle(h + 90.0f * static_cast<f32>(dir)) * step,
                        h + 90.0f * static_cast<f32>(dir), -1, seed.depth + 1, total, dir});
      }
      run = 0.0f;
      turn_at = r.range(60.0f, 150.0f);
    }
  }
  if (!joined)
    pts.push_back(p);
  if (total < 40.0f) {
    // Too short to be worth it; its branches go with it.
    more.erase(std::remove_if(more.begin(), more.end(), [&](const alley_seed &s) { return s.parent == -1; }),
               more.end());
    return;
  }
  road al;
  al.kind = road_kind::alley;
  al.width = alley_w;
  al.sidewalk = 0.0f;
  al.pts = std::move(pts);
  al.parent = seed.parent;
  // "Hẻm 45 Lê Lợi", its branches "Hẻm 45/12 Lê Lợi".
  const i32 number = static_cast<i32>(seed.parent_s / 16.0f) * 2 + (seed.parent_side > 0 ? 1 : 2);
  const std::string &pbase = road_base[static_cast<size_t>(seed.parent)];
  const std::string &pnum = road_number[static_cast<size_t>(seed.parent)];
  const std::string num = pnum.empty() ? std::to_string(number) : pnum + "/" + std::to_string(number);
  al.name = "Hẻm " + num + " " + pbase;
  const i32 id = add_road(std::move(al));
  if (id < 0)
    return;
  road_base.push_back(pbase);
  road_number.push_back(num);
  for (alley_seed &s : more)
    if (s.parent == -1)
      s.parent = id;
  if (!joined) {
    const road &rd = m.roads[static_cast<size_t>(id)];
    add_spot({rd.pts.back(), {alley_w * 0.5f, alley_w * 0.5f}, angle_of(rd.pts.back() - rd.pts[rd.pts.size() - 2])},
             spot_kind::dead_end, ground::free, false);
  }
}

void generator::make_alleys() {
  rng r = stage_rng(9);
  road_base.clear();
  road_number.clear();
  for (const road &rd : m.roads) {
    road_base.push_back(rd.name);
    road_number.push_back("");
  }
  for (i32 bi = 0; bi < static_cast<i32>(m.blocks.size()); ++bi) {
    const block &bk = m.blocks[static_cast<size_t>(bi)];
    const profile &pf = prof(m.districts[static_cast<size_t>(bk.district)].kind);
    if (pf.alley_spacing <= 0.0f || static_cast<f32>(bk.cells) * cs * cs < 150.0f * 150.0f)
      continue;
    // Mouths: block cells beside a street, spaced out, away from corners.
    std::vector<i32> edge;
    for (i32 y = static_cast<i32>(bk.bounds.pos.y / cs); y <= static_cast<i32>((bk.bounds.pos.y + bk.bounds.size.y) / cs); ++y)
      for (i32 x = static_cast<i32>(bk.bounds.pos.x / cs); x <= static_cast<i32>((bk.bounds.pos.x + bk.bounds.size.x) / cs); ++x) {
        if (!m.inside(x, y) || m.at(x, y).block != bi || m.at(x, y).g != ground::free)
          continue;
        const i32 nx[4] = {x + 1, x - 1, x, x};
        const i32 ny[4] = {y, y, y + 1, y - 1};
        for (i32 k = 0; k < 4; ++k)
          if (m.inside(nx[k], ny[k])) {
            const cell_info &n = m.at(nx[k], ny[k]);
            if (n.g == ground::road && n.road_id >= 0 && n.road != road_kind::alley) {
              edge.push_back(idx(x, y));
              break;
            }
          }
      }
    for (i32 i = static_cast<i32>(edge.size()) - 1; i > 0; --i)
      std::swap(edge[static_cast<size_t>(i)], edge[static_cast<size_t>(r.range(0, i))]);
    std::vector<vec2> mouths;
    std::vector<alley_seed> queue;
    const f32 spacing = pf.alley_spacing * r.range(0.85f, 1.2f);
    for (const i32 c : edge) {
      const vec2 p = m.center_of(c % m.cols, c / m.cols);
      bool near = false;
      for (const vec2 q : mouths)
        if (distance(p, q) < spacing)
          near = true;
      if (near)
        continue;
      // Which road it opens onto, and not at a corner where two meet.
      i32 rid = -1;
      const i32 x = c % m.cols, y = c / m.cols;
      bool corner = false;
      for (i32 yy = y - 6; yy <= y + 6 && !corner; ++yy)
        for (i32 xx = x - 6; xx <= x + 6; ++xx) {
          if (!m.inside(xx, yy))
            continue;
          const cell_info &n = m.at(xx, yy);
          if (n.g != ground::road || n.road_id < 0 || n.road == road_kind::alley)
            continue;
          if (rid < 0)
            rid = n.road_id;
          else if (n.road_id != rid) {
            corner = true;
            break;
          }
        }
      if (corner || rid < 0)
        continue;
      vec2 q;
      f32 s;
      closest_on(m.roads[static_cast<size_t>(rid)].pts, p, q, &s);
      vec2 dir = normalize(p - q);
      if (length_sq(p - q) < 1.0f)
        continue;
      // Square to the block's grain when close to it.
      f32 h = angle_of(dir);
      for (i32 k = 0; k < 4; ++k) {
        const f32 g = bk.axis + 90.0f * static_cast<f32>(k);
        if (std::fabs(wrap_deg(h - g)) < 25.0f) {
          h = g;
          break;
        }
      }
      const vec2 t = normalize(m.roads[static_cast<size_t>(rid)].pts.back() - m.roads[static_cast<size_t>(rid)].pts.front());
      const i32 side = cross(t, dir) > 0.0f ? 1 : -1;
      mouths.push_back(p);
      queue.push_back({q, p, h, rid, 0, s, side});
    }
    for (size_t qi = 0; qi < queue.size() && qi < 200; ++qi) {
      std::vector<alley_seed> more;
      const alley_seed seed = queue[qi];
      grow_alley(seed, bi, pf, r, more);
      for (const alley_seed &s : more)
        if (s.parent >= 0)
          queue.push_back(s);
    }
  }
}

} // namespace sandtable::city
