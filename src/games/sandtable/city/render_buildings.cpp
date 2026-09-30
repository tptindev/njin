#include "render_common.h"

#include <algorithm>
#include <cmath>

// The buildings: boxes on the table, each kind built from a few of them.
// Tube houses get the Vietnamese street look: a bright narrow facade, a
// balcony on every floor, an awning and a shop sign on the ground floor, and
// a steel water tank on the roof. At night lit windows and signs glow.

namespace sandtable::city {

namespace {

instances boxes;  // every wall, roof, balcony, awning and sign
instances tanks;  // water tanks on the roofs
instances glow;   // lit windows and signs, drawn unlit at night

// Where each building's instances are in the three batches, so a building
// drawn cut open (render_cutaway.cpp) can be left out without a rebuild.
struct span {
  u32 box0, box1, tank0, tank1, glow0, glow1;
};
std::vector<span> spans;

const rgba facades[] = {rgb8(236, 222, 190), rgb8(234, 200, 110), rgb8(214, 168, 92), rgb8(160, 196, 214),
                        rgb8(170, 210, 180), rgb8(228, 170, 170), rgb8(236, 236, 230), rgb8(240, 190, 150),
                        rgb8(196, 180, 214), rgb8(190, 186, 180)};
const rgba roofs_flat[] = {rgb8(150, 148, 144), rgb8(122, 120, 116), rgb8(168, 164, 156)};
const rgba roofs_tile[] = {rgb8(176, 86, 58), rgb8(160, 76, 52), rgb8(186, 104, 70)};
const rgba roofs_tin[] = {rgb8(70, 110, 150), rgb8(150, 84, 56), rgb8(120, 128, 132), rgb8(80, 120, 90)};
const rgba awnings[] = {rgb8(200, 50, 44), rgb8(40, 90, 170), rgb8(40, 140, 80), rgb8(230, 140, 40),
                        rgb8(230, 200, 60), rgb8(236, 236, 236)};
constexpr rgba col_window = rgb8(255, 214, 140);
constexpr f32 floor_h = floor_height;

struct frame {
  vec2 c, u, v; // centre; along the front; from the front to the back
  f32 hx, hy;
  f32 angle;
  vec2 at(f32 a, f32 b) const { return c + u * a + v * b; }
};

frame frame_of(const building &b) {
  return {b.box.center, b.box.axis_x(), b.box.axis_y(), b.box.half.x, b.box.half.y, b.box.angle};
}

rgba roof_for(const building &b, district_kind dk) {
  switch (dk) {
  case district_kind::old_quarter:
    return pick01(b.look, 5) < 0.6f ? pick(roofs_tile, b.look, 6) : pick(roofs_flat, b.look, 6);
  case district_kind::docks:
  case district_kind::industrial:
    return pick(roofs_tin, b.look, 6);
  case district_kind::new_urban:
    return pick(roofs_flat, b.look, 6);
  default:
    return pick01(b.look, 5) < 0.3f ? pick(roofs_tin, b.look, 6)
           : pick01(b.look, 7) < 0.3f ? pick(roofs_tile, b.look, 6)
                                      : pick(roofs_flat, b.look, 6);
  }
}

// Windows on the front, lit at random, one row a floor.
void windows(const frame &f, i32 floors, f32 from, u32 look) {
  const i32 across = std::max(1, static_cast<i32>(f.hx * 2.0f / 11.0f));
  for (i32 fl = 0; fl < floors; ++fl)
    for (i32 k = 0; k < across; ++k) {
      if (pick01(look, 100 + static_cast<u32>(fl * 16 + k)) > 0.45f)
        continue;
      const f32 a = -f.hx + (static_cast<f32>(k) + 0.5f) * (f.hx * 2.0f / static_cast<f32>(across));
      glow.box(f.at(a, -f.hy - 0.4f), from + static_cast<f32>(fl) * floor_h + 4.0f, {3.5f, 5.0f, 0.6f}, f.angle,
               col_window);
    }
}

void tube_house(const building &b, const frame &f, district_kind dk, const city_map &map) {
  const rgba wall = pick(facades, b.look, 1);
  const rgba roof = roof_for(b, dk);
  boxes.box(f.c, 0.0f, {f.hx * 2.0f, b.height, f.hy * 2.0f}, f.angle, wall);
  boxes.box(f.c, b.height, {f.hx * 2.0f - 1.0f, 1.5f, f.hy * 2.0f - 1.0f}, f.angle, roof);
  // A floor set back on top of some.
  f32 top = b.height;
  if (pick01(b.look, 2) < 0.3f && b.floors < 6) {
    const vec2 at = f.at(0.0f, f.hy * 0.4f);
    boxes.box(at, b.height, {f.hx * 2.0f - 2.0f, floor_h, f.hy * 1.1f}, f.angle, shade(wall, 0.95f));
    boxes.box(at, b.height + floor_h, {f.hx * 2.0f - 3.0f, 1.2f, f.hy * 1.1f - 1.0f}, f.angle, roof);
    top += floor_h;
  }
  // Balconies up the front.
  for (i32 fl = 1; fl < b.floors; ++fl)
    boxes.box(f.at(0.0f, -f.hy - 1.6f), static_cast<f32>(fl) * floor_h - 1.0f, {f.hx * 2.0f - 2.0f, 1.0f, 3.2f},
              f.angle, shade(wall, 0.8f));
  const bool shop = b.business >= 0;
  if (shop || pick01(b.look, 3) < 0.35f)
    boxes.box(f.at(0.0f, -f.hy - 3.0f), 9.5f, {f.hx * 2.0f - 2.0f, 0.8f, 6.0f}, f.angle, pick(awnings, b.look, 4));
  if (shop) {
    const rgba sign = business_color(map.businesses[static_cast<size_t>(b.business)].kind);
    boxes.box(f.at(0.0f, -f.hy - 0.6f), 11.5f, {f.hx * 2.0f - 4.0f, 4.5f, 1.0f}, f.angle, sign);
    glow.box(f.at(0.0f, -f.hy - 0.8f), 11.5f, {f.hx * 2.0f - 4.0f, 4.5f, 1.0f}, f.angle, sign);
  }
  if (pick01(b.look, 8) < 0.75f) {
    const f32 side = pick01(b.look, 9) < 0.5f ? -1.0f : 1.0f;
    tanks.post(f.at(side * std::max(0.0f, f.hx - 4.0f), f.hy - 5.0f), top + 1.5f, 2.6f, 5.0f,
               pick01(b.look, 10) < 0.7f ? rgb8(206, 208, 212) : rgb8(60, 110, 190));
  }
  windows(f, b.floors, 0.0f, b.look);
}

void small_house(const building &b, const frame &f, district_kind dk) {
  const rgba wall = pick(facades, b.look, 1);
  boxes.box(f.c, 0.0f, {f.hx * 2.0f, b.height, f.hy * 2.0f}, f.angle, shade(wall, 0.92f));
  boxes.box(f.c, b.height, {f.hx * 2.0f + 1.0f, 1.5f, f.hy * 2.0f + 1.0f}, f.angle, roof_for(b, dk));
  if (pick01(b.look, 8) < 0.4f)
    tanks.post(f.at(0.0f, f.hy * 0.4f), b.height + 1.5f, 2.2f, 4.0f, rgb8(206, 208, 212));
  windows(f, b.floors, 0.0f, b.look);
}

void apartment(const building &b, const frame &f) {
  const rgba wall = pick01(b.look, 1) < 0.5f ? rgb8(226, 226, 220) : rgb8(214, 206, 190);
  boxes.box(f.c, 0.0f, {f.hx * 2.0f, b.height, f.hy * 2.0f}, f.angle, wall);
  boxes.box(f.c, b.height, {f.hx * 2.0f - 2.0f, 2.0f, f.hy * 2.0f - 2.0f}, f.angle, rgb8(140, 140, 138));
  // Balcony bands front and back.
  for (i32 fl = 1; fl < b.floors; ++fl)
    for (const f32 side : {-1.0f, 1.0f})
      boxes.box(f.at(0.0f, side * (f.hy + 1.2f)), static_cast<f32>(fl) * floor_h - 1.0f,
                {f.hx * 2.0f - 4.0f, 1.2f, 2.4f}, f.angle, rgb8(170, 172, 176));
  for (i32 k = 0; k < 3; ++k)
    tanks.post(f.at(-f.hx * 0.5f + static_cast<f32>(k) * f.hx * 0.5f, 0.0f), b.height + 2.0f, 2.8f, 5.0f,
               rgb8(206, 208, 212));
  windows(f, b.floors, 0.0f, b.look);
}

void shed(const building &b, const frame &f, bool warehouse) {
  const rgba wall = warehouse ? (pick01(b.look, 1) < 0.5f ? rgb8(170, 176, 180) : rgb8(150, 160, 176))
                              : (pick01(b.look, 1) < 0.5f ? rgb8(168, 120, 96) : rgb8(160, 156, 150));
  boxes.box(f.c, 0.0f, {f.hx * 2.0f, b.height, f.hy * 2.0f}, f.angle, wall);
  boxes.box(f.c, b.height, {f.hx * 2.0f + 2.0f, 1.2f, f.hy * 2.0f + 2.0f}, f.angle, pick(roofs_tin, b.look, 6));
  // A roof ridge, so it reads as a pitched tin roof from above.
  boxes.box(f.c, b.height + 1.2f, {f.hx * 2.0f + 2.0f, 1.5f, 3.0f}, f.angle, shade(pick(roofs_tin, b.look, 6), 0.85f));
  boxes.box(f.at(0.0f, -f.hy - 0.3f), 0.0f, {std::min(f.hx * 1.2f, 30.0f), std::min(b.height - 3.0f, 16.0f), 1.0f},
            f.angle, rgb8(60, 62, 66));
}

void market_hall(const building &b, const frame &f) {
  boxes.box(f.c, 0.0f, {f.hx * 2.0f - 6.0f, b.height, f.hy * 2.0f - 6.0f}, f.angle, rgb8(226, 214, 180));
  const rgba roof = pick01(b.look, 2) < 0.5f ? rgb8(60, 130, 90) : rgb8(186, 70, 50);
  boxes.box(f.c, b.height, {f.hx * 2.0f + 4.0f, 2.0f, f.hy * 2.0f + 4.0f}, f.angle, roof);
  boxes.box(f.c, b.height + 2.0f, {f.hx * 1.4f, 6.0f, f.hy * 1.2f}, f.angle, shade(roof, 0.9f));
  boxes.box(f.c, b.height + 8.0f, {f.hx * 1.5f, 1.5f, f.hy * 1.3f}, f.angle, roof);
}

void pagoda(const building &b, const frame &f) {
  const rgba roof = rgb8(170, 60, 40);
  boxes.box(f.c, 0.0f, {f.hx * 1.6f, b.height, f.hy * 1.6f}, f.angle, rgb8(236, 196, 90));
  f32 h = b.height, sx = f.hx * 2.0f + 6.0f, sy = f.hy * 2.0f + 6.0f;
  for (i32 tier = 0; tier < 3; ++tier) {
    boxes.box(f.c, h, {sx, 2.0f, sy}, f.angle, roof);
    boxes.box(f.c, h + 2.0f, {sx * 0.6f, 5.0f, sy * 0.6f}, f.angle, rgb8(236, 196, 90));
    h += 7.0f;
    sx *= 0.62f;
    sy *= 0.62f;
  }
}

void school(const building &b, const frame &f) {
  boxes.box(f.c, 0.0f, {f.hx * 2.0f, b.height, f.hy * 2.0f}, f.angle, rgb8(236, 196, 96));
  boxes.box(f.c, b.height, {f.hx * 2.0f + 2.0f, 1.5f, f.hy * 2.0f + 2.0f}, f.angle, rgb8(176, 76, 56));
  for (i32 fl = 1; fl < b.floors; ++fl)
    boxes.box(f.at(0.0f, -f.hy - 1.5f), static_cast<f32>(fl) * floor_h - 1.0f, {f.hx * 2.0f - 2.0f, 1.0f, 3.0f},
              f.angle, rgb8(236, 236, 230));
  tanks.post(f.at(-f.hx - 20.0f, -f.hy - 30.0f), 0.0f, 0.4f, 30.0f, rgb8(200, 200, 200)); // flagpole
  boxes.box(f.at(-f.hx - 17.0f, -f.hy - 30.0f), 25.0f, {6.0f, 4.0f, 0.4f}, f.angle, rgb8(210, 30, 30));
  windows(f, b.floors, 0.0f, b.look);
}

void hotel(const building &b, const frame &f, const city_map &map) {
  boxes.box(f.c, 0.0f, {f.hx * 2.0f, b.height, f.hy * 2.0f}, f.angle, rgb8(120, 150, 176));
  boxes.box(f.c, b.height, {f.hx * 2.0f - 2.0f, 2.0f, f.hy * 2.0f - 2.0f}, f.angle, rgb8(90, 96, 104));
  for (i32 fl = 1; fl < b.floors; ++fl)
    boxes.box(f.c, static_cast<f32>(fl) * floor_h - 1.0f, {f.hx * 2.0f + 1.0f, 1.0f, f.hy * 2.0f + 1.0f}, f.angle,
              rgb8(220, 220, 214));
  const rgba sign = b.business >= 0 ? business_color(map.businesses[static_cast<size_t>(b.business)].kind)
                                    : rgb8(40, 70, 170);
  boxes.box(f.at(0.0f, -f.hy + 2.0f), b.height + 2.0f, {f.hx * 1.6f, 7.0f, 1.2f}, f.angle, sign);
  glow.box(f.at(0.0f, -f.hy + 1.6f), b.height + 2.0f, {f.hx * 1.6f, 7.0f, 1.2f}, f.angle, sign);
  windows(f, b.floors, 0.0f, b.look);
}

} // namespace

void buildings_build(context &ctx, const city_map &map) {
  boxes.clear();
  tanks.clear();
  glow.clear();
  spans.clear();
  for (const building &b : map.buildings) {
    span sp{boxes.count(), 0, tanks.count(), 0, glow.count(), 0};
    const frame f = frame_of(b);
    const district_kind dk =
        b.district >= 0 ? map.districts[static_cast<size_t>(b.district)].kind : district_kind::residential;
    switch (b.kind) {
    case building_kind::tube_house: tube_house(b, f, dk, map); break;
    case building_kind::house: small_house(b, f, dk); break;
    case building_kind::apartment: apartment(b, f); break;
    case building_kind::warehouse: shed(b, f, true); break;
    case building_kind::workshop: shed(b, f, false); break;
    case building_kind::market_hall: market_hall(b, f); break;
    case building_kind::pagoda: pagoda(b, f); break;
    case building_kind::school: school(b, f); break;
    case building_kind::hotel: hotel(b, f, map); break;
    default: small_house(b, f, dk); break;
    }
    sp.box1 = boxes.count();
    sp.tank1 = tanks.count();
    sp.glow1 = glow.count();
    spans.push_back(sp);
  }
  boxes.upload(ctx);
  tanks.upload(ctx);
  glow.upload(ctx);
}

namespace {

// Draws a batch but for the instances of the buildings in `skip` (sorted).
void draw_skipping(context &ctx, const instances &batch, mesh3d_kind mesh, const std::vector<i32> &skip,
                   u32 span::*from, u32 span::*to) {
  u32 at = 0;
  for (const i32 b : skip) {
    if (b < 0 || b >= static_cast<i32>(spans.size()))
      continue;
    const span &sp = spans[static_cast<size_t>(b)];
    batch.draw_range(ctx, mesh, at, sp.*from);
    at = std::max(at, sp.*to);
  }
  batch.draw_range(ctx, mesh, at, batch.count());
}

} // namespace

void buildings_draw(context &ctx, const view_options &opt) {
  material3d_set(ctx, {.specular = 0.08f, .shininess = 12.0f});
  draw_skipping(ctx, boxes, mesh3d_cube, opt.cut, &span::box0, &span::box1);
  material3d_set(ctx, {.specular = 0.5f, .shininess = 40.0f});
  draw_skipping(ctx, tanks, mesh3d_cylinder_low, opt.cut, &span::tank0, &span::tank1);
  if (opt.night > 0.3f) {
    material3d_set(ctx, {.unlit = true, .cast_shadows = false});
    draw_skipping(ctx, glow, mesh3d_cube, opt.cut, &span::glow0, &span::glow1);
  }
  material3d_set(ctx, {});
}

void buildings_cleanup(context &ctx) {
  boxes.destroy(ctx);
  tanks.destroy(ctx);
  glow.destroy(ctx);
}

} // namespace sandtable::city
