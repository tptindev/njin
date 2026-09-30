#include "interior.h"
#include "render_facade.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// Buildings cut open: roof and upper floors taken off, so the inside shows
// from above, built from a curated slice of a PSX-style modular house kit
// (assets/models/interior/; see SOURCE.txt there) laid out by
// city/interior.*. And picking a building with the mouse.

namespace sandtable::city {

namespace {

// The kit's own grid: a piece is 4 file units wide (wallWood.glb, ...); one
// interior_cell is that many world units, so this one factor turns a piece's
// own vertices into 3D units directly.
constexpr f32 kit_scale = kit_unit * unit3d;

// The height of the floor being drawn open (world units): everything of it
// stands on this.
f32 base = 0.0f;

// The floors below the open one, whole, from outside.
instances lower_boxes, lower_detail, lower_tanks, lower_glow;
facade_batches lower{lower_boxes, lower_detail, lower_tanks, lower_glow};

struct kit_piece {
  const char *name;
  model_handle model;
  instances inst;
};
std::vector<kit_piece> kit;

// The pieces used (assets/models/interior/*.glb, a subset of the pack's
// files: see SOURCE.txt there): the structure, then the furniture
// interior_furnish.cpp sets out.
constexpr const char *kit_names[] = {
    "floorWood",   "floorTiles", "wallWood",   "cornerPillarWood", "stairsWood",    "stairsGuardWood",
    "couchSmall",  "couchBig",   "tableSmall", "table",            "chair",         "chair2",
    "tv",          "plant",      "plant3",     "carpet",           "carpet2",       "bed",
    "bed2",        "bed3",       "cabinet",    "cabinetBig",       "tableLamp",     "cabinetSink",
    "fridge",      "oven",       "toilet",     "bathroomSink",     "bathroomSink2", "bathtub",
    "shelves",     "shelves2",   "sideboard",  "product",          "product2",      "product3",
    "box",         "box2",       "pallet",     "trashBin",         "doormat",
};

i32 piece_index(const char *name) {
  for (i32 i = 0; i < static_cast<i32>(kit.size()); ++i)
    if (std::strcmp(kit[static_cast<size_t>(i)].name, name) == 0)
      return i;
  return -1;
}

// `at` and `angle` in table coordinates (world units, njin::obb::angle
// convention); `sx`/`sy`/`sz` scale the piece along its own x, y, z on top of
// kit_unit (a wall stretched to its real length and cut low, furniture
// as it is); `lift` world units up.
void put(const char *name, vec2 at, f32 angle, f32 sx = 1.0f, f32 sy = 1.0f, f32 sz = 1.0f, f32 lift = 0.0f) {
  const i32 i = piece_index(name);
  if (i < 0) {
    NJIN_WARN("interior: no piece \"%s\"", name);
    return;
  }
  kit[static_cast<size_t>(i)].inst.add3(to3d(at, (lift + base) * unit3d),
                                        {kit_scale * sx, kit_scale * sy, kit_scale * sz}, colors::white, -angle);
}

instances caps;    // the dark top of every cut wall
instances outline; // the selected building's footprint, unlit
constexpr rgba col_cut = rgb8(62, 50, 44);

// The grid's frame in the world: `a` along u from its left edge, `b` along v
// from its front edge.
struct grid_frame {
  const building &b;
  f32 gw, gd;
  vec2 at(f32 a, f32 bb) const {
    return b.box.center + b.box.axis_x() * (a - gw * 0.5f) + b.box.axis_y() * (bb - gd * 0.5f);
  }
};

// A stretch of cut wall from (a0, b0) to (a1, b1), along one axis.
void wall_run(const grid_frame &g, f32 a0, f32 b0, f32 a1, f32 b1) {
  const f32 len = std::fabs(a1 - a0) + std::fabs(b1 - b0);
  if (len < 0.5f)
    return;
  const bool along_u = std::fabs(a1 - a0) > std::fabs(b1 - b0);
  const vec2 mid = g.at((a0 + a1) * 0.5f, (b0 + b1) * 0.5f);
  const f32 angle = g.b.box.angle + (along_u ? 0.0f : 90.0f);
  // wallWood.glb is a single face 1.9 file units off its own centre line:
  // brought back onto the line, and put twice, turned round, as a face drawn
  // from behind is culled.
  for (const f32 turn : {0.0f, 180.0f}) {
    const vec2 back = from_angle(angle + turn + 90.0f) * (1.9f * kit_unit);
    put("wallWood", mid - back, angle + turn, len / interior_cell, cut_height / interior_cell, 1.0f);
  }
  caps.box(mid, base + cut_height, {len + 0.8f, 0.7f, 1.0f}, angle, col_cut);
}

// One cell's wall on an edge from (a0, b0), `len` long along u or v: whole,
// with a doorway `door` of the way along, or not there at all.
void edge_wall(const grid_frame &g, wall_kind k, f32 door, f32 a0, f32 b0, f32 len, bool along_u) {
  if (k == wall_kind::none)
    return;
  const f32 da = along_u ? 1.0f : 0.0f, db = along_u ? 0.0f : 1.0f;
  if (k == wall_kind::solid) {
    wall_run(g, a0, b0, a0 + da * len, b0 + db * len);
    return;
  }
  const f32 mid = clamp(door * len, door_width * 0.5f, len - door_width * 0.5f);
  const f32 g0 = mid - door_width * 0.5f, g1 = mid + door_width * 0.5f;
  wall_run(g, a0, b0, a0 + da * g0, b0 + db * g0);
  wall_run(g, a0 + da * g1, b0 + db * g1, a0 + da * len, b0 + db * len);
}

// The floors under floor `floor`: the body up to it, the front with the
// ground floor's shop or door, and the windows and balconies of the floors
// between; a slab to stand the open floor on.
void floors_below(const building &b, const city_map &map, i32 floor) {
  if (floor <= 0)
    return;
  const frame f = frame_of(b);
  const f32 top = static_cast<f32>(floor) * floor_height;
  const bool tube = b.kind == building_kind::tube_house || b.kind == building_kind::house;
  const rgba wall = tube ? front_color(b) : rgb8(220, 216, 206);
  lower_boxes.box(f.c, 0.0f, {f.hx * 2.0f - 0.2f, top, f.hy * 2.0f}, f.angle, tube ? concrete_color(b) : wall);
  front_face(lower, f, top, wall);
  if (b.business >= 0 && b.kind == building_kind::tube_house)
    shopfront(lower, f, business_color(map.businesses[static_cast<size_t>(b.business)].kind), top, b.look);
  else
    house_front(lower, f, b.look);
  upper_floors(lower, b, f, wall, 1, floor);
  lower_boxes.box(f.c, top - 0.8f, {f.hx * 2.0f + 0.4f, 0.8f, f.hy * 2.0f + 0.4f}, f.angle, rgb8(150, 146, 140));
}

void cut_open(const building &b, const city_map &map, i32 floor) {
  floor = std::clamp(floor, 0, std::max(0, b.floors - 1));
  floors_below(b, map, floor);
  base = static_cast<f32>(floor) * floor_height;
  const interior_layout L = build_interior(b, floor);
  const grid_frame g{b, static_cast<f32>(L.nx) * L.cell_x, static_cast<f32>(L.nz) * L.cell_z};
  const f32 cx = L.cell_x, cz = L.cell_z;

  for (i32 z = 0; z < L.nz; ++z)
    for (i32 x = 0; x < L.nx; ++x) {
      const grid_cell &c = L.cell(x, z);
      const f32 a = static_cast<f32>(x) * cx, bb = static_cast<f32>(z) * cz;
      const interior_room &room = L.rooms[static_cast<size_t>(c.room)];
      put(room.floor == floor_finish::tiles ? "floorTiles" : "floorWood", g.at(a + cx * 0.5f, bb + cz * 0.5f),
          b.box.angle, cx / interior_cell, 1.0f, cz / interior_cell);
      edge_wall(g, c.west, c.west_door, a, bb, cz, false);
      edge_wall(g, c.south, c.south_door, a, bb, cx, true);
      if (x == L.nx - 1)
        edge_wall(g, c.east, c.east_door, a + cx, bb, cz, false);
      if (z == L.nz - 1)
        edge_wall(g, c.north, c.north_door, a, bb + cz, cx, true);
    }
  // A post at each corner of the footprint, as high as the cut walls.
  for (const vec2 corner : {vec2{0.0f, 0.0f}, vec2{g.gw, 0.0f}, vec2{0.0f, g.gd}, vec2{g.gw, g.gd}}) {
    // cornerPillarWood.glb stands at (-1.915, +1.915) of its own origin.
    constexpr f32 thick = 1.4f;
    const vec2 at =
        g.at(corner.x, corner.y) - (b.box.axis_x() * -1.915f + b.box.axis_y() * 1.915f) * (kit_unit * thick);
    put("cornerPillarWood", at, b.box.angle, thick, cut_height / interior_cell + 0.02f, thick);
  }
  // The stairs: stairsWood.glb's origin is the top of the flight, which runs
  // down 4 file units over 8; raised so it climbs up from the floor.
  for (const stair_flight &s : L.stairs) {
    const f32 lift = 4.0f * kit_unit * s.sy;
    put("stairsWood", s.top, s.angle, s.sx, s.sy, s.sz, lift);
    put("stairsGuardWood", s.top, s.angle, s.sx, s.sy, s.sz, lift);
  }
  for (const furn_item &f : L.furniture)
    put(f.piece, f.pos, f.angle, f.scale, f.scale, f.scale, f.lift);
  base = 0.0f;
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

void cutaway_init(context &ctx) {
  if (!kit.empty())
    return; // already loaded: view_init() runs once, but guard against a second call
  kit.reserve(sizeof(kit_names) / sizeof(kit_names[0]));
  char path[96];
  for (const char *name : kit_names) {
    std::snprintf(path, sizeof(path), "assets/models/interior/%s.glb", name);
    const model_handle m = model_load(ctx, path);
    if (m.id == 0)
      NJIN_WARN("interior: %s did not load", path);
    kit.push_back({name, m, {}});
  }
}

void cutaway_draw(context &ctx, const city_map &map, const view_options &opt) {
  for (kit_piece &p : kit)
    p.inst.clear();
  caps.clear();
  outline.clear();
  lower_boxes.clear();
  lower_detail.clear();
  lower_tanks.clear();
  lower_glow.clear();
  for (const i32 id : opt.cut)
    if (id >= 0 && id < static_cast<i32>(map.buildings.size()))
      cut_open(map.buildings[static_cast<size_t>(id)], map, id == opt.selected || opt.around ? opt.floor : 0);
  if (opt.selected >= 0 && opt.selected < static_cast<i32>(map.buildings.size()))
    outline_of(map.buildings[static_cast<size_t>(opt.selected)]);

  material3d_set(ctx, {.specular = 0.1f, .shininess = 14.0f});
  for (kit_piece &p : kit) {
    if (p.inst.count() == 0 || p.model.id == 0)
      continue;
    p.inst.upload(ctx);
    draw_instanced3d(ctx, p.model, p.inst.buffer, 0, p.inst.count());
  }
  caps.upload(ctx);
  material3d_set(ctx, {.specular = 0.05f, .shininess = 8.0f});
  caps.draw(ctx, mesh3d_cube);
  lower_boxes.upload(ctx);
  lower_detail.upload(ctx);
  lower_tanks.upload(ctx);
  lower_glow.upload(ctx);
  material3d_set(ctx, {.specular = 0.08f, .shininess = 12.0f});
  lower_boxes.draw(ctx, mesh3d_cube);
  lower_detail.draw(ctx, mesh3d_cube);
  lower_tanks.draw(ctx, mesh3d_cylinder_low);
  if (opt.night > 0.3f) {
    material3d_set(ctx, {.unlit = true, .cast_shadows = false});
    lower_glow.draw(ctx, mesh3d_cube);
  }
  outline.upload(ctx);
  material3d_set(ctx, {.unlit = true, .cast_shadows = false});
  outline.draw(ctx, mesh3d_cube);
  material3d_set(ctx, {});
}

// Called on every city regen (view_cleanup(), right before view_build()
// rebuilds): drops the per-frame instance buffers, not the kit's models
// (loaded once by cutaway_init(), not tied to any one city).
void cutaway_cleanup(context &ctx) {
  for (kit_piece &p : kit)
    p.inst.destroy(ctx);
  caps.destroy(ctx);
  outline.destroy(ctx);
  lower_boxes.destroy(ctx);
  lower_detail.destroy(ctx);
  lower_tanks.destroy(ctx);
  lower_glow.destroy(ctx);
}

// Unloads the kit's models: game shutdown only (view_shutdown()).
void cutaway_shutdown(context &ctx) {
  for (kit_piece &p : kit)
    if (p.model.id != 0)
      model_unload(ctx, p.model);
  kit.clear();
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
