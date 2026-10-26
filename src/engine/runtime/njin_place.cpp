#include "njin_place.h"
#include <algorithm>
#include <cmath>

namespace njin {
namespace {
bool inside(const place_grid &g, cell c) { return c.x >= 0 && c.y >= 0 && c.x < g.width && c.y < g.height; }
usize index_of(const place_grid &g, cell c) { return (usize)c.y * (usize)g.width + (usize)c.x; }
} // namespace

place_grid place_grid_make(vec2 origin, vec2 cell_size, i32 width, i32 height) {
  place_grid g;
  g.origin = origin;
  g.cell_size = cell_size;
  g.width = std::max(width, 0);
  g.height = std::max(height, 0);
  g.owner.assign((usize)g.width * (usize)g.height, place_none);
  return g;
}

cell place_cell_at(const place_grid &g, vec2 pos) {
  if (g.cell_size.x <= 0.0f || g.cell_size.y <= 0.0f)
    return {};
  return {(i32)std::floor((pos.x - g.origin.x) / g.cell_size.x),
          (i32)std::floor((pos.y - g.origin.y) / g.cell_size.y)};
}

cell place_snap(const place_grid &g, vec2 pos, i32 w, i32 h) {
  // The corner sits half the thing back from `pos`, rounded to the nearest cell.
  if (g.cell_size.x <= 0.0f || g.cell_size.y <= 0.0f)
    return {};
  const f32 x = (pos.x - g.origin.x) / g.cell_size.x - (f32)std::max(w, 1) * 0.5f;
  const f32 y = (pos.y - g.origin.y) / g.cell_size.y - (f32)std::max(h, 1) * 0.5f;
  return {(i32)std::lround(x), (i32)std::lround(y)};
}

rect place_rect(const place_grid &g, cell at, i32 w, i32 h) {
  return {{g.origin.x + (f32)at.x * g.cell_size.x, g.origin.y + (f32)at.y * g.cell_size.y},
          {(f32)std::max(w, 0) * g.cell_size.x, (f32)std::max(h, 0) * g.cell_size.y}};
}

u32 place_owner(const place_grid &g, cell c) {
  return inside(g, c) && g.owner.size() == (usize)g.width * (usize)g.height ? g.owner[index_of(g, c)] : place_none;
}

bool place_fits(const place_grid &g, cell at, i32 w, i32 h, u32 ignore) {
  if (w < 1 || h < 1 || !inside(g, at) || !inside(g, {at.x + w - 1, at.y + h - 1}))
    return false;
  if (g.owner.size() != (usize)g.width * (usize)g.height)
    return false;
  for (i32 y = at.y; y < at.y + h; y++) {
    for (i32 x = at.x; x < at.x + w; x++) {
      const u32 o = g.owner[index_of(g, {x, y})];
      if (o != place_none && o != ignore)
        return false;
    }
  }
  return true;
}

bool place_put(place_grid &g, cell at, i32 w, i32 h, u32 id) {
  if (id == place_none || !place_fits(g, at, w, h, id))
    return false;
  place_remove(g, id);
  for (i32 y = at.y; y < at.y + h; y++)
    for (i32 x = at.x; x < at.x + w; x++)
      g.owner[index_of(g, {x, y})] = id;
  return true;
}

i32 place_remove(place_grid &g, u32 id) {
  if (id == place_none)
    return 0;
  i32 n = 0;
  for (u32 &o : g.owner) {
    if (o == id) {
      o = place_none;
      n++;
    }
  }
  return n;
}

bool place_find_free(const place_grid &g, i32 w, i32 h, cell &out) {
  for (i32 y = 0; y + h <= g.height; y++) {
    for (i32 x = 0; x + w <= g.width; x++) {
      if (place_fits(g, {x, y}, w, h)) {
        out = {x, y};
        return true;
      }
    }
  }
  return false;
}
} // namespace njin
