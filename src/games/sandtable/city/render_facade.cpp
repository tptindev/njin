#include "render_facade.h"

#include <algorithm>
#include <cmath>

namespace sandtable::city {

namespace {

const rgba facades[] = {rgb8(236, 222, 190), rgb8(234, 200, 110), rgb8(214, 168, 92), rgb8(160, 196, 214),
                        rgb8(170, 210, 180), rgb8(228, 170, 170), rgb8(236, 236, 230), rgb8(240, 190, 150),
                        rgb8(196, 180, 214), rgb8(190, 186, 180)};
const rgba concretes[] = {rgb8(176, 172, 166), rgb8(160, 158, 154), rgb8(190, 186, 178), rgb8(150, 146, 140)};
const rgba metals[] = {rgb8(150, 154, 156), rgb8(86, 120, 104), rgb8(84, 104, 140), rgb8(170, 164, 150)};
const rgba cloths[] = {rgb8(220, 60, 50), rgb8(60, 110, 200), rgb8(240, 220, 90), rgb8(240, 240, 236),
                       rgb8(80, 170, 110), rgb8(230, 130, 170)};
constexpr rgba col_leaf = rgb8(78, 136, 62);
constexpr rgba col_pot = rgb8(170, 88, 56);

} // namespace

frame frame_of(const building &b) {
  return {b.box.center, b.box.axis_x(), b.box.axis_y(), b.box.half.x, b.box.half.y, b.box.angle};
}

rgba front_color(const building &b) { return pick(facades, b.look, 1); }
rgba concrete_color(const building &b) { return pick(concretes, b.look, 11); }

void balcony(facade_batches &B, const frame &f, f32 base, rgba wall, u32 look, u32 salt) {
  const f32 w = f.hx * 2.0f - 1.5f, depth = 4.0f;
  B.detail.box(f.front(0.0f, depth * 0.5f), base - 0.8f, {w, 0.8f, depth}, f.angle, shade(wall, 0.85f));
  if (pick01(look, 40) < 0.45f) {
    // A low solid wall at the edge, painted like the front.
    B.detail.box(f.front(0.0f, depth - 0.3f), base, {w, 4.2f, 0.6f}, f.angle, shade(wall, 0.95f));
  } else {
    // A steel rail on thin posts.
    const rgba rail = pick(metals, look, 41);
    B.detail.box(f.front(0.0f, depth - 0.3f), base + 4.6f, {w, 0.5f, 0.5f}, f.angle, rail);
    B.detail.box(f.front(0.0f, depth - 0.3f), base + 2.2f, {w, 0.3f, 0.3f}, f.angle, rail);
    const i32 posts = std::max(2, static_cast<i32>(w / 5.0f));
    for (i32 k = 0; k <= posts; ++k)
      B.detail.box(f.front(-w * 0.5f + w * static_cast<f32>(k) / static_cast<f32>(posts), depth - 0.3f), base,
                  {0.4f, 4.6f, 0.4f}, f.angle, rail);
  }
  // Pot plants on the edge.
  if (pick01(look, salt * 7u + 1u) < 0.5f)
    for (i32 k = 0; k < 1 + static_cast<i32>(pick01(look, salt * 7u + 2u) * 3.0f); ++k) {
      const f32 a = -w * 0.4f + w * 0.8f * pick01(look, salt * 7u + 10u + static_cast<u32>(k));
      B.detail.box(f.front(a, depth - 1.4f), base, {1.8f, 1.6f, 1.8f}, f.angle, col_pot);
      B.detail.box(f.front(a, depth - 1.4f), base + 1.6f, {2.4f, 2.2f, 2.4f}, f.angle + 45.0f, col_leaf);
    }
  // Washing hung out on a line.
  if (pick01(look, salt * 7u + 3u) < 0.3f)
    for (i32 k = 0; k < 4; ++k) {
      const f32 a = -w * 0.35f + w * 0.7f * static_cast<f32>(k) / 3.0f;
      B.detail.box(f.front(a, depth - 0.9f), base + 6.0f, {2.2f, 3.4f, 0.3f}, f.angle,
                  pick(cloths, look, salt * 7u + 20u + static_cast<u32>(k)));
    }
}

void tiled_roof(facade_batches &B, const frame &f, f32 top, rgba col) {
  constexpr f32 slope = 24.0f; // degrees
  const f32 s = slope * pi / 180.0f;
  const f32 half = f.hy + 1.5f; // a little over the front and back
  const f32 len = half / std::cos(s);
  const f32 rise = half * std::tan(s);
  for (const f32 side : {-1.0f, 1.0f}) {
    // Tipped round u: the back slope's far edge down, the front's near one.
    B.boxes.tilted(f.at(0.0f, side * half * 0.5f), top + rise * 0.5f + 0.6f, {f.hx * 2.0f + 1.5f, 1.2f, len},
                   f.angle, side * slope, col);
  }
  B.boxes.box(f.at(0.0f, 0.0f), top + rise + 0.3f, {f.hx * 2.0f + 1.8f, 1.2f, 1.8f}, f.angle, shade(col, 0.8f));
  // The gable ends: a wall stepped up under the slopes.
  for (i32 k = 0; k < 3; ++k) {
    const f32 depth = f.hy * 2.0f * (1.0f - static_cast<f32>(k) / 3.0f);
    for (const f32 side : {-1.0f, 1.0f})
      B.boxes.box(f.at(side * (f.hx - 0.4f), 0.0f), top + rise * static_cast<f32>(k) / 3.0f,
                  {0.8f, rise / 3.0f, depth}, f.angle, shade(col, 0.6f));
  }
}

} // namespace sandtable::city
