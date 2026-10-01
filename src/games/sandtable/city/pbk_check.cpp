#include "pbk.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <deque>

// The hard rules of building_rules.json::validation, checked on a plan and,
// when given, on its assembly. Walkable ground is a raster: a room graph
// that joins up proves nothing until an actor's centre, kept the actor's
// radius and the wall margin clear of every wall, door leaf and prop, can
// get from the street door to every room (rules/README.vi.md).

namespace sandtable::city::pbk {

namespace {

constexpr f32 nav_cell = 0.05f;

std::string fmt(const char *f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char *f, ...) {
  char buf[256];
  va_list a;
  va_start(a, f);
  std::vsnprintf(buf, sizeof(buf), f, a);
  va_end(a);
  return buf;
}

// Fills the cells whose centres are inside `poly`, row by row.
template <typename Fn> void scan_polygon(const polygon &poly, f32 cell, i32 nx, i32 ny, Fn fn) {
  std::vector<f32> xs;
  for (i32 y = 0; y < ny; ++y) {
    const f32 cy = (static_cast<f32>(y) + 0.5f) * cell;
    xs.clear();
    for (size_t i = 0, n = poly.size(); i < n; ++i) {
      const vec2 a = poly[i], b = poly[(i + 1) % n];
      if ((a.y > cy) != (b.y > cy))
        xs.push_back(a.x + (cy - a.y) * (b.x - a.x) / (b.y - a.y));
    }
    std::sort(xs.begin(), xs.end());
    for (size_t k = 0; k + 1 < xs.size(); k += 2) {
      const i32 x0 = std::max(0, static_cast<i32>(std::ceil(xs[k] / cell - 0.5f)));
      const i32 x1 = std::min(nx - 1, static_cast<i32>(std::floor(xs[k + 1] / cell - 0.5f)));
      for (i32 x = x0; x <= x1; ++x)
        fn(y * nx + x);
    }
  }
}

// A rectangle turned `angle_deg` (math angle in the plan): cells inside.
template <typename Fn> void scan_obb(vec2 c, vec2 half, f32 angle_deg, f32 cell, i32 nx, i32 ny, Fn fn) {
  const vec2 u = from_angle(angle_deg), v{-u.y, u.x};
  polygon p = {c - u * half.x - v * half.y, c + u * half.x - v * half.y, c + u * half.x + v * half.y,
               c - u * half.x + v * half.y};
  scan_polygon(p, cell, nx, ny, fn);
}

bool top_floor(const plan &p, i32 f) { return f == p.storeys() - 1; }

// Whether a point is in any void of floor `f`.
bool in_void(const plan &p, i32 f, vec2 q) {
  if (f < 0 || f >= p.storeys())
    return false;
  for (const polygon &v : p.floors[static_cast<size_t>(f)].voids)
    if (point_in_polygon(v, q))
      return true;
  return false;
}

// The point inside a portal's opening on the side of room `inside`.
bool portal_side(const plan &p, const portal &q, const std::string &inside, i32 floor, vec2 &out, f32 reach) {
  const room *r = p.find_room(floor, inside);
  if (!r)
    return false;
  const vec2 n = normalize(q.normal);
  for (const f32 s : {1.0f, -1.0f}) {
    const vec2 at = q.center + n * (reach * s);
    if (point_in_polygon(r->poly, at)) {
      out = at;
      return true;
    }
  }
  return false;
}

// The sector a door leaf sweeps, sampled: points between the hinge and the
// leaf's tip at every angle it passes.
std::vector<vec2> sweep_points(const door &d) {
  std::vector<vec2> out;
  for (i32 a = 1; a <= 12; ++a) {
    const f32 yaw = d.closed_angle + d.swing * static_cast<f32>(a) / 12.0f;
    const f32 r = yaw * pi / 180.0f;
    const vec2 dir{std::cos(r), -std::sin(r)}; // plan yaw: module +X
    for (i32 k = 1; k <= 8; ++k)
      out.push_back(d.hinge + dir * (d.width * static_cast<f32>(k) / 8.0f));
  }
  return out;
}

bool in_solid(const solid &s, vec2 q, f32 z) {
  if (z < s.z0 || z > s.z1)
    return false;
  const vec2 u = from_angle(s.angle), v{-u.y, u.x};
  const vec2 d = q - s.c;
  return std::fabs(dot(d, u)) <= s.half.x && std::fabs(dot(d, v)) <= s.half.y;
}

bool in_furniture(const furniture &f, vec2 q, f32 pad) {
  const vec2 u = from_angle(f.angle), v{-u.y, u.x};
  const vec2 d = q - f.c;
  return std::fabs(dot(d, u)) <= f.half.x + pad && std::fabs(dot(d, v)) <= f.half.y + pad;
}

} // namespace

// --- Walkable raster ---------------------------------------------------------------------

bool nav_floor::at(vec2 p) const {
  const i32 i = index(p);
  return i >= 0 && walk[static_cast<size_t>(i)] != 0;
}

i32 nav_floor::index(vec2 p) const {
  const i32 x = static_cast<i32>(std::floor(p.x / cell)), y = static_cast<i32>(std::floor(p.y / cell));
  return x >= 0 && y >= 0 && x < nx && y < ny ? y * nx + x : -1;
}

nav_floor build_nav(const plan &p, i32 f, const std::vector<std::string> &shut, const assembly *as) {
  const rules &R = load_rules();
  nav_floor nav;
  nav.cell = nav_cell;
  nav.nx = static_cast<i32>(std::ceil(p.width / nav.cell));
  nav.ny = static_cast<i32>(std::ceil(p.depth / nav.cell));
  nav.walk.assign(static_cast<size_t>(nav.nx * nav.ny), 0);
  const auto set = [&](u8 v) { return [&nav, v](i32 i) { nav.walk[static_cast<size_t>(i)] = v; }; };
  const floor_plan &fl = p.floors[static_cast<size_t>(f)];
  for (const room &r : fl.rooms)
    scan_polygon(r.poly, nav.cell, nav.nx, nav.ny, set(1));
  // The stair: its flights and turn landing are the way up, not this floor.
  if (p.stair.on && !top_floor(p, f))
    for (const box2 &b : {p.stair.lower_flight, p.stair.upper_flight, p.stair.turn_landing})
      scan_polygon(rect_poly(b), nav.cell, nav.nx, nav.ny, set(0));
  for (const polygon &v : fl.voids)
    scan_polygon(v, nav.cell, nav.nx, nav.ny, set(0));
  // Openings through the walls.
  for (const portal &q : p.portals) {
    if (q.vertical() || q.floor_from != f)
      continue;
    if (q.kind == "door" && std::find(shut.begin(), shut.end(), q.id) != shut.end())
      continue;
    const vec2 n = normalize(q.normal);
    scan_obb(q.center, {q.clear * 0.5f, 0.32f}, angle_of(vec2{-n.y, n.x}), nav.cell, nav.nx, nav.ny, set(1));
  }
  if (as) {
    for (const furniture &fu : as->furniture)
      if (fu.floor == f)
        scan_obb(fu.c, fu.half, fu.angle, nav.cell, nav.nx, nav.ny, set(0));
    // An open leaf stands against its swing's end.
    for (const door &d : as->doors) {
      if (d.floor != f || std::find(shut.begin(), shut.end(), d.portal) != shut.end())
        continue;
      const f32 yaw = (d.closed_angle + d.swing) * pi / 180.0f;
      const vec2 dir{std::cos(yaw), -std::sin(yaw)};
      scan_obb(d.hinge + dir * (d.width * 0.5f), {d.width * 0.5f, std::max(d.thickness, nav.cell) * 0.5f},
               angle_of(dir), nav.cell, nav.nx, nav.ny, set(0));
    }
  }
  // Erode: keep a cell when its centre is the actor's radius and the wall
  // margin clear of everything blocked (two-pass chamfer distance).
  const f32 inf = 1e9f;
  std::vector<f32> d(nav.walk.size());
  for (size_t i = 0; i < d.size(); ++i)
    d[i] = nav.walk[i] ? inf : 0.0f;
  const i32 nx = nav.nx, ny = nav.ny;
  const f32 diag = 1.41421356f;
  const auto at = [&](i32 x, i32 y) { return x < 0 || y < 0 || x >= nx || y >= ny ? 0.0f : d[static_cast<size_t>(y * nx + x)]; };
  for (i32 y = 0; y < ny; ++y)
    for (i32 x = 0; x < nx; ++x) {
      f32 &c = d[static_cast<size_t>(y * nx + x)];
      if (c == 0.0f)
        continue;
      c = std::min({c, at(x - 1, y) + 1.0f, at(x, y - 1) + 1.0f, at(x - 1, y - 1) + diag, at(x + 1, y - 1) + diag});
    }
  for (i32 y = ny - 1; y >= 0; --y)
    for (i32 x = nx - 1; x >= 0; --x) {
      f32 &c = d[static_cast<size_t>(y * nx + x)];
      if (c == 0.0f)
        continue;
      c = std::min({c, at(x + 1, y) + 1.0f, at(x, y + 1) + 1.0f, at(x + 1, y + 1) + diag, at(x - 1, y + 1) + diag});
    }
  const f32 keep = R.actor_radius + R.wall_margin;
  for (size_t i = 0; i < d.size(); ++i)
    nav.walk[i] = nav.walk[i] && (d[i] - 0.5f) * nav.cell >= keep - 1e-4f ? 1 : 0;
  return nav;
}

std::vector<std::string> unreachable_rooms(const plan &p, const std::vector<std::string> &shut, const assembly *as) {
  std::vector<std::string> out;
  const i32 floors = p.storeys();
  std::vector<nav_floor> nav;
  for (i32 f = 0; f < floors; ++f)
    nav.push_back(build_nav(p, f, shut, as));
  std::vector<std::vector<u8>> seen(static_cast<size_t>(floors));
  for (i32 f = 0; f < floors; ++f)
    seen[static_cast<size_t>(f)].assign(nav[static_cast<size_t>(f)].walk.size(), 0);
  // From the street door.
  const portal *entry = nullptr;
  for (const portal &q : p.portals)
    if (q.exterior())
      entry = &q;
  const auto flood = [&](i32 f, std::deque<i32> &open) {
    const nav_floor &n = nav[static_cast<size_t>(f)];
    std::vector<u8> &s = seen[static_cast<size_t>(f)];
    while (!open.empty()) {
      const i32 i = open.front();
      open.pop_front();
      const i32 x = i % n.nx, y = i / n.nx;
      const i32 nb[4][2] = {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}};
      for (const auto &c : nb) {
        if (c[0] < 0 || c[1] < 0 || c[0] >= n.nx || c[1] >= n.ny)
          continue;
        const i32 j = c[1] * n.nx + c[0];
        if (n.walk[static_cast<size_t>(j)] && !s[static_cast<size_t>(j)]) {
          s[static_cast<size_t>(j)] = 1;
          open.push_back(j);
        }
      }
    }
  };
  // Seeds every walkable cell of `area` on floor f.
  const auto seed_box = [&](i32 f, const box2 &b, std::deque<i32> &open) {
    const nav_floor &n = nav[static_cast<size_t>(f)];
    scan_polygon(rect_poly(b), n.cell, n.nx, n.ny, [&](i32 i) {
      if (n.walk[static_cast<size_t>(i)] && !seen[static_cast<size_t>(f)][static_cast<size_t>(i)]) {
        seen[static_cast<size_t>(f)][static_cast<size_t>(i)] = 1;
        open.push_back(i);
      }
    });
  };
  if (entry && (entry->kind != "door" || std::find(shut.begin(), shut.end(), entry->id) == shut.end())) {
    const std::string inside = entry->from == "outside" ? entry->to : entry->from;
    vec2 at{};
    if (portal_side(p, *entry, inside, 0, at, 0.5f)) {
      std::deque<i32> open;
      const vec2 n = normalize(entry->normal);
      const vec2 along{-n.y, n.x};
      // A little patch round the point just inside, in case the cell itself is
      // on the eroded edge.
      for (f32 a = -0.1f; a <= 0.1f; a += nav_cell)
        for (f32 b = -0.1f; b <= 0.1f; b += nav_cell) {
          const i32 i = nav[0].index(at + along * a + n * b);
          if (i >= 0 && nav[0].walk[static_cast<size_t>(i)] && !seen[0][static_cast<size_t>(i)]) {
            seen[0][static_cast<size_t>(i)] = 1;
            open.push_back(i);
          }
        }
      flood(0, open);
    }
  }
  // Up the stairs: from the lower landing reached to the upper landing above.
  for (i32 f = 0; f + 1 < floors && p.stair.on; ++f) {
    bool reached = false;
    scan_polygon(rect_poly(p.stair.lower_landing), nav_cell, nav[static_cast<size_t>(f)].nx,
                 nav[static_cast<size_t>(f)].ny, [&](i32 i) { reached = reached || seen[static_cast<size_t>(f)][static_cast<size_t>(i)]; });
    if (!reached)
      break;
    std::deque<i32> open;
    seed_box(f + 1, p.stair.upper_landing, open);
    flood(f + 1, open);
  }
  for (i32 f = 0; f < floors; ++f) {
    const nav_floor &n = nav[static_cast<size_t>(f)];
    for (const room &r : p.floors[static_cast<size_t>(f)].rooms) {
      bool any = false;
      scan_polygon(r.poly, n.cell, n.nx, n.ny, [&](i32 i) { any = any || seen[static_cast<size_t>(f)][static_cast<size_t>(i)]; });
      if (!any)
        out.push_back(r.id);
    }
  }
  return out;
}

// --- The checks ----------------------------------------------------------------------

check_report check_plan(const plan &p, const assembly *as) {
  check_report rep;
  const rules &R = load_rules();
  const manifest &M = load_manifest();
  auto &err = rep.errors;
  auto &warn = rep.warnings;
  if (!R.loaded)
    err.push_back("building_rules.json did not load");
  if (!M.loaded)
    err.push_back("export_manifest.json did not load");
  if (!rep.errors.empty())
    return rep;
  if (p.rule_version != R.version)
    err.push_back(fmt("rule_version %d, rules are %d", p.rule_version, R.version));
  std::string why;
  if (!shape_supported(p.shape, &why))
    err.push_back(why);
  if (!p.holes.empty())
    err.push_back("footprint holes (courtyards) are not built by this runtime");
  // The outline matches the shape it names.
  const polygon outline = footprint_inset(p, 0.0f, 96);
  for (const vec2 q : p.outer)
    if (distance_to_outline(outline, q) > 0.02f) {
      err.push_back(fmt("outer_m point (%.3f, %.3f) is off the %s outline", q.x, q.y, p.shape.c_str()));
      break;
    }
  if (std::fabs(polygon_area(p.outer) - polygon_area(outline)) > 0.05f)
    err.push_back("outer_m's area does not match its shape");

  const i32 floors = p.storeys();
  // Rooms: known, inside, big and wide enough, apart.
  f32 area_score = 0.0f;
  i32 area_n = 0;
  for (i32 f = 0; f < floors; ++f) {
    const floor_plan &fl = p.floors[static_cast<size_t>(f)];
    if (std::fabs(fl.elevation - static_cast<f32>(fl.level) * storey) > 1e-3f || fl.level != f)
      err.push_back(fmt("floor %d is at %.2f m, storeys are 3 m", f, fl.elevation));
    for (const room &r : fl.rooms) {
      const room_rule *rr = R.room(r.type);
      if (!rr) {
        err.push_back(r.id + ": unknown room type " + r.type);
        continue;
      }
      for (const vec2 q : r.poly)
        if (!point_in_polygon(p.outer, q) || distance_to_outline(p.outer, q) < ext_wall - 0.005f) {
          err.push_back(fmt("%s reaches into the exterior wall at (%.2f, %.2f)", r.id.c_str(), q.x, q.y));
          break;
        }
      if (r.area + 1e-3f < rr->min_area)
        err.push_back(fmt("%s (%s) is %.2f m2, the minimum is %.1f", r.id.c_str(), r.type.c_str(), r.area, rr->min_area));
      const f32 cw = clear_width(r.poly);
      if (cw + 1e-3f < rr->min_width)
        err.push_back(fmt("%s (%s) is %.2f m wide, the minimum is %.2f", r.id.c_str(), r.type.c_str(), cw, rr->min_width));
      if (rr->target_area > 0.0f) {
        area_score += std::min(1.0f, r.area / rr->target_area);
        ++area_n;
      }
    }
    // Overlap, on the walkable raster's grid.
    const i32 nx = static_cast<i32>(std::ceil(p.width / nav_cell)), ny = static_cast<i32>(std::ceil(p.depth / nav_cell));
    std::vector<i16> owner(static_cast<size_t>(nx * ny), -1);
    for (size_t k = 0; k < fl.rooms.size(); ++k) {
      bool clash = false;
      std::string with;
      scan_polygon(fl.rooms[k].poly, nav_cell, nx, ny, [&](i32 i) {
        i16 &o = owner[static_cast<size_t>(i)];
        if (o >= 0 && !clash) {
          clash = true;
          with = fl.rooms[static_cast<size_t>(o)].id;
        }
        o = static_cast<i16>(k);
      });
      if (clash)
        err.push_back(fl.rooms[k].id + " overlaps " + with);
    }
  }

  // Portals: between rooms that exist, through a real wall.
  for (const portal &q : p.portals) {
    const auto room_ok = [&](const std::string &id, i32 f) { return id == "outside" || p.find_room(f, id); };
    if (!room_ok(q.from, q.floor_from) || !room_ok(q.to, q.floor_to)) {
      err.push_back(q.id + ": joins a room that is not on its floor");
      continue;
    }
    if (q.vertical())
      continue;
    if (q.kind == "door" && q.clear + 1e-3f < R.door_clear)
      err.push_back(fmt("%s: %.2f m clear, doors need %.2f", q.id.c_str(), q.clear, R.door_clear));
    if (q.exterior()) {
      const module_info *mi = M.find(q.module_id);
      if (!mi || mi->family != "DoorRigged")
        err.push_back(q.id + ": the street door's module " + q.module_id + " is not a DoorRigged of the kit");
      if (distance_to_outline(p.outer, q.center) > 0.15f)
        err.push_back(q.id + ": the street door is not in the exterior wall");
      continue;
    }
    const room *a = p.find_room(q.floor_from, q.from);
    const room *b = p.find_room(q.floor_to, q.to);
    if (distance_to_outline(a->poly, q.center) > 0.2f || distance_to_outline(b->poly, q.center) > 0.2f)
      err.push_back(q.id + ": not on the wall between " + q.from + " and " + q.to);
  }

  // The room graph: bedrooms are not passed through, bathrooms do not open
  // into kitchens.
  for (i32 f = 0; f < floors; ++f)
    for (const room &r : p.floors[static_cast<size_t>(f)].rooms) {
      i32 links = 0;
      for (const portal &q : p.portals)
        if (!q.vertical() && q.floor_from == f && (q.from == r.id || q.to == r.id)) {
          ++links;
          const std::string other = q.from == r.id ? q.to : q.from;
          const room *o = p.find_room(f, other);
          if (r.type == "bathroom" && o && (o->type == "kitchen" || o->type == "living_dining_kitchen"))
            err.push_back(r.id + ": the bathroom opens straight into the kitchen " + o->id);
        }
      if (r.type == "bedroom" && links > 1)
        err.push_back(r.id + ": a bedroom is passed through to reach another room");
    }

  // The stair: one core on every floor, flights and landings to the rule,
  // the slabs above cut open where the walking line needs the headroom.
  if (floors > 1) {
    const stair_core &s = p.stair;
    if (!s.on) {
      err.push_back("floors > 1 and no stair core");
    } else {
      if (s.split[0] + s.split[1] != R.risers || std::fabs(s.riser * static_cast<f32>(R.risers) - storey) > 1e-3f)
        err.push_back("the stair's risers do not make a storey");
      const bool straight = s.type == "straight";
      // A straight flight's run is along y in this runtime (one flight, no turn).
      const f32 flight_w = straight ? s.lower_flight.w() : std::min(s.lower_flight.w(), s.upper_flight.w());
      if (flight_w + 1e-3f < R.stair_flight)
        err.push_back(fmt("stair flights %.2f m wide, the rule is %.2f", flight_w, R.stair_flight));
      std::vector<const box2 *> landings = {&s.lower_landing, &s.upper_landing};
      if (!straight)
        landings.push_back(&s.turn_landing);
      for (const box2 *b : landings)
        if (std::min(b->w(), b->h()) + 1e-3f < std::min(R.stair_landing, R.stair_flight) ||
            (straight && b->h() + 1e-3f < R.stair_landing))
          err.push_back("a stair landing is shallower than the rule");
      if (std::fabs(s.lower_flight.h() - s.tread * static_cast<f32>(s.split[0])) > 1e-2f ||
          (!straight && std::fabs(s.upper_flight.h() - s.tread * static_cast<f32>(s.split[1])) > 1e-2f))
        err.push_back("a flight's run does not match its treads");
      box2 core0{};
      for (i32 f = 0; f < floors; ++f) {
        const std::string id = static_cast<size_t>(f) < s.room_ids.size() ? s.room_ids[static_cast<size_t>(f)] : "";
        const room *c = p.find_room(f, id);
        if (!c) {
          err.push_back(fmt("floor %d has no stair room", f));
          continue;
        }
        const box2 b = bounds_of(c->poly);
        if (f == 0)
          core0 = b;
        else if (std::fabs(b.x0 - core0.x0) + std::fabs(b.y0 - core0.y0) + std::fabs(b.x1 - core0.x1) +
                     std::fabs(b.y1 - core0.y1) > 0.01f)
          err.push_back(fmt("the stair core moves on floor %d", f));
        for (const box2 *fb : {&s.lower_flight, &s.upper_flight, &s.turn_landing})
          if (fb->w() > 0.0f && (!b.contains({fb->x0, fb->y0}, 1e-3f) || !b.contains({fb->x1, fb->y1}, 1e-3f)))
            err.push_back(fmt("a flight leaves the stair room on floor %d", f));
        if (f > 0 && p.floors[static_cast<size_t>(f)].voids.empty())
          err.push_back(fmt("floor %d's slab has no void over the stair", f));
      }
      // Headroom against the slab above (with its void), sampled along the line.
      for (i32 f = 0; f + 1 < floors; ++f) {
        const f32 base = p.floors[static_cast<size_t>(f)].elevation;
        const f32 under = p.floors[static_cast<size_t>(f + 1)].elevation - slab;
        for (size_t k = 0; k + 1 < s.walking_line.size(); ++k) {
          const vec3 a = s.walking_line[k], b = s.walking_line[k + 1];
          for (i32 t = 0; t <= 20; ++t) {
            const f32 u = static_cast<f32>(t) / 20.0f;
            const vec3 q{a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u, a.z + (b.z - a.z) * u};
            if (q.z > storey - 1e-3f)
              continue; // arrived: on the floor above
            if (!in_void(p, f + 1, {q.x, q.y}) && under - (base + q.z) < R.headroom - 1e-3f) {
              err.push_back(fmt("stair headroom %.2f m at (%.2f, %.2f) on floor %d", under - (base + q.z), q.x, q.y, f));
              k = s.walking_line.size();
              break;
            }
          }
        }
        const vec3 top = s.walking_line.back();
        if (in_void(p, f + 1, {top.x, top.y}))
          err.push_back(fmt("the stair arrives over the void on floor %d", f + 1));
      }
    }
  }

  // Wet stacks: a bathroom above a bathroom.
  for (i32 f = 1; f < floors; ++f)
    for (const room &r : p.floors[static_cast<size_t>(f)].rooms) {
      if (r.type != "bathroom")
        continue;
      const box2 b = bounds_of(r.poly);
      bool stacked = false;
      for (const room &d : p.floors[static_cast<size_t>(f - 1)].rooms) {
        if (d.type != "bathroom")
          continue;
        const box2 e = bounds_of(d.poly);
        stacked = stacked || (std::fabs(b.x0 - e.x0) <= R.wet_tolerance && std::fabs(b.y0 - e.y0) <= R.wet_tolerance &&
                              std::fabs(b.x1 - e.x1) <= R.wet_tolerance && std::fabs(b.y1 - e.y1) <= R.wet_tolerance);
      }
      if (!stacked)
        err.push_back(r.id + ": the bathroom is not over the one below");
    }

  // Windows: on a wall that may have them, into the room they light.
  const std::vector<bay_slot> bays = exterior_bays(p);
  f32 light_score = 0.0f;
  i32 light_n = 0;
  for (const aperture &a : p.apertures) {
    if (!M.find(a.module_id))
      err.push_back("aperture module " + a.module_id + " is not in the manifest");
    const room *r = p.find_room(a.floor, a.room);
    if (!r) {
      err.push_back("an aperture lights " + a.room + ", which is not on its floor");
      continue;
    }
    if (distance_to_outline(p.outer, a.center) > 0.05f)
      err.push_back(a.room + ": an aperture is not on the facade");
    if (!point_in_polygon(r->poly, a.center + a.inward * 0.4f))
      err.push_back(fmt("an aperture at (%.2f, %.2f) does not open into %s", a.center.x, a.center.y, a.room.c_str()));
    const bay_slot *near = nullptr;
    for (const bay_slot &b : bays)
      if (!near || distance(b.center, a.center) < distance(near->center, a.center))
        near = &b;
    if (near && !side_glazable(p, near->side))
      err.push_back(a.room + ": a window in the " + near->side + " wall, which is blank");
  }
  for (i32 f = 0; f < floors; ++f)
    for (const room &r : p.floors[static_cast<size_t>(f)].rooms) {
      const room_rule *rr = R.room(r.type);
      if (!rr || !rr->daylight)
        continue;
      f32 glass = 0.0f;
      for (const aperture &a : p.apertures)
        if (a.floor == f && a.room == r.id)
          glass += a.glazed;
      const f32 need = R.glazing_ratio * r.area;
      if (glass <= 0.0f)
        err.push_back(r.id + " (" + r.type + ") needs daylight and has no window");
      else if (glass + 1e-3f < need)
        warn.push_back(fmt("%s: %.2f m2 of glass, the target is %.2f", r.id.c_str(), glass, need));
      light_score += std::min(1.0f, glass / std::max(need, 1e-3f));
      ++light_n;
    }

  // Walkable: every room from the street door, doors open.
  for (const std::string &id : unreachable_rooms(p, {}, as))
    err.push_back(id + " cannot be reached on foot from the street door");

  f32 furniture_score = 1.0f;
  if (as) {
    // Module ids, the curve's sockets, a bay laid once.
    for (const module_place &m : as->modules)
      if (!M.find(m.id)) {
        err.push_back("module " + m.id + " is not in the manifest");
        break;
      }
    for (const std::string &id : p.required_modules)
      if (!M.find(id))
        err.push_back("required module " + id + " is not in the manifest");
    for (const bay_slot &b : bays)
      if (b.arc && b.last && distance(b.end, {p.width - p.radius, 0.0f}) > 0.01f)
        err.push_back(fmt("the curve's pieces end %.3f m off the front", distance(b.end, {p.width - p.radius, 0.0f})));
    std::vector<std::pair<i32, vec2>> taken;
    for (const module_place &m : as->modules) {
      const module_info *mi = M.find(m.id);
      if (!mi || m.floor < 0 || m.dressing || m.inside)
        continue; // a dressing has no substrate; the Stair stands inside
      const bool shell = mi->family != "Column" && mi->family != "Parapet" && mi->family != "Coping" &&
                         mi->family != "FloorBand" && mi->family.rfind("ArcParapet", 0) != 0 &&
                         mi->family.rfind("ArcCoping", 0) != 0 && mi->family.rfind("ArcBand", 0) != 0;
      if (!shell)
        continue;
      std::vector<vec3> mids = {module_to_plan({mi->hi.x * 0.5f, 0, 0}, m.pos, m.yaw)};
      if (mi->family.find("Corner") != std::string::npos && mi->family.find("Post") == std::string::npos)
        mids = {module_to_plan({1, 0, 0}, m.pos, m.yaw), module_to_plan({2, 0, -1}, m.pos, m.yaw)};
      for (const vec3 c : mids) {
        for (const auto &[f, q] : taken)
          if (f == m.floor && distance(q, {c.x, c.y}) < 0.5f) {
            err.push_back(fmt("two modules with substrate in one bay at (%.2f, %.2f) floor %d", c.x, c.y, m.floor));
            break;
          }
        taken.push_back({m.floor, {c.x, c.y}});
      }
    }
    // Doors: openings free of collision, sweeps clear of walls, props and each other.
    for (const portal &q : p.portals) {
      if (q.vertical() || q.kind == "open")
        continue;
      const f32 base = p.floors[static_cast<size_t>(q.floor_from)].elevation;
      for (const solid &s : as->solids)
        if (s.kind != sk_leaf && s.kind != sk_prop && s.kind != sk_shell && in_solid(s, q.center, base + 1.0f)) {
          err.push_back(q.id + ": collision closes the doorway");
          break;
        }
    }
    for (size_t i = 0; i < as->doors.size(); ++i) {
      const door &d = as->doors[i];
      const floor_plan &fl = p.floors[static_cast<size_t>(d.floor)];
      const std::vector<vec2> pts = sweep_points(d);
      // The leaf passes through its own opening: the portal's hole in the wall.
      const portal *own = nullptr;
      for (const portal &q : p.portals)
        if (q.id == d.portal)
          own = &q;
      bool wall = false, prop = false;
      for (const vec2 q : pts) {
        bool in_room = false;
        for (const room &r : fl.rooms)
          in_room = in_room || point_in_polygon(r.poly, q) || distance_to_outline(r.poly, q) < 0.02f;
        if (!in_room && own) {
          const vec2 n = normalize(own->normal), along{-n.y, n.x};
          // A street door's centre may be on the outer face (the rounded
          // example) or in the wall's middle: its hole is the whole wall deep.
          const f32 thick = own->exterior() ? ext_wall * 2.2f : partition;
          in_room = std::fabs(dot(q - own->center, along)) <= own->aperture * 0.5f + 0.02f &&
                    std::fabs(dot(q - own->center, n)) <= thick * 0.5f + 0.03f;
        }
        if (!in_room)
          wall = true;
        for (const furniture &fu : as->furniture)
          if (fu.floor == d.floor && in_furniture(fu, q, 0.0f))
            prop = true;
      }
      if (wall)
        err.push_back(d.portal + ": the door's leaf sweeps through a wall");
      if (prop)
        err.push_back(d.portal + ": a prop stands in the door's sweep");
      for (size_t j = i + 1; j < as->doors.size(); ++j) {
        const door &e = as->doors[j];
        if (e.floor != d.floor || distance(e.hinge, d.hinge) > d.width + e.width)
          continue;
        const std::vector<vec2> other = sweep_points(e);
        bool clash = false;
        for (const vec2 a : pts)
          for (const vec2 b : other)
            clash = clash || distance(a, b) < 0.05f;
        if (clash)
          err.push_back(d.portal + " and " + e.portal + ": door sweeps cross");
      }
    }
    // Props: inside their room, off the portals, within the coverage limit.
    const preset_rule *P = R.preset(p.space_preset);
    const f32 limit = P ? P->furniture_max : 0.25f;
    f32 cover_sum = 0.0f;
    i32 cover_n = 0;
    for (i32 f = 0; f < floors; ++f)
      for (const room &r : p.floors[static_cast<size_t>(f)].rooms) {
        f32 cover = 0.0f;
        for (const furniture &fu : as->furniture) {
          if (fu.floor != f || fu.room != r.id)
            continue;
          cover += fu.half.x * fu.half.y * 4.0f;
          const vec2 u = from_angle(fu.angle), v{-u.y, u.x};
          for (const vec2 c : {fu.c + u * fu.half.x + v * fu.half.y, fu.c - u * fu.half.x + v * fu.half.y,
                               fu.c + u * fu.half.x - v * fu.half.y, fu.c - u * fu.half.x - v * fu.half.y})
            if (!point_in_polygon(r.poly, c)) {
              err.push_back(fu.model + " in " + r.id + " stands outside the room");
              break;
            }
          for (const portal &q : p.portals)
            if (!q.vertical() && q.floor_from == f && in_furniture(fu, q.center, R.portal_keepout + q.clear * 0.5f))
              err.push_back(fu.model + " in " + r.id + " is within the keep-out of " + q.id);
        }
        if (r.type == "corridor" || r.type == "stair") {
          if (cover > 0.0f)
            err.push_back(r.id + ": props in the circulation");
          continue;
        }
        if (cover > limit * r.area + 1e-3f)
          err.push_back(fmt("%s: props cover %.0f%% of the floor, the limit is %.0f%%", r.id.c_str(),
                            100.0f * cover / r.area, 100.0f * limit));
        cover_sum += cover / std::max(r.area * limit, 1e-3f);
        ++cover_n;
      }
    if (cover_n > 0)
      furniture_score = 1.0f - std::min(1.0f, cover_sum / static_cast<f32>(cover_n)) * 0.5f;
  }

  rep.score = 0.3f * (area_n ? area_score / static_cast<f32>(area_n) : 1.0f) +
              0.25f * (light_n ? light_score / static_cast<f32>(light_n) : 1.0f) + 0.2f + 0.15f +
              0.1f * furniture_score;
  return rep;
}

} // namespace sandtable::city::pbk
