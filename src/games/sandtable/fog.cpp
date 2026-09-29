#include "fog.h"
#include "weather.h"

#include <algorithm>
#include <cmath>

namespace sandtable {

namespace {

constexpr f32 cell = 16.0f; // world units per fog cell
constexpr i32 cols = static_cast<i32>(world_width / cell);
constexpr i32 rows = static_cast<i32>(world_height / cell);

std::vector<u8> seen(static_cast<usize>(cols * rows), 0);
// Cells already used as the centre of a sight circle of some radius this frame,
// so a crowd of men in one cell is stamped once.
std::vector<u16> stamped(static_cast<usize>(cols * rows), 0);

// How far an arm sees, world units, by day.
f32 sight(arm a) {
  switch (a) {
  case arm::cavalry:
    return 260.0f;
  case arm::boat:
    return 220.0f;
  case arm::artillery:
    return 160.0f;
  default:
    return 180.0f;
  }
}

void stamp(vec2 at, f32 radius) {
  const i32 cx = static_cast<i32>(std::floor(at.x / cell));
  const i32 cy = static_cast<i32>(std::floor(at.y / cell));
  if (cx < 0 || cy < 0 || cx >= cols || cy >= rows)
    return;
  const u16 r = static_cast<u16>(radius / cell);
  u16 &done = stamped[static_cast<usize>(cy * cols + cx)];
  if (done >= r)
    return;
  done = r;
  const i32 ri = static_cast<i32>(r);
  for (i32 dy = -ri; dy <= ri; ++dy)
    for (i32 dx = -ri; dx <= ri; ++dx) {
      if (dx * dx + dy * dy > ri * ri)
        continue;
      const i32 x = cx + dx, y = cy + dy;
      if (x >= 0 && y >= 0 && x < cols && y < rows)
        seen[static_cast<usize>(y * cols + x)] = 1;
    }
}

bool seen_at(i32 x, i32 y) {
  if (x < 0 || y < 0 || x >= cols || y >= rows)
    return true; // the frame round the table is never fogged
  return seen[static_cast<usize>(y * cols + x)] != 0;
}

} // namespace

void fog_update(context &) {
  std::fill(seen.begin(), seen.end(), 0);
  std::fill(stamped.begin(), stamped.end(), 0);
  // Less is seen at night.
  const f32 night = 1.0f - 0.3f * darkness();
  if (state.screen == phase::deploy) {
    // The camp: the whole deployment zone, and round each chip set down.
    for (i32 y = 0; y < rows; ++y)
      for (i32 x = 0; x < cols; ++x)
        if (point_in_rect({(static_cast<f32>(x) + 0.5f) * cell, (static_cast<f32>(y) + 0.5f) * cell}, player_zone))
          seen[static_cast<usize>(y * cols + x)] = 1;
    for (const board_chip &c : state.board)
      stamp(c.pos, sight(c.type) * night);
    return;
  }
  for (const soldier &s : state.soldiers)
    if (s.alive && s.owner == side::player)
      stamp(s.pos, sight(s.type) * night);
}

bool fog_visible(vec2 pos) {
  return seen_at(static_cast<i32>(std::floor(pos.x / cell)), static_cast<i32>(std::floor(pos.y / cell)));
}

void fog_draw(context &ctx) {
  const vec2 lo = scr2w(ctx, {0.0f, 0.0f});
  const vec2 hi = scr2w(ctx, screen_size(ctx));
  const i32 x0 = std::max(0, static_cast<i32>(std::floor(lo.x / cell)));
  const i32 y0 = std::max(0, static_cast<i32>(std::floor(lo.y / cell)));
  const i32 x1 = std::min(cols - 1, static_cast<i32>(std::floor(hi.x / cell)));
  const i32 y1 = std::min(rows - 1, static_cast<i32>(std::floor(hi.y / cell)));
  const rgba fog = rgb(20, 24, 22);
  const rgba edge = rgb(20, 24, 22, 150);
  for (i32 y = y0; y <= y1; ++y) {
    // Unseen cells in runs, one rectangle each.
    i32 x = x0;
    while (x <= x1) {
      if (seen_at(x, y)) {
        // A seen cell next to the fog is half in it: a soft pixel edge.
        if (!seen_at(x - 1, y) || !seen_at(x + 1, y) || !seen_at(x, y - 1) || !seen_at(x, y + 1))
          draw_rect(ctx, {{static_cast<f32>(x) * cell, static_cast<f32>(y) * cell}, {cell, cell}}, edge);
        ++x;
        continue;
      }
      const i32 start = x;
      while (x <= x1 && !seen_at(x, y))
        ++x;
      draw_rect(ctx, {{static_cast<f32>(start) * cell, static_cast<f32>(y) * cell},
                      {static_cast<f32>(x - start) * cell, cell}},
                fog);
    }
  }
}

} // namespace sandtable
