#include "render_common.h"

#include <algorithm>
#include <cmath>

// Buildings cut open: roof and upper floors taken off, the ground floor's
// walls cut down to waist height with the cut face dark, so the shell shows
// from above: floor, doors, the walls between rooms, the stairs. No
// furniture yet. And picking a building with the mouse.

namespace sandtable::city {

namespace {

instances shell;   // floors, walls, stairs; rebuilt each frame (a few dozen buildings)
instances columns; // pillars of the big halls
instances outline; // the selected building's footprint, unlit

constexpr f32 wall_h = 7.0f;   // cut height, world units: a man (20) stands well above
constexpr f32 wall_t = 1.6f;   // thickness
constexpr f32 slab = 0.8f;     // floor thickness
constexpr f32 door_w = 9.0f;

constexpr rgba col_plaster = rgb8(234, 228, 214);
constexpr rgba col_cut = rgb8(74, 64, 58); // the top of a cut wall
const rgba floors[] = {rgb8(206, 196, 176), rgb8(186, 146, 112), rgb8(214, 212, 204), rgb8(176, 170, 160)};

struct frame {
  vec2 c, u, v; // centre; along the front; from the front to the back
  f32 hx, hy, angle;
  vec2 at(f32 a, f32 b) const { return c + u * a + v * b; }
};

// A wall from a to b standing on the floor, its cut face on top.
void wall(vec2 a, vec2 b, rgba col = col_plaster) {
  const f32 len = distance(a, b);
  if (len < 0.5f)
    return;
  const vec2 mid = (a + b) * 0.5f;
  const f32 ang = angle_of(b - a);
  shell.box(mid, slab, {len, wall_h - 0.4f, wall_t}, ang, col);
  shell.box(mid, slab + wall_h - 0.4f, {len, 0.4f, wall_t}, ang, col_cut);
}

// A wall from a to b with a gap `w` wide centred `t` (0 to 1) along it.
void wall_gap(vec2 a, vec2 b, f32 t, f32 w) {
  const f32 len = distance(a, b);
  if (len <= w + 2.0f) {
    return; // all gap
  }
  const vec2 d = (b - a) / len;
  const f32 g0 = clamp(t * len - w * 0.5f, 0.0f, len - w), g1 = g0 + w;
  wall(a, a + d * g0);
  wall(a + d * g1, b);
}

// Steps up along `dir` from `from`, `w` wide, to the first floor's height.
void stairs(vec2 from, vec2 dir, f32 w, f32 run, f32 angle) {
  constexpr i32 steps = 6;
  for (i32 i = 0; i < steps; ++i) {
    const f32 h = (static_cast<f32>(i) + 1.0f) * 1.8f;
    shell.box(from + dir * ((static_cast<f32>(i) + 0.5f) * run / steps), slab, {w, h, run / steps}, angle,
              shade(col_plaster, 0.9f - 0.03f * static_cast<f32>(i)));
  }
}

// The four outer walls: the front open for a shop, a door otherwise.
void outer(const frame &f, bool shop_front, f32 door_at) {
  const vec2 fl = f.at(-f.hx, -f.hy), fr = f.at(f.hx, -f.hy), bl = f.at(-f.hx, f.hy), br = f.at(f.hx, f.hy);
  wall(fl, bl);
  wall(fr, br);
  wall(bl, br);
  if (shop_front) {
    // Rolled-up shutters: only the pillars at the corners.
    const f32 pillar = std::min(3.0f, f.hx * 0.25f);
    wall(fl, fl + f.u * pillar);
    wall(fr - f.u * pillar, fr);
  } else {
    wall_gap(fl, fr, door_at, door_w);
  }
}

// Where the door is along the front, 0 to 1.
f32 door_along(const building &b, const frame &f) {
  const f32 a = dot(b.door - f.c, f.u);
  return clamp((a + f.hx) / (f.hx * 2.0f), 0.15f, 0.85f);
}

void tube_house(const building &b, const frame &f) {
  outer(f, b.business >= 0, door_along(b, f));
  // The stairs up one side, two thirds back, then a wall across with a way
  // through on the other side; a kitchen at the back of the deep ones.
  const f32 side = pick01(b.look, 21) < 0.5f ? -1.0f : 1.0f;
  const f32 stair_w = std::min(7.0f, f.hx * 0.45f);
  const f32 run = std::min(22.0f, f.hy * 0.6f);
  const f32 cross_at = -f.hy + f.hy * 2.0f * 0.58f;
  if (b.floors > 1)
    stairs(f.at(side * (f.hx - stair_w * 0.5f - wall_t), cross_at - run), f.v, stair_w, run, f.angle);
  wall_gap(f.at(-f.hx, cross_at), f.at(f.hx, cross_at), side > 0.0f ? 0.2f : 0.8f, 8.0f);
  if (f.hy * 2.0f > 70.0f) {
    const f32 back = f.hy - 14.0f;
    wall_gap(f.at(-f.hx, back), f.at(f.hx, back), side > 0.0f ? 0.75f : 0.25f, 7.0f);
  }
}

void small_house(const building &b, const frame &f) {
  outer(f, b.business >= 0, door_along(b, f));
  if (f.hy * 2.0f > 30.0f)
    wall_gap(f.at(-f.hx, 0.0f), f.at(f.hx, 0.0f), pick01(b.look, 22) < 0.5f ? 0.25f : 0.75f, 7.0f);
}

// A corridor along the length, rooms either side, a door into each.
void corridor(const building &b, const frame &f, f32 room) {
  outer(f, false, 0.5f);
  const f32 half = 5.0f;
  const i32 n = std::max(1, static_cast<i32>(f.hx * 2.0f / room));
  const f32 step = f.hx * 2.0f / static_cast<f32>(n);
  for (const f32 s : {-1.0f, 1.0f}) {
    const f32 y = s * half;
    for (i32 k = 0; k < n; ++k) {
      const f32 a0 = -f.hx + static_cast<f32>(k) * step, a1 = a0 + step;
      wall_gap(f.at(a0, y), f.at(a1, y), 0.3f, 6.0f);
      if (k > 0)
        wall(f.at(a0, y), f.at(a0, s * f.hy));
    }
  }
  // The way in from the front to the corridor.
  wall_gap(f.at(-f.hx, -half), f.at(f.hx, -half), 0.5f, door_w);
  if (b.floors > 1)
    stairs(f.at(f.hx - 10.0f, -half + 1.0f), f.v, 7.0f, half * 2.0f - 2.0f, f.angle);
}

// A hall on pillars; an office walled off in a back corner.
void hall(const building &b, const frame &f, rgba pillar, bool office) {
  outer(f, false, 0.5f);
  const f32 grid = 22.0f;
  for (f32 a = -f.hx + grid; a < f.hx - grid * 0.5f; a += grid)
    for (f32 c = -f.hy + grid; c < f.hy - grid * 0.5f; c += grid)
      columns.post(f.at(a, c), slab, 1.4f, wall_h + 2.0f, pillar);
  if (office) {
    const f32 w = std::min(24.0f, f.hx), d = std::min(18.0f, f.hy);
    wall_gap(f.at(f.hx - w, f.hy - d), f.at(f.hx, f.hy - d), 0.3f, 6.0f);
    wall(f.at(f.hx - w, f.hy - d), f.at(f.hx - w, f.hy));
  }
  (void)b;
}

void cut_open(const building &b) {
  const frame f{b.box.center, b.box.axis_x(), b.box.axis_y(), b.box.half.x, b.box.half.y, b.box.angle};
  shell.box(f.c, 0.0f, {f.hx * 2.0f, slab, f.hy * 2.0f}, f.angle, pick(floors, b.look, 20));
  switch (b.kind) {
  case building_kind::tube_house: tube_house(b, f); break;
  case building_kind::apartment:
  case building_kind::hotel: corridor(b, f, 18.0f); break;
  case building_kind::school: corridor(b, f, 30.0f); break;
  case building_kind::warehouse: hall(b, f, rgb8(150, 150, 150), true); break;
  case building_kind::workshop: hall(b, f, rgb8(120, 110, 100), true); break;
  case building_kind::market_hall: hall(b, f, rgb8(200, 190, 170), false); break;
  case building_kind::pagoda: hall(b, f, rgb8(170, 50, 36), false); break;
  default: small_house(b, f); break;
  }
}

void outline_of(const building &b) {
  const rgba c{1.0f, 0.82f, 0.25f, 1.0f};
  for (i32 k = 0; k < 4; ++k) {
    const vec2 a = b.box.corner(k), e = b.box.corner((k + 1) % 4);
    const vec2 out = normalize((a + e) * 0.5f - b.box.center) * 1.5f;
    outline.box((a + e) * 0.5f + out, 0.0f, {distance(a, e) + 3.0f, 0.6f, 1.2f}, angle_of(e - a), c);
  }
}

} // namespace

void cutaway_draw(context &ctx, const city_map &map, const view_options &opt) {
  shell.clear();
  columns.clear();
  outline.clear();
  for (const i32 id : opt.cut)
    if (id >= 0 && id < static_cast<i32>(map.buildings.size()))
      cut_open(map.buildings[static_cast<size_t>(id)]);
  if (opt.selected >= 0 && opt.selected < static_cast<i32>(map.buildings.size()))
    outline_of(map.buildings[static_cast<size_t>(opt.selected)]);
  if (shell.count() == 0 && outline.count() == 0)
    return;
  shell.upload(ctx);
  columns.upload(ctx);
  outline.upload(ctx);
  material3d_set(ctx, {.specular = 0.08f, .shininess = 12.0f});
  shell.draw(ctx, mesh3d_cube);
  columns.draw(ctx, mesh3d_cylinder_low);
  material3d_set(ctx, {.unlit = true, .cast_shadows = false});
  outline.draw(ctx, mesh3d_cube);
  material3d_set(ctx, {});
}

void cutaway_cleanup(context &ctx) {
  shell.destroy(ctx);
  columns.destroy(ctx);
  outline.destroy(ctx);
}

i32 view_pick(context &ctx, const city_map &map, vec2 screen) {
  const ray3d ray = camera3d_ray(ctx, table_camera(), screen);
  // In world units: table (x, y) and height.
  const vec2 o{ray.origin.x / unit3d, ray.origin.z / unit3d};
  const vec2 dxy{ray.direction.x, ray.direction.z};
  const f32 oh = ray.origin.y / unit3d, dh = ray.direction.y;
  i32 best = -1;
  f32 best_t = 1e30f;
  for (i32 i = 0; i < static_cast<i32>(map.buildings.size()); ++i) {
    const building &b = map.buildings[static_cast<size_t>(i)];
    const vec2 u = b.box.axis_x(), v = b.box.axis_y(), d = o - b.box.center;
    // The ray in the building's own frame; a slab test on each axis.
    const f32 p[3] = {dot(d, u), oh, dot(d, v)};
    const f32 r[3] = {dot(dxy, u), dh, dot(dxy, v)};
    const f32 lo[3] = {-b.box.half.x, 0.0f, -b.box.half.y}, hi[3] = {b.box.half.x, b.height, b.box.half.y};
    f32 t0 = 0.0f, t1 = best_t;
    bool miss = false;
    for (i32 k = 0; k < 3 && !miss; ++k) {
      if (std::fabs(r[k]) < 1e-8f) {
        miss = p[k] < lo[k] || p[k] > hi[k];
        continue;
      }
      f32 a = (lo[k] - p[k]) / r[k], c = (hi[k] - p[k]) / r[k];
      if (a > c)
        std::swap(a, c);
      t0 = std::max(t0, a);
      t1 = std::min(t1, c);
      miss = t0 > t1;
    }
    if (!miss && t0 < best_t) {
      best_t = t0;
      best = i;
    }
  }
  return best;
}

} // namespace sandtable::city
