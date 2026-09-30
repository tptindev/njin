#include "render_facade.h"

#include <algorithm>
#include <cmath>

namespace sandtable::city {

namespace {

constexpr f32 floor_h = floor_height;
const rgba facades[] = {rgb8(236, 222, 190), rgb8(234, 200, 110), rgb8(214, 168, 92), rgb8(160, 196, 214),
                        rgb8(170, 210, 180), rgb8(228, 170, 170), rgb8(236, 236, 230), rgb8(240, 190, 150),
                        rgb8(196, 180, 214), rgb8(190, 186, 180)};
const rgba concretes[] = {rgb8(176, 172, 166), rgb8(160, 158, 154), rgb8(190, 186, 178), rgb8(150, 146, 140)};
const rgba frames[] = {rgb8(234, 232, 224), rgb8(128, 86, 52), rgb8(70, 110, 90), rgb8(60, 62, 66)};
const rgba metals[] = {rgb8(150, 154, 156), rgb8(86, 120, 104), rgb8(84, 104, 140), rgb8(170, 164, 150)};
const rgba cloths[] = {rgb8(220, 60, 50), rgb8(60, 110, 200), rgb8(240, 220, 90), rgb8(240, 240, 236),
                       rgb8(80, 170, 110), rgb8(230, 130, 170)};
constexpr rgba col_glass = rgb8(46, 58, 66);
constexpr rgba col_lit = rgb8(255, 214, 140);
constexpr rgba col_dark = rgb8(40, 34, 30);
constexpr rgba col_ac = rgb8(226, 228, 226);
constexpr rgba col_leaf = rgb8(78, 136, 62);
constexpr rgba col_pot = rgb8(170, 88, 56);

// How many windows fit across a front `w` wide.
i32 across(f32 w) { return w < 26.0f ? 1 : w < 44.0f ? 2 : std::max(3, static_cast<i32>(w / 15.0f)); }

f32 slot(const frame &f, i32 k, i32 n) {
  return -f.hx + (static_cast<f32>(k) + 0.5f) * (f.hx * 2.0f / static_cast<f32>(n));
}

} // namespace

frame frame_of(const building &b) {
  return {b.box.center, b.box.axis_x(), b.box.axis_y(), b.box.half.x, b.box.half.y, b.box.angle};
}

rgba front_color(const building &b) { return pick(facades, b.look, 1); }
rgba concrete_color(const building &b) { return pick(concretes, b.look, 11); }

void upper_floors(facade_batches &B, const building &b, const frame &f, rgba wall, i32 from, i32 to) {
  for (i32 fl = from; fl < to; ++fl) {
    const f32 base = static_cast<f32>(fl) * floor_h;
    const u32 salt = static_cast<u32>(fl) * 13u;
    const bool has_balcony = pick01(b.look, 70 + salt) < 0.65f;
    ledge(B, f, base, shade(wall, 0.82f));
    window_row(B, f, base, b.look, salt + 1u, has_balcony);
    if (has_balcony)
      balcony(B, f, base, wall, b.look, salt + 2u);
    if (pick01(b.look, 71 + salt) < 0.35f)
      ac_unit(B, f, (pick01(b.look, 72 + salt) < 0.5f ? -1.0f : 1.0f) * (f.hx - 3.0f),
              base + (has_balcony ? 11.0f : 2.0f));
  }
}

void front_face(facade_batches &B, const frame &f, f32 height, rgba col) {
  B.boxes.box(f.front(0.0f, 0.3f), 0.0f, {f.hx * 2.0f, height, 0.6f}, f.angle, col);
}

void ledge(facade_batches &B, const frame &f, f32 base, rgba col) {
  B.detail.box(f.front(0.0f, 0.7f), base - 0.5f, {f.hx * 2.0f + 0.4f, 0.9f, 1.4f}, f.angle, col);
}

void window_row(facade_batches &B, const frame &f, f32 base, u32 look, u32 salt, bool door) {
  const i32 n = across(f.hx * 2.0f);
  const f32 w = std::min(7.5f, f.hx * 2.0f / static_cast<f32>(n) - 3.0f);
  const f32 h = door ? floor_h - 5.5f : 8.0f;
  const f32 sill = base + (door ? 0.8f : 5.0f);
  const rgba fr = pick(frames, look, 30);
  for (i32 k = 0; k < n; ++k) {
    const f32 a = slot(f, k, n);
    B.detail.box(f.front(a, 0.8f), sill - 0.6f, {w + 1.2f, h + 1.2f, 0.4f}, f.angle, fr);
    B.detail.box(f.front(a, 1.0f), sill, {w, h, 0.4f}, f.angle, col_glass);
    // A bar across the middle, as the glass is in two leaves.
    B.detail.box(f.front(a, 1.2f), sill, {0.4f, h, 0.2f}, f.angle, fr);
    if (pick01(look, salt * 16u + static_cast<u32>(k)) < 0.45f)
      B.glow.box(f.front(a, 1.25f), sill + 0.3f, {w - 0.6f, h - 0.6f, 0.2f}, f.angle, col_lit);
  }
}

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

void shopfront(facade_batches &B, const frame &f, rgba sign, f32 height, u32 look) {
  const f32 w = f.hx * 2.0f;
  // The shop open to the street: dark inside, a pillar each side.
  B.detail.box(f.front(0.0f, 0.4f), 0.0f, {w - 3.0f, floor_h - 4.0f, 0.3f}, f.angle, col_dark);
  B.glow.box(f.front(0.0f, 0.5f), 1.0f, {w - 4.0f, floor_h - 6.0f, 0.2f}, f.angle, shade(col_lit, 0.55f));
  // The shutter rolled up at the top.
  B.detail.box(f.front(0.0f, 1.0f), floor_h - 4.5f, {w - 2.0f, 2.2f, 1.8f}, f.angle, pick(metals, look, 50));
  // Awning and sign.
  B.detail.box(f.front(0.0f, 3.2f), floor_h - 7.0f, {w - 1.0f, 0.7f, 6.0f}, f.angle, sign);
  B.detail.box(f.front(0.0f, 0.9f), floor_h - 2.0f, {w - 3.0f, 4.0f, 0.8f}, f.angle, shade(sign, 1.15f));
  B.glow.box(f.front(0.0f, 1.35f), floor_h - 1.6f, {w - 4.0f, 3.2f, 0.2f}, f.angle, sign);
  // A tall sign up the front, on the busier shops.
  if (height > floor_h * 2.0f && pick01(look, 51) < 0.4f) {
    const f32 side = pick01(look, 52) < 0.5f ? -1.0f : 1.0f;
    const f32 h = std::min(floor_h * 2.0f, height - floor_h - 4.0f);
    const vec2 at = f.front(side * (f.hx - 1.5f), 3.0f);
    B.detail.box(at, floor_h + 2.0f, {1.0f, h, 5.0f}, f.angle, sign);
    B.glow.box(at, floor_h + 2.5f, {1.2f, h - 1.0f, 4.4f}, f.angle, sign);
  }
}

void house_front(facade_batches &B, const frame &f, u32 look) {
  const f32 w = f.hx * 2.0f;
  if (pick01(look, 60) < 0.6f) {
    // A steel shutter, ribbed.
    const rgba m = pick(metals, look, 61);
    const f32 sw = std::min(w - 3.0f, 18.0f), sh = floor_h - 5.0f;
    B.detail.box(f.front(0.0f, 0.5f), 0.0f, {sw, sh, 0.4f}, f.angle, m);
    for (i32 k = 1; k < 6; ++k)
      B.detail.box(f.front(0.0f, 0.75f), sh * static_cast<f32>(k) / 6.0f, {sw, 0.35f, 0.3f}, f.angle, shade(m, 0.8f));
    B.detail.box(f.front(0.0f, 0.9f), sh, {sw + 1.0f, 1.8f, 1.4f}, f.angle, shade(m, 0.9f));
  } else {
    // A wooden door, and a window beside it.
    const f32 side = pick01(look, 62) < 0.5f ? -1.0f : 1.0f;
    const f32 da = side * std::max(0.0f, f.hx - 5.5f);
    B.detail.box(f.front(da, 0.5f), 0.0f, {6.0f, 12.0f, 0.4f}, f.angle, rgb8(110, 70, 42));
    if (w > 18.0f) {
      const f32 wa = -side * std::max(0.0f, f.hx - 7.0f);
      B.detail.box(f.front(wa, 0.6f), 4.4f, {7.2f, 7.2f, 0.4f}, f.angle, pick(frames, look, 30));
      B.detail.box(f.front(wa, 0.8f), 5.0f, {6.0f, 6.0f, 0.4f}, f.angle, col_glass);
    }
  }
}

void parapet(facade_batches &B, const frame &f, f32 top, rgba col) {
  constexpr f32 h = 2.4f, t = 0.8f;
  B.boxes.box(f.at(0.0f, -f.hy + t * 0.5f), top, {f.hx * 2.0f, h, t}, f.angle, col);
  B.boxes.box(f.at(0.0f, f.hy - t * 0.5f), top, {f.hx * 2.0f, h, t}, f.angle, col);
  B.boxes.box(f.at(-f.hx + t * 0.5f, 0.0f), top, {t, h, f.hy * 2.0f}, f.angle, col);
  B.boxes.box(f.at(f.hx - t * 0.5f, 0.0f), top, {t, h, f.hy * 2.0f}, f.angle, col);
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

void ac_unit(facade_batches &B, const frame &f, f32 a, f32 base) {
  B.detail.box(f.front(a, 1.4f), base, {4.4f, 3.0f, 2.0f}, f.angle, col_ac);
  B.detail.box(f.front(a, 2.45f), base + 0.6f, {2.6f, 1.8f, 0.1f}, f.angle, rgb8(90, 94, 96));
}

} // namespace sandtable::city
