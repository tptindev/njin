#include "collision.h"
#include "_collide.h"
#include "_comps.h"
#include "_tilemap.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_draw.h"
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

// Calls fnc(x, y) for every non-empty tile overlapping `area`.
template <class Fnc>
void each_tile_in(const tilemap &map, vec2 origin, rect area, Fnc &&fnc) {
  if (area.size.x < 0.0f || area.size.y < 0.0f)
    return;
  const cell a = tilemap_cell_at(map, origin, area.pos);
  const cell b = tilemap_cell_at(map, origin, area.pos + area.size - vec2{1e-4f, 1e-4f});
  for (i32 y = a.y; y <= b.y; y++) {
    for (i32 x = a.x; x <= b.x; x++) {
      if (tilemap_get(map, x, y) >= 0)
        fnc(x, y);
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
  ecs_register(ctx, phase_post_update, detect);
  ecs_register(ctx, phase_render, draw_debug);
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

collision_move_result collision_move(njin_ctx &ctx, entt::entity entity, vec2 delta) {
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
    tr->pos += delta;
    result.moved = delta;
    return result;
  }

  // Everything that could block this move: the union of where the box is and
  // where it wants to go.
  rect swept = start;
  swept.pos.x = std::min(start.pos.x, start.pos.x + delta.x);
  swept.pos.y = std::min(start.pos.y, start.pos.y + delta.y);
  swept.size.x += std::abs(delta.x);
  swept.size.y += std::abs(delta.y);

  struct obstacle {
    rect box;
    entt::entity entity;
  };
  std::vector<obstacle> blockers;
  for (auto [other, otr, col] : registry.view<const transform, const collider>().each()) {
    if (other == entity || !col.enabled || col.trigger || col.shape == collider_tiles)
      continue;
    if (!layers_allow(self->layer, self->mask, col.layer, col.mask))
      continue;
    if (const child_of *link = registry.try_get<child_of>(other); link != nullptr && link->parent == entity)
      continue;
    const rect b = collider_bounds(otr, col);
    if (rects_overlap(b, swept))
      blockers.push_back({b, other});
  }
  each_tiles(registry, [&](entt::entity tiles, const collider &col, const tilemap &map, vec2 origin) {
    if (!layers_allow(self->layer, self->mask, col.layer, col.mask))
      return;
    each_tile_in(map, origin, swept, [&](i32 x, i32 y) {
      blockers.push_back({tilemap_cell_rect(map, origin, x, y), tiles});
    });
  });

  // Sweeps `r` by `step` along one axis and stops it at the first blocker in
  // the way, so a long step cannot tunnel through a thin wall. Blockers `r`
  // already overlaps are ignored, so an entity that starts stuck can move out.
  const auto sweep = [&](rect &r, f32 step, bool horizontal, entt::entity &hit) {
    const i32 a = horizontal ? 0 : 1; // moving axis
    const i32 o = 1 - a;              // other axis
    const auto lo = [](const rect &x, i32 axis) { return axis == 0 ? x.pos.x : x.pos.y; };
    const auto len = [](const rect &x, i32 axis) { return axis == 0 ? x.size.x : x.size.y; };
    f32 allowed = step;
    for (const obstacle &ob : blockers) {
      if (rects_overlap(r, ob.box))
        continue;
      // Must share the other axis (strictly: touching sides slide past).
      if (!(lo(r, o) < lo(ob.box, o) + len(ob.box, o) && lo(ob.box, o) < lo(r, o) + len(r, o)))
        continue;
      f32 gap = 0.0f;
      if (step > 0.0f) {
        gap = lo(ob.box, a) - (lo(r, a) + len(r, a));
        if (gap < 0.0f || gap >= allowed)
          continue;
      } else {
        gap = (lo(ob.box, a) + len(ob.box, a)) - lo(r, a);
        if (gap > 0.0f || gap <= allowed)
          continue;
      }
      allowed = gap;
      hit = ob.entity;
    }
    if (horizontal)
      r.pos.x += allowed;
    else
      r.pos.y += allowed;
    return allowed != step;
  };

  rect r = start;
  if (delta.x != 0.0f)
    result.hit_x = sweep(r, delta.x, true, result.other_x);
  if (delta.y != 0.0f)
    result.hit_y = sweep(r, delta.y, false, result.other_y);
  result.moved = r.pos - start.pos;
  tr->pos += result.moved;
  return result;
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
      each_tile_in(*map, tr.pos, area, [&](i32 x, i32 y) {
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
      for (i32 i = 0; i < max_steps && t_enter <= 1.0f && t_enter < best_t; i++) {
        if (tilemap_get(*map, c.x, c.y) >= 0) {
          consider(t_enter, enter_normal, entity);
          break;
        }
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
