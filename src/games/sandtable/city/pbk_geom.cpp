#include "../view.h"
#include "pbk.h"
#include "geometry.h"

#include <algorithm>
#include <cmath>

// The footprint of a plan, its facade cut into bays the way the kit's
// modules chain, and the adapter from plan metres to the table and to 3D.

namespace sandtable::city::pbk {

namespace {
constexpr f32 deg = pi / 180.0f;

bool rounded(const plan &p) { return p.shape == "CornerShopHouseRounded" && p.radius > 0.0f; }

// The arc family of a radius: R4 in 30-degree pieces, R2 in 45
// (building_rules.json assembly.curve_families).
f32 arc_piece_angle(f32 radius) { return radius > 3.0f ? 30.0f : 45.0f; }
} // namespace

bool shape_supported(const std::string &shape, std::string *why) {
  if (shape == "LShape" || shape == "TShape" || shape == "Rectangle" || shape == "CornerShopHouse" || shape == "CornerShopHouse3Fronts" ||
      shape == "CornerShopHouseRounded")
    return true;
  if (why)
    *why = "footprint shape " + shape +
           " is not built by this runtime (Courtyard/U need custom layouts, rules/README.vi.md)";
  return false;
}

polygon footprint_inset(const plan &p, f32 d, i32 steps) {
  const f32 W = p.width, D = p.depth;
  if (p.shape == "LShape")
    return {{d,d},{W-d,d},{W-d,D-4-d},{W-4-d,D-4-d},{W-4-d,D-d},{d,D-d}};
  if (p.shape == "TShape")
    return {{d,d},{W-d,d},{W-d,D-4-d},{W-4-d,D-4-d},{W-4-d,D-d},
            {4+d,D-d},{4+d,D-4-d},{d,D-4-d}};
  if (!rounded(p))
    return {{d, d}, {W - d, d}, {W - d, D - d}, {d, D - d}};
  const f32 R = p.radius;
  const vec2 c{W - R, R};
  const f32 r = R - d;
  polygon out{{d, d}};
  for (i32 i = 0; i <= steps; ++i) {
    const f32 a = (-90.0f + 90.0f * static_cast<f32>(i) / static_cast<f32>(steps)) * deg;
    out.push_back(c + vec2{std::cos(a), std::sin(a)} * r);
  }
  out.push_back({W - d, D - d});
  out.push_back({d, D - d});
  return out;
}

bool side_glazable(const plan &p, const std::string &side) {
  if (std::find(p.frontages.begin(), p.frontages.end(), side) != p.frontages.end())
    return true;
  if (side == "rounded_corner")
    return false;
  const archetype_rule *a = load_rules().archetype(p.archetype);
  if (p.archetype == "detached_spacious" || p.archetype == "courtyard_house" || (a && a->free_standing))
    return true; // standing free on all sides
  if (side == "north")
    return !(a && a->rear_blank);
  return false; // a party wall
}

bool facade_axis(const bay_slot &b) {
  if (b.arc || b.count <= 3) return true;
  const i32 edge = std::min(b.index, b.count - 1 - b.index);
  return edge % 2 == 1;
}

std::vector<bay_slot> exterior_bays(const plan &p) {
  std::vector<bay_slot> out;
  const f32 W = p.width, D = p.depth;
  const bool rnd = rounded(p);
  const f32 R = rnd ? p.radius : 0.0f;
  // Straight sides in the order the modules run (module +X along the wall,
  // +Z out of it: against the plan polygon's own order).
  struct side_def {
    const char *name;
    vec2 a, b, out;
  };
  std::vector<side_def> sides = {{"south", {W - R, 0.0f}, {0.0f, 0.0f}, {0.0f, -1.0f}},
                                 {"west", {0.0f, 0.0f}, {0.0f, D}, {-1.0f, 0.0f}},
                                 {"north", {0.0f, D}, {W, D}, {0.0f, 1.0f}},
                                 {"east", {W, D}, {W, R}, {1.0f, 0.0f}}};
  if (p.shape == "LShape" || p.shape == "TShape") {
    sides.clear();
    const polygon outline = footprint_inset(p, 0);
    for (size_t i = 0; i < outline.size(); ++i) {
      const vec2 a = outline[(i + 1) % outline.size()], b = outline[i];
      const vec2 along = normalize(b-a), normal{-along.y, along.x};
      const char *name = normal.y < -0.5f ? "south" : normal.y > 0.5f ? "north" : normal.x > 0 ? "east" : "west";
      sides.push_back({name, a, b, normal});
    }
  }
  for (const side_def &s : sides) {
    const f32 len = distance(s.a, s.b);
    const i32 n = std::max(1, static_cast<i32>(std::lround(len / bay)));
    const vec2 along = normalize(s.b - s.a);
    for (i32 i = 0; i < n; ++i) {
      bay_slot b;
      b.side = s.name;
      b.index = i;
      b.count = n;
      b.origin = s.a + along * (bay * static_cast<f32>(i));
      b.end = b.origin + along * bay;
      b.center = b.origin + along * (bay * 0.5f);
      b.out = s.out;
      b.yaw = b.end_yaw = yaw_facing(s.out);
      b.first = i == 0;
      b.last = i == n - 1;
      out.push_back(b);
    }
  }
  if (rnd) {
    // From the east side's foot round to the front: each piece's origin is
    // the last one's right socket, turned by its right tangent.
    const f32 A = arc_piece_angle(R);
    const i32 n = std::max(1, static_cast<i32>(std::lround(90.0f / A)));
    const vec2 c{W - R, R};
    vec2 at{W, R};
    f32 yaw = yaw_facing({1.0f, 0.0f});
    const module_info *mi = load_manifest().find(p.style + "/ArcWall_R" + std::to_string(static_cast<i32>(R)) +
                                                 "_A" + std::to_string(static_cast<i32>(A)));
    for (i32 i = 0; i < n; ++i) {
      bay_slot b;
      b.side = "rounded_corner";
      b.index = i;
      b.count = n;
      b.arc = true;
      b.origin = at;
      b.yaw = yaw;
      const f32 mid = -(static_cast<f32>(i) + 0.5f) * A * deg;
      b.out = {std::cos(mid), std::sin(mid)};
      b.center = c + b.out * R;
      if (mi) {
        const vec3 e = module_to_plan(mi->socket("right"), {at.x, at.y, 0.0f}, yaw);
        b.end = {e.x, e.y};
        b.end_yaw = yaw + mi->right_tangent;
      } else {
        const f32 a1 = -(static_cast<f32>(i) + 1.0f) * A * deg;
        b.end = c + vec2{std::cos(a1), std::sin(a1)} * R;
        b.end_yaw = yaw + A;
      }
      b.first = i == 0;
      b.last = i == n - 1;
      at = b.end;
      yaw = b.end_yaw;
      out.push_back(b);
    }
  }
  return out;
}

const room *room_behind(const plan &p, i32 floor, const bay_slot &b) {
  if (floor < 0 || floor >= p.storeys())
    return nullptr;
  const vec2 probe = b.center - b.out * 0.4f;
  for (const room &r : p.floors[static_cast<size_t>(floor)].rooms)
    if (point_in_polygon(r.poly, probe))
      return &r;
  return nullptr;
}

// --- The town's buildings ----------------------------------------------------------------

namespace {
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
} // namespace

std::vector<request> requests_for_building(const city_map &map, i32 id, vec2 &center, std::string *why) {
  std::vector<request> out;
  const building &b = map.buildings[static_cast<size_t>(id)];
  const auto no = [&](const std::string &w) {
    if (why)
      *why = w;
    return std::vector<request>{};
  };
  if (std::find(map.hq_sites.begin(), map.hq_sites.end(), id) != map.hq_sites.end())
    return no("gang seat");
  const i32 wb = static_cast<i32>(std::floor(b.box.half.x * 2.0f / units_per_metre / bay + 1e-3f));
  const i32 db = static_cast<i32>(std::floor(b.box.half.y * 2.0f / units_per_metre / bay + 1e-3f));
  std::vector<const char *> kinds;
  // Real road frontage and neighbour clearance, rather than treating every lot as detached.
  const auto street_side = [&](vec2 normal, f32 extent) {
    const vec2 at = b.box.center + normal * extent;
    for (const road &rd : map.roads) {
      if (rd.kind == road_kind::alley) continue;
      vec2 q{};
      if (closest_on(rd.pts, at, q) < rd.reach() + 18.0f && dot(q-at, normal) > 0) return true;
    }
    return false;
  };
  const bool east = street_side(b.box.axis_x(), b.box.half.x);
  const bool west = street_side(-b.box.axis_x(), b.box.half.x);
  bool detached = true;
  for (const building &other : map.buildings) {
    if (&other == &b) continue;
    for (const f32 side : {-1.0f, 1.0f})
      if (other.box.contains(b.box.center + b.box.axis_x() * (side * (b.box.half.x + 3)))) detached = false;
  }
  bool balcony_clear = true;
  const vec2 front = -b.box.axis_y();
  for (const f32 x : {-b.box.half.x * 0.75f, 0.0f, b.box.half.x * 0.75f}) {
    const vec2 at = b.box.center + b.box.axis_x() * x + front * (b.box.half.y + 1.2f * units_per_metre);
    const cell_info *c = map.cell_at(at);
    if (!c || c->g == ground::water)
      balcony_clear = false;
    for (const road &rd : map.roads) {
      vec2 q{};
      if (closest_on(rd.pts, at, q) < rd.reach() + 0.1f) balcony_clear = false;
    }
    for (const building &other : map.buildings)
      if (&other != &b && other.box.contains(at)) balcony_clear = false;
  }
  switch (b.kind) {
  case building_kind::tube_house:
  case building_kind::house:
    if (b.business >= 0)
      kinds = {"shop_house", "tube_shop_house"};
    else
      kinds = detached && b.kind == building_kind::house ? std::vector<const char *>{"detached_spacious", "townhouse", "tube_house"} : std::vector<const char *>{"townhouse", "tube_house", "detached_spacious"};
    break;
  case building_kind::apartment: kinds = {"apartment_block"}; break;
  case building_kind::hotel: kinds = {"hotel"}; break;
  case building_kind::school: kinds = {"school"}; break;
  case building_kind::workshop: kinds = {"workshop_hall"}; break;
  case building_kind::warehouse: kinds = {"warehouse_hall"}; break;
  case building_kind::market_hall: kinds = {"market_hall"}; break;
  default: return no(std::string("kind ") + building_name(b.kind));
  }
  if ((b.kind == building_kind::house || b.kind == building_kind::tube_house) && b.business >= 0 && east) {
    if (west) kinds.insert(kinds.begin(), "corner_shop_3_fronts");
    kinds.insert(kinds.begin(), "corner_shop_house");
    if (sub_seed(b.look, 109) % 3 != 0) kinds.insert(kinds.begin(), "corner_shop_rounded");
  }
  if (b.kind == building_kind::house && b.business < 0) {
    if (sub_seed(b.look, 111) % 3 != 0) kinds.insert(kinds.begin(), "l_wing_house");
    if (sub_seed(b.look, 112) % 3 == 0) kinds.insert(kinds.begin(), "t_wing_house");
  }
  std::string last;
  for (const char *k : kinds) {
    const archetype_rule *a = load_rules().archetype(k);
    if (!a)
      continue;
    if (wb < a->width_bays[0] || db < a->depth_bays[0]) {
      last = std::string(k) + " needs " + std::to_string(a->width_bays[0]) + "x" + std::to_string(a->depth_bays[0]) +
             " bays, the lot is " + std::to_string(wb) + "x" + std::to_string(db);
      continue;
    }
    if (b.floors < a->floors[0] || b.floors > a->floors[1]) {
      last = std::string(k) + ": " + std::to_string(b.floors) + " floors is out of its range";
      continue;
    }
    request req;
    req.seed = b.look;
    req.balconies = balcony_clear;
    req.archetype = k;
    req.floors = b.floors;
    req.district = b.district >= 0 ? district_key(map.districts[static_cast<size_t>(b.district)].kind) : "residential";
    // A lot wider or deeper than the archetype's: the house takes its most, at the front.
    req.width = static_cast<f32>(std::min(wb, a->width_bays[1])) * bay;
    req.depth = static_cast<f32>(std::min(db, a->depth_bays[1])) * bay;
    out.push_back(req);
  }
  if (out.empty())
    return no(last.empty() ? "no archetype" : last);
  center = b.box.center + b.box.axis_y() * (out.front().depth * 0.5f * units_per_metre - b.box.half.y);
  return out;
}

// --- Frames ------------------------------------------------------------------------------

f32 yaw_facing(vec2 out) { return std::atan2(out.x, out.y) / deg; }

vec3 module_to_plan(vec3 m, vec3 origin, f32 yaw) {
  const f32 r = yaw * deg, c = std::cos(r), s = std::sin(r);
  return {origin.x + m.x * c + m.z * s, origin.y - m.x * s + m.z * c, origin.z + m.y};
}

vec2 placement::dir_to_table(vec2 d) const {
  return from_angle(angle) * d.x + from_angle(angle + 90.0f) * d.y;
}

vec2 placement::to_table(vec2 local) const {
  return center + dir_to_table({(local.x - width * 0.5f) * units_per_metre, (local.y - depth * 0.5f) * units_per_metre});
}

vec2 placement::to_local(vec2 table) const {
  const vec2 d = table - center;
  return {dot(d, from_angle(angle)) / units_per_metre + width * 0.5f,
          dot(d, from_angle(angle + 90.0f)) / units_per_metre + depth * 0.5f};
}

f32 placement::yaw_of(f32 plan_yaw) const { return plan_yaw - angle; }

vec3 placement::to_render(vec3 local) const {
  return to3d(to_table({local.x, local.y}), local.z * units_per_metre * unit3d);
}

} // namespace sandtable::city::pbk
