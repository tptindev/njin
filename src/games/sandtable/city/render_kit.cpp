#include "render_kit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sandtable::city {

namespace {

// The file of each piece, in the order of `piece`.
const char *const piece_files[piece_count] = {
    "Trim_Plain_3", "Trim_Window", "Trim_FirstFloor_Window_001", "Trim_FirstFloor_Wall", "Trim_Column_Center",
    "Trim_Wall_Guard", "Cornice_Trim_Center", "DoorFrame_Trim",
    "Brick_Plain_3", "Brick_Window_Square_Single", "Brick_Window_Trim_Single", "Brick_Window_CurvedDouble",
    "Brick_BottomTrim", "Brick_TopTrim", "Brick_CornerColumn_Center", "Cornice_Brick_Center", "DoorFrame_Wooden",
    "Metal_Plain_3", "Metal_Window_Half", "Metal_FullWindow", "Metal_Window", "Metal_FirstFloor_Window",
    "Metal_FirstFloor_Wall", "Metal_Column_Center", "Cornice_Metal_Center", "DoorFrame_Metal_Single",
    "Door_1", "Door_2", "Prop_ACUnit", "Prop_Bollard", "Prop_Planter_Single", "Prop_ManholeCover",
};

// A piece as kit.json measures it, in the kit's metres: its bounds and the
// boxes of its glass (x0, y0, z0, x1, y1, z1).
struct piece_info {
  model_handle model{};
  vec3 lo{}, hi{};
  std::vector<std::array<f32, 6>> glass;
  f32 width() const { return hi.x - lo.x; }
};
std::array<piece_info, piece_count> pieces;

// The kit in world units: a storey is the kit's 3 m.
constexpr f32 per_metre = floor_height / 3.0f;

// A way of building: the pieces of one material.
struct style {
  piece plain, window, shop, ground, door_frame, door, column, cornice; // door: piece::count for none
};
constexpr style plaster{piece::trim_plain,      piece::trim_window, piece::trim_shop,   piece::trim_ground,
                        piece::trim_door_frame, piece::door_glass,  piece::trim_column, piece::trim_cornice};
constexpr style brick{piece::brick_plain,      piece::brick_window, piece::metal_shop,  piece::brick_ground,
                      piece::brick_door_frame, piece::door_wood,    piece::trim_column, piece::brick_cornice};
constexpr style metal{piece::metal_plain,      piece::metal_window_half, piece::metal_shop,   piece::metal_ground,
                      piece::metal_door_frame, piece::count,             piece::metal_column, piece::metal_cornice};
constexpr rgba white{1.0f, 1.0f, 1.0f, 1.0f};

// One side of a building: the middle of its foot, the way along it (the
// piece's +x) and out of it (the piece's +z), and its length.
struct side {
  vec2 mid, along, out;
  f32 len;
};

side side_of(vec2 mid, vec2 out, f32 len) { return {mid, {out.y, -out.x}, out, len}; }

// The four sides of a frame: front, back, left, right.
std::array<side, 4> sides_of(const frame &f) {
  return {side_of(f.at(0.0f, -f.hy), -f.v, f.hx * 2.0f), side_of(f.at(0.0f, f.hy), f.v, f.hx * 2.0f),
          side_of(f.at(-f.hx, 0.0f), -f.u, f.hy * 2.0f), side_of(f.at(f.hx, 0.0f), f.u, f.hy * 2.0f)};
}

// How many panels of `p` across `len`: the kit's width, stretched a little.
i32 panels(piece p, f32 len) {
  const f32 w = pieces[static_cast<size_t>(p)].width() * per_metre;
  return std::max(1, static_cast<i32>(std::lround(len / std::max(w, 1.0f))));
}

const rgba lit_windows[] = {rgb8(255, 214, 140), rgb8(255, 232, 186), rgb8(236, 240, 255), rgb8(255, 196, 120)};

// Piece `p` with the middle of its foot at `at`, `base` up, facing `out`,
// `width` world units across; `tall` is how high 3 m of it stands (a storey:
// floor_height, whatever the piece's own height). A window lit at night (by
// `look` and `salt`) gets a glow over its glass.
void put(kit_sink &s, piece p, vec2 at, f32 base, vec2 out, f32 width, f32 tall, rgba col, u32 look = 0,
         u32 salt = 0) {
  const piece_info &info = pieces[static_cast<size_t>(p)];
  if (info.model.id == 0)
    return;
  const vec2 along{out.y, -out.x};
  const f32 sx = width / std::max(info.width(), 0.01f), sy = tall / 3.0f;
  // The piece's own middle, which is not always its origin.
  const vec2 origin = at - along * ((info.lo.x + info.hi.x) * 0.5f * sx);
  const f32 yaw = std::atan2(out.x, out.y) * 180.0f / pi;
  s.parts[static_cast<size_t>(p)]->add3(to3d(origin, base * unit3d), vec3{sx, sy, per_metre} * unit3d, col, yaw);
  for (const auto &g : info.glass) {
    if (pick01(look, salt) > 0.38f)
      break;
    // Just in front of the glass, over all of it.
    const vec2 c = origin + along * ((g[0] + g[3]) * 0.5f * sx) + out * (g[2] * per_metre + 0.2f);
    s.extras.glow.box(c, base + g[1] * sy, {(g[3] - g[0]) * sx, (g[4] - g[1]) * sy, 0.2f}, angle_of(along),
                      pick(lit_windows, look, salt + 1u));
  }
}

// A run of panels along side `sd` on the storey at `base`: `pick_piece(i, n)`
// says which piece goes in slot `i` of `n`.
template <typename Pick>
void run(kit_sink &s, const side &sd, f32 base, f32 tall, piece sizing, rgba col, u32 look, u32 salt,
         Pick pick_piece) {
  const i32 n = panels(sizing, sd.len);
  const f32 w = sd.len / static_cast<f32>(n);
  for (i32 i = 0; i < n; ++i) {
    const piece p = pick_piece(i, n);
    if (p == piece::count)
      continue;
    const vec2 at = sd.mid + sd.along * ((static_cast<f32>(i) + 0.5f) * w - sd.len * 0.5f);
    put(s, p, at, base, sd.out, w, tall, col, look, salt + static_cast<u32>(i) * 17u);
  }
}

// The same piece all along a side.
void wall(kit_sink &s, const side &sd, f32 base, piece p, rgba col, u32 look = 0, u32 salt = 0) {
  run(s, sd, base, floor_height, p, col, look, salt, [p](i32, i32) { return p; });
}

// The ground floor of side `sd`: piece `p` all along, and the door (its
// frame, and the door in it) in the middle slot.
void ground_with_door(kit_sink &s, const style &st, const side &sd, piece p, rgba col, u32 look, u32 salt) {
  const i32 n = panels(p, sd.len);
  const f32 w = sd.len / static_cast<f32>(n);
  for (i32 i = 0; i < n; ++i) {
    const vec2 at = sd.mid + sd.along * ((static_cast<f32>(i) + 0.5f) * w - sd.len * 0.5f);
    if (i != n / 2) {
      put(s, p, at, 0.0f, sd.out, w, floor_height, col, look, salt + static_cast<u32>(i) * 17u);
      continue;
    }
    put(s, st.door_frame, at, 0.0f, sd.out, w, floor_height, col);
    if (st.door != piece::count)
      put(s, st.door, at, 0.0f, sd.out, per_metre, floor_height, white);
  }
}

// A thing hung on a wall at its own size: an air conditioner.
void hang(kit_sink &s, piece p, vec2 at, f32 base, vec2 out) {
  put(s, p, at, base, out, pieces[static_cast<size_t>(p)].width() * per_metre, floor_height, white);
}

// The cornice along a side, `tall` world units high, over the storeys.
void cornice(kit_sink &s, const style &st, const side &sd, f32 top, rgba col) {
  run(s, sd, top, 4.0f * 3.0f, st.cornice, col, 0, 0, [&](i32, i32) { return st.cornice; });
}

void water_tank(kit_sink &s, const frame &f, f32 top, u32 look, f32 back) {
  const f32 side = pick01(look, 9) < 0.5f ? -1.0f : 1.0f;
  const vec2 at = f.at(side * std::max(0.0f, f.hx - 4.0f), back);
  s.extras.tanks.post(at, top + 2.5f, 2.6f, 5.0f, pick01(look, 10) < 0.7f ? rgb8(206, 208, 212) : rgb8(60, 110, 190));
  s.extras.boxes.box(at, top, {5.0f, 2.5f, 5.0f}, f.angle, rgb8(96, 98, 100));
}

const rgba roofs_flat[] = {rgb8(150, 148, 144), rgb8(122, 120, 116), rgb8(168, 164, 156)};
const rgba roofs_tile[] = {rgb8(176, 86, 58), rgb8(160, 76, 52), rgb8(186, 104, 70)};
const rgba roofs_tin[] = {rgb8(70, 110, 150), rgb8(150, 84, 56), rgb8(120, 128, 132), rgb8(80, 120, 90)};
const rgba bricks[] = {rgb8(255, 255, 255), rgb8(236, 222, 214), rgb8(214, 204, 196)};
const rgba glass_metal[] = {rgb8(236, 238, 240), rgb8(200, 214, 226), rgb8(226, 216, 196)};

bool tiled(const building &b, district_kind dk) {
  return dk == district_kind::old_quarter ? pick01(b.look, 5) < 0.6f
                                          : dk != district_kind::new_urban && pick01(b.look, 7) < 0.15f;
}

// A flat roof inside the cornice, and a tank on some.
void flat_roof(kit_sink &s, const building &b, const frame &f, f32 top, rgba col, f32 tank_chance) {
  s.extras.boxes.box(f.c, top - 0.5f, {f.hx * 2.0f - 0.6f, 0.8f, f.hy * 2.0f - 0.6f}, f.angle, col);
  if (pick01(b.look, 8) < tank_chance)
    water_tank(s, f, top, b.look, f.hy * 0.4f);
}

// An awning and a sign over a shop, lit at night.
void shop_sign(kit_sink &s, const frame &f, rgba sign) {
  const f32 w = f.hx * 2.0f;
  s.extras.detail.box(f.front(0.0f, 3.2f), floor_height - 5.5f, {w - 1.0f, 0.7f, 6.0f}, f.angle, sign);
  s.extras.detail.box(f.front(0.0f, 0.9f), floor_height - 1.0f, {w - 3.0f, 4.0f, 0.8f}, f.angle, shade(sign, 1.15f));
  s.extras.glow.box(f.front(0.0f, 1.35f), floor_height - 0.6f, {w - 4.0f, 3.2f, 0.2f}, f.angle, sign);
}

// --- Each kind of building ---------------------------------------------------------

// Nhà ống and small houses: a painted plaster front (or brick in the old
// quarter), a shop or a door on the ground floor, windows above, balconies
// and air conditioners; bare side walls where the neighbours lean on them.
void street_house(const building &b, const city_map &map, district_kind dk, i32 floors, bool roof, kit_sink &s) {
  const frame f = frame_of(b);
  const std::array<side, 4> sd = sides_of(f);
  const bool is_brick = (dk == district_kind::old_quarter && pick01(b.look, 30) < 0.35f) || pick01(b.look, 31) < 0.08f;
  const style &st = is_brick ? brick : plaster;
  const rgba front = is_brick ? pick(bricks, b.look, 32) : front_color(b);
  const rgba body = is_brick ? shade(front, 0.9f) : concrete_color(b);
  const bool tube = b.kind == building_kind::tube_house;
  const bool shop = b.business >= 0;
  for (i32 fl = 0; fl < floors; ++fl) {
    const f32 base = static_cast<f32>(fl) * floor_height;
    const u32 salt = static_cast<u32>(fl) * 101u;
    if (fl == 0) {
      if (shop)
        wall(s, sd[0], 0.0f, st.shop, front, b.look, salt);
      else
        ground_with_door(s, st, sd[0], st.ground, front, b.look, salt);
    } else {
      wall(s, sd[0], base, st.window, front, b.look, salt);
      if (pick01(b.look, 70 + salt) < 0.55f)
        balcony(s.extras, f, base, front, b.look, salt);
      if (pick01(b.look, 80 + salt) < 0.35f)
        hang(s, piece::ac_unit, f.front(f.hx * 0.55f, 0.0f), base + 12.0f, -f.v);
    }
    // The back: windows above on houses standing free, bare on tube houses.
    wall(s, sd[1], base, tube || fl == 0 ? st.plain : st.window, tube ? body : front, b.look, salt + 5u);
    for (i32 k = 2; k < 4; ++k)
      wall(s, sd[static_cast<size_t>(k)], base, tube ? st.plain : (fl == 0 ? st.plain : st.window),
           tube ? body : front, b.look, salt + static_cast<u32>(k));
  }
  if (floors > 0 && shop)
    shop_sign(s, f, business_color(map.businesses[static_cast<size_t>(b.business)].kind));
  if (!roof)
    return;
  const f32 top = static_cast<f32>(floors) * floor_height;
  if (tiled(b, dk) || (!tube && pick01(b.look, 13) < 0.4f)) {
    tiled_roof(s.extras, f, top, tiled(b, dk) ? pick(roofs_tile, b.look, 6) : pick(roofs_tin, b.look, 6));
    return;
  }
  cornice(s, st, sd[0], top, front);
  cornice(s, st, sd[1], top, tube ? body : front);
  for (i32 k = 2; k < 4; ++k)
    cornice(s, st, sd[static_cast<size_t>(k)], top, tube ? body : front);
  flat_roof(s, b, f, top + 1.5f, pick(roofs_flat, b.look, 6), tube ? 0.75f : 0.4f);
}

// Glass and metal all round: flats, hotels. Columns on the corners.
void tower(const building &b, const city_map &map, bool hotel, i32 floors, bool roof, kit_sink &s) {
  const frame f = frame_of(b);
  const std::array<side, 4> sd = sides_of(f);
  const rgba col = pick(glass_metal, b.look, 33);
  const piece window = hotel ? piece::metal_window_full : piece::metal_window_wide;
  for (i32 fl = 0; fl < floors; ++fl) {
    const f32 base = static_cast<f32>(fl) * floor_height;
    const u32 salt = static_cast<u32>(fl) * 101u;
    for (i32 k = 0; k < 4; ++k) {
      const side &e = sd[static_cast<size_t>(k)];
      if (fl == 0 && k == 0)
        ground_with_door(s, metal, e, piece::metal_shop, col, b.look, salt);
      else if (fl == 0)
        wall(s, e, 0.0f, piece::metal_ground, col, b.look, salt + static_cast<u32>(k));
      else if (k < 2 || hotel)
        wall(s, e, base, window, col, b.look, salt + static_cast<u32>(k) * 7u);
      else
        run(s, e, base, floor_height, piece::metal_plain, col, b.look, salt + static_cast<u32>(k) * 7u,
            [](i32 i, i32) { return i % 2 == 0 ? piece::metal_plain : piece::metal_window_half; });
      // Air conditioners on the flats.
      if (!hotel && fl > 0 && k < 2 && pick01(b.look, 200u + salt + static_cast<u32>(k)) < 0.5f)
        hang(s, piece::ac_unit, e.mid + e.along * (e.len * 0.3f), base + 3.0f, e.out);
    }
    for (i32 k = 0; k < 4; ++k) {
      const vec2 corner = f.at((k & 1) ? f.hx : -f.hx, (k & 2) ? f.hy : -f.hy);
      put(s, piece::metal_column, corner, base, -f.v, 0.5f * per_metre, floor_height, col);
    }
  }
  if (hotel && b.business >= 0)
    shop_sign(s, f, business_color(map.businesses[static_cast<size_t>(b.business)].kind));
  if (!roof)
    return;
  const f32 top = static_cast<f32>(floors) * floor_height;
  for (const side &e : sd)
    cornice(s, metal, e, top, col);
  flat_roof(s, b, f, top + 1.5f, rgb8(140, 140, 138), 0.0f);
  for (i32 k = 0; k < 3; ++k)
    s.extras.tanks.post(f.at(-f.hx * 0.5f + static_cast<f32>(k) * f.hx * 0.5f, 0.0f), top + 2.0f, 2.8f, 5.0f,
                        rgb8(206, 208, 212));
}

// Red brick all round: schools, market halls, workshops; warehouses in
// sheet metal. Arched windows on the market, a tin roof on the sheds.
void brick_hall(const building &b, i32 floors, bool roof, kit_sink &s) {
  const frame f = frame_of(b);
  const std::array<side, 4> sd = sides_of(f);
  const bool warehouse = b.kind == building_kind::warehouse;
  const bool market = b.kind == building_kind::market_hall;
  const bool shed = warehouse || b.kind == building_kind::workshop;
  const style &st = warehouse ? metal : brick;
  const rgba col = warehouse ? pick(glass_metal, b.look, 34) : pick(bricks, b.look, 35);
  // Sheds and markets are one tall storey or two: stretch to their height.
  const f32 storey = shed || market ? std::max(floor_height, (b.height - 4.0f) / static_cast<f32>(std::max(1, b.floors)))
                                    : floor_height;
  for (i32 fl = 0; fl < floors; ++fl) {
    const f32 base = static_cast<f32>(fl) * storey;
    const u32 salt = static_cast<u32>(fl) * 101u;
    for (i32 k = 0; k < 4; ++k) {
      const side &e = sd[static_cast<size_t>(k)];
      const piece win = market ? piece::brick_arches : warehouse ? piece::metal_window_half : piece::brick_window_trim;
      const piece p = fl == 0 && !market ? st.ground : (shed && k > 0 ? st.plain : win);
      if (fl == 0 && k == 0 && !market && !shed) {
        ground_with_door(s, st, e, p, col, b.look, salt);
        continue;
      }
      run(s, e, base, storey, p, col, b.look, salt + static_cast<u32>(k) * 7u, [&](i32 i, i32) {
        return shed && fl > 0 ? (i % 2 == 0 ? p : st.plain) : p;
      });
    }
  }
  if (shed)
    s.extras.detail.box(f.front(0.0f, 0.3f), 0.0f, {std::min(f.hx * 1.2f, 30.0f), std::min(storey - 3.0f, 16.0f), 1.0f},
                        f.angle, rgb8(60, 62, 66));
  if (!roof)
    return;
  const f32 top = static_cast<f32>(floors) * storey;
  if (shed) {
    tiled_roof(s.extras, f, top, pick(roofs_tin, b.look, 6));
  } else if (market) {
    const rgba r = pick01(b.look, 2) < 0.5f ? rgb8(60, 130, 90) : rgb8(186, 70, 50);
    s.extras.boxes.box(f.c, top, {f.hx * 2.0f + 4.0f, 2.0f, f.hy * 2.0f + 4.0f}, f.angle, r);
    s.extras.boxes.box(f.c, top + 2.0f, {f.hx * 1.4f, 6.0f, f.hy * 1.2f}, f.angle, shade(r, 0.9f));
    s.extras.boxes.box(f.c, top + 8.0f, {f.hx * 1.5f, 1.5f, f.hy * 1.3f}, f.angle, r);
  } else {
    for (const side &e : sd)
      cornice(s, brick, e, top, col);
    tiled_roof(s.extras, f, top + 4.0f, rgb8(176, 76, 56));
  }
}

// The pagoda is not in the kit: its tiers of boxes, as from afar.
void pagoda(const building &b, kit_sink &s) {
  const frame f = frame_of(b);
  const rgba roof = rgb8(170, 60, 40);
  s.extras.boxes.box(f.c, 0.0f, {f.hx * 1.6f, b.height, f.hy * 1.6f}, f.angle, rgb8(236, 196, 90));
  f32 h = b.height, sx = f.hx * 2.0f + 6.0f, sy = f.hy * 2.0f + 6.0f;
  for (i32 tier = 0; tier < 3; ++tier) {
    s.extras.boxes.box(f.c, h, {sx, 2.0f, sy}, f.angle, roof);
    s.extras.boxes.box(f.c, h + 2.0f, {sx * 0.6f, 5.0f, sy * 0.6f}, f.angle, rgb8(236, 196, 90));
    h += 7.0f;
    sx *= 0.62f;
    sy *= 0.62f;
  }
}

// --- The city's batches --------------------------------------------------------------

std::array<chunked, piece_count> cparts;
chunked cboxes, cdetail, ctanks, cglow;

// Where each building's instances are in every batch, for leaving out the
// ones open in the cutaway.
constexpr i32 batch_count = piece_count + 4;
std::vector<std::array<std::pair<u32, u32>, batch_count>> spans;

chunked &batch(i32 k) {
  if (k < piece_count)
    return cparts[static_cast<size_t>(k)];
  switch (k - piece_count) {
  case 0: return cboxes;
  case 1: return cdetail;
  case 2: return ctanks;
  default: return cglow;
  }
}

kit_sink city_sink() {
  kit_sink s{{}, {cboxes.inst, cdetail.inst, ctanks.inst, cglow.inst}};
  for (i32 k = 0; k < piece_count; ++k)
    s.parts[static_cast<size_t>(k)] = &cparts[static_cast<size_t>(k)].inst;
  return s;
}

skip_list skips(const std::vector<i32> &cut, i32 k) {
  skip_list out;
  for (const i32 b : cut)
    if (b >= 0 && b < static_cast<i32>(spans.size()))
      out.push_back(spans[static_cast<size_t>(b)][static_cast<size_t>(k)]);
  std::sort(out.begin(), out.end());
  return out;
}

} // namespace

void kit_init(context &ctx) {
  if (pieces[0].model.id != 0)
    return;
  json_value table;
  if (!json_load("assets/models/city/kit.json", table))
    NJIN_WARN("city kit: assets/models/city/kit.json did not load");
  char path[96];
  for (i32 k = 0; k < piece_count; ++k) {
    piece_info &info = pieces[static_cast<size_t>(k)];
    std::snprintf(path, sizeof(path), "assets/models/city/%s.gltf", piece_files[k]);
    info.model = model_load(ctx, path);
    if (info.model.id == 0)
      NJIN_WARN("city kit: %s did not load", path);
    const json_value &t = table[piece_files[k]];
    info.lo = {t["min"][0].f32_or(-1.0f), t["min"][1].f32_or(0.0f), t["min"][2].f32_or(-0.2f)};
    info.hi = {t["max"][0].f32_or(1.0f), t["max"][1].f32_or(3.0f), t["max"][2].f32_or(0.0f)};
    for (usize g = 0; g < t["glass"].size(); ++g) {
      std::array<f32, 6> box{};
      for (usize i = 0; i < 6; ++i)
        box[i] = t["glass"][g][i].f32_or(0.0f);
      info.glass.push_back(box);
    }
    // Matte walls; the dark glass a little glossy.
    for (i32 m = 0; m < model_material_count(ctx, info.model); ++m) {
      model_material mm = model_material_get(ctx, info.model, m);
      const bool glass = mm.color.b > mm.color.r && mm.color.r < 0.25f;
      mm.surface.specular = glass ? 0.6f : 0.06f;
      mm.surface.shininess = glass ? 48.0f : 10.0f;
      model_material_set(ctx, info.model, m, mm);
    }
  }
}

void kit_shutdown(context &ctx) {
  for (piece_info &info : pieces) {
    if (info.model.id != 0)
      model_unload(ctx, info.model);
    info = piece_info{};
  }
}

model_handle kit_model(piece p) { return pieces[static_cast<size_t>(p)].model; }

void kit_building(const building &b, const city_map &map, i32 floors, bool roof, kit_sink &out) {
  const district_kind dk =
      b.district >= 0 ? map.districts[static_cast<size_t>(b.district)].kind : district_kind::residential;
  floors = std::clamp(floors, 0, b.floors);
  switch (b.kind) {
  case building_kind::tube_house:
  case building_kind::house: street_house(b, map, dk, floors, roof, out); break;
  case building_kind::apartment: tower(b, map, false, floors, roof, out); break;
  case building_kind::hotel: tower(b, map, true, floors, roof, out); break;
  case building_kind::pagoda:
    if (roof)
      pagoda(b, out);
    break;
  default: brick_hall(b, floors, roof, out); break;
  }
}

void kit_build(context &ctx, const city_map &map) {
  const i32 chunks = chunk_count();
  for (i32 k = 0; k < batch_count; ++k)
    batch(k).begin(chunks);
  spans.assign(map.buildings.size(), {});
  std::vector<std::vector<i32>> in_chunk(static_cast<size_t>(chunks));
  for (i32 i = 0; i < static_cast<i32>(map.buildings.size()); ++i)
    in_chunk[static_cast<size_t>(chunk_of(map.buildings[static_cast<size_t>(i)].box.center))].push_back(i);
  kit_sink sink = city_sink();
  for (i32 ch = 0; ch < chunks; ++ch) {
    for (i32 k = 0; k < batch_count; ++k)
      batch(k).mark(ch);
    for (const i32 bi : in_chunk[static_cast<size_t>(ch)]) {
      auto &sp = spans[static_cast<size_t>(bi)];
      for (i32 k = 0; k < batch_count; ++k)
        sp[static_cast<size_t>(k)].first = batch(k).inst.count();
      const building &b = map.buildings[static_cast<size_t>(bi)];
      kit_building(b, map, b.floors, true, sink);
      for (i32 k = 0; k < batch_count; ++k)
        sp[static_cast<size_t>(k)].second = batch(k).inst.count();
    }
  }
  for (i32 k = 0; k < batch_count; ++k) {
    batch(k).end();
    batch(k).inst.upload(ctx);
  }
}

void kit_draw(context &ctx, const view_options &opt) {
  // The whole town from the kit (about 65 thousand pieces held 60 fps on the
  // development machine), so there is no line where boxes turn into buildings.
  const f32 r = cull().detail_r;
  i32 detailed = 0;
  for (i32 c = 0; c < chunk_count(); ++c)
    detailed += chunk_visible(c) && chunk_detailed(c, r) ? 1 : 0;
  count_detailed(detailed);
  const auto near = [](i32) { return true; };
  for (i32 k = 0; k < piece_count; ++k) {
    const skip_list sk = skips(opt.cut, k);
    draw_chunks_model(ctx, cparts[static_cast<size_t>(k)], pieces[static_cast<size_t>(k)].model, near, &sk);
  }
  const skip_list sb = skips(opt.cut, piece_count), sd = skips(opt.cut, piece_count + 1),
                  st = skips(opt.cut, piece_count + 2), sg = skips(opt.cut, piece_count + 3);
  material3d_set(ctx, {.specular = 0.08f, .shininess = 12.0f});
  draw_chunks(ctx, cboxes, mesh3d_cube, near, &sb);
  draw_chunks(ctx, cdetail, mesh3d_cube, near, &sd);
  material3d_set(ctx, {.specular = 0.5f, .shininess = 40.0f});
  draw_chunks(ctx, ctanks, mesh3d_cylinder_low, near, &st);
  if (opt.night > 0.3f) {
    material3d_set(ctx, {.unlit = true, .cast_shadows = false});
    draw_chunks(ctx, cglow, mesh3d_cube, near, &sg);
  }
  material3d_set(ctx, {});
}

void kit_cleanup(context &ctx) {
  for (i32 k = 0; k < batch_count; ++k) {
    batch(k).inst.destroy(ctx);
    batch(k).start.clear();
  }
  spans.clear();
}

} // namespace sandtable::city
