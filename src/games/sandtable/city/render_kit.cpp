#include "render_kit.h"

#include "pbk.h"
#include "pbk_render.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace sandtable::city {

namespace {

// The kit's modules by manifest index, their models, and the index of an ID;
// after them one part a leaf of each animated module (a door's leaf, a
// shutter), `leaf_part` the first of a module's.
std::vector<std::string> ids;
std::vector<model_handle> models;
std::vector<std::string> part_keys;
std::map<std::string, i32> index_of;
std::vector<i32> leaf_part;

// A metre of the kit in 3D units: the manifest's engine_render_scale.
f32 render_scale = 0.1875f;
constexpr f32 per_metre = units_per_metre; // world units a metre

i32 module_index(const std::string &id) {
  const auto it = index_of.find(id);
  return it == index_of.end() ? -1 : it->second;
}

// The roles a bay can take, the kit's module family names.
enum role : u8 { r_wall, r_window, r_shop, r_door, r_balcony, r_none };
const char *const role_names[] = {"Wall", "Window", "Shopfront", "DoorRigged", "Balcony"};

// One building's modules: its style's ID for each role, found once.
struct style_ids {
  i32 role[5] = {-1, -1, -1, -1, -1};
  i32 column = -1, parapet = -1, slope = -1, ridge = -1, gable = -1;
  bool shutters = false; // its windows have wooden shutters
};

style_ids ids_of(const std::string &style) {
  style_ids s;
  for (i32 k = 0; k < 5; ++k)
    s.role[k] = module_index(style + "/" + role_names[k]);
  s.column = module_index(style + "/Column");
  s.parapet = module_index(style + "/Parapet");
  s.slope = module_index(style + "/RoofSlope");
  s.ridge = module_index(style + "/Ridge");
  s.gable = module_index(style + "/Gable");
  const pbk::module_info *w = pbk::load_manifest().find(style + "/Window");
  s.shutters = w && w->shutters;
  return s;
}

const char *district_key(district_kind k) {
  switch (k) {
  case district_kind::old_quarter: return "old_quarter";
  case district_kind::market: return "market";
  case district_kind::nightlife: return "nightlife";
  case district_kind::docks: return "docks";
  case district_kind::industrial: return "industrial";
  case district_kind::new_urban: return "new_urban";
  default: return "residential";
  }
}

// The style of a building: towers modern, sheds and halls brick, houses by
// their district's weights in the rules (appearance.district_weights), drawn
// from the same seed stream the plans use, so a house the rules lay out in
// full keeps its style.
std::string style_of(const building &b, district_kind dk) {
  switch (b.kind) {
  case building_kind::apartment:
  case building_kind::hotel: return "Modern";
  case building_kind::warehouse:
  case building_kind::workshop:
  case building_kind::school:
  case building_kind::market_hall: return "Brick";
  default: break;
  }
  const pbk::rules &R = pbk::load_rules();
  for (const auto &[d, w] : R.district_styles) {
    if (d != district_key(dk))
      continue;
    f32 sum = 0.0f;
    for (const auto &[s, x] : w)
      sum += x;
    f32 x = static_cast<f32>(pbk::sub_seed(b.look, R.salt_appearance) % 100000u) / 100000.0f * sum;
    for (const auto &[s, v] : w) {
      if (x < v)
        return s;
      x -= v;
    }
    return w.empty() ? "Modern" : w.back().first;
  }
  return "Modern";
}

// One side of a building: where its modules start (table units, on the
// outer face), the way along it (the modules' +X) and out of it (+Z), its
// length in world units.
struct side {
  vec2 start, along, out;
  f32 len;
};

side side_of(vec2 mid, vec2 out, f32 len) {
  const vec2 along{out.y, -out.x};
  return {mid - along * (len * 0.5f), along, out, len};
}

// Front, back, left, right.
std::array<side, 4> sides_of(const frame &f) {
  return {side_of(f.at(0.0f, -f.hy), -f.v, f.hx * 2.0f), side_of(f.at(0.0f, f.hy), f.v, f.hx * 2.0f),
          side_of(f.at(-f.hx, 0.0f), -f.u, f.hy * 2.0f), side_of(f.at(f.hx, 0.0f), f.u, f.hy * 2.0f)};
}

f32 yaw_of(vec2 out) { return std::atan2(out.x, out.y) * 180.0f / pi; }

i32 bays_on(f32 len) { return std::max(1, static_cast<i32>(std::lround(len / (2.0f * per_metre)))); }

const rgba lit_windows[] = {rgb8(255, 214, 140), rgb8(255, 232, 186), rgb8(236, 240, 255), rgb8(255, 196, 120)};

struct put_ctx {
  kit_sink &s;
  rgba tint;
  u32 look;
};


// Module `m` with its origin at `origin` (table), `base` world units up,
// facing `out`, at the kit's own size unless `sx`/`sy`/`sz` stretch it (only
// the roof's slopes and gables are: generate.py stretches those too).
// Whether the town's window at `origin` has its shutters open: a little more
// than half are, by day as by night (the rules' periods: 0.75..0.85 by day).
bool shutters_open(u32 look, vec2 origin) {
  const u32 h = pbk::sub_seed(look ^ static_cast<u32>(std::lround(origin.x * 7.0f)), static_cast<u32>(std::lround(origin.y * 13.0f)));
  return h % 100u < 60u;
}

void put(put_ctx &p, i32 m, vec2 origin, f32 base, vec2 out, f32 sx = 1.0f, f32 sy = 1.0f, f32 sz = 1.0f) {
  if (m < 0 || m >= static_cast<i32>(leaf_part.size()) || !p.s.parts[static_cast<size_t>(m)])
    return;
  const vec3 at = to3d(origin, base * unit3d);
  p.s.parts[static_cast<size_t>(m)]->add3(at, vec3{sx, sy, sz} * render_scale, colors::white, yaw_of(out));
  // An animated module's leaves at rest: the door shut, the shutters as the
  // house keeps them.
  if (leaf_part[static_cast<size_t>(m)] < 0)
    return;
  const pbk::module_info *mi = pbk::load_manifest().find(ids[static_cast<size_t>(m)]);
  const pbk::module_rig &rig = pbk::module_rig_of(ids[static_cast<size_t>(m)]);
  const pbk::door_clip c = pbk::clip_of(mi);
  const f32 t = mi && mi->shutters && shutters_open(p.look, origin) ? c.open_pose : c.shut_pose;
  for (i32 k = 0; k < static_cast<i32>(rig.leaves.size()); ++k) {
    const size_t part = static_cast<size_t>(leaf_part[static_cast<size_t>(m)] + k);
    if (part >= p.s.parts.size() || !p.s.parts[part])
      continue;
    const pbk::leaf_pose lp = pbk::leaf_pose_at(rig, k, t, at, yaw_of(out), render_scale);
    p.s.parts[part]->add_turned(lp.pos, lp.scale, colors::white, lp.rot);
  }
}

// The openings the kit's generator cuts in its wall ring (generate.py
// APERTURES): width, height, sill, metres. The street door is the Door's.
struct opening {
  f32 w, h, sill;
};
bool opening_of(role r, opening &o) {
  switch (r) {
  case r_window: o = {1.18f, 1.50f, 0.90f}; return true;
  case r_door: o = {1.05f, 2.35f, 0.0f}; return true;
  case r_shop: o = {1.70f, 2.48f, 0.0f}; return true;
  case r_balcony: o = {1.60f, 2.40f, 0.0f}; return true;
  default: return false;
  }
}

// The glow of a lit window or shop glass at night (the modules' own glass,
// measured from the GLBs).
void glow(put_ctx &p, role r, vec2 origin, f32 base, const side &sd, u32 salt, bool shuttered) {
  if ((r != r_window && r != r_shop) || pick01(p.look, salt) > 0.38f)
    return;
  // Shut wooden shutters hide the room; open ones show it (there is no glass).
  if (r == r_window && shuttered && !shutters_open(p.look, origin))
    return;
  const bool shop = r == r_shop;
  const f32 x0 = shop ? 0.2f : 0.46f, x1 = shop ? 1.8f : 1.54f, y0 = shop ? 0.03f : 0.93f, y1 = shop ? 2.45f : 2.37f;
  const vec2 c = origin + sd.along * ((x0 + x1) * 0.5f * per_metre) - sd.out * (0.13f * per_metre);
  p.s.extras.glow.box(c, base + y0 * per_metre, {(x1 - x0) * per_metre, (y1 - y0) * per_metre, 0.2f},
                      angle_of(sd.along), pick(lit_windows, p.look, salt + 1u));
}

// A run of the wall ring along `sd`, from `a` to `b` metres along it,
// `z0`..`z1` metres up the storey at `base`; `out0`..`in1` metres across it
// (0 the outer face, + inward).
void ring(put_ctx &p, const side &sd, f32 base, f32 a, f32 b, f32 z0, f32 z1, f32 out0, f32 in1, rgba col) {
  if (b - a < 0.005f || z1 - z0 < 0.005f)
    return;
  const vec2 c = sd.start + sd.along * ((a + b) * 0.5f * per_metre) - sd.out * ((out0 + in1) * 0.5f * per_metre);
  p.s.extras.boxes.box(c, base + z0 * per_metre, {(b - a) * per_metre, (z1 - z0) * per_metre, (in1 - out0) * per_metre},
                       angle_of(sd.along), col);
}

// The colours of the kit's own wall and trim materials (rules
// appearance.palettes, the same numbers generate.py gives its materials).
struct colours {
  rgba wall, trim, slab;
};
colours colours_of(const std::string &style) {
  const pbk::rules &R = pbk::load_rules();
  colours c{rgb8(180, 180, 180), rgb8(230, 230, 230), rgb8(150, 146, 140)};
  if (const auto *pal = R.palette(style)) {
    c.wall = pbk::linear_to_srgb((*pal)[0]);
    c.trim = pbk::linear_to_srgb((*pal)[1]);
  }
  return c;
}

// The bays of a side as generate.py lays them: whole 2 m bays, every one at
// the kit's size; a lot whose length is not whole bays keeps the rest as
// plain wall, half at each end.
struct bays {
  i32 n;
  f32 margin; // metres
};
bays bays_of(const side &sd) {
  const f32 len = sd.len / per_metre;
  const i32 n = static_cast<i32>(std::floor(len / pbk::bay + 1e-4f));
  return {n, (len - static_cast<f32>(n) * pbk::bay) * 0.5f};
}

// One storey the way generate.py's continuous_storey makes it: a welded wall
// ring 0.2 m thick with the openings cut once; on each bay its module's
// dressing (generate.py facade_dressing: no substrate, no floor band); a
// continuous floor band round the top.
template <typename Pick>
void storey(put_ctx &p, const style_ids &st, const colours &col, const frame &f, f32 base, u32 salt,
            Pick pick_role) {
  const std::array<side, 4> sd = sides_of(f);
  for (i32 k = 0; k < 4; ++k) {
    const side &e = sd[static_cast<size_t>(k)];
    const f32 len = e.len / per_metre;
    const bays bs = bays_of(e);
    // The front and back run the whole length; the sides fit between them.
    const f32 lo = k < 2 ? 0.0f : pbk::ext_wall, hi = len - lo;
    f32 at = lo;
    for (i32 i = 0; i < bs.n; ++i) {
      const role r = pick_role(k, i, bs.n);
      const f32 x = bs.margin + static_cast<f32>(i) * pbk::bay;
      const vec2 origin = e.start + e.along * (x * per_metre);
      if (r != r_none)
        put(p, st.role[r], origin, base, e.out);
      glow(p, r, origin, base, e, salt + static_cast<u32>(k * 97 + i * 17), r == r_window && st.shutters);
      opening o{};
      if (!opening_of(r, o))
        continue;
      const f32 a = x + 1.0f - o.w * 0.5f, b = x + 1.0f + o.w * 0.5f;
      ring(p, e, base, at, a, 0.0f, pbk::storey, 0.0f, pbk::ext_wall, col.wall);
      ring(p, e, base, a, b, 0.0f, o.sill, 0.0f, pbk::ext_wall, col.wall);
      ring(p, e, base, a, b, o.sill + o.h, pbk::storey, 0.0f, pbk::ext_wall, col.wall);
      at = b;
    }
    ring(p, e, base, at, hi, 0.0f, pbk::storey, 0.0f, pbk::ext_wall, col.wall);
    // The floor band: 2.84-3.0 m up, 0.065 m proud of the wall and 0.025 m in
    // (generate.py: outer -0.065, inner 0.025); mitred at the corners.
    const f32 blo = k < 2 ? -0.065f : 0.025f;
    ring(p, e, base, blo, len - blo, 2.84f, pbk::storey, -0.065f, 0.025f, col.trim);
  }
}

void water_tank(kit_sink &s, const frame &f, f32 top, u32 look, f32 back) {
  const f32 side = pick01(look, 9) < 0.5f ? -1.0f : 1.0f;
  const vec2 at = f.at(side * std::max(0.0f, f.hx - 4.0f), back);
  s.extras.tanks.post(at, top + 2.5f, 2.6f, 5.0f, pick01(look, 10) < 0.7f ? rgb8(206, 208, 212) : rgb8(60, 110, 190));
  s.extras.boxes.box(at, top, {5.0f, 2.5f, 5.0f}, f.angle, rgb8(96, 98, 100));
}

// The roof's edge of a flat roof (generate.py continuous_parapet): the
// parapet 0.76 m of wall, its coping 0.08 m of trim from 0.04 m proud to
// 0.24 m in; the roof's slab inside it.
void roof_flat(put_ctx &p, const colours &col, const building &b, const frame &f, f32 top, f32 tank_chance) {
  const std::array<side, 4> sd = sides_of(f);
  for (i32 k = 0; k < 4; ++k) {
    const side &e = sd[static_cast<size_t>(k)];
    const f32 len = e.len / per_metre;
    const f32 lo = k < 2 ? 0.0f : pbk::ext_wall;
    ring(p, e, top, lo, len - lo, 0.0f, 0.76f, 0.0f, pbk::ext_wall, col.wall);
    const f32 clo = k < 2 ? -0.04f : 0.24f;
    ring(p, e, top, clo, len - clo, 0.76f, 0.84f, -0.04f, 0.24f, col.trim);
  }
  p.s.extras.boxes.box(f.c, top - pbk::slab * per_metre, {f.hx * 2.0f, pbk::slab * per_metre, f.hy * 2.0f}, f.angle,
                       col.slab);
  if (pick01(b.look, 8) < tank_chance)
    water_tank(p.s, f, top, b.look, f.hy * 0.4f);
}

// A pitched roof the way generate.py lays one (roof 'Gable'): on each bay of
// the front and the back one RoofSlope stretched from the eave to the middle
// (scale (1, run/2, run/2) of its 2 m run and 1 m rise, so the pitch stays),
// a Ridge on top at the middle, and the kit's Gable closing each end,
// stretched across the whole depth and up to the ridge. The slopes span the
// whole front, its plain ends too.
void roof_pitched(put_ctx &p, const style_ids &st, const colours &col, const frame &f, f32 top) {
  const f32 run = f.hy / per_metre; // metres from an eave to the ridge
  const f32 k = run / 2.0f;
  const std::array<side, 4> sd = sides_of(f);
  const i32 n = bays_on(sd[0].len);
  const f32 sx = sd[0].len / (static_cast<f32>(n) * 2.0f * per_metre);
  for (i32 face = 0; face < 2; ++face) {
    const side &e = sd[static_cast<size_t>(face)];
    for (i32 i = 0; i < n; ++i)
      put(p, st.slope, e.start + e.along * (static_cast<f32>(i) * sx * 2.0f * per_metre), top, e.out, sx, k, k);
  }
  const side ridge = side_of(f.c, -f.v, f.hx * 2.0f);
  for (i32 i = 0; i < n; ++i)
    put(p, st.ridge, ridge.start + ridge.along * (static_cast<f32>(i) * sx * 2.0f * per_metre), top + k * per_metre,
        -f.v, sx, 1.0f);
  for (i32 end = 2; end < 4; ++end) {
    const side &e = sd[static_cast<size_t>(end)];
    put(p, st.gable, e.start, top, e.out, e.len / (4.0f * per_metre), k);
  }
  // The top floor's ceiling under the slopes (generate.py lays Floor tiles there).
  p.s.extras.boxes.box(f.c, top - pbk::slab * per_metre, {f.hx * 2.0f - 0.2f, pbk::slab * per_metre, f.hy * 2.0f - 0.2f},
                       f.angle, col.slab);
}

bool tiled(const building &b, district_kind dk) {
  return dk == district_kind::old_quarter ? pick01(b.look, 5) < 0.6f
                                          : dk != district_kind::new_urban && pick01(b.look, 7) < 0.15f;
}

// How a building of the town is dressed, in generate.py's terms.
struct dress {
  bool shop = false;       // shopfronts along the street on the ground floor
  bool party = false;      // the side walls lean on the neighbours: blank
  bool rear_blank = false; // so does the back
  bool door = true;        // the street door in the middle of the front
  f32 windows = 0.8f;      // window_density
  f32 balconies = 0.25f;   // balcony_chance, street front only
  bool pitched = false;
  f32 tank = 0.0f;
};

// generate.py's role for a bay: the door on the street in the middle bay,
// shopfronts along the street, balconies by chance above it, windows by the
// density elsewhere; blank walls where the neighbours are.
role role_for(const dress &d, const building &b, i32 level, i32 k, i32 i, i32 n) {
  const bool street = k == 0;
  const u32 h = pbk::sub_seed(b.look, static_cast<u32>(level * 1009 + k * 131 + i * 17 + 7));
  const f32 r = static_cast<f32>(h % 10000u) / 10000.0f;
  const f32 r2 = static_cast<f32>((h / 10000u) % 10000u) / 10000.0f;
  if (level == 0 && street && d.door && i == n / 2)
    return r_door;
  if (level == 0 && street && d.shop)
    return r_shop;
  if ((k >= 2 && d.party) || (k == 1 && d.rear_blank))
    return r_wall;
  if (level > 0 && street && r2 < d.balconies)
    return r_balcony;
  return r < d.windows ? r_window : r_wall;
}

void dressed_building(const building &b, const std::string &style, const dress &d, i32 floors, bool roof,
                      kit_sink &s) {
  const frame f = frame_of(b);
  const style_ids st = ids_of(style);
  const colours col = colours_of(style);
  put_ctx p{s, colors::white, b.look};
  for (i32 fl = 0; fl < floors; ++fl)
    storey(p, st, col, f, static_cast<f32>(fl) * floor_height, static_cast<u32>(fl) * 101u,
           [&](i32 k, i32 i, i32 n) { return role_for(d, b, fl, k, i, n); });
  if (!roof)
    return;
  const f32 top = static_cast<f32>(floors) * floor_height;
  if (d.pitched)
    roof_pitched(p, st, col, f, top);
  else
    roof_flat(p, col, b, f, top, d.tank);
}

// The rules' ranges (appearance.window_density, balcony_chance) for one building.
f32 in_range(const building &b, u32 salt, f32 lo, f32 hi) { return lo + (hi - lo) * pick01(b.look, salt); }

// --- Each kind of building ---------------------------------------------------------

void street_house(const building &b, district_kind dk, i32 floors, bool roof, kit_sink &s) {
  const pbk::rules &R = pbk::load_rules();
  dress d;
  d.shop = b.business >= 0;
  d.party = b.kind == building_kind::tube_house;
  d.rear_blank = d.party;
  d.windows = in_range(b, 21, R.window_density[0], R.window_density[1]);
  d.balconies = in_range(b, 22, 0.15f, 0.4f);
  d.pitched = tiled(b, dk) || (!d.party && pick01(b.look, 13) < 0.4f);
  d.tank = d.party ? 0.75f : 0.4f;
  dressed_building(b, style_of(b, dk), d, floors, roof, s);
}

void tower(const building &b, bool hotel, i32 floors, bool roof, kit_sink &s) {
  dress d;
  d.shop = true;
  d.windows = hotel ? 0.48f : 0.32f;
  d.balconies = hotel ? 0.0f : 0.35f;
  dressed_building(b, "Modern", d, floors, roof, s);
  if (!roof)
    return;
  const frame f = frame_of(b);
  const f32 top = static_cast<f32>(floors) * floor_height;
  for (i32 k = 0; k < 3; ++k)
    s.extras.tanks.post(f.at(-f.hx * 0.5f + static_cast<f32>(k) * f.hx * 0.5f, 0.0f), top + 2.0f, 2.8f, 5.0f,
                        rgb8(206, 208, 212));
}

// Brick halls: schools, market halls, workshops and warehouses, storey by
// storey of the kit's 3 m.
void brick_hall(const building &b, i32 floors, bool roof, kit_sink &s) {
  dress d;
  const bool market = b.kind == building_kind::market_hall;
  const bool shed = b.kind == building_kind::warehouse || b.kind == building_kind::workshop;
  d.shop = market;
  d.windows = shed ? 0.18f : 0.35f;
  d.balconies = 0.0f;
  d.pitched = shed || market;
  dressed_building(b, "Brick", d, floors, roof, s);
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

std::vector<chunked> cparts;
chunked cboxes, cdetail, ctanks, cglow, cfar;

// Where each building's instances are in every batch, for leaving out the
// ones open in the cutaway and the ones the procedural plans draw.
std::vector<std::vector<std::pair<u32, u32>>> spans;

i32 batch_count() { return static_cast<i32>(cparts.size()) + 5; }

chunked &batch(i32 k) {
  if (k < static_cast<i32>(cparts.size()))
    return cparts[static_cast<size_t>(k)];
  switch (k - static_cast<i32>(cparts.size())) {
  case 0: return cboxes;
  case 1: return cdetail;
  case 2: return ctanks;
  case 3: return cglow;
  default: return cfar;
  }
}

kit_sink city_sink() {
  kit_sink s{{}, {cboxes.inst, cdetail.inst, ctanks.inst, cglow.inst}};
  for (chunked &c : cparts)
    s.parts.push_back(&c.inst);
  return s;
}

skip_list skips(const std::vector<i32> &cut, i32 k) {
  skip_list out;
  for (const i32 b : cut)
    if (b >= 0 && b < static_cast<i32>(spans.size()))
      out.push_back(spans[static_cast<size_t>(b)][static_cast<size_t>(k)]);
  // The houses the procedural plans draw (render_pbk.cpp).
  for (const i32 b : pbk_ready_list())
    if (b < static_cast<i32>(spans.size()) && std::find(cut.begin(), cut.end(), b) == cut.end())
      out.push_back(spans[static_cast<size_t>(b)][static_cast<size_t>(k)]);
  std::sort(out.begin(), out.end());
  return out;
}

} // namespace

i32 kit_module_count() { return static_cast<i32>(models.size()); }
const std::string &kit_module_id(i32 i) {
  static const std::string leaf = "leaf";
  return i < static_cast<i32>(ids.size()) ? ids[static_cast<size_t>(i)] : leaf;
}
model_handle kit_model(i32 i) {
  return i >= 0 && i < static_cast<i32>(models.size()) ? models[static_cast<size_t>(i)] : model_handle{};
}

void kit_init(context &ctx) {
  if (!ids.empty())
    return;
  const pbk::manifest &m = pbk::load_manifest();
  if (!m.loaded)
    NJIN_WARN("city kit: the procedural building kit's manifest did not load");
  render_scale = m.render_scale;
  for (const pbk::module_info &mi : m.modules) {
    index_of[mi.id] = static_cast<i32>(ids.size());
    ids.push_back(mi.id);
    // Each module's dressing, its meshes merged by material (pbk_render
    // caches them by revision and ID): the town lays its own wall ring, as
    // the kit's generator does. Roof pieces have no substrate: whole.
    models.push_back(pbk::batch_model(ctx, mi.id + "#d"));
    part_keys.push_back(mi.id + "#d");
  }
  // The leaves of the animated modules, one part each.
  leaf_part.assign(ids.size(), -1);
  for (size_t i = 0, n = ids.size(); i < n; ++i) {
    const pbk::module_rig &rig = pbk::module_rig_of(ids[i]);
    if (!rig.ok)
      continue;
    leaf_part[i] = static_cast<i32>(models.size());
    for (i32 k = 0; k < static_cast<i32>(rig.leaves.size()); ++k) {
      models.push_back(pbk::leaf_model(ctx, ids[i], k));
      part_keys.push_back(ids[i] + "#leaf" + std::to_string(k));
    }
  }
  cparts.resize(models.size());
}

void kit_shutdown(context &) {
  // The modules' models belong to pbk_render: pbk_shutdown() unloads them.
  ids.clear();
  models.clear();
  part_keys.clear();
  index_of.clear();
  leaf_part.clear();
  cparts.clear();
}

void kit_building(const building &b, const city_map &map, i32 floors, bool roof, kit_sink &out) {
  const district_kind dk =
      b.district >= 0 ? map.districts[static_cast<size_t>(b.district)].kind : district_kind::residential;
  floors = std::clamp(floors, 0, b.floors);
  switch (b.kind) {
  case building_kind::tube_house:
  case building_kind::house: street_house(b, dk, floors, roof, out); break;
  case building_kind::apartment: tower(b, false, floors, roof, out); break;
  case building_kind::hotel: tower(b, true, floors, roof, out); break;
  case building_kind::pagoda:
    if (roof)
      pagoda(b, out);
    break;
  default: brick_hall(b, floors, roof, out); break;
  }
}

void kit_build(context &ctx, const city_map &map) {
  const i32 chunks = chunk_count();
  for (i32 k = 0; k < batch_count(); ++k)
    batch(k).begin(chunks);
  spans.assign(map.buildings.size(), std::vector<std::pair<u32, u32>>(static_cast<size_t>(batch_count())));
  std::vector<std::vector<i32>> in_chunk(static_cast<size_t>(chunks));
  for (i32 i = 0; i < static_cast<i32>(map.buildings.size()); ++i)
    in_chunk[static_cast<size_t>(chunk_of(map.buildings[static_cast<size_t>(i)].box.center))].push_back(i);
  kit_sink sink = city_sink();
  for (i32 ch = 0; ch < chunks; ++ch) {
    for (i32 k = 0; k < batch_count(); ++k)
      batch(k).mark(ch);
    for (const i32 bi : in_chunk[static_cast<size_t>(ch)]) {
      auto &sp = spans[static_cast<size_t>(bi)];
      for (i32 k = 0; k < batch_count(); ++k)
        sp[static_cast<size_t>(k)].first = batch(k).inst.count();
      const building &b = map.buildings[static_cast<size_t>(bi)];
      kit_building(b, map, b.floors, true, sink);
      cfar.inst.box(b.box.center, 0,
                    {b.box.half.x * 2, b.floors * pbk::storey * units_per_metre, b.box.half.y * 2},
                    b.box.angle, {0.72f, 0.65f, 0.54f, 1});
      for (i32 k = 0; k < batch_count(); ++k)
        sp[static_cast<size_t>(k)].second = batch(k).inst.count();
    }
  }
  u32 total = 0;
  for (i32 k = 0; k < batch_count(); ++k) {
    batch(k).end();
    total += batch(k).inst.count();
    if (batch(k).inst.count() > 0)
      batch(k).inst.upload(ctx);
  }
  NJIN_INFO("city kit: %u instances (procedural kit modules and boxes) for %d buildings", total,
            static_cast<i32>(map.buildings.size()));
}

void kit_draw(context &ctx, const view_options &opt) {
  pbk::window_lighting(ctx, opt.night, opt.camera == camera_mode::observation);
  // The whole town from the kit, chunk by chunk as the camera sees it.
  const f32 r = cull().detail_r;
  i32 detailed = 0;
  for (i32 c = 0; c < chunk_count(); ++c)
    detailed += chunk_visible(c) && chunk_detailed(c, r) ? 1 : 0;
  count_detailed(detailed);
  const auto near = [r](i32 c) { return chunk_detailed(c, r); };
  const auto far = [&](i32 c) { return !near(c); };
  const i32 n = static_cast<i32>(cparts.size());
  for (i32 k = 0; k < n; ++k) {
    if (cparts[static_cast<size_t>(k)].inst.count() == 0)
      continue;
    const skip_list sk = skips(opt.cut, k);
    const pbk::module_info *mi = k < static_cast<i32>(ids.size()) ? pbk::load_manifest().find(ids[k]) : nullptr;
    const bool roof = mi && (mi->family == "RoofSlope" || mi->family == "Ridge" || mi->family == "Gable" ||
                             mi->family == "Parapet" || mi->family == "Coping");
    draw_window_chunks(ctx, cparts[k], part_keys[k], !roof, opt, &sk);
  }
  const skip_list sb = skips(opt.cut, n), sd = skips(opt.cut, n + 1), st = skips(opt.cut, n + 2),
                  sg = skips(opt.cut, n + 3);
  material3d_set(ctx, {.specular = 0.08f, .shininess = 12.0f});
  draw_chunks(ctx, cboxes, mesh3d_cube, near, &sb);
  draw_chunks(ctx, cdetail, mesh3d_cube, near, &sd);
  const skip_list sf = skips(opt.cut, n + 4);
  draw_chunks(ctx, cfar, mesh3d_cube, far, &sf);
  material3d_set(ctx, {.specular = 0.5f, .shininess = 40.0f});
  draw_chunks(ctx, ctanks, mesh3d_cylinder_low, near, &st);
  if (opt.night > 0.3f && opt.camera == camera_mode::observation) {
    material3d_set(ctx, {.unlit = true, .cast_shadows = false});
    draw_chunks(ctx, cglow, mesh3d_cube, near, &sg);
  }
  material3d_set(ctx, {});
}

void kit_cleanup(context &ctx) {
  for (i32 k = 0; k < batch_count(); ++k) {
    batch(k).inst.destroy(ctx);
    batch(k).start.clear();
  }
  spans.clear();
}

} // namespace sandtable::city
