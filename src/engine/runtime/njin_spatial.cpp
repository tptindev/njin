#include "njin_spatial.h"
#include <algorithm>
#include <cmath>
#include <utility>

// Both structures keep the items in one array, reordered so that a grid cell
// or a quadtree leaf is a contiguous run: a query reads memory in order and
// never follows a pointer per item. The k-nearest search is the same for both:
// visit cells or nodes nearest first, keep the best k in a sorted array, and
// stop at the first region that cannot beat the k-th.

namespace njin {

namespace {
struct item_rec {
  vec2 pos;
  f32 radius;
  u32 group;
  u32 id; // index in the array given to spatial_build
  bool fixed;
};

struct quad_node {
  rect box;
  u32 first, count; // items of this subtree: items[first .. first + count)
  i32 child;        // first of 4 consecutive children, -1 for a leaf
};

// Squared distance from p to the nearest point of r (0 inside).
f32 dist_sq_to(rect r, vec2 p) {
  const f32 dx = std::max({r.pos.x - p.x, 0.0f, p.x - (r.pos.x + r.size.x)});
  const f32 dy = std::max({r.pos.y - p.y, 0.0f, p.y - (r.pos.y + r.size.y)});
  return dx * dx + dy * dy;
}
} // namespace

struct spatial_impl {
  spatial_kind kind = spatial_grid;
  std::vector<item_rec> items; // by cell or by leaf
  f32 max_radius = 0.0f;
  // grid
  vec2 origin{};
  f32 cell = 1.0f;
  i32 cw = 0, ch = 0;
  std::vector<u32> cell_start; // cell c holds items[cell_start[c] .. cell_start[c + 1])
  // quadtree
  std::vector<quad_node> nodes;
  // scratch
  std::vector<item_rec> loose;
  std::vector<u32> cell_of, fill;
};

spatial_index::spatial_index() = default;
spatial_index::~spatial_index() = default;
spatial_index::spatial_index(spatial_index &&) noexcept = default;
spatial_index &spatial_index::operator=(spatial_index &&) noexcept = default;

namespace {
void build_grid(spatial_impl &s, rect bounds, f32 cell_size, u32 count) {
  f32 cell = cell_size > 0.0f ? cell_size : std::max(2.0f * s.max_radius, 1.0f);
  // No more than 4 cells per item (and at least 1024), so a tiny radius on a
  // big map does not allocate a huge grid.
  const f64 max_cells = std::max(4.0 * count, 1024.0);
  const f64 area = (f64)std::max(bounds.size.x, 1.0f) * (f64)std::max(bounds.size.y, 1.0f);
  if (area / ((f64)cell * cell) > max_cells)
    cell = (f32)std::sqrt(area / max_cells);
  s.origin = bounds.pos;
  s.cell = cell;
  s.cw = std::max(1, (i32)(bounds.size.x / cell) + 1);
  s.ch = std::max(1, (i32)(bounds.size.y / cell) + 1);

  // Counting sort by cell.
  s.cell_start.assign((std::size_t)s.cw * (std::size_t)s.ch + 1, 0);
  s.cell_of.resize(count);
  for (u32 i = 0; i < count; i++) {
    const vec2 p = s.loose[i].pos;
    const i32 cx = std::clamp((i32)std::floor((p.x - s.origin.x) / cell), 0, s.cw - 1);
    const i32 cy = std::clamp((i32)std::floor((p.y - s.origin.y) / cell), 0, s.ch - 1);
    s.cell_of[i] = (u32)(cy * s.cw + cx);
    s.cell_start[s.cell_of[i] + 1]++;
  }
  for (std::size_t c = 1; c < s.cell_start.size(); c++)
    s.cell_start[c] += s.cell_start[c - 1];
  s.fill.assign(s.cell_start.begin(), s.cell_start.end() - 1);
  s.items.resize(count);
  for (u32 i = 0; i < count; i++)
    s.items[s.fill[s.cell_of[i]]++] = s.loose[i];
}

void build_quadtree(spatial_impl &s, rect bounds, u32 leaf_size, u32 max_depth) {
  s.items = s.loose;
  s.nodes.clear();
  s.nodes.push_back({bounds, 0, (u32)s.items.size(), -1});
  leaf_size = std::max(leaf_size, 1u);
  struct todo {
    u32 node, depth;
  };
  std::vector<todo> stack{{0, 0}};
  while (!stack.empty()) {
    const todo t = stack.back();
    stack.pop_back();
    const quad_node n = s.nodes[t.node];
    if (n.count <= leaf_size || t.depth >= max_depth)
      continue;
    // Split at the centre: left | right, then top | bottom in each half.
    const vec2 c = rect_center(n.box);
    item_rec *first = s.items.data() + n.first, *last = first + n.count;
    item_rec *mid_x = std::partition(first, last, [&](const item_rec &it) { return it.pos.x < c.x; });
    item_rec *mid_l = std::partition(first, mid_x, [&](const item_rec &it) { return it.pos.y < c.y; });
    item_rec *mid_r = std::partition(mid_x, last, [&](const item_rec &it) { return it.pos.y < c.y; });
    const vec2 h = n.box.size * 0.5f;
    const rect boxes[4] = {{n.box.pos, h}, {{n.box.pos.x, c.y}, h}, {{c.x, n.box.pos.y}, h}, {c, h}};
    item_rec *const bounds_of[5] = {first, mid_l, mid_x, mid_r, last};
    const i32 child = (i32)s.nodes.size();
    s.nodes[t.node].child = child;
    for (u32 q = 0; q < 4; q++) {
      const u32 f = (u32)(bounds_of[q] - s.items.data()), cnt = (u32)(bounds_of[q + 1] - bounds_of[q]);
      s.nodes.push_back({boxes[q], f, cnt, -1});
      stack.push_back({(u32)child + q, t.depth + 1});
    }
  }
}

// The k best so far, nearest first. Holds positions in the stored array;
// spatial_nearest() turns them into item names at the end.
struct best_k {
  spatial_hit *out;
  u32 k, n = 0;
  f32 worst() const { return n == k ? out[k - 1].distance_sq : INFINITY; }
  void add(u32 id, f32 d2) {
    u32 at = n < k ? n++ : k - 1;
    for (; at > 0 && out[at - 1].distance_sq > d2; at--)
      out[at] = out[at - 1];
    out[at] = {id, d2};
  }
};

// One run of items, tested against the query.
inline void scan(const spatial_impl &s, const spatial_query &q, u32 from, u32 to, best_k &best) {
  for (u32 i = from; i < to; i++) {
    const item_rec &it = s.items[i];
    if (it.id == q.skip || (q.group != 0 && it.group == q.group))
      continue;
    const f32 d2 = length_sq(it.pos - q.at);
    const f32 limit = q.touching ? q.radius + it.radius : q.radius;
    if (d2 >= limit * limit || d2 >= best.worst())
      continue;
    if (q.filter && !q.filter(it.id, q.user))
      continue;
    best.add(i, d2);
  }
}

// Rings of cells round the query's cell, nearest first; a ring is only read
// if its cells can still hold something within reach and nearer than the k-th.
void nearest_grid(const spatial_impl &s, const spatial_query &q, f32 reach, best_k &best) {
  const i32 qx = std::clamp((i32)std::floor((q.at.x - s.origin.x) / s.cell), 0, s.cw - 1);
  const i32 qy = std::clamp((i32)std::floor((q.at.y - s.origin.y) / s.cell), 0, s.ch - 1);
  const f32 reach_sq = reach * reach;
  const i32 rings = std::max({qx, qy, s.cw - 1 - qx, s.ch - 1 - qy});
  for (i32 r = 0; r <= rings; r++) {
    if (r > 0) {
      // Everything outside the square of rings 0 .. r-1 is at least this far.
      const rect inner{s.origin + vec2{(f32)(qx - r + 1), (f32)(qy - r + 1)} * s.cell,
                       vec2{(f32)(2 * r - 1), (f32)(2 * r - 1)} * s.cell};
      const vec2 p = q.at;
      const f32 gap = std::min({p.x - inner.pos.x, inner.pos.x + inner.size.x - p.x, p.y - inner.pos.y,
                                inner.pos.y + inner.size.y - p.y});
      if (gap > 0.0f && (gap * gap >= reach_sq || gap * gap >= best.worst()))
        return;
    }
    const auto visit = [&](i32 cx, i32 cy) {
      if (cx < 0 || cy < 0 || cx >= s.cw || cy >= s.ch)
        return;
      const std::size_t c = (std::size_t)(cy * s.cw + cx);
      if (s.cell_start[c] == s.cell_start[c + 1])
        return;
      // Edge cells stretch to infinity: items outside the bounds live there.
      f32 x0 = s.origin.x + (f32)cx * s.cell, x1 = x0 + s.cell;
      f32 y0 = s.origin.y + (f32)cy * s.cell, y1 = y0 + s.cell;
      if (cx == 0) x0 = -INFINITY;
      if (cy == 0) y0 = -INFINITY;
      if (cx == s.cw - 1) x1 = INFINITY;
      if (cy == s.ch - 1) y1 = INFINITY;
      const f32 dx = std::max({x0 - q.at.x, 0.0f, q.at.x - x1}), dy = std::max({y0 - q.at.y, 0.0f, q.at.y - y1});
      const f32 d2 = dx * dx + dy * dy;
      if (d2 < reach_sq && d2 < best.worst())
        scan(s, q, s.cell_start[c], s.cell_start[c + 1], best);
    };
    if (r == 0) {
      visit(qx, qy);
      continue;
    }
    for (i32 cx = qx - r; cx <= qx + r; cx++) { // top and bottom rows
      visit(cx, qy - r);
      visit(cx, qy + r);
    }
    for (i32 cy = qy - r + 1; cy <= qy + r - 1; cy++) { // left and right columns
      visit(qx - r, cy);
      visit(qx + r, cy);
    }
  }
}

void nearest_quadtree(const spatial_impl &s, const spatial_query &q, f32 reach, best_k &best) {
  const f32 reach_sq = reach * reach;
  // Depth-first, nearer children first. A child is only pushed if it can still
  // hold something within reach, and checked again when popped because the
  // k-th best may have moved closer meanwhile. At most 3 siblings wait per level.
  struct entry {
    f32 d2;
    u32 node;
  };
  entry stack[4 * 64];
  u32 top = 0;
  stack[top++] = {dist_sq_to(s.nodes[0].box, q.at), 0};
  while (top > 0) {
    const entry e = stack[--top];
    if (e.d2 >= reach_sq || e.d2 >= best.worst())
      continue;
    const quad_node &n = s.nodes[e.node];
    if (n.child < 0) {
      scan(s, q, n.first, n.first + n.count, best);
      continue;
    }
    entry kids[4];
    u32 m = 0;
    for (u32 c = 0; c < 4; c++) {
      const quad_node &k = s.nodes[(u32)n.child + c];
      if (k.count == 0)
        continue;
      const f32 d2 = dist_sq_to(k.box, q.at);
      if (d2 < reach_sq && d2 < best.worst())
        kids[m++] = {d2, (u32)n.child + c};
    }
    // Insertion sort, farthest first, so the nearest is popped first.
    for (u32 i = 1; i < m; i++)
      for (u32 j = i; j > 0 && kids[j - 1].d2 < kids[j].d2; j--)
        std::swap(kids[j - 1], kids[j]);
    for (u32 c = 0; c < m && top < sizeof stack / sizeof stack[0]; c++)
      stack[top++] = kids[c];
  }
}
} // namespace

void spatial_build(spatial_index &index, const spatial_desc &desc, const spatial_item *items, u32 count) {
  if (!index.impl)
    index.impl = std::make_unique<spatial_impl>();
  spatial_impl &s = *index.impl;
  s.kind = desc.kind;
  s.max_radius = 0.0f;
  s.loose.resize(count);
  vec2 lo{INFINITY, INFINITY}, hi{-INFINITY, -INFINITY};
  for (u32 i = 0; i < count; i++) {
    const spatial_item &it = items[i];
    s.loose[i] = {it.pos, std::max(it.radius, 0.0f), it.group, i, it.fixed};
    s.max_radius = std::max(s.max_radius, it.radius);
    lo = {std::min(lo.x, it.pos.x), std::min(lo.y, it.pos.y)};
    hi = {std::max(hi.x, it.pos.x), std::max(hi.y, it.pos.y)};
  }
  if (count == 0)
    lo = hi = {};
  rect bounds = desc.bounds;
  if (bounds.size.x <= 0.0f || bounds.size.y <= 0.0f)
    bounds = rect{lo, hi - lo};
  if (desc.kind == spatial_quadtree) {
    // The tree must hold every item, even those outside desc.bounds.
    const vec2 a{std::min(bounds.pos.x, lo.x), std::min(bounds.pos.y, lo.y)};
    const vec2 b{std::max(bounds.pos.x + bounds.size.x, hi.x), std::max(bounds.pos.y + bounds.size.y, hi.y)};
    build_quadtree(s, rect{a, b - a}, desc.leaf_size, std::min(desc.max_depth, 60u));
  } else {
    build_grid(s, bounds, desc.cell_size, count);
  }
}

u32 spatial_size(const spatial_index &index) { return index.impl ? (u32)index.impl->items.size() : 0; }

u32 spatial_nearest(const spatial_index &index, const spatial_query &q, spatial_hit *out, u32 k) {
  if (!index.impl || k == 0 || index.impl->items.empty() || !(q.radius >= 0.0f))
    return 0;
  const spatial_impl &s = *index.impl;
  const f32 reach = q.touching ? q.radius + s.max_radius : q.radius;
  best_k best{out, k};
  if (s.kind == spatial_quadtree)
    nearest_quadtree(s, q, reach, best);
  else
    nearest_grid(s, q, reach, best);
  for (u32 j = 0; j < best.n; j++)
    out[j].item = s.items[out[j].item].id;
  return best.n;
}

u32 spatial_separate(const spatial_index &index, std::vector<vec2> &push, u32 k) {
  const u32 count = spatial_size(index);
  push.assign(count, vec2{});
  if (count == 0 || k == 0)
    return 0;
  const spatial_impl &s = *index.impl;
  constexpr u32 max_k = 32;
  k = std::min(k, max_k);
  spatial_hit hits[max_k];
  u32 contacts = 0;
  // In stored order, so neighbouring queries read neighbouring memory.
  for (const item_rec &a : s.items) {
    if (a.fixed || a.radius <= 0.0f)
      continue;
    const spatial_query q{.at = a.pos, .radius = a.radius, .touching = true, .skip = a.id, .group = a.group};
    best_k best{hits, k};
    if (s.kind == spatial_quadtree)
      nearest_quadtree(s, q, a.radius + s.max_radius, best);
    else
      nearest_grid(s, q, a.radius + s.max_radius, best);
    if (best.n == 0)
      continue;
    contacts += best.n;
    vec2 p{};
    for (u32 j = 0; j < best.n; j++) {
      const item_rec &b = s.items[hits[j].item];
      const f32 d = std::sqrt(hits[j].distance_sq);
      // Two on the same spot part in a direction of their own.
      const vec2 away = d > 1e-4f ? (a.pos - b.pos) / d : from_angle((f32)((a.id * 137u) % 360u));
      p += away * ((a.radius + b.radius - d) * (b.fixed ? 1.0f : 0.5f));
    }
    // Several pushes can add up past what any one overlap needs.
    if (const f32 len = length(p); len > a.radius)
      p *= a.radius / len;
    push[a.id] = p;
  }
  return contacts;
}
} // namespace njin
