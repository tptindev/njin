#include "collision.h"
#include "_collide.h"
#include "_comps.h"
#include "_tilemap.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_draw.h"
#include "njin_body.h"
#include "njin_log.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace njin {
namespace {
// A collider covering more grid cells than this is tested against everything
// instead of being inserted cell by cell (a huge boss, a level-wide trigger).
constexpr i32 max_cells_per_collider = 256;

// One box or circle collider, resolved to world space for this pass.
struct shape_ref {
  entt::entity entity{};
  collider_shape shape = collider_box;
  rect bounds{};
  circle round{};
  u32 layer = 0;
  u32 mask = 0;
  bool trigger = false;
};

u32 id_of(entt::entity e) { return (u32)entt::to_integral(e); }

u64 pair_key(entt::entity a, entt::entity b) {
  const u32 x = id_of(a);
  const u32 y = id_of(b);
  return x < y ? ((u64)x << 32) | y : ((u64)y << 32) | x;
}

entt::entity pair_first(u64 key) { return (entt::entity)(u32)(key >> 32); }
entt::entity pair_second(u64 key) { return (entt::entity)(u32)(key & 0xFFFFFFFFu); }

bool layers_allow(u32 layer_a, u32 mask_a, u32 layer_b, u32 mask_b) {
  return (mask_a & layer_b) != 0 && (mask_b & layer_a) != 0;
}

shape_ref resolve(entt::entity entity, const transform &tr, const collider &col) {
  shape_ref s{};
  s.entity = entity;
  s.shape = col.shape;
  s.bounds = collider_bounds(tr, col);
  s.round = circle{rect_center(s.bounds), s.bounds.size.x * 0.5f};
  s.layer = col.layer;
  s.mask = col.mask;
  s.trigger = col.trigger;
  return s;
}

bool shapes_overlap(const shape_ref &a, const shape_ref &b) {
  if (a.shape == collider_circle && b.shape == collider_circle)
    return circles_overlap(a.round, b.round);
  if (a.shape == collider_circle)
    return circle_rect_overlap(a.round, b.bounds);
  if (b.shape == collider_circle)
    return circle_rect_overlap(b.round, a.bounds);
  return rects_overlap(a.bounds, b.bounds);
}

bool shape_overlaps_rect(const shape_ref &s, rect r) {
  return s.shape == collider_circle ? circle_rect_overlap(s.round, r)
                                    : rects_overlap(s.bounds, r);
}

bool shape_overlaps_circle(const shape_ref &s, circle c) {
  return s.shape == collider_circle ? circles_overlap(s.round, c)
                                    : circle_rect_overlap(c, s.bounds);
}

bool shape_contains(const shape_ref &s, vec2 p) {
  return s.shape == collider_circle ? point_in_circle(p, s.round)
                                    : point_in_rect(p, s.bounds);
}

// Calls fnc(entity, tilemap, origin) for every enabled collider_tiles whose
// layers pass `mask` (or the two-way rule when `layer` is given).
template <class Fnc>
void each_tiles(const entt::registry &registry, Fnc &&fnc) {
  for (auto [entity, tr, col, map] :
       registry.view<const transform, const collider, const tilemap>().each()) {
    if (col.enabled && col.shape == collider_tiles)
      fnc(entity, col, map, tr.pos);
  }
}

// Calls fnc(x, y, shape) for every tile overlapping `area` that collides
// (non-empty, and not tile_none).
template <class Fnc>
void each_tile_in(const tilemap &map, vec2 origin, rect area, Fnc &&fnc) {
  if (area.size.x < 0.0f || area.size.y < 0.0f)
    return;
  const cell a = tilemap_cell_at(map, origin, area.pos);
  const cell b = tilemap_cell_at(map, origin, area.pos + area.size - vec2{1e-4f, 1e-4f});
  for (i32 y = a.y; y <= b.y; y++) {
    for (i32 x = a.x; x <= b.x; x++) {
      const tile_shape shape = tilemap_shape(map, tilemap_get(map, x, y));
      if (shape != tile_none)
        fnc(x, y, shape);
    }
  }
}

// --- detection ---

void detect(njin_ctx &ctx) {
  entt::registry &registry = world(ctx);
  collision_state &state = ctx.collision;
  const f32 cell = state.cell_size;

  std::vector<shape_ref> shapes;
  for (auto [entity, tr, col] : registry.view<const transform, const collider>().each()) {
    if (col.enabled && col.shape != collider_tiles)
      shapes.push_back(resolve(entity, tr, col));
  }

  // Uniform grid, as a flat list of (cell, shape) sorted by cell: every shape
  // goes into each cell its bounds touch, and shapes that share a run of equal
  // cells are candidates. Shapes touching too many cells are checked against
  // everything instead.
  std::vector<std::pair<u64, i32>> &cells = state.cells;
  std::vector<u64> &candidates = state.candidates;
  cells.clear();
  candidates.clear();
  std::vector<i32> large;
  for (i32 i = 0; i < (i32)shapes.size(); i++) {
    const rect &b = shapes[(usize)i].bounds;
    const i32 x0 = (i32)std::floor(b.pos.x / cell);
    const i32 y0 = (i32)std::floor(b.pos.y / cell);
    const i32 x1 = (i32)std::floor((b.pos.x + b.size.x) / cell);
    const i32 y1 = (i32)std::floor((b.pos.y + b.size.y) / cell);
    if ((i64)(x1 - x0 + 1) * (i64)(y1 - y0 + 1) > max_cells_per_collider) {
      large.push_back(i);
      continue;
    }
    for (i32 y = y0; y <= y1; y++) {
      for (i32 x = x0; x <= x1; x++)
        cells.emplace_back(((u64)(u32)x << 32) | (u32)y, i);
    }
  }
  std::sort(cells.begin(), cells.end());
  for (usize start = 0; start < cells.size();) {
    usize end = start + 1;
    while (end < cells.size() && cells[end].first == cells[start].first)
      end++;
    for (usize a = start; a < end; a++) {
      for (usize b = a + 1; b < end; b++)
        candidates.push_back(((u64)(u32)cells[a].second << 32) | (u32)cells[b].second);
    }
    start = end;
  }
  for (const i32 big : large) {
    for (i32 other = 0; other < (i32)shapes.size(); other++) {
      if (other != big)
        candidates.push_back(((u64)(u32)std::min(big, other) << 32) |
                             (u32)std::max(big, other));
    }
  }
  // A pair sharing several cells shows up once per cell.
  std::sort(candidates.begin(), candidates.end());
  candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

  std::vector<std::pair<u64, bool>> now;
  for (const u64 c : candidates) {
    const shape_ref &a = shapes[(usize)(c >> 32)];
    const shape_ref &b = shapes[(usize)(c & 0xFFFFFFFFu)];
    if (!layers_allow(a.layer, a.mask, b.layer, b.mask) || !shapes_overlap(a, b))
      continue;
    now.emplace_back(pair_key(a.entity, b.entity), a.trigger || b.trigger);
  }
  std::sort(now.begin(), now.end());

  // Walk last frame's and this frame's sorted pairs together: only in now is
  // enter, in both is stay, only in last is exit.
  entt::dispatcher &dispatcher = events(ctx);
  const std::vector<std::pair<u64, bool>> &last = state.touching;
  usize i = 0;
  usize j = 0;
  while (i < now.size() || j < last.size()) {
    const bool take_now = j >= last.size() || (i < now.size() && now[i].first <= last[j].first);
    const bool take_last = i >= now.size() || (j < last.size() && last[j].first <= now[i].first);
    const u64 key = take_now ? now[i].first : last[j].first;
    const bool trigger = take_now ? now[i].second : last[j].second;
    const entt::entity a = pair_first(key);
    const entt::entity b = pair_second(key);
    if (take_now && take_last) {
      dispatcher.enqueue(collision_stay{a, b, trigger});
      dispatcher.enqueue(collision_stay{b, a, trigger});
    } else if (take_now) {
      dispatcher.enqueue(collision_enter{a, b, trigger});
      dispatcher.enqueue(collision_enter{b, a, trigger});
    } else {
      // The index alone is not enough after a destroy: the slot may already
      // hold a new entity, so check full validity (index and version).
      if (registry.valid(a))
        dispatcher.enqueue(collision_exit{a, b, trigger});
      if (registry.valid(b))
        dispatcher.enqueue(collision_exit{b, a, trigger});
    }
    i += take_now ? 1 : 0;
    j += take_last ? 1 : 0;
  }
  state.touching = std::move(now);
}

void draw_debug(njin_ctx &ctx) {
  if (!ctx.collision.debug)
    return;
  const rgba solid{0.2f, 1.0f, 0.3f, 0.9f};
  const rgba trig{1.0f, 0.85f, 0.1f, 0.9f};
  for (auto [entity, tr, col] : world(ctx).view<const transform, const collider>().each()) {
    if (col.shape == collider_tiles)
      continue;
    const rgba color = col.enabled ? (col.trigger ? trig : solid)
                                   : rgba{0.5f, 0.5f, 0.5f, 0.6f};
    const rect b = collider_bounds(tr, col);
    if (col.shape == collider_circle)
      draw_circle_lines(ctx, rect_center(b), b.size.x * 0.5f, 1.0f, color);
    else
      draw_rect_lines(ctx, b, 1.0f, color);
  }
}

void setup(njin_ctx &ctx) {
  ecs_register(ctx, phase_post_update, detect, "detect");
  ecs_register(ctx, phase_render, draw_debug, "draw_debug");
}

// --- raycast helpers ---

// Segment from `o` along `d` (t in 0..1) against a rectangle. Returns the
// entry t and the face normal, or false.
bool ray_rect(vec2 o, vec2 d, rect r, f32 &t_hit, vec2 &normal) {
  f32 t0 = 0.0f;
  f32 t1 = 1.0f;
  vec2 n{};
  const f32 lo[2] = {r.pos.x, r.pos.y};
  const f32 hi[2] = {r.pos.x + r.size.x, r.pos.y + r.size.y};
  const f32 oo[2] = {o.x, o.y};
  const f32 dd[2] = {d.x, d.y};
  for (i32 axis = 0; axis < 2; axis++) {
    if (dd[axis] == 0.0f) {
      if (oo[axis] < lo[axis] || oo[axis] > hi[axis])
        return false;
      continue;
    }
    f32 a = (lo[axis] - oo[axis]) / dd[axis];
    f32 b = (hi[axis] - oo[axis]) / dd[axis];
    f32 sign = -1.0f;
    if (a > b) {
      std::swap(a, b);
      sign = 1.0f;
    }
    if (a > t0) {
      t0 = a;
      n = axis == 0 ? vec2{sign, 0.0f} : vec2{0.0f, sign};
    }
    t1 = std::min(t1, b);
    if (t0 > t1)
      return false;
  }
  t_hit = t0;
  // Started inside: there is no face crossed; point back along the ray.
  normal = t0 > 0.0f ? n : normalize(-d);
  return true;
}

bool ray_circle(vec2 o, vec2 d, circle c, f32 &t_hit, vec2 &normal) {
  const vec2 m = o - c.center;
  const f32 cc = dot(m, m) - c.radius * c.radius;
  if (cc <= 0.0f) { // starts inside
    t_hit = 0.0f;
    normal = normalize(m);
    return true;
  }
  const f32 a = dot(d, d);
  if (a == 0.0f)
    return false;
  const f32 b = dot(m, d);
  const f32 disc = b * b - a * cc;
  if (b > 0.0f || disc < 0.0f)
    return false;
  const f32 t = (-b - std::sqrt(disc)) / a;
  if (t < 0.0f || t > 1.0f)
    return false;
  t_hit = t;
  normal = normalize(m + d * t);
  return true;
}
} // namespace

mod_desc collision_module() {
  return mod_desc{.name = "njin.collision", .setup = setup};
}

void collision_set_debug(njin_ctx &ctx, bool on) { ctx.collision.debug = on; }

void collision_set_cell_size(njin_ctx &ctx, f32 size) {
  if (size > 0.0f)
    ctx.collision.cell_size = size;
  else
    NJIN_WARN("collision_set_cell_size: size must be > 0");
}

// --- movement ---

namespace {
// One thing that can stop a move. `kind` is a tile_shape: tile_solid,
// tile_one_way or a slope (colliders only ever use the first two).
struct obstacle {
  rect box;
  entt::entity entity;
  tile_shape kind = tile_solid;
  bool tile = false;
};

constexpr f32 move_eps = 0.01f;

f32 bottom_of(const rect &r) { return r.pos.y + r.size.y; }
f32 right_of(const rect &r) { return r.pos.x + r.size.x; }

// Height (world y) of a slope's surface at world x.
f32 slope_y(const obstacle &ob, f32 x) {
  const f32 u = ob.box.size.x > 0.0f ? (x - ob.box.pos.x) / ob.box.size.x : 0.0f;
  return bottom_of(ob.box) - ob.box.size.y * tile_surface(ob.kind, u);
}

// A moving box against the obstacles around it, one axis at a time.
struct mover {
  std::vector<obstacle> obstacles;
  bool drop_through = false;
  f32 feet = 0.0f;       // how far below the box's bottom a low tile may rise and be stepped onto from a slope
  bool on_slope = false; // standing on a slope when the move began

  bool one_way_ok(const obstacle &ob, f32 bottom) const {
    return !drop_through && bottom <= ob.box.pos.y + move_eps;
  }

  // The rectangle `ob` blocks with when moving along an axis, or an empty one.
  rect face(const obstacle &ob, f32 step, bool horizontal) const {
    if (!tile_is_slope(ob.kind))
      return ob.box;
    if (!horizontal) // moving up: the underside of a slope is solid
      return step < 0.0f ? ob.box : rect{};
    // The high side of a slope is a wall; the low side is open. Walking onto
    // it from level ground at its foot or its top is not blocked.
    const f32 h = tile_surface(ob.kind, step > 0.0f ? 0.0f : 1.0f);
    const f32 top = bottom_of(ob.box) - ob.box.size.y * h + feet;
    if (top >= bottom_of(ob.box))
      return rect{};
    return rect{{ob.box.pos.x, top}, {ob.box.size.x, bottom_of(ob.box) - top}};
  }

  // Sweeps `r` by `step` along one axis and stops it at the first obstacle
  // in the way, so a long step cannot tunnel through a thin wall. Obstacles
  // `r` already overlaps are skipped, so an entity that starts stuck can move
  // out. Returns the index of the obstacle hit, or -1.
  i32 sweep(rect &r, f32 step, bool horizontal) const {
    const i32 a = horizontal ? 0 : 1; // moving axis
    const i32 o = 1 - a;              // other axis
    const auto lo = [](const rect &x, i32 axis) { return axis == 0 ? x.pos.x : x.pos.y; };
    const auto len = [](const rect &x, i32 axis) { return axis == 0 ? x.size.x : x.size.y; };
    f32 allowed = step;
    i32 hit = -1;
    const f32 cx = r.pos.x + r.size.x * 0.5f;
    for (i32 i = 0; i < (i32)obstacles.size(); i++) {
      const obstacle &ob = obstacles[(usize)i];
      if (ob.kind == tile_one_way && (horizontal || step < 0.0f || !one_way_ok(ob, bottom_of(r))))
        continue;
      if (!horizontal && step > 0.0f && tile_is_slope(ob.kind)) {
        // Falling onto a slope lands on its surface under the box's centre.
        if (cx < ob.box.pos.x || cx >= right_of(ob.box))
          continue;
        const f32 gap = slope_y(ob, cx) - bottom_of(r);
        if (gap < -move_eps || gap >= allowed)
          continue;
        allowed = std::max(gap, 0.0f);
        hit = i;
        continue;
      }
      // On a slope, low tiles at the feet are walked onto, not bumped into.
      if (horizontal && on_slope && ob.tile && ob.kind == tile_solid &&
          ob.box.pos.y >= bottom_of(r) - feet)
        continue;
      const rect box = face(ob, step, horizontal);
      if (box.size.x <= 0.0f || box.size.y <= 0.0f || rects_overlap(r, box))
        continue;
      // Must share the other axis (strictly: touching sides slide past).
      if (!(lo(r, o) < lo(box, o) + len(box, o) && lo(box, o) < lo(r, o) + len(r, o)))
        continue;
      f32 gap = 0.0f;
      if (step > 0.0f) {
        gap = lo(box, a) - (lo(r, a) + len(r, a));
        if (gap < 0.0f || gap >= allowed)
          continue;
      } else {
        gap = (lo(box, a) + len(box, a)) - lo(r, a);
        if (gap > 0.0f || gap <= allowed)
          continue;
      }
      allowed = gap;
      hit = i;
    }
    if (horizontal)
      r.pos.x += allowed;
    else
      r.pos.y += allowed;
    return hit;
  }

  // Highest ground under the centre of `r`'s bottom edge, from `up` above it
  // to `down` below: a slope's surface, the top of a solid tile, or of a
  // one-way platform the box started above. Colliders are left to the sweep.
  // Returns its index, or -1.
  i32 ground_under(const rect &r, f32 up, f32 down, f32 start_bottom, f32 &y) const {
    const f32 cx = r.pos.x + r.size.x * 0.5f;
    const f32 bottom = bottom_of(r);
    i32 best = -1;
    for (i32 i = 0; i < (i32)obstacles.size(); i++) {
      const obstacle &ob = obstacles[(usize)i];
      if (!ob.tile || cx < ob.box.pos.x || cx >= right_of(ob.box))
        continue;
      f32 s = ob.box.pos.y;
      if (tile_is_slope(ob.kind))
        s = slope_y(ob, cx);
      else if (ob.kind == tile_one_way && !one_way_ok(ob, start_bottom))
        continue;
      if (s < bottom - up || s > bottom + down)
        continue;
      if (best < 0 || s < y) {
        best = i;
        y = s;
      }
    }
    return best;
  }
};
} // namespace

collision_move_result collision_move(njin_ctx &ctx, entt::entity entity, vec2 delta) {
  return collision_move(ctx, entity, delta, collision_move_opts{});
}

collision_move_result collision_move(njin_ctx &ctx, entt::entity entity, vec2 delta,
                                     const collision_move_opts &opts) {
  entt::registry &registry = world(ctx);
  collision_move_result result{};
  transform *tr = registry.valid(entity) ? registry.try_get<transform>(entity) : nullptr;
  const collider *self = tr != nullptr ? registry.try_get<collider>(entity) : nullptr;
  if (self == nullptr || self->shape == collider_tiles) {
    NJIN_WARN("collision_move: entity needs a transform and a box or circle collider");
    return result;
  }
  const rect start = collider_bounds(*tr, *self);
  if (!self->enabled) {
    if (!opts.test_only)
      tr->pos += delta;
    result.moved = delta;
    return result;
  }

  mover m;
  m.drop_through = opts.drop_through;
  // A slope rises at most one tile per tile, so between the centre of the
  // box's bottom (which rides the slope) and its edges there is at most half
  // its width.
  m.feet = std::min(start.size.x * 0.5f + 1.0f, start.size.y * 0.5f);
  const f32 snap = std::max(opts.snap_down, 0.0f);

  // Everything that could block this move: the union of where the box is and
  // where it wants to go, grown enough to find the ground under it.
  rect swept = start;
  swept.pos.x = std::min(start.pos.x, start.pos.x + delta.x);
  swept.pos.y = std::min(start.pos.y, start.pos.y + delta.y) - m.feet;
  swept.size.x += std::abs(delta.x);
  swept.size.y += std::abs(delta.y) + m.feet * 2.0f + snap + 1.0f;

  for (auto [other, otr, col] : registry.view<const transform, const collider>().each()) {
    if (other == entity || !col.enabled || col.trigger || col.shape == collider_tiles)
      continue;
    if (!layers_allow(self->layer, self->mask, col.layer, col.mask))
      continue;
    if (const child_of *link = registry.try_get<child_of>(other); link != nullptr && link->parent == entity)
      continue;
    const rect b = collider_bounds(otr, col);
    if (rects_overlap(b, swept))
      m.obstacles.push_back(
          {b, other, col.one_way && col.shape == collider_box ? tile_one_way : tile_solid, false});
  }
  each_tiles(registry, [&](entt::entity tiles, const collider &col, const tilemap &map, vec2 origin) {
    if (!layers_allow(self->layer, self->mask, col.layer, col.mask))
      return;
    each_tile_in(map, origin, swept, [&](i32 x, i32 y, tile_shape shape) {
      m.obstacles.push_back({tilemap_cell_rect(map, origin, x, y), tiles, shape, true});
    });
  });

  const f32 start_bottom = bottom_of(start);
  f32 ground_y = 0.0f;
  {
    const i32 g = m.ground_under(start, 1.0f, 1.0f, start_bottom, ground_y);
    m.on_slope = g >= 0 && tile_is_slope(m.obstacles[(usize)g].kind);
  }

  rect r = start;
  if (delta.x != 0.0f) {
    const i32 hit = m.sweep(r, delta.x, true);
    result.hit_x = hit >= 0;
    if (hit >= 0)
      result.other_x = m.obstacles[(usize)hit].entity;
  }
  if (delta.y != 0.0f) {
    const i32 hit = m.sweep(r, delta.y, false);
    result.hit_y = hit >= 0;
    if (hit >= 0)
      result.other_y = m.obstacles[(usize)hit].entity;
  }

  if (delta.y >= 0.0f) {
    // Walking up a slope (or onto the step at its top) leaves the feet under
    // the surface: lift them onto it. A fast fall may sink by one step's worth.
    const f32 up = m.feet + std::max(delta.y, 0.0f);
    const i32 g = m.ground_under(r, up, 0.0f, start_bottom, ground_y);
    if (g >= 0 && ground_y < bottom_of(r) - move_eps) {
      r.pos.y = ground_y - r.size.y;
      result.hit_y = true;
      result.other_y = m.obstacles[(usize)g].entity;
    } else if (snap > 0.0f && !result.hit_y) {
      // Walking down a slope: stay on the ground when it is close below,
      // instead of floating off it.
      rect probe = r;
      if (m.sweep(probe, snap, false) >= 0)
        r = probe;
    }
  }

  // What it stands on: a short probe down from where the move ended.
  {
    rect probe = r;
    const i32 g = m.sweep(probe, 0.5f, false);
    if (g >= 0) {
      const obstacle &ob = m.obstacles[(usize)g];
      result.grounded = true;
      result.ground = ob.entity;
      result.on_slope = tile_is_slope(ob.kind);
      result.ground_one_way = ob.kind == tile_one_way;
    }
  }

  result.moved = r.pos - start.pos;
  if (!opts.test_only)
    tr->pos += result.moved;
  return result;
}

void collision_move_platform(njin_ctx &ctx, entt::entity platform, vec2 delta) {
  entt::registry &registry = world(ctx);
  transform *tr = registry.valid(platform) ? registry.try_get<transform>(platform) : nullptr;
  const collider *col = tr != nullptr ? registry.try_get<collider>(platform) : nullptr;
  if (col == nullptr || col->shape == collider_tiles) {
    NJIN_WARN("collision_move_platform: entity needs a transform and a box collider");
    return;
  }
  if (delta.x == 0.0f && delta.y == 0.0f)
    return;
  const rect before = collider_bounds(*tr, *col);

  // Who rides: bodies whose bottom sits on the platform's top.
  std::vector<entt::entity> riders;
  std::vector<entt::entity> others;
  for (auto [e, etr, ecol] : registry.view<const transform, const collider>().each()) {
    if (e == platform || !ecol.enabled || ecol.trigger || ecol.shape == collider_tiles)
      continue;
    if (!registry.any_of<platform_rider, platformer_body, topdown_body>(e))
      continue;
    if (const child_of *link = registry.try_get<child_of>(e); link != nullptr && link->parent == platform)
      continue;
    const rect b = collider_bounds(etr, ecol);
    const bool above = std::abs(bottom_of(b) - before.pos.y) <= 1.0f &&
                       b.pos.x < right_of(before) && before.pos.x < right_of(b);
    (above ? riders : others).push_back(e);
  }

  tr->pos += delta;
  for (const entt::entity e : riders)
    collision_move(ctx, e, delta);
  if (col->one_way)
    return;
  // Push aside what the platform ran into.
  const rect after = collider_bounds(*tr, *col);
  for (const entt::entity e : others) {
    const rect b = collider_bounds(registry.get<transform>(e), registry.get<collider>(e));
    if (!rects_overlap(b, after))
      continue;
    vec2 push{};
    if (std::abs(delta.x) >= std::abs(delta.y))
      push.x = delta.x > 0.0f ? right_of(after) - b.pos.x : after.pos.x - right_of(b);
    else
      push.y = delta.y > 0.0f ? bottom_of(after) - b.pos.y : after.pos.y - bottom_of(b);
    collision_move(ctx, e, push);
  }
}

// --- queries ---

namespace {
template <class Test, class TileTest>
i32 overlap_query(const njin_ctx &ctx, rect area, std::vector<entt::entity> *out,
                  u32 mask, bool include_triggers, Test &&test, TileTest &&tile_test) {
  const entt::registry &registry = ctx.ecs.registry;
  i32 count = 0;
  for (auto [entity, tr, col] : registry.view<const transform, const collider>().each()) {
    if (!col.enabled || (col.layer & mask) == 0 || (col.trigger && !include_triggers))
      continue;
    if (col.shape == collider_tiles) {
      const tilemap *map = registry.try_get<tilemap>(entity);
      if (map == nullptr)
        continue;
      bool any = false;
      each_tile_in(*map, tr.pos, area, [&](i32 x, i32 y, tile_shape) {
        any = any || tile_test(tilemap_cell_rect(*map, tr.pos, x, y));
      });
      if (!any)
        continue;
    } else if (!test(resolve(entity, tr, col))) {
      continue;
    }
    count++;
    if (out != nullptr)
      out->push_back(entity);
  }
  return count;
}
} // namespace

i32 collision_overlap_rect(const njin_ctx &ctx, rect area, std::vector<entt::entity> *out,
                           u32 mask, bool include_triggers) {
  return overlap_query(
      ctx, area, out, mask, include_triggers,
      [&](const shape_ref &s) { return shape_overlaps_rect(s, area); },
      [&](rect tile) { return rects_overlap(tile, area); });
}

i32 collision_overlap_circle(const njin_ctx &ctx, circle area, std::vector<entt::entity> *out,
                             u32 mask, bool include_triggers) {
  const rect bounds = rect_from_center(area.center, vec2{area.radius, area.radius} * 2.0f);
  return overlap_query(
      ctx, bounds, out, mask, include_triggers,
      [&](const shape_ref &s) { return shape_overlaps_circle(s, area); },
      [&](rect tile) { return circle_rect_overlap(area, tile); });
}

i32 collision_overlap_point(const njin_ctx &ctx, vec2 point, std::vector<entt::entity> *out,
                            u32 mask, bool include_triggers) {
  // A tiny area so the tile lookup finds the one cell holding the point.
  const rect area{point, {1e-3f, 1e-3f}};
  return overlap_query(
      ctx, area, out, mask, include_triggers,
      [&](const shape_ref &s) { return shape_contains(s, point); },
      [&](rect tile) { return point_in_rect(point, tile); });
}

raycast_hit collision_raycast(const njin_ctx &ctx, vec2 from, vec2 to, u32 mask,
                              bool include_triggers, entt::entity ignore) {
  const entt::registry &registry = ctx.ecs.registry;
  const vec2 d = to - from;
  raycast_hit best{};
  f32 best_t = std::numeric_limits<f32>::max();
  const auto consider = [&](f32 t, vec2 normal, entt::entity entity) {
    if (t < best_t) {
      best_t = t;
      best = raycast_hit{.hit = true, .entity = entity, .point = from + d * t,
                         .normal = normal, .distance = length(d) * t};
    }
  };

  for (auto [entity, tr, col] : registry.view<const transform, const collider>().each()) {
    if (entity == ignore || !col.enabled || (col.layer & mask) == 0 ||
        (col.trigger && !include_triggers))
      continue;
    f32 t = 0.0f;
    vec2 n{};
    if (col.shape == collider_tiles) {
      const tilemap *map = registry.try_get<tilemap>(entity);
      if (map == nullptr)
        continue;
      // Walk the cells the segment crosses, in order (Amanatides–Woo).
      const vec2 origin = tr.pos;
      cell c = tilemap_cell_at(*map, origin, from);
      const cell last = tilemap_cell_at(*map, origin, to);
      const i32 step_x = d.x > 0.0f ? 1 : -1;
      const i32 step_y = d.y > 0.0f ? 1 : -1;
      const rect first = tilemap_cell_rect(*map, origin, c.x, c.y);
      const f32 next_x = step_x > 0 ? first.pos.x + first.size.x : first.pos.x;
      const f32 next_y = step_y > 0 ? first.pos.y + first.size.y : first.pos.y;
      const f32 inf = std::numeric_limits<f32>::max();
      f32 t_max_x = d.x != 0.0f ? (next_x - from.x) / d.x : inf;
      f32 t_max_y = d.y != 0.0f ? (next_y - from.y) / d.y : inf;
      const f32 t_dx = d.x != 0.0f ? map->tile_size.x / std::abs(d.x) : inf;
      const f32 t_dy = d.y != 0.0f ? map->tile_size.y / std::abs(d.y) : inf;
      f32 t_enter = 0.0f;
      vec2 enter_normal = normalize(-d);
      const i32 max_steps = std::abs(last.x - c.x) + std::abs(last.y - c.y) + 2;
      bool first_cell = true;
      for (i32 i = 0; i < max_steps && t_enter <= 1.0f && t_enter < best_t; i++) {
        const tile_shape shape = tilemap_shape(*map, tilemap_get(*map, c.x, c.y));
        if (shape == tile_solid) {
          consider(t_enter, enter_normal, entity);
          break;
        }
        if (shape == tile_one_way && !first_cell && enter_normal.y < 0.0f) {
          consider(t_enter, enter_normal, entity); // only its top, from above
          break;
        }
        if (tile_is_slope(shape)) {
          // Inside the cell the surface is a straight line, so how far the ray
          // is below it changes linearly from entry to exit.
          const rect cr = tilemap_cell_rect(*map, origin, c.x, c.y);
          const f32 t_exit = std::min(std::min(t_max_x, t_max_y), 1.0f);
          const auto below = [&](f32 t) {
            const vec2 p = from + d * t;
            const f32 u = (p.x - cr.pos.x) / cr.size.x;
            return p.y - (cr.pos.y + cr.size.y - cr.size.y * tile_surface(shape, u));
          };
          const f32 f0 = below(t_enter);
          const f32 f1 = below(t_exit);
          if (f0 >= 0.0f) {
            consider(t_enter, enter_normal, entity);
            break;
          }
          if (f1 >= 0.0f) {
            const f32 t = t_enter + (t_exit - t_enter) * (-f0 / (f1 - f0));
            // Surface y = bottom - h(u) * height: its slope in world units.
            const f32 k = (tile_surface(shape, 1.0f) - tile_surface(shape, 0.0f)) *
                          cr.size.y / cr.size.x;
            consider(t, normalize(vec2{-k, -1.0f}), entity);
            break;
          }
        }
        first_cell = false;
        if (t_max_x < t_max_y) {
          t_enter = t_max_x;
          t_max_x += t_dx;
          c.x += step_x;
          enter_normal = {(f32)-step_x, 0.0f};
        } else {
          t_enter = t_max_y;
          t_max_y += t_dy;
          c.y += step_y;
          enter_normal = {0.0f, (f32)-step_y};
        }
      }
      continue;
    }
    const shape_ref s = resolve(entity, tr, col);
    const bool hit = s.shape == collider_circle ? ray_circle(from, d, s.round, t, n)
                                                : ray_rect(from, d, s.bounds, t, n);
    if (hit)
      consider(t, n, entity);
  }
  return best;
}
} // namespace njin
