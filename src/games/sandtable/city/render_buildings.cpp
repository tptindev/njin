#include "render_facade.h"
#include "render_lod.h"

#include <algorithm>
#include <cmath>

// The buildings from outside, each kind put together from the street-front
// parts of render_facade.cpp. Tube houses get the Vietnamese street look: a
// bare concrete body with a narrow painted front, a shop or a shutter on the
// ground floor, a ledge, windows and a balcony on every floor above (pots,
// washing, an air conditioner), a parapet or a tiled roof, a steel water tank.
// At night lit windows and signs glow.

namespace sandtable::city {

namespace {

chunked cboxes;  // bodies, fronts, roofs: always
chunked cdetail; // windows, balconies, shutters, signs: close up
chunked ctanks;  // water tanks on the roofs
chunked cglow;   // lit windows and signs, drawn unlit at night
instances &boxes = cboxes.inst;
instances &tanks = ctanks.inst;
instances &glow = cglow.inst;
facade_batches batches{cboxes.inst, cdetail.inst, ctanks.inst, cglow.inst};

// Where each building's instances are in the three batches, so a building
// drawn cut open (render_cutaway.cpp) can be left out without a rebuild.
struct span {
  u32 box0, box1, detail0, detail1, tank0, tank1, glow0, glow1;
};
std::vector<span> spans;

const rgba roofs_flat[] = {rgb8(150, 148, 144), rgb8(122, 120, 116), rgb8(168, 164, 156)};
const rgba roofs_tile[] = {rgb8(176, 86, 58), rgb8(160, 76, 52), rgb8(186, 104, 70)};
const rgba roofs_tin[] = {rgb8(70, 110, 150), rgb8(150, 84, 56), rgb8(120, 128, 132), rgb8(80, 120, 90)};
constexpr f32 floor_h = floor_height;

bool tiled(const building &b, district_kind dk) {
  return dk == district_kind::old_quarter ? pick01(b.look, 5) < 0.6f
                                          : dk != district_kind::new_urban && pick01(b.look, 7) < 0.15f;
}

rgba roof_for(const building &b, district_kind dk) {
  if (dk == district_kind::docks || dk == district_kind::industrial)
    return pick(roofs_tin, b.look, 6);
  return tiled(b, dk) ? pick(roofs_tile, b.look, 6) : pick(roofs_flat, b.look, 6);
}

void water_tank(const frame &f, f32 top, u32 look, f32 back) {
  const f32 side = pick01(look, 9) < 0.5f ? -1.0f : 1.0f;
  const vec2 at = f.at(side * std::max(0.0f, f.hx - 4.0f), back);
  tanks.post(at, top + 2.5f, 2.6f, 5.0f, pick01(look, 10) < 0.7f ? rgb8(206, 208, 212) : rgb8(60, 110, 190));
  // Its stand.
  boxes.box(at, top, {5.0f, 2.5f, 5.0f}, f.angle, rgb8(96, 98, 100));
}

void tube_house(const building &b, const frame &f, district_kind dk, const city_map &map) {
  const rgba wall = front_color(b);
  boxes.box(f.c, 0.0f, {f.hx * 2.0f - 0.2f, b.height, f.hy * 2.0f}, f.angle, concrete_color(b));
  front_face(batches, f, b.height, wall);
  if (b.business >= 0)
    shopfront(batches, f, business_color(map.businesses[static_cast<size_t>(b.business)].kind), b.height, b.look);
  else
    house_front(batches, f, b.look);
  upper_floors(batches, b, f, wall, 1, b.floors);

  f32 top = b.height;
  if (tiled(b, dk)) {
    tiled_roof(batches, f, top, roof_for(b, dk));
  } else {
    boxes.box(f.c, top, {f.hx * 2.0f - 0.8f, 0.6f, f.hy * 2.0f - 0.8f}, f.angle, roof_for(b, dk));
    parapet(batches, f, top, shade(wall, 0.9f));
    // A floor set back on the roof of some, with its own window.
    if (pick01(b.look, 2) < 0.3f && b.floors < 6) {
      const frame back{f.at(0.0f, f.hy * 0.35f), f.u, f.v, f.hx - 1.0f, f.hy * 0.55f, f.angle};
      boxes.box(back.c, top, {back.hx * 2.0f, floor_h, back.hy * 2.0f}, f.angle, shade(concrete_color(b), 1.05f));
      front_face(batches, back, top + floor_h, shade(wall, 0.95f));
      window_row(batches, back, top, b.look, 90u, true);
      boxes.box(back.c, top + floor_h, {back.hx * 2.0f, 0.8f, back.hy * 2.0f}, f.angle, roof_for(b, dk));
      top += floor_h;
    }
    if (pick01(b.look, 8) < 0.75f)
      water_tank(f, top, b.look, f.hy - 5.0f);
  }
}

void small_house(const building &b, const frame &f, district_kind dk) {
  const rgba wall = shade(front_color(b), 0.92f);
  boxes.box(f.c, 0.0f, {f.hx * 2.0f, b.height, f.hy * 2.0f}, f.angle, wall);
  house_front(batches, f, b.look);
  upper_floors(batches, b, f, wall, 1, b.floors);
  if (tiled(b, dk) || pick01(b.look, 13) < 0.4f) {
    tiled_roof(batches, f, b.height, tiled(b, dk) ? roof_for(b, dk) : pick(roofs_tin, b.look, 6));
  } else {
    boxes.box(f.c, b.height, {f.hx * 2.0f - 0.8f, 0.6f, f.hy * 2.0f - 0.8f}, f.angle, roof_for(b, dk));
    parapet(batches, f, b.height, shade(wall, 0.9f));
    if (pick01(b.look, 8) < 0.4f)
      water_tank(f, b.height, b.look, f.hy * 0.3f);
  }
}

// A block of flats: the same window and balcony on every floor, front and
// back, and air conditioners all over.
void apartment(const building &b, const frame &f) {
  const rgba wall = pick01(b.look, 1) < 0.5f ? rgb8(226, 226, 220) : rgb8(214, 206, 190);
  boxes.box(f.c, 0.0f, {f.hx * 2.0f, b.height, f.hy * 2.0f}, f.angle, wall);
  const frame back{f.c, -f.u, -f.v, f.hx, f.hy, f.angle + 180.0f};
  house_front(batches, f, b.look);
  for (const frame *side : {&f, &back})
    for (i32 fl = side == &f ? 1 : 0; fl < b.floors; ++fl) {
      const f32 base = static_cast<f32>(fl) * floor_h;
      window_row(batches, *side, base, b.look, static_cast<u32>(fl) * 7u + (side == &f ? 0u : 500u), fl > 0);
      if (fl > 0)
        boxes.box(side->front(0.0f, 1.5f), base - 0.8f, {side->hx * 2.0f - 2.0f, 0.8f, 3.0f}, f.angle,
                  rgb8(186, 188, 190));
      if (pick01(b.look, 200u + static_cast<u32>(fl)) < 0.5f)
        ac_unit(batches, *side, side->hx - 4.0f, base + 12.0f);
    }
  boxes.box(f.c, b.height, {f.hx * 2.0f - 2.0f, 0.8f, f.hy * 2.0f - 2.0f}, f.angle, rgb8(140, 140, 138));
  parapet(batches, f, b.height, shade(wall, 0.9f));
  for (i32 k = 0; k < 3; ++k)
    tanks.post(f.at(-f.hx * 0.5f + static_cast<f32>(k) * f.hx * 0.5f, 0.0f), b.height + 2.0f, 2.8f, 5.0f,
               rgb8(206, 208, 212));
}

void shed(const building &b, const frame &f, bool warehouse) {
  const rgba wall = warehouse ? (pick01(b.look, 1) < 0.5f ? rgb8(170, 176, 180) : rgb8(150, 160, 176))
                              : (pick01(b.look, 1) < 0.5f ? rgb8(168, 120, 96) : rgb8(160, 156, 150));
  boxes.box(f.c, 0.0f, {f.hx * 2.0f, b.height, f.hy * 2.0f}, f.angle, wall);
  tiled_roof(batches, f, b.height, pick(roofs_tin, b.look, 6));
  // A big door, and a strip of high windows along the front.
  boxes.box(f.front(0.0f, 0.3f), 0.0f, {std::min(f.hx * 1.2f, 30.0f), std::min(b.height - 3.0f, 16.0f), 1.0f},
            f.angle, rgb8(60, 62, 66));
  if (b.height > 20.0f)
    boxes.box(f.front(0.0f, 0.4f), b.height - 6.0f, {f.hx * 1.8f, 3.0f, 0.5f}, f.angle, rgb8(46, 58, 66));
}

void market_hall(const building &b, const frame &f) {
  boxes.box(f.c, 0.0f, {f.hx * 2.0f - 6.0f, b.height, f.hy * 2.0f - 6.0f}, f.angle, rgb8(226, 214, 180));
  const rgba roof = pick01(b.look, 2) < 0.5f ? rgb8(60, 130, 90) : rgb8(186, 70, 50);
  boxes.box(f.c, b.height, {f.hx * 2.0f + 4.0f, 2.0f, f.hy * 2.0f + 4.0f}, f.angle, roof);
  boxes.box(f.c, b.height + 2.0f, {f.hx * 1.4f, 6.0f, f.hy * 1.2f}, f.angle, shade(roof, 0.9f));
  boxes.box(f.c, b.height + 8.0f, {f.hx * 1.5f, 1.5f, f.hy * 1.3f}, f.angle, roof);
  // Arches along the front.
  const i32 n = std::max(2, static_cast<i32>(f.hx / 10.0f));
  for (i32 k = 0; k < n; ++k) {
    const f32 a = -f.hx + 3.0f + (static_cast<f32>(k) + 0.5f) * (f.hx * 2.0f - 6.0f) / static_cast<f32>(n);
    boxes.box(f.at(a, -f.hy + 3.2f), 0.0f, {8.0f, b.height - 5.0f, 0.4f}, f.angle, rgb8(60, 52, 44));
  }
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
  tiled_roof(batches, f, b.height, rgb8(176, 76, 56));
  for (i32 fl = 0; fl < b.floors; ++fl) {
    const f32 base = static_cast<f32>(fl) * floor_h;
    window_row(batches, f, base, b.look, static_cast<u32>(fl) * 5u, false);
    if (fl > 0)
      boxes.box(f.front(0.0f, 1.5f), base - 1.0f, {f.hx * 2.0f - 2.0f, 1.0f, 3.0f}, f.angle, rgb8(236, 236, 230));
  }
  tanks.post(f.at(-f.hx - 20.0f, -f.hy - 30.0f), 0.0f, 0.4f, 30.0f, rgb8(200, 200, 200)); // flagpole
  boxes.box(f.at(-f.hx - 17.0f, -f.hy - 30.0f), 25.0f, {6.0f, 4.0f, 0.4f}, f.angle, rgb8(210, 30, 30));
}

void hotel(const building &b, const frame &f, const city_map &map) {
  boxes.box(f.c, 0.0f, {f.hx * 2.0f, b.height, f.hy * 2.0f}, f.angle, rgb8(120, 150, 176));
  boxes.box(f.c, b.height, {f.hx * 2.0f - 2.0f, 2.0f, f.hy * 2.0f - 2.0f}, f.angle, rgb8(90, 96, 104));
  for (i32 fl = 1; fl < b.floors; ++fl) {
    boxes.box(f.c, static_cast<f32>(fl) * floor_h - 1.0f, {f.hx * 2.0f + 1.0f, 1.0f, f.hy * 2.0f + 1.0f}, f.angle,
              rgb8(220, 220, 214));
    window_row(batches, f, static_cast<f32>(fl) * floor_h, b.look, static_cast<u32>(fl) * 3u, false);
  }
  const rgba sign = b.business >= 0 ? business_color(map.businesses[static_cast<size_t>(b.business)].kind)
                                    : rgb8(40, 70, 170);
  boxes.box(f.at(0.0f, -f.hy + 2.0f), b.height + 2.0f, {f.hx * 1.6f, 7.0f, 1.2f}, f.angle, sign);
  glow.box(f.at(0.0f, -f.hy + 1.6f), b.height + 2.0f, {f.hx * 1.6f, 7.0f, 1.2f}, f.angle, sign);
  // A lobby of glass doors.
  boxes.box(f.front(0.0f, 0.5f), 0.0f, {f.hx * 1.2f, floor_h - 5.0f, 0.4f}, f.angle, rgb8(46, 58, 66));
  glow.box(f.front(0.0f, 0.7f), 0.5f, {f.hx * 1.1f, floor_h - 6.0f, 0.2f}, f.angle, rgb8(255, 214, 140));
}

} // namespace

void buildings_build(context &ctx, const city_map &map) {
  const i32 chunks = chunk_count();
  for (chunked *c : {&cboxes, &cdetail, &ctanks, &cglow})
    c->begin(chunks);
  spans.assign(map.buildings.size(), span{});
  // Chunk after chunk, so each chunk is one range of every batch.
  std::vector<std::vector<i32>> in_chunk(static_cast<size_t>(chunks));
  for (i32 i = 0; i < static_cast<i32>(map.buildings.size()); ++i)
    in_chunk[static_cast<size_t>(chunk_of(map.buildings[static_cast<size_t>(i)].box.center))].push_back(i);
  for (i32 ch = 0; ch < chunks; ++ch) {
    for (chunked *c : {&cboxes, &cdetail, &ctanks, &cglow})
      c->mark(ch);
    for (const i32 bi : in_chunk[static_cast<size_t>(ch)]) {
    const building &b = map.buildings[static_cast<size_t>(bi)];
    span sp{boxes.count(), 0, cdetail.inst.count(), 0, tanks.count(), 0, glow.count(), 0};
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
    sp.detail1 = cdetail.inst.count();
    sp.tank1 = tanks.count();
    sp.glow1 = glow.count();
    spans[static_cast<size_t>(bi)] = sp;
    }
  }
  for (chunked *c : {&cboxes, &cdetail, &ctanks, &cglow}) {
    c->end();
    c->inst.upload(ctx);
  }
}

namespace {

// The spans of the buildings open in the cutaway, in one batch.
skip_list skips(const std::vector<i32> &cut, u32 span::*from, u32 span::*to) {
  skip_list out;
  for (const i32 b : cut)
    if (b >= 0 && b < static_cast<i32>(spans.size()))
      out.emplace_back(spans[static_cast<size_t>(b)].*from, spans[static_cast<size_t>(b)].*to);
  std::sort(out.begin(), out.end());
  return out;
}

} // namespace

void buildings_draw(context &ctx, const view_options &opt) {
  const skip_list sb = skips(opt.cut, &span::box0, &span::box1);
  const skip_list sd = skips(opt.cut, &span::detail0, &span::detail1);
  const skip_list st = skips(opt.cut, &span::tank0, &span::tank1);
  const skip_list sg = skips(opt.cut, &span::glow0, &span::glow1);
  const f32 detail_r = cull().detail_r;
  i32 detailed = 0;
  for (i32 c = 0; c < chunk_count(); ++c)
    detailed += chunk_visible(c) && chunk_detailed(c, detail_r) ? 1 : 0;
  count_detailed(detailed);
  const auto clear = [](i32 c) { return !chunk_hazy(c); };
  const auto hazy = [](i32 c) { return chunk_hazy(c); };
  const auto fine = [detail_r](i32 c) { return !chunk_hazy(c) && chunk_detailed(c, detail_r); };

  material3d_set(ctx, {.specular = 0.08f, .shininess = 12.0f});
  draw_chunks(ctx, cboxes, mesh3d_cube, clear, &sb);
  draw_chunks(ctx, cdetail, mesh3d_cube, fine, &sd);
  material3d_set(ctx, {.specular = 0.5f, .shininess = 40.0f});
  draw_chunks(ctx, ctanks, mesh3d_cylinder_low, clear, &st);
  if (cull().focused) {
    // Beyond the focus: the bare bodies and roofs only, through a haze.
    haze_on(ctx);
    material3d_set(ctx, {.specular = 0.02f, .shininess = 8.0f});
    draw_chunks(ctx, cboxes, mesh3d_cube, hazy, &sb);
    draw_chunks(ctx, ctanks, mesh3d_cylinder_low, hazy, &st);
    haze_off(ctx);
  }
  if (opt.night > 0.3f) {
    material3d_set(ctx, {.unlit = true, .cast_shadows = false});
    draw_chunks(ctx, cglow, mesh3d_cube, clear, &sg);
  }
  material3d_set(ctx, {});
}

void buildings_cleanup(context &ctx) {
  for (chunked *c : {&cboxes, &cdetail, &ctanks, &cglow}) {
    c->inst.destroy(ctx);
    c->start.clear();
  }
}

} // namespace sandtable::city
