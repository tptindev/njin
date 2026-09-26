#include "njin_nav.h"
#include "njin_collision.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include <algorithm>
#include <cmath>
#include <queue>

namespace njin {
namespace {
bool inside(const nav_grid &g, cell c) { return c.x >= 0 && c.y >= 0 && c.x < g.width && c.y < g.height; }
i32 index_of(const nav_grid &g, cell c) { return c.y * g.width + c.x; }

// Every cell whose area overlaps `area` (touching edges excluded).
template <class Fnc> void each_cell(const nav_grid &g, rect area, Fnc &&fnc) {
  if (g.cell_size.x <= 0.0f || g.cell_size.y <= 0.0f)
    return;
  const cell a = nav_cell_at(g, area.pos);
  const cell b = nav_cell_at(g, area.pos + area.size - vec2{1e-3f, 1e-3f});
  for (i32 y = std::max(a.y, 0); y <= std::min(b.y, g.height - 1); y++)
    for (i32 x = std::max(a.x, 0); x <= std::min(b.x, g.width - 1); x++)
      fnc(cell{x, y});
}

// Octile distance in cells, the exact cost of an empty grid with diagonals.
f32 heuristic(cell a, cell b, bool diagonal) {
  const f32 dx = (f32)std::abs(a.x - b.x);
  const f32 dy = (f32)std::abs(a.y - b.y);
  if (!diagonal)
    return dx + dy;
  return std::max(dx, dy) + (1.41421356f - 1.0f) * std::min(dx, dy);
}
} // namespace

nav_grid nav_grid_make(vec2 origin, vec2 cell_size, i32 width, i32 height, u8 cost) {
  nav_grid g;
  g.origin = origin;
  g.cell_size = cell_size;
  g.width = std::max(width, 0);
  g.height = std::max(height, 0);
  g.cost.assign((usize)g.width * (usize)g.height, cost);
  return g;
}

nav_grid nav_grid_from_world(const njin_ctx &ctx, rect area, vec2 cell_size, u32 mask) {
  const i32 w = cell_size.x > 0.0f ? (i32)std::ceil(area.size.x / cell_size.x) : 0;
  const i32 h = cell_size.y > 0.0f ? (i32)std::ceil(area.size.y / cell_size.y) : 0;
  nav_grid g = nav_grid_make(area.pos, cell_size, w, h, 1);
  const entt::registry &reg = ctx.ecs.registry;
  for (auto [e, tr, col] : reg.view<const transform, const collider>().each()) {
    if (!col.enabled || col.trigger || (col.layer & mask) == 0)
      continue;
    if (col.shape != collider_tiles) {
      each_cell(g, collider_bounds(tr, col), [&](cell c) { g.cost[(usize)index_of(g, c)] = 0; });
      continue;
    }
    const tilemap *map = reg.try_get<tilemap>(e);
    if (map == nullptr)
      continue;
    // Every colliding tile blocks the nav cells under it.
    for (const auto &[key, chunk] : map->chunks) {
      const cell base = tile_chunk_coord(key);
      for (i32 i = 0; i < tile_chunk_size * tile_chunk_size; i++) {
        const i32 value = chunk.tiles[(usize)i];
        if (value < 0 || tilemap_shape(*map, value) == tile_none)
          continue;
        const i32 tx = base.x * tile_chunk_size + i % tile_chunk_size;
        const i32 ty = base.y * tile_chunk_size + i / tile_chunk_size;
        each_cell(g, tilemap_cell_rect(*map, tr.pos, tx, ty),
                  [&](cell c) { g.cost[(usize)index_of(g, c)] = 0; });
      }
    }
  }
  return g;
}

cell nav_cell_at(const nav_grid &g, vec2 pos) {
  if (g.cell_size.x <= 0.0f || g.cell_size.y <= 0.0f)
    return {};
  return {(i32)std::floor((pos.x - g.origin.x) / g.cell_size.x),
          (i32)std::floor((pos.y - g.origin.y) / g.cell_size.y)};
}

vec2 nav_cell_center(const nav_grid &g, cell c) {
  return {g.origin.x + ((f32)c.x + 0.5f) * g.cell_size.x, g.origin.y + ((f32)c.y + 0.5f) * g.cell_size.y};
}

u8 nav_cost(const nav_grid &g, cell c) {
  return inside(g, c) ? g.cost[(usize)index_of(g, c)] : (u8)0;
}

void nav_set_cost(nav_grid &g, cell c, u8 cost) {
  if (inside(g, c))
    g.cost[(usize)index_of(g, c)] = cost;
}

void nav_set_area(nav_grid &g, rect area, u8 cost) {
  each_cell(g, area, [&](cell c) { g.cost[(usize)index_of(g, c)] = cost; });
}

bool nav_line_clear(const nav_grid &g, vec2 a, vec2 b) {
  // Walk every cell the segment crosses (Amanatides–Woo), in order.
  cell c = nav_cell_at(g, a);
  const cell last = nav_cell_at(g, b);
  const vec2 d = b - a;
  const i32 sx = d.x > 0.0f ? 1 : -1;
  const i32 sy = d.y > 0.0f ? 1 : -1;
  const f32 inf = 1e30f;
  const vec2 corner = g.origin + vec2{(f32)(c.x + (sx > 0 ? 1 : 0)) * g.cell_size.x,
                                      (f32)(c.y + (sy > 0 ? 1 : 0)) * g.cell_size.y};
  f32 tx = d.x != 0.0f ? (corner.x - a.x) / d.x : inf;
  f32 ty = d.y != 0.0f ? (corner.y - a.y) / d.y : inf;
  const f32 dtx = d.x != 0.0f ? g.cell_size.x / std::abs(d.x) : inf;
  const f32 dty = d.y != 0.0f ? g.cell_size.y / std::abs(d.y) : inf;
  const i32 steps = std::abs(last.x - c.x) + std::abs(last.y - c.y) + 1;
  for (i32 i = 0; i < steps; i++) {
    if (nav_cost(g, c) == 0)
      return false;
    if (c.x == last.x && c.y == last.y)
      return true;
    if (tx < ty) {
      tx += dtx;
      c.x += sx;
    } else {
      ty += dty;
      c.y += sy;
    }
  }
  return true;
}

bool nav_find_path(const nav_grid &g, vec2 from, vec2 to, std::vector<vec2> &out,
                   const nav_path_opts &opts) {
  out.clear();
  const cell start = nav_cell_at(g, from);
  const cell goal = nav_cell_at(g, to);
  if (!inside(g, start) || g.width <= 0 || g.height <= 0)
    return false;

  const usize n = (usize)g.width * (usize)g.height;
  std::vector<f32> cost_so_far(n, -1.0f);
  std::vector<i32> came_from(n, -1);
  using node = std::pair<f32, i32>; // f, index
  std::priority_queue<node, std::vector<node>, std::greater<node>> open;
  const i32 s = index_of(g, start);
  cost_so_far[(usize)s] = 0.0f;
  open.push({heuristic(start, goal, opts.diagonal), s});

  const bool goal_ok = inside(g, goal) && nav_cost(g, goal) != 0;
  i32 found = -1;
  i32 closest = s;
  f32 closest_h = heuristic(start, goal, opts.diagonal);
  i32 expanded = 0;
  static constexpr i32 dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  while (!open.empty() && expanded < opts.max_nodes) {
    const auto [f, cur] = open.top();
    open.pop();
    const cell c{cur % g.width, cur / g.width};
    const f32 here = cost_so_far[(usize)cur];
    if (f > here + heuristic(c, goal, opts.diagonal) + 1e-3f)
      continue; // a stale entry: this cell was reached cheaper since
    expanded++;
    if (goal_ok && cur == index_of(g, goal)) {
      found = cur;
      break;
    }
    const f32 h = heuristic(c, goal, opts.diagonal);
    if (h < closest_h) {
      closest_h = h;
      closest = cur;
    }
    for (i32 k = 0; k < (opts.diagonal ? 8 : 4); k++) {
      const cell nb{c.x + dirs[k][0], c.y + dirs[k][1]};
      const u8 enter = nav_cost(g, nb);
      if (enter == 0)
        continue;
      const bool diag = k >= 4;
      if (diag) {
        const bool side_a = nav_cost(g, {c.x + dirs[k][0], c.y}) != 0;
        const bool side_b = nav_cost(g, {c.x, c.y + dirs[k][1]}) != 0;
        if (opts.cut_corners ? (!side_a && !side_b) : (!side_a || !side_b))
          continue;
      }
      const f32 step = (diag ? 1.41421356f : 1.0f) * (f32)enter;
      const i32 ni = index_of(g, nb);
      const f32 next = here + step;
      if (cost_so_far[(usize)ni] >= 0.0f && cost_so_far[(usize)ni] <= next)
        continue;
      cost_so_far[(usize)ni] = next;
      came_from[(usize)ni] = cur;
      open.push({next + heuristic(nb, goal, opts.diagonal), ni});
    }
  }

  const i32 end = found >= 0 ? found : (opts.partial ? closest : -1);
  if (end < 0 || end == s) {
    if (found >= 0)
      out.push_back(to);
    return found >= 0;
  }
  std::vector<cell> cells;
  for (i32 i = end; i != s && i >= 0; i = came_from[(usize)i])
    cells.push_back({i % g.width, i / g.width});
  std::reverse(cells.begin(), cells.end());
  for (const cell &c : cells)
    out.push_back(nav_cell_center(g, c));
  if (found >= 0)
    out.back() = to;

  if (opts.smooth && out.size() > 1) {
    // String pulling: from each kept point, jump to the farthest point still
    // in plain sight.
    std::vector<vec2> smooth;
    vec2 at = from;
    usize i = 0;
    while (i < out.size()) {
      usize far = i;
      for (usize j = out.size(); j-- > i + 1;) {
        if (nav_line_clear(g, at, out[j])) {
          far = j;
          break;
        }
      }
      smooth.push_back(out[far]);
      at = out[far];
      i = far + 1;
    }
    out = std::move(smooth);
  }
  return found >= 0;
}

vec2 nav_steer(nav_agent &agent, vec2 pos) {
  while (!agent.done() && distance(pos, agent.path[(usize)agent.next]) <= agent.reach)
    agent.next++;
  if (agent.done())
    return {};
  return normalize(agent.path[(usize)agent.next] - pos);
}
} // namespace njin
