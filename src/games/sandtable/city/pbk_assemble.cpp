#include "interior.h"
#include "pbk.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

// A plan made solid: the kit's modules round the facade, floor by floor;
// what the kit has not got, made here from the same plan (slabs with their
// stair voids, partitions with their doorways, lintels, the dogleg stair and
// its rails, the leaves of the doors between rooms); the boxes the physics
// and the cutaway use; and the furniture. The renderer, the physics and the
// checks all read this one assembly, so what is drawn is what is walked.

namespace sandtable::city::pbk {

namespace {

constexpr f32 raster = 0.02f; // runtime geometry's grid: the plan's 2 cm
constexpr f32 deg = pi / 180.0f;

vec2 along_of(f32 yaw) { return {std::cos(yaw * deg), -std::sin(yaw * deg)}; }
f32 yaw_of_dir(vec2 d) { return std::atan2(-d.y, d.x) / deg; }

// A grid of cells over the plan, set where a solid goes, then cut into as
// few rectangles as rows allow.
struct cells {
  i32 nx = 0, ny = 0;
  std::vector<u8> v;
  cells(f32 w, f32 d) : nx(static_cast<i32>(std::ceil(w / raster))), ny(static_cast<i32>(std::ceil(d / raster))) {
    v.assign(static_cast<size_t>(nx * ny), 0);
  }
  template <typename Fn> void scan(const polygon &poly, Fn fn) {
    std::vector<f32> xs;
    for (i32 y = 0; y < ny; ++y) {
      const f32 cy = (static_cast<f32>(y) + 0.5f) * raster;
      xs.clear();
      for (size_t i = 0, n = poly.size(); i < n; ++i) {
        const vec2 a = poly[i], b = poly[(i + 1) % n];
        if ((a.y > cy) != (b.y > cy))
          xs.push_back(a.x + (cy - a.y) * (b.x - a.x) / (b.y - a.y));
      }
      std::sort(xs.begin(), xs.end());
      for (size_t k = 0; k + 1 < xs.size(); k += 2) {
        const i32 x0 = std::max(0, static_cast<i32>(std::ceil(xs[k] / raster - 0.5f)));
        const i32 x1 = std::min(nx - 1, static_cast<i32>(std::floor(xs[k + 1] / raster - 0.5f)));
        for (i32 x = x0; x <= x1; ++x)
          fn(static_cast<size_t>(y * nx + x));
      }
    }
  }
  void fill(const polygon &p, u8 val) {
    scan(p, [&](size_t i) { v[i] = val; });
  }
  // Rectangles covering the set cells: runs per row, merged down while the
  // next row has the same run. Slivers thinner than `min_side` are dropped.
  std::vector<box2> rects(f32 min_side) const {
    struct run {
      i32 x0, x1, y0;
    };
    std::vector<box2> out;
    std::vector<run> open, next;
    const auto close = [&](const run &r, i32 y_end) {
      const box2 b{static_cast<f32>(r.x0) * raster, static_cast<f32>(r.y0) * raster,
                   static_cast<f32>(r.x1 + 1) * raster, static_cast<f32>(y_end) * raster};
      if (b.w() >= min_side - 1e-4f && b.h() >= min_side - 1e-4f)
        out.push_back(b);
    };
    for (i32 y = 0; y <= ny; ++y) {
      next.clear();
      if (y < ny) {
        for (i32 x = 0; x < nx;) {
          if (!v[static_cast<size_t>(y * nx + x)]) {
            ++x;
            continue;
          }
          i32 e = x;
          while (e + 1 < nx && v[static_cast<size_t>(y * nx + e + 1)])
            ++e;
          next.push_back({x, e, y});
          x = e + 1;
        }
      }
      // A run that goes on below keeps its start row.
      for (const run &o : open) {
        auto it = std::find_if(next.begin(), next.end(), [&](const run &n) { return n.x0 == o.x0 && n.x1 == o.x1; });
        if (it != next.end())
          it->y0 = o.y0;
        else
          close(o, y);
      }
      open = next;
    }
    return out;
  }
};

solid box_solid(const box2 &b, f32 z0, f32 z1, i32 floor, u8 kind, rgba col) {
  return {{(b.x0 + b.x1) * 0.5f, (b.y0 + b.y1) * 0.5f}, {b.w() * 0.5f, b.h() * 0.5f}, 0.0f, z0, z1, floor, kind, col};
}

// A wall-thick box along `a`-`b`, `depth` in toward `inward`.
solid wall_solid(vec2 a, vec2 b, vec2 inward, f32 depth, f32 z0, f32 z1, i32 floor, u8 kind, rgba col) {
  const vec2 mid = (a + b) * 0.5f + inward * (depth * 0.5f);
  return {mid, {distance(a, b) * 0.5f, depth * 0.5f}, angle_of(b - a), z0, z1, floor, kind, col};
}

// The old interior kit's furniture (assets/models/interior): its metres per
// file unit, as render_cutaway.cpp draws it (kit_unit world units).
constexpr f32 furn_scale = kit_unit / units_per_metre;

struct furn_model {
  vec2 size{};  // footprint, metres: x across the front, y front to back
  f32 height = 0.0f;
  bool ok = false;
};

const furn_model &furniture_size(const std::string &name) {
  static std::mutex lock;
  const std::lock_guard<std::mutex> hold(lock);
  static std::map<std::string, furn_model> cache;
  auto it = cache.find(name);
  if (it != cache.end())
    return it->second;
  furn_model &m = cache[name];
  const glb_summary g = read_glb("assets/models/interior/" + name + ".glb");
  if (g.ok && g.hi.x > g.lo.x) {
    m.size = {(g.hi.x - g.lo.x) * furn_scale, (g.hi.z - g.lo.z) * furn_scale};
    m.height = (g.hi.y - g.lo.y) * furn_scale;
    m.ok = true;
  }
  return m;
}

// What each kind of room is furnished with, the first ones first.
std::vector<const char *> furniture_for(const std::string &type) {
  if (type == "living")
    return {"couchBig", "tableSmall", "tv", "plant", "cabinet"};
  if (type == "living_dining_kitchen")
    return {"couchBig", "table", "fridge", "oven", "cabinetSink", "plant"};
  if (type == "kitchen")
    return {"fridge", "oven", "cabinetSink", "table"};
  if (type == "bedroom")
    return {"bed", "cabinet", "plant3"};
  if (type == "bathroom")
    return {"toilet", "bathroomSink", "bathtub"};
  if (type == "storage")
    return {"shelves", "shelves2", "box", "box2"};
  if (type == "utility")
    return {"shelves2", "cabinetSink", "box"};
  if (type == "office")
    return {"table", "shelves", "cabinetBig"};
  if (type == "shop")
    return {"shelves2", "shelves2", "sideboard", "shelves", "product", "plant"};
  if (type == "apartment_unit")
    return {"couchBig", "table", "bed", "fridge", "oven", "cabinetSink", "cabinet", "plant"};
  if (type == "hotel_room")
    return {"bed2", "cabinet", "tableLamp", "plant3"};
  if (type == "classroom")
    return {"table", "table", "table", "shelves", "cabinetBig"};
  if (type == "hall")
    return {"pallet", "pallet", "box", "box2", "shelves", "shelves2", "pallet"};
  if (type == "market_floor")
    return {"shelves2", "shelves2", "sideboard", "sideboard", "product", "product2", "product3", "plant"};
  return {};
}

struct builder {
  const plan &p;
  const rules &R;
  const manifest &M;
  assembly as;
  std::array<rgba, 4> pal{};
  rgba inner_wall{}, concrete{}, frame_col{};

  builder(const plan &pl) : p(pl), R(load_rules()), M(load_manifest()) {
    if (const auto *pp = R.palette(p.style))
      for (size_t k = 0; k < 4; ++k)
        pal[k] = linear_to_srgb((*pp)[k]); // shown as sRGB, like the modules
    else
      pal = {rgba{0.7f, 0.7f, 0.7f, 1}, rgba{0.9f, 0.9f, 0.9f, 1}, rgba{0.2f, 0.2f, 0.2f, 1}, rgba{0.3f, 0.3f, 0.3f, 1}};
    // Inside walls a pale plaster of the style's trim; floors in concrete.
    inner_wall = {pal[1].r * 0.95f, pal[1].g * 0.93f, pal[1].b * 0.9f, 1.0f};
    concrete = {0.62f, 0.6f, 0.57f, 1.0f};
    frame_col = pal[2];
  }

  f32 elev(i32 f) const { return static_cast<f32>(f) * storey; }

  void place(const std::string &id, vec2 at, f32 z, f32 yaw, i32 floor, i32 door = -1, bool dressing = false) {
    module_place m{id, {at.x, at.y, z}, yaw, floor, door};
    m.dressing = dressing;
    // A window with wooden shutters: they open and shut, both rigs of a corner one together.
    if (const module_info *mi = M.find(id); mi && mi->shutters && door < 0) {
      m.shutter = static_cast<i32>(as.shutters.size());
      as.shutters.push_back({id, static_cast<i32>(as.modules.size()), floor});
    }
    as.modules.push_back(m);
  }

  // --- The facade --------------------------------------------------------------------

  std::string role_id(const std::string &role, bool arc) const {
    if (!arc)
      return p.style + "/" + role;
    const i32 a = p.radius > 3.0f ? 30 : 45;
    return p.style + "/Arc" + role + "_R" + std::to_string(static_cast<i32>(p.radius)) + "_A" + std::to_string(a);
  }

  void facade() {
    const std::vector<bay_slot> bays = exterior_bays(p);
    const i32 floors = p.storeys();
    const portal *entry = nullptr;
    for (const portal &q : p.portals)
      if (q.exterior())
        entry = &q;
    // roles[f][bay]: Wall, Window, Shopfront, DoorRigged.
    std::vector<std::vector<std::string>> roles(static_cast<size_t>(floors), std::vector<std::string>(bays.size(), "Wall"));
    i32 door_bay = -1;
    for (i32 f = 0; f < floors; ++f)
      for (size_t i = 0; i < bays.size(); ++i) {
        const bay_slot &b = bays[i];
        std::string &role = roles[static_cast<size_t>(f)][i];
        const room *behind = room_behind(p, f, b);
        const bool front = std::find(p.frontages.begin(), p.frontages.end(), b.side) != p.frontages.end();
        if (f == 0 && entry && !b.arc && distance(entry->center, b.center - b.out * (ext_wall * 0.5f)) < 0.3f) {
          role = "DoorRigged";
          door_bay = static_cast<i32>(i);
          continue;
        }
        if (f == 0 && entry && b.arc && b.index == b.count / 2 && entry->axis == "tangent") {
          role = "DoorRigged";
          continue;
        }
        bool set = false;
        for (const aperture &a : p.apertures)
          if (a.floor == f && distance(a.center, b.center) < 0.3f) {
            role = a.module_id.find("Balcony") != std::string::npos ? "Balcony" :
                a.module_id.find("Shopfront") != std::string::npos ? "Shopfront" : "Window";
            set = true;
          }
        if (set)
          continue;
        if (f == 0 && front && behind && behind->type == "shop")
          role = "Shopfront";
        // Openings come from the plan. Adding random windows here used to
        // destroy the designed rhythm and bypass glazing/lighting data.
      }
    // Plan apertures carry the shared vertical axes and room constraints.
    (void)door_bay;

    const size_t n = bays.size();

    // The bays.
    for (i32 f = 0; f < floors; ++f)
      for (size_t i = 0; i < n; ++i) {
        const bay_slot &b = bays[i];
        const std::string &role = roles[static_cast<size_t>(f)][i];
        const f32 z0 = elev(f), z1 = elev(f) + storey;
        if (role == "DoorRigged" && b.arc) {
          // Planar on the curve's middle tangent (assembly.rounded_corner_entrance).
          const f32 yaw = (b.yaw + b.end_yaw) * 0.5f;
          const vec2 along = along_of(yaw);
          const vec2 origin = b.center - along * 1.0f;
          street_door(origin, yaw, b.out);
          // The curve's piece is 2.07 m of chord, the door 2 m flat: the bit
          // left at each end is bridged with plain wall.
          const vec2 e0 = origin, e1 = origin + along * 2.0f;
          for (const auto &[from, to] : {std::pair<vec2, vec2>{b.origin, e0}, std::pair<vec2, vec2>{e1, b.end}})
            if (distance(from, to) > 0.01f) {
              solid s = wall_solid(from, to, -b.out, ext_wall, z0, z1, f, sk_facade, pal[0]);
              as.solids.push_back(s);
            }
          as.notes.push_back("the street door on the curve is planar; 4 cm of chord each side bridged with plain wall");
          continue;
        }
        if (role == "DoorRigged") {
          street_door(b.origin, b.yaw, b.out);
          continue;
        }
        if (b.arc) {
          place(role_id(role, true), b.origin, z0, b.yaw, f);
        } else {
          // As generate.py: the dressing on the bay, the welded ring round it.
          place(role_id(role, false), b.origin, z0, b.yaw, f, -1, true);
          shell_bay(b, role, z0, f);
        }
        as.solids.push_back(wall_solid(b.origin, b.end, -b.out, ext_wall, z0, z1, f, sk_exterior, pal[0]));
        if (role == "Balcony") {
          // The asset draws its slab and rails; use separate thin collision.
          const vec2 along = normalize(b.end - b.origin);
          as.solids.push_back({b.center + b.out * 0.6f, {1.0f, 0.6f}, angle_of(along),
            z0 - slab, z0, f, sk_collision, concrete});
          as.solids.push_back(wall_solid(b.origin + b.out * 1.2f, b.end + b.out * 1.2f,
            -b.out, 0.06f, z0, z0 + 1.1f, f, sk_collision, frame_col));
          for (const vec2 edge : {b.origin, b.end})
            as.solids.push_back(wall_solid(edge, edge + b.out * 1.2f, along, 0.06f,
              z0, z0 + 1.1f, f, sk_collision, frame_col));
        }
      }

    // Base / body / crown: continuous datums, including the entrance bay.
    // Upper floor joints are quiet; the street-level lintel and roof carry
    // the stronger horizontal accents. Keep all dressing out of openings.
    for (size_t i = 0; i < bays.size(); ++i) {
      const auto &b = bays[i];
      if (b.arc) continue; // the curved assets already contain their trim
      const bool street = std::find(p.frontages.begin(), p.frontages.end(), b.side) != p.frontages.end();
      const f32 ext0 = b.first ? 0.065f : 0.0f, ext1 = b.last ? 0.065f : 0.0f;
      if (roles[0][i] == "Wall" || roles[0][i] == "Window")
        shell_box(b, 0, bay, 0.02f, 0.28f, -0.035f, 0.025f, 0, concrete);
      // End piers frame the elevation; no pilaster on every 2 m tile.
      // They stay in the smallest shopfront's 15 cm solid margin.
      const rgba pier = {pal[0].r * 0.92f, pal[0].g * 0.92f, pal[0].b * 0.92f, 1};
      for (i32 f = 0; f < floors; ++f) {
        const f32 bottom = f == 0 ? 0.28f : elev(f);
        if (b.first) shell_box(b, 0, 0.12f, bottom, elev(f + 1), -0.025f, 0.025f, f, pier);
        if (b.last) shell_box(b, bay - 0.12f, bay, bottom, elev(f + 1), -0.025f, 0.025f, f, pier);
        const bool crown = f == floors - 1;
        const bool base = f == 0 && street && floors > 1;
        const f32 height = crown ? 0.18f : base ? 0.14f : 0.045f;
        const f32 proud = crown ? 0.10f : base ? 0.065f : 0.025f;
        shell_box(b, -ext0, bay + ext1, elev(f + 1) - height, elev(f + 1),
                  -proud, 0.025f, f, crown || base ? pal[1] : pal[0]);
      }
    }

    // The roof's edge: generate.py's continuous parapet (0.76 m of wall, its
    // coping 0.08 m of trim from 0.04 m proud to 0.24 m in) on the straight
    // sides, the kit's arc parapets round the curve.
    const f32 top = elev(floors);
    for (size_t i = 0; i < n; ++i) {
      const bay_slot &b = bays[i];
      if (b.arc) {
        place(role_id("Parapet", true), b.origin, top, b.yaw, -1);
        continue;
      }
      const f32 ext0 = b.first ? 0.04f : 0.0f, ext1 = b.last ? 0.04f : 0.0f;
      shell_box(b, 0.0f, bay, top, top + 0.76f, 0.0f, ext_wall, floors, pal[0]);
      shell_box(b, -ext0, bay + ext1, top + 0.76f, top + 0.84f, -0.04f, 0.24f, floors, pal[1]);
    }
  }

  // A box of the shell on bay `b`: `a`..`e` metres along it, `z0`..`z1` up,
  // `o`..`i` across (0 the outer face, + inward).
  void shell_box(const bay_slot &b, f32 a, f32 e, f32 z0, f32 z1, f32 o, f32 i, i32 floor, rgba col) {
    if (e - a < 0.005f || z1 - z0 < 0.005f)
      return;
    const vec2 along = normalize(b.end - b.origin);
    const vec2 c = b.origin + along * ((a + e) * 0.5f) - b.out * ((o + i) * 0.5f);
    as.solids.push_back({c, {(e - a) * 0.5f, (i - o) * 0.5f}, angle_of(along), z0, z1, floor, sk_shell, col});
  }

  // The welded ring on one bay with its opening cut (generate.py APERTURES),
  // and the floor band at its top, mitred at a corner.
  void shell_bay(const bay_slot &b, const std::string &role, f32 z0, i32 f) {
    f32 w = 0.0f, h = 0.0f, sill = 0.0f;
    if (role == "Window") {
      w = 1.18f;
      h = 1.50f;
      sill = 0.90f;
    } else if (role == "Shopfront") {
      w = 1.70f;
      h = 2.48f;
    } else if (role == "Balcony") {
      w = 1.60f;
      h = 2.40f;
    }
    if (w > 0.0f) {
      const f32 a = 1.0f - w * 0.5f, e = 1.0f + w * 0.5f;
      shell_box(b, 0.0f, a, z0, z0 + storey, 0.0f, ext_wall, f, pal[0]);
      shell_box(b, e, bay, z0, z0 + storey, 0.0f, ext_wall, f, pal[0]);
      shell_box(b, a, e, z0, z0 + sill, 0.0f, ext_wall, f, pal[0]);
      shell_box(b, a, e, z0 + sill + h, z0 + storey, 0.0f, ext_wall, f, pal[0]);
    } else {
      shell_box(b, 0.0f, bay, z0, z0 + storey, 0.0f, ext_wall, f, pal[0]);
    }
  }

  void street_door(vec2 origin, f32 yaw, vec2 out) {
    const std::string id = p.style + "/DoorRigged";
    const module_info *mi = M.find(id);
    door d;
    for (const portal &q : p.portals)
      if (q.exterior())
        d.portal = q.id;
    d.rigged = true;
    d.module_id = id;
    d.floor = 0;
    const vec3 h = module_to_plan(mi ? mi->socket("hinge") : vec3{0.525f, 0.0f, -0.1f}, {origin.x, origin.y, 0.0f}, yaw);
    d.hinge = {h.x, h.y};
    d.closed_angle = yaw;
    // The bone's own turn from the shut pose to the open one: the leaf swings inward.
    const module_rig &rig = module_rig_of(id);
    const door_clip clip = clip_of(mi);
    d.swing = rig.ok ? rig.turn(0, clip.open_pose, clip.shut_pose) : 90.0f;
    d.width = 0.95f;
    d.thickness = 0.06f;
    d.z0 = 0.03f;
    d.z1 = 2.32f;
    const i32 di = static_cast<i32>(as.doors.size());
    as.doors.push_back(d);
    place(id, origin, 0.0f, yaw, 0, di);
    // The frame round the opening (aperture 1.05 m in the middle of 2 m).
    const vec2 along = along_of(yaw);
    const vec2 in = -out;
    const vec2 a = origin, j0 = origin + along * 0.475f, j1 = origin + along * 1.525f, b = origin + along * 2.0f;
    as.solids.push_back(wall_solid(a, j0, in, ext_wall, 0.0f, storey, 0, sk_exterior, pal[0]));
    as.solids.push_back(wall_solid(j1, b, in, ext_wall, 0.0f, storey, 0, sk_exterior, pal[0]));
    as.solids.push_back(wall_solid(j0, j1, in, ext_wall, 2.35f, storey, 0, sk_exterior, pal[0]));
  }

  // --- Inside -----------------------------------------------------------------------------

  void slabs() {
    const polygon inner = footprint_inset(p, ext_wall, 64);
    const i32 floors = p.storeys();
    for (i32 f = 0; f <= floors; ++f) {
      cells c(p.width, p.depth);
      c.fill(inner, 1);
      if (f < floors)
        for (const polygon &v : p.floors[static_cast<size_t>(f)].voids)
          c.fill(v, 0);
      // The ground floor a little over the street; the roof's slab under the parapet.
      const f32 top = f == 0 ? 0.02f : elev(f);
      for (const box2 &b : c.rects(raster))
        as.solids.push_back(box_solid(b, top - slab, top, f, sk_slab, f == floors ? pal[3] : concrete));
    }
  }

  void partitions() {
    const polygon inner = footprint_inset(p, ext_wall, 64);
    for (i32 f = 0; f < p.storeys(); ++f) {
      const floor_plan &fl = p.floors[static_cast<size_t>(f)];
      cells c(p.width, p.depth);
      c.fill(inner, 1);
      for (const room &r : fl.rooms)
        c.fill(r.poly, 0);
      const f32 z0 = elev(f), z1 = elev(f) + storey - slab;
      for (const portal &q : p.portals) {
        if (q.vertical() || q.exterior() || q.floor_from != f)
          continue;
        const vec2 n = normalize(q.normal), along{-n.y, n.x};
        const vec2 h{q.aperture * 0.5f, partition * 0.5f + 0.03f};
        const polygon hole = {q.center - along * h.x - n * h.y, q.center + along * h.x - n * h.y,
                              q.center + along * h.x + n * h.y, q.center - along * h.x + n * h.y};
        c.fill(hole, 0);
        // The lintel over it.
        as.solids.push_back({q.center, {q.aperture * 0.5f, partition * 0.5f}, angle_of(along), z0 + q.height, z1, f,
                             sk_lintel, inner_wall});
      }
      for (const box2 &b : c.rects(0.06f))
        as.solids.push_back(box_solid(b, z0, z1, f, sk_partition, inner_wall));
    }
  }

  // A straight flight: the kit's own Stair module (1.2 m wide, 15 risers,
  // 3.75 m run) drawn on each floor but the top, its steps as collision; a
  // rail on the open side, and round the hole in the slab above.
  void straight_stair() {
    const stair_core &s = p.stair;
    const i32 floors = p.storeys();
    const std::string id = p.style + "/Stair";
    const module_info *mi = M.find(id);
    const box2 &fl = s.lower_flight;
    const vec2 foot{(fl.x0 + fl.x1) * 0.5f, fl.y0};
    // The module climbs toward its -Z: its +Z points back at the street.
    const f32 yaw = yaw_facing({0.0f, -1.0f});
    const vec3 entry = mi ? mi->socket("entry") : vec3{1.0f, 0.0f, 0.0f};
    const vec3 e = module_to_plan(entry, {0.0f, 0.0f, 0.0f}, yaw);
    const bool open_east = fl.x0 < p.width * 0.5f; // the way past is on the other side
    for (i32 f = 0; f + 1 < floors; ++f) {
      const f32 z = elev(f);
      as.modules.push_back({id, {foot.x - e.x, foot.y - e.y, z}, yaw, f, -1, true});
      for (i32 i = 0; i < s.split[0]; ++i) {
        const f32 top = z + s.riser * static_cast<f32>(i + 1);
        const box2 b{fl.x0, fl.y0 + s.tread * static_cast<f32>(i), fl.x1, fl.y0 + s.tread * static_cast<f32>(i + 1)};
        as.solids.push_back(box_solid(b, z, top, f, sk_collision, concrete));
      }
      // A rail up the open side of the flight.
      const f32 rx = open_east ? fl.x1 + 0.02f : fl.x0 - 0.02f;
      as.solids.push_back(box_solid({rx - 0.025f, fl.y0 + 0.5f, rx + 0.025f, fl.y1}, z + 0.9f, z + storey, f, sk_rail,
                                    frame_col));
    }
    for (i32 f = 1; f < floors; ++f) {
      const f32 z = elev(f);
      const f32 rx = open_east ? fl.x1 + 0.06f : fl.x0 - 0.06f;
      const f32 vy0 = fl.y0 + 0.6f;
      as.solids.push_back(box_solid({rx - 0.025f, vy0, rx + 0.025f, fl.y1}, z, z + 1.0f, f, sk_rail, frame_col));
      if (f == floors - 1)
        as.solids.push_back(box_solid({std::min(fl.x0, rx), vy0 - 0.05f, std::max(fl.x1, rx), vy0}, z, z + 1.0f, f,
                                      sk_rail, frame_col));
    }
  }

  void stair() {
    if (!p.stair.on)
      return;
    if (p.stair.type == "straight") {
      straight_stair();
      return;
    }
    const stair_core &s = p.stair;
    const i32 floors = p.storeys();
    const f32 waist = 0.36f;
    for (i32 f = 0; f + 1 < floors; ++f) {
      const f32 e = elev(f);
      // Lower flight: up from the lower landing toward the back.
      for (i32 i = 0; i < s.split[0]; ++i) {
        const f32 top = e + s.riser * static_cast<f32>(i + 1);
        const box2 b{s.lower_flight.x0, s.lower_flight.y0 + s.tread * static_cast<f32>(i), s.lower_flight.x1,
                     s.lower_flight.y0 + s.tread * static_cast<f32>(i + 1)};
        as.solids.push_back(box_solid(b, std::max(e, top - waist), top, f, sk_stair, concrete));
      }
      const f32 turn = e + s.riser * static_cast<f32>(s.split[0]);
      as.solids.push_back(box_solid(s.turn_landing, turn - slab, turn, f, sk_stair, concrete));
      // Upper flight: from the turn landing back toward the front.
      for (i32 j = 0; j < s.split[1]; ++j) {
        const f32 top = turn + s.riser * static_cast<f32>(j + 1);
        const box2 b{s.upper_flight.x0, s.upper_flight.y1 - s.tread * static_cast<f32>(j + 1), s.upper_flight.x1,
                     s.upper_flight.y1 - s.tread * static_cast<f32>(j)};
        as.solids.push_back(box_solid(b, top - waist, top, f, sk_stair, concrete));
      }
      // The balustrade in the gap between the flights, up to the floor above.
      const bool lower_west = s.lower_flight.x0 < s.upper_flight.x0;
      const f32 gx0 = lower_west ? s.lower_flight.x1 : s.upper_flight.x1;
      const f32 gx1 = lower_west ? s.upper_flight.x0 : s.lower_flight.x0;
      as.solids.push_back(box_solid({gx0 + 0.07f, s.lower_flight.y0, gx1 - 0.07f, s.turn_landing.y0}, e, e + storey,
                                    f, sk_rail, frame_col));
    }
    // Rails round the void on the floors above.
    for (i32 f = 1; f < floors; ++f) {
      const f32 e = elev(f);
      const bool west = s.lower_flight.x0 < s.upper_flight.x0;
      const f32 notch_x = west ? s.lower_flight.x1 + 0.08f : s.lower_flight.x0 - 0.08f;
      const f32 vy0 = s.lower_flight.y0 - 0.12f, vy1 = s.upper_flight.y0;
      as.solids.push_back(box_solid({notch_x - 0.025f, vy0, notch_x + 0.025f, vy1}, e, e + 1.0f, f, sk_rail, frame_col));
      if (f == floors - 1) {
        // Nothing goes on up from the top floor: rail its edge over the lower flight.
        const f32 x0 = west ? s.lower_flight.x0 - 0.12f : notch_x, x1 = west ? notch_x : s.lower_flight.x1 + 0.12f;
        as.solids.push_back(box_solid({x0, vy0 - 0.05f, x1, vy0}, e, e + 1.0f, f, sk_rail, frame_col));
      }
    }
  }

  // The leaves of the doors between rooms: hinged at one end of the opening,
  // swinging into the room the door leads to, wherever the sweep stays clear.
  void interior_doors() {
    for (const portal &q : p.portals) {
      if (q.vertical() || q.exterior() || q.kind != "door")
        continue;
      const floor_plan &fl = p.floors[static_cast<size_t>(q.floor_from)];
      const vec2 n = normalize(q.normal), along{-n.y, n.x};
      const room *to = p.find_room(q.floor_to, q.to);
      const room *from = p.find_room(q.floor_from, q.from);
      // The side `to` is on.
      f32 side = 1.0f;
      if (to && !point_in_polygon(to->poly, q.center + n * 0.3f))
        side = -1.0f;
      door best;
      bool found = false;
      // Into the room, never across the corridor; between the corridor and
      // the stair, into the stair's landing.
      const auto rank = [](const room *r) { return !r ? 9 : r->type == "corridor" ? 2 : r->type == "stair" ? 1 : 0; };
      const room *order[2] = {to, from};
      if (rank(from) < rank(to))
        std::swap(order[0], order[1]);
      for (const room *into : order) {
        if (!into || found)
          continue;
        const f32 sd = into == to ? side : -side;
        // The hinge at the end nearer the room's side wall first, so the open
        // leaf lies against it instead of across the way in.
        const box2 rb = bounds_of(into->poly);
        const auto wall_gap = [&](f32 end) {
          const vec2 e = q.center + along * (end * q.aperture * 0.5f);
          return std::min({e.x - rb.x0, rb.x1 - e.x, e.y - rb.y0, rb.y1 - e.y}) < 0.0f
                     ? 1e9f
                     : std::fabs(dot(along, {1, 0})) > 0.5f ? std::min(e.x - rb.x0, rb.x1 - e.x)
                                                            : std::min(e.y - rb.y0, rb.y1 - e.y);
        };
        const f32 first = wall_gap(-1.0f) <= wall_gap(1.0f) ? -1.0f : 1.0f;
        for (const f32 end : {first, -first}) {
          door d;
          d.portal = q.id;
          d.floor = q.floor_from;
          d.width = q.aperture - 0.1f;
          d.thickness = 0.04f;
          d.z0 = 0.01f;
          d.z1 = 2.1f;
          d.hinge = q.center + along * (end * (q.aperture * 0.5f - 0.05f)) + n * (sd * partition * 0.5f);
          d.closed_angle = yaw_of_dir(along * -end);
          // Open: the leaf turned to point into the room.
          const vec2 open_dir = n * sd;
          const f32 open_yaw = yaw_of_dir(open_dir);
          f32 sw = open_yaw - d.closed_angle;
          while (sw > 180.0f)
            sw -= 360.0f;
          while (sw < -180.0f)
            sw += 360.0f;
          d.swing = sw;
          // Clear of the walls round it, and of the doors already hung.
          bool ok = true;
          for (i32 a = 1; a <= 12 && ok; ++a)
            for (i32 k = 2; k <= 8 && ok; ++k) {
              const vec2 dir = along_of(d.closed_angle + d.swing * static_cast<f32>(a) / 12.0f);
              const vec2 pt = d.hinge + dir * (d.width * static_cast<f32>(k) / 8.0f);
              bool inside = false;
              for (const room &r : fl.rooms)
                inside = inside || point_in_polygon(r.poly, pt);
              ok = inside;
              for (const door &o : as.doors)
                if (o.floor == d.floor && distance(o.hinge, pt) < o.width + 0.05f) {
                  const vec2 od = along_of(o.closed_angle + o.swing * 0.5f);
                  ok = ok && distance(o.hinge + od * (o.width * 0.5f), pt) > o.width * 0.75f;
                }
            }
          if (ok) {
            best = d;
            found = true;
            break;
          }
          if (!found && end == first && into == to)
            best = d; // the fallback the check will report on
        }
      }
      as.doors.push_back(best);
    }
  }

  // --- Furniture ------------------------------------------------------------------------------

  // Whether a footprint is clear: in its room, off the portals and door
  // sweeps, apart from what is already there.
  bool clear(const room &r, i32 f, const furniture &fu) const {
    const vec2 u = from_angle(fu.angle), v{-u.y, u.x};
    for (const vec2 c : {fu.c + u * fu.half.x + v * fu.half.y, fu.c - u * fu.half.x + v * fu.half.y,
                         fu.c + u * fu.half.x - v * fu.half.y, fu.c - u * fu.half.x - v * fu.half.y})
      if (!point_in_polygon(r.poly, c) || distance_to_outline(r.poly, c) < 0.0f)
        return false;
    const auto inside = [&](vec2 q, f32 pad) {
      const vec2 d = q - fu.c;
      return std::fabs(dot(d, u)) <= fu.half.x + pad && std::fabs(dot(d, v)) <= fu.half.y + pad;
    };
    for (const portal &q : p.portals)
      if (!q.vertical() && q.floor_from == f && inside(q.center, R.portal_keepout + q.aperture * 0.5f + 0.05f))
        return false;
    // The stair's walking area stays clear (no props in circulation anyway).
    for (const door &d : as.doors) {
      if (d.floor != f)
        continue;
      for (i32 a = 0; a <= 12; ++a)
        for (i32 k = 1; k <= 8; ++k) {
          const vec2 pt = d.hinge + along_of(d.closed_angle + d.swing * static_cast<f32>(a) / 12.0f) *
                                         (d.width * static_cast<f32>(k) / 8.0f);
          if (inside(pt, 0.05f))
            return false;
        }
    }
    for (const furniture &o : as.furniture) {
      if (o.floor != f)
        continue;
      const vec2 ou = from_angle(o.angle), ov{-ou.y, ou.x};
      // Separating axes of two rectangles.
      bool apart = false;
      for (const vec2 ax : {u, v, ou, ov}) {
        const f32 ra = fu.half.x * std::fabs(dot(u, ax)) + fu.half.y * std::fabs(dot(v, ax));
        const f32 rb = o.half.x * std::fabs(dot(ou, ax)) + o.half.y * std::fabs(dot(ov, ax));
        apart = apart || std::fabs(dot(o.c - fu.c, ax)) > ra + rb + 0.05f;
      }
      if (!apart)
        return false;
    }
    return true;
  }

  void furnish() {
    const preset_rule *P = R.preset(p.space_preset);
    const f32 limit = P ? P->furniture_max : 0.25f;
    rng r(sub_seed(p.seed, R.salt_furniture));
    for (i32 f = 0; f < p.storeys(); ++f)
      for (const room &rm : p.floors[static_cast<size_t>(f)].rooms) {
        const std::vector<const char *> list = furniture_for(rm.type);
        if (list.empty())
          continue;
        const box2 b = bounds_of(rm.poly);
        f32 cover = 0.0f;
        for (const char *name : list) {
          const furn_model fm = furniture_size(name);
          if (!fm.ok)
            continue;
          const f32 area = fm.size.x * fm.size.y;
          if (cover + area > limit * rm.area * 0.9f)
            continue;
          // Backs to a wall: try the four sides of the room from a random start.
          bool placed = false;
          const i32 side0 = r.range(0, 3);
          for (i32 s = 0; s < 4 && !placed; ++s) {
            const i32 side = (side0 + s) % 4;
            // The wall's line, the way into the room, the run along it.
            vec2 a, dir, in;
            f32 len;
            switch (side) {
            case 0: a = {b.x0, b.y0}; dir = {1, 0}; in = {0, 1}; len = b.w(); break;
            case 1: a = {b.x1, b.y0}; dir = {0, 1}; in = {-1, 0}; len = b.h(); break;
            case 2: a = {b.x1, b.y1}; dir = {-1, 0}; in = {0, -1}; len = b.w(); break;
            default: a = {b.x0, b.y1}; dir = {0, -1}; in = {1, 0}; len = b.h(); break;
            }
            const f32 start = r.range(0.0f, 1.0f);
            for (i32 k = 0; k < 24 && !placed; ++k) {
              const f32 t = std::fmod(start + static_cast<f32>(k) / 24.0f, 1.0f);
              const f32 along = fm.size.x * 0.5f + 0.05f + t * std::max(0.0f, len - fm.size.x - 0.1f);
              furniture fu;
              fu.model = name;
              fu.floor = f;
              fu.room = rm.id;
              // The model's +Z (its front) into the room: its x along the wall.
              fu.angle = -yaw_facing(in);
              fu.half = fm.size * 0.5f;
              fu.c = a + dir * along + in * (fm.size.y * 0.5f + 0.03f);
              fu.height = fm.height;
              if (clear(rm, f, fu)) {
                as.furniture.push_back(fu);
                cover += area;
                placed = true;
              }
            }
          }
        }
      }
    // Props must not cut a room off: drop the last ones of a room until every
    // room is reached (rules: reduce optional furniture).
    // A room the walls already cut off is the plan's fault, not the props':
    // leave that to the check.
    {
      assembly bare = as;
      bare.furniture.clear();
      if (!unreachable_rooms(p, {}, &bare).empty())
        return finish_props();
    }
    for (i32 guard = 0; guard < 64; ++guard) {
      const std::vector<std::string> lost = unreachable_rooms(p, {}, &as);
      if (lost.empty() || as.furniture.empty())
        break;
      bool dropped = false;
      for (size_t k = as.furniture.size(); k-- > 0 && !dropped;)
        for (const std::string &id : lost)
          if (as.furniture[k].room == id) {
            as.furniture.erase(as.furniture.begin() + static_cast<std::ptrdiff_t>(k));
            dropped = true;
            break;
          }
      if (!dropped) {
        // A prop elsewhere blocks the way: the newest goes.
        as.furniture.pop_back();
      }
    }
    finish_props();
  }

  void finish_props() {
    for (const furniture &fu : as.furniture)
      as.solids.push_back({fu.c, fu.half, fu.angle, elev(fu.floor), elev(fu.floor) + std::max(0.3f, fu.height),
                           fu.floor, sk_prop, colors::white});
  }
};

} // namespace

assembly assemble(const plan &p) {
  builder b(p);
  b.facade();
  b.slabs();
  b.partitions();
  b.stair();
  b.interior_doors();
  b.furnish();
  if (p.generator["roof"].is(json_value::string) && std::string(p.generator["roof"].str) != "Flat")
    b.as.notes.push_back("roof " + p.generator["roof"].str + " built flat: the kit's gable spans 4 m");

  return std::move(b.as);
}

} // namespace sandtable::city::pbk
