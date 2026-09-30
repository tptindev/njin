#include "interior.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// Furnishing a laid-out building, room by room: each piece set with its back
// to a wall and its front to the room, never on another piece, never in a
// doorway or on a tube house's walkway; small things (a lamp, goods) set on
// top of a table or a shelf. What each room gets is its kind's recipe below.

namespace sandtable::city {

namespace {

// --- The pieces: their real size, where their origin is, which way they face -----------
//
// File units (kit_unit world units each), measured from the models
// (assets/models/interior/). `turn` turns a model so that its back is toward
// -z of the place it is put in: pieces modelled facing +z need 0, those
// facing +x (their back, the tank or the headboard, at -x) need 90.

struct prop {
  const char *name;
  f32 sx, sy, sz; // size along the file's x, y, z
  f32 cx, low, cz; // middle of its footprint, and its lowest point
  i32 turn;
};

constexpr prop props[] = {
    {"couchSmall", 1.42f, 1.10f, 1.15f, 0.0f, 0.0f, 0.0f, 0},
    {"couchBig", 2.37f, 1.10f, 1.15f, 0.0f, 0.0f, 0.0f, 0},
    {"tableSmall", 0.88f, 0.64f, 0.63f, 0.0f, 0.0f, 0.0f, 0},
    {"table", 1.67f, 0.72f, 1.10f, 0.0f, 0.0f, 0.0f, 0},
    {"chair", 0.62f, 1.20f, 0.77f, 0.0f, 0.0f, -0.04f, 0},
    {"chair2", 0.72f, 1.24f, 0.73f, 0.0f, 0.0f, -0.02f, 0},
    {"tv", 0.66f, 0.53f, 0.59f, 0.0f, 0.0f, -0.10f, 0},
    {"plant", 0.55f, 0.95f, 0.72f, -0.04f, 0.0f, 0.02f, 0},
    {"plant3", 0.92f, 1.77f, 0.90f, 0.03f, 0.0f, 0.02f, 0},
    {"carpet", 1.90f, 0.0f, 2.82f, 0.0f, 0.0f, 0.0f, 0},
    {"carpet2", 2.21f, 0.0f, 2.42f, 0.0f, 0.0f, 0.0f, 0},
    {"bed", 3.11f, 1.18f, 2.10f, -0.03f, 0.0f, 0.0f, 90},
    {"bed2", 2.76f, 1.81f, 1.67f, 0.0f, 0.0f, 0.0f, 90},
    {"bed3", 2.46f, 1.02f, 1.23f, 0.0f, 0.0f, 0.0f, 90},
    {"cabinet", 1.56f, 2.03f, 0.56f, 0.0f, 0.0f, 0.01f, 0},
    {"cabinetBig", 1.66f, 2.51f, 0.59f, 0.0f, 0.0f, 0.05f, 0},
    {"tableLamp", 0.27f, 0.44f, 0.27f, 0.0f, 0.0f, 0.0f, 0},
    {"cabinetSink", 0.86f, 1.36f, 1.00f, 0.53f, 0.0f, 0.0f, 90},
    {"fridge", 1.48f, 2.52f, 1.09f, 0.0f, 0.0f, 0.07f, 0},
    {"oven", 0.81f, 1.22f, 1.29f, 0.02f, 0.0f, 0.0f, 90},
    {"toilet", 1.25f, 1.29f, 0.72f, -0.04f, 0.0f, 0.0f, 90},
    {"bathroomSink", 0.97f, 1.26f, 2.08f, 0.03f, 0.0f, 0.0f, 90},
    {"bathroomSink2", 0.66f, 1.21f, 0.82f, 0.06f, 0.0f, 0.0f, 90},
    {"bathtub", 1.51f, 0.90f, 2.81f, 0.05f, 0.0f, 0.0f, 90},
    {"shelves", 2.23f, 1.92f, 0.57f, 0.0f, 0.0f, 0.0f, 0},
    {"shelves2", 2.81f, 2.42f, 0.71f, 0.0f, 0.0f, 0.0f, 0},
    {"sideboard", 1.65f, 0.93f, 0.63f, 0.0f, 0.0f, 0.02f, 180},
    {"product", 0.10f, 0.24f, 0.16f, 0.0f, 0.0f, 0.0f, 0},
    {"product2", 0.14f, 0.06f, 0.16f, 0.0f, 0.0f, 0.0f, 0},
    {"product3", 0.14f, 0.24f, 0.16f, 0.0f, 0.0f, 0.0f, 0},
    {"box", 0.42f, 0.38f, 0.79f, 0.0f, 0.0f, 0.0f, 0},
    {"box2", 0.69f, 0.31f, 0.72f, 0.0f, 0.0f, 0.02f, 0},
    {"pallet", 1.41f, 0.17f, 1.41f, 0.0f, 0.0f, 0.0f, 0},
    {"trashBin", 0.42f, 0.84f, 0.56f, 0.0f, 0.0f, 0.0f, 0},
    {"doormat", 1.06f, 0.0f, 0.63f, 0.0f, 0.0f, 0.0f, 0},
};

const prop *find(const char *name) {
  for (const prop &p : props)
    if (std::strcmp(p.name, name) == 0)
      return &p;
  return nullptr;
}

constexpr f32 floor_top = 0.05f; // the kit's floor tiles, file units up

// --- Rectangles in the building's own frame ------------------------------------------------
//
// Grid-local world units: `a` along u from the grid's left edge, `b` along v
// from its front edge.

struct zone {
  f32 a0, b0, a1, b1;
  bool overlaps(const zone &o, f32 gap) const {
    return a0 < o.a1 + gap && o.a0 < a1 + gap && b0 < o.b1 + gap && o.b0 < b1 + gap;
  }
};

enum class side : u8 { back, front, left, right };

// --- Filling one room ----------------------------------------------------------------------

struct placer {
  const building &b;
  interior_layout &L;
  rng &r;
  zone room{};
  std::vector<zone> used;
  f32 gw = 0.0f, gd = 0.0f;

  placer(const building &bb, interior_layout &l, rng &rr) : b(bb), L(l), r(rr) {
    gw = static_cast<f32>(L.nx) * L.cell_x;
    gd = static_cast<f32>(L.nz) * L.cell_z;
  }

  vec2 world(f32 a, f32 bb) const {
    return b.box.center + b.box.axis_x() * (a - gw * 0.5f) + b.box.axis_y() * (bb - gd * 0.5f);
  }

  // The angle of a frame whose +z faces into the room from that wall.
  f32 facing(side s) const {
    switch (s) {
    case side::back: return b.box.angle + 180.0f;
    case side::front: return b.box.angle;
    case side::left: return b.box.angle - 90.0f;
    default: return b.box.angle + 90.0f;
    }
  }

  // A piece with its footprint's middle at (a, bb), its back toward -z of a
  // frame turned `frame` degrees; `lift` world units up.
  void emit(const prop &p, f32 a, f32 bb, f32 frame, f32 scale, f32 lift) {
    const f32 angle = frame + static_cast<f32>(p.turn);
    const f32 k = kit_unit * scale;
    const vec2 origin =
        world(a, bb) - (from_angle(angle) * p.cx + from_angle(angle + 90.0f) * p.cz) * k;
    L.furniture.push_back({p.name, origin, angle, scale, lift + (floor_top - p.low) * k});
  }

  // A piece's footprint in the room frame: `w` along the wall, `d` into the room.
  static void footprint(const prop &p, f32 scale, f32 &w, f32 &d) {
    const bool turned = p.turn == 90 || p.turn == 270;
    w = (turned ? p.sz : p.sx) * kit_unit * scale;
    d = (turned ? p.sx : p.sz) * kit_unit * scale;
  }

  bool free(const zone &z) const {
    if (z.a0 < room.a0 - 0.01f || z.b0 < room.b0 - 0.01f || z.a1 > room.a1 + 0.01f || z.b1 > room.b1 + 0.01f)
      return false;
    for (const zone &u : used)
      if (z.overlaps(u, 0.4f))
        return false;
    return true;
  }

  // Against a wall of the room, as near `at` (0 one end of the wall, 1 the
  // other) as it goes. Returns where it went, or false.
  bool against(const char *name, side s, f32 at, zone *out = nullptr, f32 scale = 1.0f) {
    const prop *p = find(name);
    if (!p)
      return false;
    f32 w, d;
    footprint(*p, scale, w, d);
    constexpr f32 margin = 0.4f;
    const bool along_a = s == side::back || s == side::front;
    const f32 lo = (along_a ? room.a0 : room.b0) + w * 0.5f + margin;
    const f32 hi = (along_a ? room.a1 : room.b1) - w * 0.5f - margin;
    if (hi < lo)
      return false;
    const f32 want = lo + (hi - lo) * clamp(at, 0.0f, 1.0f);
    // Out from the wanted spot, half a unit at a time, alternately either
    // way, until the whole wall has been tried.
    const i32 tries = std::min(800, static_cast<i32>((hi - lo) / 0.5f) * 2 + 2);
    for (i32 i = 0; i < tries; ++i) {
      const f32 step = static_cast<f32>((i + 1) / 2) * 0.5f * (i % 2 == 0 ? 1.0f : -1.0f);
      const f32 t = want + step;
      if (t < lo - 0.01f || t > hi + 0.01f)
        continue;
      zone z{};
      switch (s) {
      case side::back: z = {t - w * 0.5f, room.b1 - margin - d, t + w * 0.5f, room.b1 - margin}; break;
      case side::front: z = {t - w * 0.5f, room.b0 + margin, t + w * 0.5f, room.b0 + margin + d}; break;
      case side::left: z = {room.a0 + margin, t - w * 0.5f, room.a0 + margin + d, t + w * 0.5f}; break;
      default: z = {room.a1 - margin - d, t - w * 0.5f, room.a1 - margin, t + w * 0.5f}; break;
      }
      if (!free(z))
        continue;
      used.push_back(z);
      emit(*p, (z.a0 + z.a1) * 0.5f, (z.b0 + z.b1) * 0.5f, facing(s), scale, 0.0f);
      if (out)
        *out = z;
      return true;
    }
    return false;
  }

  // Standing free at (a, bb), turned so its front faces `s`'s way.
  bool at(const char *name, f32 a, f32 bb, side s, zone *out = nullptr, f32 scale = 1.0f) {
    const prop *p = find(name);
    if (!p)
      return false;
    f32 w, d;
    footprint(*p, scale, w, d);
    const bool along_a = s == side::back || s == side::front;
    const zone z = along_a ? zone{a - w * 0.5f, bb - d * 0.5f, a + w * 0.5f, bb + d * 0.5f}
                           : zone{a - d * 0.5f, bb - w * 0.5f, a + d * 0.5f, bb + w * 0.5f};
    if (!free(z))
      return false;
    used.push_back(z);
    emit(*p, a, bb, facing(s), scale, 0.0f);
    if (out)
      *out = z;
    return true;
  }

  // On top of something already placed: a lamp on a table, goods on a shelf.
  void on(const char *name, const zone &under, f32 top, f32 fa, f32 fb, side s, f32 scale = 1.0f) {
    const prop *p = find(name);
    if (!p)
      return;
    emit(*p, lerp(under.a0, under.a1, fa), lerp(under.b0, under.b1, fb), facing(s), scale, top);
  }

  // Lying on the floor under other things (a rug, a mat): takes no room.
  void flat(const char *name, f32 a, f32 bb, side s, f32 scale = 1.0f) {
    if (const prop *p = find(name))
      emit(*p, a, bb, facing(s), scale, 0.0f);
  }

  f32 height(const char *name, f32 scale = 1.0f) const {
    const prop *p = find(name);
    return p ? p->sy * kit_unit * scale : 0.0f;
  }
};

// --- The room's shape and its ways through -------------------------------------------------

// The room's bounding rectangle, cells of other rooms inside it marked as
// taken, and a clear space before every doorway and opening, and along a
// tube house's walkway.
bool frame_room(placer &P, i32 room_id) {
  const interior_layout &L = P.L;
  i32 x0 = L.nx, z0 = L.nz, x1 = -1, z1 = -1;
  for (i32 z = 0; z < L.nz; ++z)
    for (i32 x = 0; x < L.nx; ++x)
      if (L.cell(x, z).room == room_id) {
        x0 = std::min(x0, x);
        z0 = std::min(z0, z);
        x1 = std::max(x1, x);
        z1 = std::max(z1, z);
      }
  if (x1 < 0)
    return false;
  const f32 cx = L.cell_x, cz = L.cell_z;
  P.room = {static_cast<f32>(x0) * cx, static_cast<f32>(z0) * cz, static_cast<f32>(x1 + 1) * cx,
            static_cast<f32>(z1 + 1) * cz};
  P.used.clear();
  constexpr f32 clear = 7.0f; // how far into the room a doorway stays clear
  const f32 half_door = door_width * 0.5f + 1.0f;
  for (i32 z = z0; z <= z1; ++z)
    for (i32 x = x0; x <= x1; ++x) {
      const grid_cell &c = L.cell(x, z);
      const f32 ca = static_cast<f32>(x) * cx, cb = static_cast<f32>(z) * cz;
      if (c.room != room_id) {
        P.used.push_back({ca, cb, ca + cx, cb + cz});
        continue;
      }
      // Each of the cell's four walls, if it bounds the room.
      struct edge {
        wall_kind k;
        f32 door;
        bool bounds;
      };
      const edge west{c.west, c.west_door, x == 0 || L.cell(x - 1, z).room != room_id};
      const edge south{c.south, c.south_door, z == 0 || L.cell(x, z - 1).room != room_id};
      const edge east = x == L.nx - 1 ? edge{c.east, c.east_door, true}
                                      : edge{L.cell(x + 1, z).west, L.cell(x + 1, z).west_door,
                                             L.cell(x + 1, z).room != room_id};
      const edge north = z == L.nz - 1 ? edge{c.north, c.north_door, true}
                                       : edge{L.cell(x, z + 1).south, L.cell(x, z + 1).south_door,
                                              L.cell(x, z + 1).room != room_id};
      auto opening = [&](const edge &e, bool along_a, f32 fixed, f32 start, f32 len, f32 inward) {
        if (!e.bounds || e.k == wall_kind::solid)
          return;
        f32 s0 = start, s1 = start + len; // a gap the whole wall long...
        if (e.k == wall_kind::door) {     // ...or just the doorway
          const f32 mid = start + e.door * len;
          s0 = mid - half_door;
          s1 = mid + half_door;
        }
        const f32 in0 = fixed, in1 = fixed + inward * clear;
        if (along_a)
          P.used.push_back({s0, std::min(in0, in1), s1, std::max(in0, in1)});
        else
          P.used.push_back({std::min(in0, in1), s0, std::max(in0, in1), s1});
      };
      opening(west, false, ca, cb, cz, 1.0f);
      opening(east, false, ca + cx, cb, cz, -1.0f);
      opening(south, true, cb, ca, cx, 1.0f);
      opening(north, true, cb + cz, ca, cx, -1.0f);
    }
  // The stairs' flights, from their top back down toward the front.
  for (const stair_flight &s : L.stairs) {
    const vec2 d = s.top - P.b.box.center;
    const f32 a = dot(d, P.b.box.axis_x()) + P.gw * 0.5f, bb = dot(d, P.b.box.axis_y()) + P.gd * 0.5f;
    const f32 w = 2.0f * kit_unit * s.sx, len = 8.0f * kit_unit * s.sz;
    P.used.push_back({a - w * 0.5f - 0.5f, bb - len - 0.5f, a + w * 0.5f + 0.5f, bb + 0.5f});
  }
  if (L.passage != 0) {
    const f32 w = door_width + 3.0f;
    if (L.passage < 0)
      P.used.push_back({P.room.a0, P.room.b0, P.room.a0 + w, P.room.b1});
    else
      P.used.push_back({P.room.a1 - w, P.room.b0, P.room.a1, P.room.b1});
  }
  return true;
}

// --- What each kind of room gets -----------------------------------------------------------

const char *pick(rng &r, std::initializer_list<const char *> opts) {
  return *(opts.begin() + r.range(0, static_cast<i32>(opts.size()) - 1));
}

// The side walls, the one away from the walkway first.
side far_side(const interior_layout &L, rng &r) {
  if (L.passage < 0)
    return side::right;
  if (L.passage > 0)
    return side::left;
  return r.chance(0.5f) ? side::left : side::right;
}
side other(side s) { return s == side::left ? side::right : side::left; }

// A small table with a lamp on it, beside something.
void nightstand(placer &P, side s, f32 at) {
  zone t;
  if (P.against("tableSmall", s, at, &t, 0.7f))
    P.on("tableLamp", t, P.height("tableSmall", 0.7f), 0.5f, 0.5f, s);
}

void living(placer &P, side far) {
  zone couch;
  const char *c = P.r.chance(0.5f) ? "couchBig" : "couchSmall";
  if (P.against(c, far, 0.5f, &couch) || P.against("couchSmall", far, 0.5f, &couch) ||
      P.against("couchSmall", side::back, 0.5f, &couch)) {
    // A low table in front of it, on a rug.
    const f32 a = (couch.a0 + couch.a1) * 0.5f, bb = (couch.b0 + couch.b1) * 0.5f;
    const f32 dx = far == side::left ? 1.0f : far == side::right ? -1.0f : 0.0f;
    const f32 dz = dx == 0.0f ? -1.0f : 0.0f;
    const f32 reach = (dx != 0.0f ? couch.a1 - couch.a0 : couch.b1 - couch.b0) * 0.5f + 4.5f;
    P.flat("carpet2", a + dx * reach, bb + dz * reach, far, 0.8f);
    P.at("tableSmall", a + dx * reach, bb + dz * reach, far);
  }
  zone board;
  if (P.against("sideboard", side::back, 0.8f, &board))
    P.on("tv", board, P.height("sideboard"), 0.5f, 0.5f, side::back);
  P.against(P.r.chance(0.5f) ? "plant" : "plant3", side::front, far == side::left ? 0.0f : 1.0f);
}

void shop(placer &P, side far) {
  // Racks down the far wall, goods on them; a counter at the back.
  for (i32 i = 0; i < 4; ++i) {
    zone rack;
    const char *name = P.r.chance(0.5f) ? "shelves" : "shelves2";
    if (!P.against(name, far, i == 0 ? 0.0f : 1.0f, &rack, 0.85f))
      break;
    const f32 h = P.height(name, 0.85f);
    for (i32 k = 0; k < 4; ++k)
      P.on(pick(P.r, {"product", "product2", "product3"}), rack, h * (0.25f + 0.3f * static_cast<f32>(k % 2)),
           0.2f + 0.2f * static_cast<f32>(k), 0.5f, far, 1.2f);
  }
  zone counter;
  if (P.against("sideboard", side::back, far == side::left ? 0.0f : 1.0f, &counter))
    for (i32 k = 0; k < 3; ++k)
      P.on(pick(P.r, {"product", "product3"}), counter, P.height("sideboard"), 0.25f + 0.25f * static_cast<f32>(k),
           0.5f, side::back, 1.2f);
}

void bedroom(placer &P, side far) {
  const char *bed = pick(P.r, {"bed", "bed2", "bed3"});
  zone z;
  if (P.against(bed, side::back, far == side::left ? 0.0f : 1.0f, &z) || P.against("bed3", side::back, 0.5f, &z) ||
      P.against("bed3", far, 0.5f, &z))
    nightstand(P, side::back, far == side::left ? 1.0f : 0.0f);
  P.against(P.r.chance(0.5f) ? "cabinet" : "cabinetBig", far, 0.0f);
  P.against("plant", side::front, 0.5f);
}

void kitchen(placer &P, side far) {
  // A counter run down the far wall: sink, stove, fridge at the end.
  P.against("fridge", far, 1.0f);
  P.against("cabinetSink", far, 0.0f);
  P.against("oven", far, 0.4f);
  zone t;
  const f32 mid_a = (P.room.a0 + P.room.a1) * 0.5f, mid_b = (P.room.b0 + P.room.b1) * 0.5f;
  if (P.at("table", mid_a, mid_b, side::front, &t, 0.8f)) {
    P.at("chair", mid_a, t.b1 + 3.0f, side::back);
    P.at("chair", mid_a, t.b0 - 3.0f, side::front);
  }
  P.against("trashBin", side::front, far == side::left ? 0.0f : 1.0f);
}

void bathroom(placer &P, side far) {
  P.against("toilet", far, 0.0f);
  P.against(P.r.chance(0.5f) ? "bathroomSink2" : "bathroomSink", far, 1.0f);
  P.against("bathtub", side::back, 0.5f) || P.against("bathtub", far, 0.5f);
}

void stair_room(placer &P, side) {
  // The flight takes the far side (interior.cpp); a pot plant at its foot.
  const bool walkway_left = P.L.passage < 0;
  P.against("plant", side::front, walkway_left ? 0.6f : 0.4f);
}

void unit_room(placer &P) {
  const side s = P.r.chance(0.5f) ? side::left : side::right;
  zone z;
  if (P.against(pick(P.r, {"bed3", "bed2"}), side::back, s == side::left ? 0.0f : 1.0f, &z) ||
      P.against("bed3", s, 0.5f, &z))
    nightstand(P, side::back, s == side::left ? 1.0f : 0.0f);
  P.against("cabinet", other(s), 0.0f);
  P.against("chair", side::front, 0.5f);
}

void classroom(placer &P) {
  // The teacher's table at the back, the class facing it in rows.
  P.against("table", side::back, 0.5f);
  const f32 w = P.room.a1 - P.room.a0;
  const i32 cols = std::clamp(static_cast<i32>(w / 9.0f), 1, 4);
  for (f32 bb = P.room.b0 + 5.0f; bb < P.room.b1 - 12.0f; bb += 8.0f)
    for (i32 c = 0; c < cols; ++c) {
      const f32 a = P.room.a0 + (static_cast<f32>(c) + 0.5f) * w / static_cast<f32>(cols);
      zone desk;
      if (P.at("tableSmall", a, bb + 3.0f, side::front, &desk, 0.8f))
        P.at("chair2", a, desk.b0 - 2.2f, side::front, nullptr, 0.8f);
    }
}

void office(placer &P) {
  zone desk;
  const f32 mid_a = (P.room.a0 + P.room.a1) * 0.5f;
  if (P.against("table", side::back, 0.5f, &desk, 0.8f))
    P.at("chair", mid_a, desk.b0 - 2.5f, side::back);
  P.against("cabinet", side::left, 0.5f) || P.against("cabinet", side::right, 0.5f);
  P.against("plant", side::front, 1.0f);
}

// --- A gang's headquarters -------------------------------------------------------------

// Sảnh anh em: sofas round the walls, a low table and a rug, the TV on a
// sideboard, a table with chairs to play cards at.
void lounge(placer &P) {
  zone couch;
  if (P.against("couchBig", side::back, 0.5f, &couch) || P.against("couchSmall", side::back, 0.5f, &couch)) {
    const f32 a = (couch.a0 + couch.a1) * 0.5f;
    P.flat("carpet2", a, couch.b0 - 9.0f, side::back);
    P.at("tableSmall", a, couch.b0 - 7.0f, side::back);
  }
  P.against("couchSmall", side::left, 0.6f) || P.against("couchSmall", side::left, 0.3f);
  P.against("couchSmall", side::right, 0.6f) || P.against("couchSmall", side::right, 0.3f);
  zone board;
  if (P.against("sideboard", side::front, 0.25f, &board))
    P.on("tv", board, P.height("sideboard"), 0.5f, 0.5f, side::front);
  zone cards;
  const f32 mid_a = (P.room.a0 + P.room.a1) * 0.5f, mid_b = (P.room.b0 + P.room.b1) * 0.5f;
  if (P.at("table", mid_a + (P.room.a1 - P.room.a0) * 0.18f, mid_b - 2.0f, side::front, &cards, 0.8f)) {
    const f32 ca = (cards.a0 + cards.a1) * 0.5f;
    P.at("chair", ca, cards.b1 + 3.0f, side::back);
    P.at("chair", ca, cards.b0 - 3.0f, side::front);
  }
  P.against("plant3", side::front, 0.95f);
  P.against("plant", side::back, 0.02f);
}

// Phòng họp: a long table down the middle, chairs either side, a cabinet.
void meeting(placer &P) {
  const f32 mid_a = (P.room.a0 + P.room.a1) * 0.5f, mid_b = (P.room.b0 + P.room.b1) * 0.5f;
  zone t;
  if (P.at("table", mid_a, mid_b, side::front, &t, 1.1f)) {
    const f32 w = t.a1 - t.a0;
    for (i32 k = 0; k < 3; ++k) {
      const f32 a = t.a0 + w * (0.2f + 0.3f * static_cast<f32>(k));
      P.at("chair", a, t.b1 + 3.0f, side::back);
      P.at("chair", a, t.b0 - 3.0f, side::front);
    }
  }
  P.against("cabinet", side::back, 0.9f);
  P.against("plant", side::front, 0.05f);
  P.against("plant", side::front, 0.95f);
}

// Văn phòng đại ca: the desk before the back wall with his chair behind it,
// two chairs for callers before it; a sofa and a rug to one side, the altar
// cabinet (with offerings) to the other, plants in the corners.
void boss_office(placer &P) {
  const f32 mid_a = (P.room.a0 + P.room.a1) * 0.5f;
  zone desk;
  if (P.at("table", mid_a, P.room.b1 - 11.0f, side::front, &desk, 0.9f)) {
    P.at("chair2", mid_a, desk.b1 + 3.2f, side::front);
    P.at("chair", mid_a - 3.5f, desk.b0 - 3.2f, side::back);
    P.at("chair", mid_a + 3.5f, desk.b0 - 3.2f, side::back);
    P.on("tableLamp", desk, P.height("table", 0.9f), 0.15f, 0.5f, side::front);
  }
  zone sofa;
  if (P.against("couchBig", side::left, 0.35f, &sofa) || P.against("couchSmall", side::left, 0.35f, &sofa)) {
    P.flat("carpet", (sofa.a0 + sofa.a1) * 0.5f + 7.0f, (sofa.b0 + sofa.b1) * 0.5f, side::left, 0.8f);
    P.at("tableSmall", sofa.a1 + 5.0f, (sofa.b0 + sofa.b1) * 0.5f, side::left);
  }
  zone altar;
  if (P.against("cabinetBig", side::right, 0.7f, &altar))
    for (i32 k = 0; k < 3; ++k)
      P.on("product3", altar, P.height("cabinetBig"), 0.5f, 0.3f + 0.2f * static_cast<f32>(k), side::right, 1.2f);
  P.against("plant3", side::back, 0.0f);
  P.against("plant3", side::back, 1.0f);
  P.against("plant", side::front, 1.0f);
}

void storage(placer &P) {
  // Pallets down every wall, boxes stacked on them; the floor between clear.
  const f32 ph = P.height("pallet");
  for (const side s : {side::back, side::left, side::right})
    for (i32 i = 0; i < 6; ++i) {
      zone z;
      if (!P.against("pallet", s, i % 2 == 0 ? 0.0f : 1.0f, &z))
        break;
      const i32 stack = P.r.range(1, 3);
      f32 top = ph;
      for (i32 k = 0; k < stack; ++k) {
        const char *box = P.r.chance(0.5f) ? "box" : "box2";
        P.on(box, z, top, 0.3f + 0.4f * static_cast<f32>(k % 2), 0.5f, s, 1.3f);
        top += P.height(box, 1.3f);
      }
    }
}

void market(placer &P) {
  // Stalls in rows down the hall, goods on each; racks along the walls.
  const f32 w = P.room.a1 - P.room.a0;
  const i32 cols = std::max(1, static_cast<i32>(w / 16.0f));
  for (f32 bb = P.room.b0 + 9.0f; bb < P.room.b1 - 6.0f; bb += 14.0f)
    for (i32 c = 0; c < cols; ++c) {
      zone stall;
      const f32 a = P.room.a0 + (static_cast<f32>(c) + 0.5f) * w / static_cast<f32>(cols);
      if (!P.at("sideboard", a, bb, side::front, &stall))
        continue;
      for (i32 k = 0; k < 4; ++k)
        P.on(pick(P.r, {"product", "product2", "product3"}), stall, P.height("sideboard"),
             0.15f + 0.23f * static_cast<f32>(k), 0.5f, side::front, 1.3f);
    }
  for (i32 i = 0; i < 3; ++i)
    P.against("shelves2", side::back, i == 0 ? 0.0f : i == 1 ? 1.0f : 0.5f, nullptr, 0.9f);
}

void temple(placer &P) {
  // The altar at the back, offerings before it, plants either side, a mat.
  zone altar;
  if (!P.against("cabinetBig", side::back, 0.5f, &altar))
    return;
  const f32 a = (altar.a0 + altar.a1) * 0.5f;
  zone offering;
  if (P.at("table", a, altar.b0 - 5.0f, side::back, &offering, 0.8f))
    for (i32 k = 0; k < 3; ++k)
      P.on("product3", offering, P.height("table", 0.8f), 0.3f + 0.2f * static_cast<f32>(k), 0.5f, side::back, 1.2f);
  P.against("plant3", side::back, 0.0f);
  P.against("plant3", side::back, 1.0f);
  P.flat("carpet", a, (P.room.b0 + altar.b0) * 0.5f, side::back);
}

void corridor(placer &P) {
  P.flat("doormat", (P.room.a0 + P.room.a1) * 0.5f, P.room.b0 + 3.0f, side::front);
  P.against("plant", side::back, 0.5f);
}

} // namespace

void furnish_interior(const building &b, interior_layout &L, rng &r) {
  placer P(b, L, r);
  for (i32 ri = 0; ri < static_cast<i32>(L.rooms.size()); ++ri) {
    if (!frame_room(P, ri))
      continue;
    const side far = far_side(L, r);
    switch (L.rooms[static_cast<size_t>(ri)].kind) {
    case room_kind::living: living(P, far); break;
    case room_kind::shop: shop(P, far); break;
    case room_kind::bedroom: bedroom(P, far); break;
    case room_kind::kitchen: kitchen(P, far); break;
    case room_kind::bathroom: bathroom(P, far); break;
    case room_kind::stair: stair_room(P, far); break;
    case room_kind::hotel_room:
    case room_kind::apartment_unit: unit_room(P); break;
    case room_kind::classroom: classroom(P); break;
    case room_kind::office: office(P); break;
    case room_kind::hall_storage: storage(P); break;
    case room_kind::hall_market: market(P); break;
    case room_kind::hall_temple:
    case room_kind::altar: temple(P); break;
    case room_kind::corridor: corridor(P); break;
    case room_kind::lounge: lounge(P); break;
    case room_kind::meeting: meeting(P); break;
    case room_kind::boss_office: boss_office(P); break;
    default: break;
    }
  }
}

} // namespace sandtable::city
