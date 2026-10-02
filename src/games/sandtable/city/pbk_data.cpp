#include "pbk.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <string_view>

// The data side of the building kit: the rules, plans and requests as JSON,
// the export manifest, what a GLB holds and its door's hinge track, and the
// polygon helpers the generator and the checks share.

namespace sandtable::city::pbk {

u32 sub_seed(u32 seed, u32 salt) {
  u32 h = seed ^ (salt * 0x9E3779B9u);
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  return h ^ (h >> 16);
}

rgba linear_to_srgb(rgba c) {
  const auto f = [](f32 v) {
    v = std::clamp(v, 0.0f, 1.0f);
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
  };
  return {f(c.r), f(c.g), f(c.b), c.a};
}

// --- Geometry ------------------------------------------------------------------------

f32 polygon_area(const polygon &p) {
  f32 a = 0.0f;
  for (size_t i = 0, n = p.size(); i < n; ++i)
    a += cross(p[i], p[(i + 1) % n]);
  return std::fabs(a) * 0.5f;
}

bool point_in_polygon(const polygon &p, vec2 q) {
  bool in = false;
  for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
    const vec2 a = p[i], b = p[j];
    if ((a.y > q.y) != (b.y > q.y) && q.x < (b.x - a.x) * (q.y - a.y) / (b.y - a.y) + a.x)
      in = !in;
  }
  return in;
}

f32 distance_to_outline(const polygon &p, vec2 q) {
  f32 best = 1e30f;
  for (size_t i = 0, n = p.size(); i < n; ++i) {
    const vec2 a = p[i], b = p[(i + 1) % n];
    const vec2 ab = b - a;
    const f32 t = std::clamp(dot(q - a, ab) / std::max(length_sq(ab), 1e-12f), 0.0f, 1.0f);
    best = std::min(best, distance(q, a + ab * t));
  }
  return best;
}

polygon clip_convex(const polygon &subject, const polygon &clip) {
  // The clip polygon's winding decides which side of each edge is inside.
  f32 wind = 0.0f;
  for (size_t i = 0; i < clip.size(); ++i)
    wind += cross(clip[i], clip[(i + 1) % clip.size()]);
  const f32 sgn = wind >= 0.0f ? 1.0f : -1.0f;
  polygon out = subject;
  for (size_t i = 0; i < clip.size() && !out.empty(); ++i) {
    const vec2 a = clip[i], b = clip[(i + 1) % clip.size()];
    const auto side = [&](vec2 q) { return cross(b - a, q - a) * sgn; };
    polygon in = std::move(out);
    out.clear();
    for (size_t k = 0; k < in.size(); ++k) {
      const vec2 cur = in[k], prev = in[(k + in.size() - 1) % in.size()];
      const f32 sc = side(cur), sp = side(prev);
      if (sc >= -1e-6f) {
        if (sp < -1e-6f)
          out.push_back(prev + (cur - prev) * (sp / (sp - sc)));
        out.push_back(cur);
      } else if (sp >= -1e-6f) {
        out.push_back(prev + (cur - prev) * (sp / (sp - sc)));
      }
    }
  }
  return out;
}

polygon rect_poly(const box2 &b) { return {{b.x0, b.y0}, {b.x1, b.y0}, {b.x1, b.y1}, {b.x0, b.y1}}; }

box2 bounds_of(const polygon &p) {
  box2 b{1e30f, 1e30f, -1e30f, -1e30f};
  for (const vec2 q : p) {
    b.x0 = std::min(b.x0, q.x);
    b.y0 = std::min(b.y0, q.y);
    b.x1 = std::max(b.x1, q.x);
    b.y1 = std::max(b.y1, q.y);
  }
  return b;
}

f32 clear_width(const polygon &p) {
  const box2 b = bounds_of(p);
  const f32 side = std::min(b.w(), b.h());
  if (p.size() <= 4)
    return side;
  // A cut corner: the widest band of the bounds' short way that is all inside,
  // sampled across the polygon.
  const bool across_x = b.w() <= b.h();
  f32 best = 0.0f;
  for (i32 k = 1; k < 40; ++k) {
    const f32 t = static_cast<f32>(k) / 40.0f;
    f32 lo = 1e30f, hi = -1e30f;
    for (i32 s = 0; s <= 200; ++s) {
      const f32 u = static_cast<f32>(s) / 200.0f;
      const vec2 q = across_x ? vec2{b.x0 + u * b.w(), b.y0 + t * b.h()} : vec2{b.x0 + t * b.w(), b.y0 + u * b.h()};
      if (point_in_polygon(p, q)) {
        lo = std::min(lo, u);
        hi = std::max(hi, u);
      }
    }
    if (hi > lo)
      best = std::max(best, (hi - lo) * (across_x ? b.w() : b.h()));
  }
  return std::min(side, best);
}

// --- JSON helpers ----------------------------------------------------------------------

namespace {

vec2 v2(const json_value &j) { return {j[usize{0}].f32_or(0.0f), j[usize{1}].f32_or(0.0f)}; }
vec3 v3(const json_value &j) { return {j[usize{0}].f32_or(0.0f), j[usize{1}].f32_or(0.0f), j[usize{2}].f32_or(0.0f)}; }

polygon poly_of(const json_value &j) {
  polygon p;
  for (usize i = 0; i < j.size(); ++i)
    p.push_back(v2(j[i]));
  return p;
}

box2 box_of(const json_value &j) {
  return {j[usize{0}].f32_or(0.0f), j[usize{1}].f32_or(0.0f), j[usize{2}].f32_or(0.0f), j[usize{3}].f32_or(0.0f)};
}

std::vector<std::string> strings_of(const json_value &j) {
  std::vector<std::string> out;
  for (usize i = 0; i < j.size(); ++i)
    if (j[i].is(json_value::string))
      out.push_back(j[i].str);
  return out;
}

json_value jv2(vec2 v) { return json_value::make_array().push(v.x).push(v.y); }
json_value jpoly(const polygon &p) {
  json_value a = json_value::make_array();
  for (const vec2 q : p)
    a.push(jv2(q));
  return a;
}
json_value jbox(const box2 &b) { return json_value::make_array().push(b.x0).push(b.y0).push(b.x1).push(b.y1); }
json_value jstrings(const std::vector<std::string> &v) {
  json_value a = json_value::make_array();
  for (const std::string &s : v)
    a.push(s);
  return a;
}
json_value jstr_or_null(const std::string &s) { return s.empty() ? json_value{} : json_value{s}; }

std::string path_in_kit(const char *rel) { return std::string(kit_dir) + "/" + rel; }

} // namespace

// --- Rules -------------------------------------------------------------------------------

const room_rule *rules::room(const std::string &type) const {
  for (const auto &[k, v] : rooms)
    if (k == type)
      return &v;
  return nullptr;
}
const archetype_rule *rules::archetype(const std::string &name) const {
  for (const auto &[k, v] : archetypes)
    if (k == name)
      return &v;
  return nullptr;
}
const preset_rule *rules::preset(const std::string &name) const {
  for (const auto &[k, v] : presets)
    if (k == name)
      return &v;
  return nullptr;
}
const std::array<rgba, 4> *rules::palette(const std::string &style) const {
  for (const auto &[k, v] : palettes)
    if (k == style)
      return &v;
  return nullptr;
}

namespace {

// Room types and archetypes of a rule package, those `r` has not got yet.
void read_room_types(const json_value &j, rules &r) {
  for (const auto &[k, v] : j["room_types"].members)
    if (!r.room(k))
      r.rooms.push_back({k,
                         {v["min_net_area_m2"].f32_or(0), v["target_net_area_m2"].f32_or(0),
                          v["min_clear_width_m"].f32_or(0), v["daylight_required"].bool_or(false),
                          v["privacy"].string_or(""), v["legacy_room_kind"].string_or("")}});
}

void read_archetypes(const json_value &j, rules &r) {
  for (const auto &[k, v] : j["archetypes"].members) {
    if (r.archetype(k))
      continue;
    archetype_rule a;
    a.shape = v["footprint_shape"].string_or("Rectangle");
    for (i32 i = 0; i < 2; ++i) {
      a.width_bays[i] = v["width_bays"][static_cast<usize>(i)].int_or(1);
      a.depth_bays[i] = v["depth_bays"][static_cast<usize>(i)].int_or(1);
      a.floors[i] = v["floors"][static_cast<usize>(i)].int_or(1);
    }
    a.ground = strings_of(v["program"]["ground"]);
    a.upper = strings_of(v["program"]["upper"]);
    a.frontages = strings_of(v["road_frontages"]);
    a.preset = v["space_preset"].string_or("spacious");
    a.rear_blank = v["rear_blank"].bool_or(false);
    for (usize i = 0; i < v["radius_m"].size(); ++i)
      a.radii.push_back(v["radius_m"][i].f32_or(4.0f));
    for (const auto &[roof, w] : v["roof_weights"].members)
      a.roofs.push_back({roof, w.f32_or(0.0f)});
    a.layout = v["layout"].string_or("band");
    a.stair = v["stair"].string_or("dogleg");
    a.party_walls = v["party_walls"].bool_or(false);
    a.free_standing = v["free_standing"].bool_or(false);
    a.shop_frontage = v["shop_frontage"].bool_or(false);
    r.archetypes.push_back({k, a});
  }
}

} // namespace

const rules &load_rules() {
  static rules r;
  if (r.loaded)
    return r;
  json_value j;
  const std::string path = path_in_kit("rules/building_rules.json");
  if (!json_load(path.c_str(), j)) {
    NJIN_WARN("pbk: %s did not load", path.c_str());
    return r;
  }
  r.version = j["version"].int_or(0);
  r.kit_revision = j["kit_revision"].string_or("");
  // Plans identify the geometry actually available to the runtime. Authoring
  // rules may already describe the next source edit, before its manual export.
  const manifest &exported = load_manifest();
  if (exported.loaded && r.kit_revision != exported.kit_revision) {
    NJIN_WARN("pbk: rules target %s; generated plans use available GLBs %s",
              r.kit_revision.c_str(), exported.kit_revision.c_str());
    r.kit_revision = exported.kit_revision;
  }
  const json_value &smp = j["sampling"];
  r.retry_count = smp["retry_count"].int_or(32);
  const json_value &st = smp["streams"];
  r.salt_footprint = static_cast<u32>(st["footprint"].int_or(101));
  r.salt_program = static_cast<u32>(st["program"].int_or(211));
  r.salt_layout = static_cast<u32>(st["layout"].int_or(307));
  r.salt_appearance = static_cast<u32>(st["appearance"].int_or(401));
  r.salt_facade = static_cast<u32>(st["facade"].int_or(503));
  r.salt_furniture = static_cast<u32>(st["furniture"].int_or(601));
  read_room_types(j, r);
  for (const auto &[k, v] : j["space_presets"].members)
    r.presets.push_back({k,
                         {v["corridor_min_m"].f32_or(1.4f), v["corridor_target_m"].f32_or(1.6f),
                          v["furniture_coverage_max"].f32_or(0.25f), v["room_area_scale"].f32_or(1.0f)}});
  read_archetypes(j, r);
  // The retro package is the source of the rules. The archetypes and room
  // types the game gained after it was cut (tube houses, flats, halls) are
  // taken from the original kit's package, only those it lacks.
  json_value legacy;
  if (json_load(legacy_rules_path, legacy)) {
    const size_t a = r.archetypes.size(), t = r.rooms.size();
    read_room_types(legacy, r);
    read_archetypes(legacy, r);
    if (r.archetypes.size() > a || r.rooms.size() > t)
      NJIN_INFO("pbk: %d archetypes and %d room types not in the retro rules taken from %s",
                static_cast<i32>(r.archetypes.size() - a), static_cast<i32>(r.rooms.size() - t), legacy_rules_path);
  }
  const json_value &ap = j["appearance"];
  for (const auto &[k, v] : ap["district_weights"].members) {
    std::vector<std::pair<std::string, f32>> w;
    for (const auto &[s, x] : v.members)
      w.push_back({s, x.f32_or(0.0f)});
    r.district_styles.push_back({k, w});
  }
  for (const auto &[k, v] : ap["palettes"].members) {
    std::array<rgba, 4> pal{};
    const char *names[4] = {"wall", "trim", "frame", "roof"};
    for (i32 i = 0; i < 4; ++i) {
      const json_value &c = v[names[i]];
      pal[static_cast<size_t>(i)] = {c[usize{0}].f32_or(1), c[usize{1}].f32_or(1), c[usize{2}].f32_or(1),
                                     c[usize{3}].f32_or(1)};
    }
    r.palettes.push_back({k, pal});
  }
  r.window_density[0] = ap["window_density"][usize{0}].f32_or(0.55f);
  r.window_density[1] = ap["window_density"][usize{1}].f32_or(0.9f);
  const json_value &c = j["circulation"];
  r.actor_radius = c["actor_radius_m"].f32_or(0.3f);
  r.wall_margin = c["wall_margin_m"].f32_or(0.1f);
  r.headroom = c["headroom_min_m"].f32_or(2.2f);
  r.door_clear = c["interior_door_min_net_open_m"].f32_or(0.9f);
  r.portal_keepout = c["furniture_distance_to_portal_m"].f32_or(0.35f);
  r.glazing_ratio = j["layout"]["daylight"]["minimum_glazed_area_to_room_area"].f32_or(0.08f);
  r.wet_tolerance = j["layout"]["wet_stack_alignment_tolerance_m"].f32_or(0.1f);
  const json_value &s = j["stairs"];
  r.stair_flight = s["flight_clear_width_m"].f32_or(1.2f);
  r.stair_landing = s["landing_clear_depth_m"].f32_or(1.4f);
  r.risers = s["risers_per_storey"].int_or(15);
  r.stair_core_width = s["dogleg"]["min_core_clear_width_m"].f32_or(2.84f);
  r.stair_core_length = s["dogleg"]["min_core_length_m"].f32_or(4.8f);
  r.loaded = r.version == rule_version && !r.rooms.empty() && !r.archetypes.empty();
  if (r.version != rule_version)
    NJIN_WARN("pbk: rules version %d, this game reads %d", r.version, rule_version);
  return r;
}

// --- Plans ---------------------------------------------------------------------------------

const room *plan::find_room(i32 floor, const std::string &id) const {
  if (floor < 0 || floor >= storeys())
    return nullptr;
  for (const room &r : floors[static_cast<size_t>(floor)].rooms)
    if (r.id == id)
      return &r;
  return nullptr;
}

bool plan_from_json(const json_value &j, plan &out, std::string *error) {
  const auto fail = [&](const char *why) {
    if (error)
      *error = why;
    return false;
  };
  if (std::string(j["schema"].string_or("")) != "sandtable.building-plan.v1")
    return fail("schema is not sandtable.building-plan.v1");
  plan p;
  p.rule_version = j["rule_version"].int_or(0);
  if (p.rule_version != rule_version)
    return fail("rule_version is not 1");
  p.kit_revision = j["kit_revision"].string_or("");
  p.seed = static_cast<u32>(j["seed"].number_or(0.0));
  p.archetype = j["archetype"].string_or("");
  p.style = j["style"].string_or("Modern");
  p.space_preset = j["space_preset"].string_or("spacious");
  p.width = j["width_m"].f32_or(0.0f);
  p.depth = j["depth_m"].f32_or(0.0f);
  const json_value &fp = j["footprint"];
  p.shape = fp["shape"].string_or("Rectangle");
  p.outer = poly_of(fp["outer_m"]);
  for (usize i = 0; i < fp["holes_m"].size(); ++i)
    p.holes.push_back(poly_of(fp["holes_m"][i]));
  p.radius = fp["radius_m"].f32_or(0.0f);
  p.frontages = strings_of(j["road_frontages"]);
  for (usize f = 0; f < j["floors"].size(); ++f) {
    const json_value &jf = j["floors"][f];
    floor_plan fl;
    fl.level = jf["level"].int_or(static_cast<i32>(f));
    fl.elevation = jf["elevation_m"].f32_or(static_cast<f32>(f) * storey);
    for (usize r = 0; r < jf["rooms"].size(); ++r) {
      const json_value &jr = jf["rooms"][r];
      room rm;
      rm.id = jr["id"].string_or("");
      rm.type = jr["type"].string_or("");
      rm.poly = poly_of(jr["polygon_m"]);
      rm.area = jr["net_area_m2"].f32_or(polygon_area(rm.poly));
      rm.daylight_edges = strings_of(jr["daylight_edges"]);
      rm.furniture_target = jr["furniture_coverage_target"].f32_or(0.2f);
      fl.rooms.push_back(std::move(rm));
    }
    for (usize v = 0; v < jf["slab_voids_m"].size(); ++v)
      fl.voids.push_back(poly_of(jf["slab_voids_m"][v]));
    p.floors.push_back(std::move(fl));
  }
  for (usize i = 0; i < j["portals"].size(); ++i) {
    const json_value &jp = j["portals"][i];
    portal q;
    q.id = jp["id"].string_or("");
    q.from = jp["from_room"].string_or("");
    q.to = jp["to_room"].string_or("");
    q.floor_from = jp["floor_from"].int_or(0);
    q.floor_to = jp["floor_to"].int_or(0);
    q.center = v2(jp["center_m"]);
    q.normal = v2(jp["normal_m"]);
    q.axis = jp["axis"].string_or("");
    q.kind = jp["kind"].string_or("open");
    q.module_id = jp["module_id"].string_or("");
    q.aperture = jp["aperture_width_m"].f32_or(1.05f);
    q.clear = jp["net_clear_width_m"].f32_or(0.9f);
    q.height = jp["height_m"].f32_or(2.35f);
    p.portals.push_back(std::move(q));
  }
  const json_value &sc = j["stair_core"];
  if (sc.is(json_value::object)) {
    stair_core &s = p.stair;
    s.on = true;
    s.id = sc["id"].string_or("main");
    s.type = sc["type"].string_or("dogleg");
    s.room_ids = strings_of(sc["room_ids"]);
    s.clear_width = sc["clear_width_m"].f32_or(1.2f);
    s.riser = sc["riser_m"].f32_or(0.2f);
    s.tread = sc["tread_m"].f32_or(0.25f);
    s.split[0] = sc["split_risers"][usize{0}].int_or(8);
    s.split[1] = sc["split_risers"][usize{1}].int_or(7);
    s.lower_flight = box_of(sc["lower_flight_m"]);
    s.upper_flight = box_of(sc["upper_flight_m"]);
    s.lower_landing = box_of(sc["lower_landing_m"]);
    s.turn_landing = box_of(sc["turn_landing_m"]);
    s.upper_landing = box_of(sc["upper_landing_m"]);
    for (usize i = 0; i < sc["walking_line_m"].size(); ++i)
      s.walking_line.push_back(v3(sc["walking_line_m"][i]));
    if (s.type == "straight") {
      // rules/building_plan.schema.json: flight_m, bottom_landing_m, top_landing_m, risers.
      s.lower_flight = box_of(sc["flight_m"]);
      s.lower_landing = box_of(sc["bottom_landing_m"]);
      s.upper_landing = box_of(sc["top_landing_m"]);
      s.split[0] = sc["risers"].int_or(15);
      s.split[1] = 0;
      s.upper_flight = s.turn_landing = box2{};
    } else if (s.type != "dogleg") {
      return fail("stair_core.type is neither dogleg nor straight");
    }
  }
  p.required_modules = strings_of(j["required_module_ids"]);
  p.runtime_geometry = strings_of(j["runtime_generated_geometry"]);
  p.provenance = j["provenance"].string_or("");
  for (usize i = 0; i < j["daylight_apertures"].size(); ++i) {
    const json_value &ja = j["daylight_apertures"][i];
    aperture a;
    a.floor = ja["floor"].int_or(0);
    a.room = ja["room_id"].string_or("");
    a.center = v2(ja["center_m"]);
    a.inward = v2(ja["inward_normal"]);
    a.module_id = ja["module_id"].string_or("");
    a.glazed = ja["glazed_area_m2"].f32_or(1.5f);
    p.apertures.push_back(std::move(a));
  }
  p.generator = j["generator"];
  if (p.outer.size() < 3 || p.width <= 0.0f || p.depth <= 0.0f || p.floors.empty())
    return fail("footprint, size or floors missing");
  out = std::move(p);
  return true;
}

json_value plan_to_json(const plan &p) {
  json_value j = json_value::make_object();
  j.set("schema", p.schema).set("rule_version", p.rule_version).set("kit_revision", p.kit_revision);
  j.set("seed", static_cast<f64>(p.seed)).set("archetype", p.archetype).set("style", p.style);
  j.set("space_preset", p.space_preset).set("width_m", p.width).set("depth_m", p.depth);
  json_value fp = json_value::make_object();
  json_value holes = json_value::make_array();
  for (const polygon &h : p.holes)
    holes.push(jpoly(h));
  fp.set("shape", p.shape).set("outer_m", jpoly(p.outer)).set("holes_m", holes);
  fp.set("radius_m", p.radius > 0.0f ? json_value{p.radius} : json_value{});
  j.set("footprint", fp).set("road_frontages", jstrings(p.frontages));
  json_value floors = json_value::make_array();
  for (const floor_plan &f : p.floors) {
    json_value jf = json_value::make_object();
    jf.set("level", f.level).set("elevation_m", f.elevation);
    json_value rooms = json_value::make_array();
    for (const room &r : f.rooms) {
      json_value jr = json_value::make_object();
      jr.set("id", r.id).set("type", r.type).set("polygon_m", jpoly(r.poly)).set("net_area_m2", r.area);
      jr.set("floor_voids_m", json_value::make_array()).set("daylight_edges", jstrings(r.daylight_edges));
      jr.set("furniture_coverage_target", r.furniture_target);
      rooms.push(jr);
    }
    json_value voids = json_value::make_array();
    for (const polygon &v : f.voids)
      voids.push(jpoly(v));
    jf.set("rooms", rooms).set("slab_voids_m", voids);
    floors.push(jf);
  }
  j.set("floors", floors);
  json_value portals = json_value::make_array();
  for (const portal &q : p.portals) {
    json_value jp = json_value::make_object();
    jp.set("id", q.id).set("floor_from", q.floor_from).set("floor_to", q.floor_to);
    jp.set("from_room", q.from).set("to_room", q.to).set("center_m", jv2(q.center)).set("axis", q.axis);
    jp.set("kind", q.kind).set("normal_m", jv2(q.normal)).set("aperture_width_m", q.aperture);
    jp.set("net_clear_width_m", q.clear).set("height_m", q.height).set("module_id", jstr_or_null(q.module_id));
    portals.push(jp);
  }
  j.set("portals", portals);
  if (p.stair.on) {
    const stair_core &s = p.stair;
    json_value sc = json_value::make_object();
    json_value line = json_value::make_array();
    for (const vec3 v : s.walking_line)
      line.push(json_value::make_array().push(v.x).push(v.y).push(v.z));
    sc.set("id", s.id).set("type", s.type).set("room_ids", jstrings(s.room_ids)).set("clear_width_m", s.clear_width);
    sc.set("riser_m", s.riser).set("tread_m", s.tread);
    if (s.type == "straight") {
      sc.set("flight_m", jbox(s.lower_flight)).set("bottom_landing_m", jbox(s.lower_landing));
      sc.set("top_landing_m", jbox(s.upper_landing)).set("risers", s.split[0]);
    } else {
      sc.set("split_risers", json_value::make_array().push(s.split[0]).push(s.split[1]));
      sc.set("lower_flight_m", jbox(s.lower_flight)).set("upper_flight_m", jbox(s.upper_flight));
      sc.set("lower_landing_m", jbox(s.lower_landing)).set("turn_landing_m", jbox(s.turn_landing));
      sc.set("upper_landing_m", jbox(s.upper_landing));
    }
    sc.set("walking_line_m", line);
    sc.set("geometry_source", "runtime_generated");
    j.set("stair_core", sc);
  } else {
    j.set("stair_core", json_value{});
  }
  j.set("required_module_ids", jstrings(p.required_modules));
  j.set("runtime_generated_geometry", jstrings(p.runtime_geometry));
  j.set("provenance", p.provenance);
  json_value aps = json_value::make_array();
  for (const aperture &a : p.apertures) {
    json_value ja = json_value::make_object();
    ja.set("floor", a.floor).set("room_id", a.room).set("center_m", jv2(a.center));
    ja.set("inward_normal", jv2(a.inward)).set("module_id", a.module_id).set("glazed_area_m2", a.glazed);
    aps.push(ja);
  }
  j.set("daylight_apertures", aps);
  if (!p.generator.is(json_value::null))
    j.set("generator", p.generator);
  return j;
}

bool load_plan(const char *path, plan &out, std::string *error) {
  json_value j;
  if (!json_load(path, j)) {
    if (error)
      *error = std::string("could not read ") + path;
    return false;
  }
  return plan_from_json(j, out, error);
}

bool request_from_json(const json_value &j, request &out) {
  if (std::string(j["schema"].string_or("")) != "sandtable.building-request.v1")
    return false;
  request r;
  r.seed = static_cast<u32>(j["seed"].number_or(1.0));
  r.rule_version = j["rule_version"].int_or(1);
  r.archetype = j["archetype"].string_or("detached_spacious");
  r.style = j["style"].string_or("");
  r.space_preset = j["space_preset"].string_or("");
  r.width = j["width_m"].f32_or(12.0f);
  r.depth = j["depth_m"].f32_or(10.0f);
  r.floors = j["floors"].int_or(2);
  r.road_sides = strings_of(j["road_sides"]);
  out = r;
  return true;
}

// --- Manifest ----------------------------------------------------------------------------

std::string kit_path(const std::string &rel) { return path_in_kit(rel.c_str()); }

vec3 module_info::socket(const char *name) const {
  for (const auto &[k, v] : sockets)
    if (k == name)
      return v;
  return {};
}

const module_info *manifest::find(const std::string &id) const {
  for (const module_info &m : modules)
    if (m.id == id)
      return &m;
  return nullptr;
}

const manifest &load_manifest() {
  static manifest m;
  static std::once_flag once;
  std::call_once(once, [] {
    json_value j;
    const std::string path = path_in_kit("export_manifest.json");
    if (!json_load(path.c_str(), j)) {
      NJIN_WARN("pbk: %s did not load", path.c_str());
      return;
    }
    // The GLBs are in metres, +Y up, the front +Z: the engine's loader takes
    // them as they are and the render scale is applied once, at the instance.
    if (std::string(j["units"].string_or("")) != "metre" || std::string(j["axes"]["up"].string_or("")) != "+Y" ||
        std::string(j["axes"]["front"].string_or("+Z")) != "+Z") {
      NJIN_WARN("pbk: %s is not in metres with +Y up and the front +Z", path.c_str());
      return;
    }
    m.kit_revision = j["kit_revision"].string_or("");
    m.visual_variant = j["visual_variant"].string_or("");
    m.source_revision = m.kit_revision;
    json_value status;
    if (json_load(path_in_kit("source_status.json").c_str(), status)) {
      m.source_revision = status["kit_revision"].string_or(m.kit_revision.c_str());
      m.export_pending = status["export_required"].bool_or(false) || m.source_revision != m.kit_revision;
    }
    NJIN_INFO("pbk: loading %s, visual %s, from %s", m.kit_revision.c_str(),
              m.visual_variant.c_str(), kit_dir);
    if (m.export_pending)
      NJIN_WARN("pbk: source %s awaits manual export; rendering existing GLBs %s",
                m.source_revision.c_str(), m.kit_revision.c_str());
    m.render_scale = j["engine_render_scale"].f32_or(0.1875f);
    for (usize i = 0; i < j["modules"].size(); ++i) {
      const json_value &e = j["modules"][i];
      module_info mi;
      mi.id = e["id"].string_or("");
      mi.path = e["path"].string_or("");
      mi.family = e["family"].string_or("");
      mi.style = e["style"].string_or("");
      mi.revision = e["revision"].string_or(m.kit_revision.c_str());
      mi.dressing_only = std::string(e["assembly_mode"].string_or("")) == "dressing_only";
      mi.lo = v3(e["bounds_gltf_m"]["min"]);
      mi.hi = v3(e["bounds_gltf_m"]["max"]);
      for (const auto &[k, v] : e["sockets_gltf_m"].members)
        mi.sockets.push_back({k, v3(v)});
      mi.right_tangent = e["connection_orientation_gltf"]["source_degrees"]["right_tangent_degrees"].f32_or(0.0f) *
                         e["connection_orientation_gltf"]["angle_sign_from_blender_z"].f32_or(1.0f);
      mi.radius = e["radius_m"].f32_or(0.0f);
      mi.arc_angle = e["angle_degrees"].f32_or(0.0f);
      mi.animated = e["animated"].bool_or(false);
      const json_value &a = e["animation"];
      if (a.is(json_value::object)) {
        mi.anim_name = a["name"].string_or("");
        mi.anim_duration = a["duration_s"].f32_or(0.0f);
        mi.open_time = a["open_time_s"].f32_or(0.0f);
        mi.closed_time = a["closed_time_s"].f32_or(0.0f);
        mi.open_angle = a["open_angle_degrees"].f32_or(90.0f);
        mi.rig_count = a["rig_count"].int_or(1);
        mi.loop = a["loop_by_default"].bool_or(false);
      }
      mi.shutters = mi.animated && (std::string(e["animation_kind"].string_or("")).find("shutter") != std::string::npos ||
                                    mi.anim_name.find("Shutter") != std::string::npos);
      if (mi.revision != m.kit_revision)
        NJIN_WARN("pbk: %s was exported at %s, the manifest is %s", mi.id.c_str(), mi.revision.c_str(),
                  m.kit_revision.c_str());
      m.modules.push_back(std::move(mi));
    }
    m.loaded = static_cast<i32>(m.modules.size()) == j["module_count"].int_or(-1);
    if (!m.loaded)
      NJIN_WARN("pbk: manifest lists %d modules, module_count says %d", static_cast<i32>(m.modules.size()),
                j["module_count"].int_or(-1));
  });
  return m;
}

door_clip clip_of(const module_info *mi) {
  door_clip c;
  if (!mi || !mi->animated)
    return c;
  const f32 t1 = std::max(mi->open_time, mi->closed_time);
  c.duration = mi->anim_duration > 0.0f ? mi->anim_duration : t1 * 4.0f;
  // The clip's first stretch goes from its pose at 0 to the other at t1; its
  // second, a second later, comes back.
  c.shut_pose = mi->closed_time;
  c.open_pose = mi->open_time;
  if (mi->closed_time <= mi->open_time) {
    c.open_from = 0.0f;
    c.open_to = t1;
    c.close_from = t1 + 1.0f;
    c.close_to = t1 + 2.0f;
  } else {
    c.close_from = 0.0f;
    c.close_to = t1;
    c.open_from = t1 + 1.0f;
    c.open_to = t1 + 2.0f;
  }
  c.open_to = std::min(c.open_to, c.duration);
  c.close_to = std::min(c.close_to, c.duration);
  return c;
}

// --- Matrices ---------------------------------------------------------------------------------

mat4 operator*(const mat4 &a, const mat4 &b) {
  mat4 r;
  for (i32 c = 0; c < 4; ++c)
    for (i32 rr = 0; rr < 4; ++rr) {
      f32 s = 0.0f;
      for (i32 k = 0; k < 4; ++k)
        s += a.m[k * 4 + rr] * b.m[c * 4 + k];
      r.m[c * 4 + rr] = s;
    }
  return r;
}

vec3 mat4_point(const mat4 &m, vec3 p) {
  return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12], m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
          m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}

mat4 mat4_affine_inverse(const mat4 &m) {
  // Rows of the 3x3 part.
  const f32 a = m.m[0], b = m.m[4], c = m.m[8];
  const f32 d = m.m[1], e = m.m[5], f = m.m[9];
  const f32 g = m.m[2], h = m.m[6], k = m.m[10];
  const f32 A = e * k - f * h, B = -(d * k - f * g), C = d * h - e * g;
  const f32 det = a * A + b * B + c * C;
  mat4 r;
  if (std::fabs(det) < 1e-12f)
    return r;
  const f32 inv = 1.0f / det;
  const f32 i00 = A * inv, i01 = -(b * k - c * h) * inv, i02 = (b * f - c * e) * inv;
  const f32 i10 = B * inv, i11 = (a * k - c * g) * inv, i12 = -(a * f - c * d) * inv;
  const f32 i20 = C * inv, i21 = -(a * h - b * g) * inv, i22 = (a * e - b * d) * inv;
  r.m[0] = i00, r.m[4] = i01, r.m[8] = i02;
  r.m[1] = i10, r.m[5] = i11, r.m[9] = i12;
  r.m[2] = i20, r.m[6] = i21, r.m[10] = i22;
  const vec3 t{m.m[12], m.m[13], m.m[14]};
  r.m[12] = -(i00 * t.x + i01 * t.y + i02 * t.z);
  r.m[13] = -(i10 * t.x + i11 * t.y + i12 * t.z);
  r.m[14] = -(i20 * t.x + i21 * t.y + i22 * t.z);
  return r;
}

mat4 mat4_yaw(f32 deg) {
  const f32 c = std::cos(deg * pi / 180.0f), s = std::sin(deg * pi / 180.0f);
  mat4 r;
  r.m[0] = c, r.m[2] = -s, r.m[8] = s, r.m[10] = c;
  return r;
}

mat4 mat4_move(vec3 v) {
  mat4 r;
  r.m[12] = v.x, r.m[13] = v.y, r.m[14] = v.z;
  return r;
}

mat4 mat4_scale(f32 s) {
  mat4 r;
  r.m[0] = r.m[5] = r.m[10] = s;
  return r;
}

vec3 mat4_euler(const mat4 &m) {
  // R = Ry * Rx * Rz (transform3d: z, then x, then y), its columns normalised.
  f32 c[9];
  for (i32 col = 0; col < 3; ++col) {
    const f32 x = m.m[col * 4], y = m.m[col * 4 + 1], z = m.m[col * 4 + 2];
    const f32 len = std::max(std::sqrt(x * x + y * y + z * z), 1e-12f);
    c[col * 3] = x / len, c[col * 3 + 1] = y / len, c[col * 3 + 2] = z / len;
  }
  const auto R = [&](i32 row, i32 col) { return c[col * 3 + row]; };
  const f32 sx = std::clamp(-R(1, 2), -1.0f, 1.0f);
  const f32 x = std::asin(sx);
  f32 y = 0.0f, z = 0.0f;
  if (std::fabs(sx) < 0.9999f) {
    y = std::atan2(R(0, 2), R(2, 2));
    z = std::atan2(R(1, 0), R(1, 1));
  } else {
    y = std::atan2(-R(2, 0), R(0, 0));
  }
  return vec3{x, y, z} * (180.0f / pi);
}

f32 mat4_yaw_of(const mat4 &m) { return std::atan2(-m.m[2], m.m[0]) * 180.0f / pi; }

// --- GLB ------------------------------------------------------------------------------------

namespace {

struct glb_file {
  json_value json;
  std::string bin;
  bool ok = false;
};

u32 read_u32(const std::string &s, size_t at) {
  u32 v = 0;
  std::memcpy(&v, s.data() + at, 4);
  return v;
}

glb_file open_glb(const std::string &path) {
  glb_file g;
  std::string data;
  if (!file_read(path.c_str(), data) || data.size() < 20 || data.compare(0, 4, "glTF") != 0)
    return g;
  const u32 json_len = read_u32(data, 12);
  if (read_u32(data, 16) != 0x4E4F534Au || 20 + static_cast<size_t>(json_len) > data.size())
    return g;
  if (!json_parse(std::string_view(data).substr(20, json_len), g.json))
    return g;
  const size_t bin_at = 20 + json_len;
  if (bin_at + 8 <= data.size() && read_u32(data, bin_at + 4) == 0x004E4942u) {
    const u32 bin_len = read_u32(data, bin_at);
    g.bin = data.substr(bin_at + 8, std::min<size_t>(bin_len, data.size() - bin_at - 8));
  }
  g.ok = true;
  return g;
}

// Accessor `index` as numbers: floats as they are, integers as their values
// (0..1 when the accessor is normalized). `components` gets its type's size.
std::vector<f32> accessor_numbers(const glb_file &g, i32 index, i32 *components) {
  std::vector<f32> out;
  if (index < 0)
    return out;
  const json_value &a = g.json["accessors"][static_cast<usize>(index)];
  const i32 type = a["componentType"].int_or(0);
  const size_t size = type == 5126 || type == 5125   ? 4
                      : type == 5123 || type == 5122 ? 2
                      : type == 5121 || type == 5120 ? 1
                                                     : 0;
  const std::string kind = a["type"].string_or("");
  const i32 n = kind == "SCALAR" ? 1 : kind == "VEC2" ? 2 : kind == "VEC3" ? 3 : kind == "VEC4" ? 4 : kind == "MAT4" ? 16 : 0;
  if (components)
    *components = n;
  if (size == 0 || n == 0)
    return out;
  const bool normalized = a["normalized"].bool_or(false);
  const json_value &bv = g.json["bufferViews"][static_cast<usize>(a["bufferView"].int_or(0))];
  const size_t base = static_cast<size_t>(bv["byteOffset"].int_or(0) + a["byteOffset"].int_or(0));
  const size_t stride = static_cast<size_t>(bv["byteStride"].int_or(n * static_cast<i32>(size)));
  const i32 count = a["count"].int_or(0);
  out.reserve(static_cast<size_t>(count * n));
  for (i32 i = 0; i < count; ++i)
    for (i32 c = 0; c < n; ++c) {
      const size_t at = base + static_cast<size_t>(i) * stride + static_cast<size_t>(c) * size;
      if (at + size > g.bin.size())
        return {};
      const char *p = g.bin.data() + at;
      f32 v = 0.0f;
      if (type == 5126) {
        std::memcpy(&v, p, 4);
      } else if (type == 5125) {
        u32 x = 0;
        std::memcpy(&x, p, 4);
        v = static_cast<f32>(x);
      } else if (type == 5123) {
        u16 x = 0;
        std::memcpy(&x, p, 2);
        v = normalized ? static_cast<f32>(x) / 65535.0f : static_cast<f32>(x);
      } else if (type == 5122) {
        i16 x = 0;
        std::memcpy(&x, p, 2);
        v = normalized ? std::max(static_cast<f32>(x) / 32767.0f, -1.0f) : static_cast<f32>(x);
      } else if (type == 5121) {
        const u8 x = static_cast<u8>(*p);
        v = normalized ? static_cast<f32>(x) / 255.0f : static_cast<f32>(x);
      } else {
        const i8 x = static_cast<i8>(*p);
        v = normalized ? std::max(static_cast<f32>(x) / 127.0f, -1.0f) : static_cast<f32>(x);
      }
      out.push_back(v);
    }
  return out;
}

mat4 trs(vec3 t, const f32 q[4], vec3 s) {
  const f32 x = q[0], y = q[1], z = q[2], w = q[3];
  const f32 v[16] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),     0,
                     2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),     0,
                     2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y), 0,
                     t.x,                     t.y,                     t.z,                     1};
  mat4 r;
  std::copy(v, v + 16, r.m);
  for (i32 c = 0; c < 3; ++c)
    for (i32 rr = 0; rr < 3; ++rr)
      r.m[c * 4 + rr] *= c == 0 ? s.x : c == 1 ? s.y : s.z;
  return r;
}

mat4 node_local(const json_value &n) {
  if (n["matrix"].size() == 16) {
    mat4 r;
    for (i32 i = 0; i < 16; ++i)
      r.m[i] = n["matrix"][static_cast<usize>(i)].f32_or(0.0f);
    return r;
  }
  const vec3 t = n.has("translation") ? v3(n["translation"]) : vec3{};
  const vec3 s = n.has("scale") ? v3(n["scale"]) : vec3{1, 1, 1};
  const json_value &qj = n["rotation"];
  const f32 q[4] = {qj[usize{0}].f32_or(0), qj[usize{1}].f32_or(0), qj[usize{2}].f32_or(0), qj[usize{3}].f32_or(1)};
  return trs(t, q, s);
}

std::vector<i32> parents_of(const json_value &nodes) {
  std::vector<i32> parent(nodes.size(), -1);
  for (usize i = 0; i < nodes.size(); ++i)
    for (usize c = 0; c < nodes[i]["children"].size(); ++c)
      parent[static_cast<size_t>(nodes[i]["children"][c].int_or(0))] = static_cast<i32>(i);
  return parent;
}

mat4 rest_world(const json_value &nodes, const std::vector<i32> &parent, i32 i) {
  mat4 m = node_local(nodes[static_cast<usize>(i)]);
  for (i32 p = parent[static_cast<size_t>(i)]; p >= 0; p = parent[static_cast<size_t>(p)])
    m = node_local(nodes[static_cast<usize>(p)]) * m;
  return m;
}

} // namespace

glb_summary read_glb(const std::string &path) {
  glb_summary s;
  const glb_file g = open_glb(path);
  if (!g.ok)
    return s;
  const json_value &nodes = g.json["nodes"];
  s.nodes = static_cast<i32>(nodes.size());
  s.meshes = static_cast<i32>(g.json["meshes"].size());
  s.materials = static_cast<i32>(g.json["materials"].size());
  s.skins = static_cast<i32>(g.json["skins"].size());
  for (usize k = 0; k < g.json["skins"].size(); ++k)
    s.joints += static_cast<i32>(g.json["skins"][k]["joints"].size());
  for (usize i = 0; i < g.json["animations"].size(); ++i)
    s.animations.push_back(g.json["animations"][i]["name"].string_or(""));
  for (usize i = 0; i < g.json["images"].size(); ++i) {
    ++s.images;
    s.embedded_images += g.json["images"][i].has("bufferView") ? 1 : 0;
  }
  for (usize i = 0; i < g.json["materials"].size(); ++i) {
    const json_value &mt = g.json["materials"][i];
    s.glass += mt["extensions"].has("KHR_materials_transmission") ? 1 : 0;
    const std::string material_name = mt["name"].string_or("");
    if (material_name.find("_glass") != std::string::npos ||
        mt["extensions"].has("KHR_materials_transmission"))
      s.glazing_materials.push_back(static_cast<i32>(i));
    if (mt["pbrMetallicRoughness"].has("baseColorTexture"))
      s.textured.push_back(mt["name"].string_or(""));
  }
  for (usize i = 0; i < g.json["meshes"].size(); ++i)
    for (usize pr = 0; pr < g.json["meshes"][i]["primitives"].size(); ++pr) {
      const json_value &prim = g.json["meshes"][i]["primitives"][pr];
      const json_value &at = prim["attributes"];
      const json_value &mt = g.json["materials"][static_cast<usize>(prim["material"].int_or(0))];
      // A texture needs UVs; a normal map tangents too. A plain metal hinge needs neither.
      const bool textured = mt["pbrMetallicRoughness"].has("baseColorTexture") || mt.has("normalTexture");
      ++s.primitives;
      if (!at.has("NORMAL") || (textured && !at.has("TEXCOORD_0")) || (mt.has("normalTexture") && !at.has("TANGENT")))
        ++s.incomplete;
      s.with_tangent += at.has("TANGENT") ? 1 : 0;
    }
  const std::vector<i32> parent = parents_of(nodes);
  s.lo = {1e30f, 1e30f, 1e30f};
  s.hi = {-1e30f, -1e30f, -1e30f};
  for (usize i = 0; i < nodes.size(); ++i) {
    const json_value &n = nodes[i];
    if (std::string(n["name"].string_or("")) == "ModuleRoot") {
      const mat4 l = node_local(n);
      const mat4 id;
      s.root_identity = std::equal(l.m, l.m + 16, id.m, [](f32 a, f32 b) { return std::fabs(a - b) < 1e-6f; });
    }
    if (!n.has("mesh"))
      continue;
    s.mesh_nodes.push_back(n["name"].string_or(""));
    const mat4 w = rest_world(nodes, parent, static_cast<i32>(i));
    const json_value &mesh = g.json["meshes"][static_cast<usize>(n["mesh"].int_or(0))];
    for (usize pr = 0; pr < mesh["primitives"].size(); ++pr) {
      const json_value &acc =
          g.json["accessors"][static_cast<usize>(mesh["primitives"][pr]["attributes"]["POSITION"].int_or(0))];
      const vec3 lo = v3(acc["min"]), hi = v3(acc["max"]);
      for (i32 k = 0; k < 8; ++k) {
        const vec3 c = mat4_point(w, {(k & 1) ? hi.x : lo.x, (k & 2) ? hi.y : lo.y, (k & 4) ? hi.z : lo.z});
        s.lo = {std::min(s.lo.x, c.x), std::min(s.lo.y, c.y), std::min(s.lo.z, c.z)};
        s.hi = {std::max(s.hi.x, c.x), std::max(s.hi.y, c.y), std::max(s.hi.z, c.z)};
      }
    }
  }
  s.ok = true;
  return s;
}

// --- Rigs -------------------------------------------------------------------------------------

namespace {

void quat_slerp(const f32 *a, const f32 *b, f32 t, f32 *out) {
  f32 d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
  f32 bb[4] = {b[0], b[1], b[2], b[3]};
  if (d < 0.0f) {
    d = -d;
    for (f32 &v : bb)
      v = -v;
  }
  f32 ka = 1.0f - t, kb = t;
  if (d < 0.9995f) {
    const f32 th = std::acos(std::clamp(d, -1.0f, 1.0f)), sn = std::sin(th);
    ka = std::sin((1.0f - t) * th) / sn;
    kb = std::sin(t * th) / sn;
  }
  f32 len = 0.0f;
  for (i32 i = 0; i < 4; ++i) {
    out[i] = a[i] * ka + bb[i] * kb;
    len += out[i] * out[i];
  }
  len = std::sqrt(std::max(len, 1e-20f));
  for (i32 i = 0; i < 4; ++i)
    out[i] /= len;
}

// Track `tr` at `t`: `n` numbers (3, or 4 for a rotation) into `out`.
void sample(const module_rig::track &tr, f32 t, i32 n, f32 *out) {
  const size_t keys = tr.times.size();
  if (keys == 0)
    return;
  // CUBICSPLINE keeps in-tangent, value, out-tangent a key: the value is the middle one.
  const size_t per = tr.values.size() == keys * static_cast<size_t>(n) * 3 ? 3 : 1;
  const auto value = [&](size_t k) {
    return tr.values.data() + (k * per + (per == 3 ? 1 : 0)) * static_cast<size_t>(n);
  };
  if (tr.values.size() < keys * per * static_cast<size_t>(n))
    return;
  if (t <= tr.times.front() || keys == 1) {
    std::copy(value(0), value(0) + n, out);
    return;
  }
  if (t >= tr.times.back()) {
    std::copy(value(keys - 1), value(keys - 1) + n, out);
    return;
  }
  const size_t k = static_cast<size_t>(std::upper_bound(tr.times.begin(), tr.times.end(), t) - tr.times.begin());
  if (tr.step) {
    std::copy(value(k - 1), value(k - 1) + n, out);
    return;
  }
  const f32 a = (t - tr.times[k - 1]) / std::max(tr.times[k] - tr.times[k - 1], 1e-6f);
  if (n == 4) {
    quat_slerp(value(k - 1), value(k), a, out);
    return;
  }
  for (i32 i = 0; i < n; ++i)
    out[i] = value(k - 1)[i] + (value(k)[i] - value(k - 1)[i]) * a;
}

} // namespace

mat4 module_rig::world(i32 node, f32 t) const {
  mat4 m;
  for (i32 i = node; i >= 0; i = nodes[static_cast<size_t>(i)].parent) {
    const node_rest &r = nodes[static_cast<size_t>(i)];
    vec3 tt = r.t, ss = r.s;
    f32 q[4] = {r.q[0], r.q[1], r.q[2], r.q[3]};
    for (const track &tr : tracks) {
      if (tr.node != i)
        continue;
      f32 v[4] = {0, 0, 0, 1};
      sample(tr, t, tr.path == 1 ? 4 : 3, v);
      if (tr.path == 0)
        tt = {v[0], v[1], v[2]};
      else if (tr.path == 1)
        std::copy(v, v + 4, q);
      else
        ss = {v[0], v[1], v[2]};
    }
    m = trs(tt, q, ss) * m;
  }
  return m;
}

mat4 module_rig::leaf_at(i32 k, f32 t) const {
  if (k < 0 || k >= static_cast<i32>(leaves.size()))
    return {};
  const size_t i = static_cast<size_t>(k);
  return world(leaf_joint[i], t) * leaf_bind[i] * leaf_unload[i];
}

f32 module_rig::turn(i32 k, f32 t, f32 from) const {
  f32 d = mat4_yaw_of(leaf_at(k, t)) - mat4_yaw_of(leaf_at(k, from));
  while (d > 180.0f)
    d -= 360.0f;
  while (d < -180.0f)
    d += 360.0f;
  return d;
}

const module_rig &module_rig_of(const std::string &module_id) {
  // The generator's worker thread and the game both read it.
  static std::mutex lock;
  const std::lock_guard<std::mutex> hold(lock);
  static std::map<std::string, module_rig> cache;
  const manifest &M = load_manifest();
  const std::string key = M.kit_revision + "|" + module_id;
  const auto it = cache.find(key);
  if (it != cache.end())
    return it->second;
  module_rig &rig = cache[key];
  const module_info *mi = M.find(module_id);
  if (!mi || !mi->animated)
    return rig;
  const glb_file g = open_glb(path_in_kit(mi->path.c_str()));
  if (!g.ok)
    return rig;
  const json_value &nodes = g.json["nodes"];
  const std::vector<i32> parent = parents_of(nodes);
  for (usize i = 0; i < nodes.size(); ++i) {
    const json_value &n = nodes[i];
    module_rig::node_rest r;
    r.parent = parent[i];
    if (n.has("translation"))
      r.t = v3(n["translation"]);
    if (n.has("scale"))
      r.s = v3(n["scale"]);
    for (usize c = 0; c < 4 && n.has("rotation"); ++c)
      r.q[c] = n["rotation"][c].f32_or(c == 3 ? 1.0f : 0.0f);
    rig.nodes.push_back(r);
  }
  rig.clip = mi->anim_name;
  rig.rigs = static_cast<i32>(g.json["skins"].size());
  for (usize a = 0; a < g.json["animations"].size(); ++a) {
    const json_value &an = g.json["animations"][a];
    if (std::string(an["name"].string_or("")) != mi->anim_name)
      continue;
    for (usize c = 0; c < an["channels"].size(); ++c) {
      const json_value &ch = an["channels"][c];
      const std::string path = ch["target"]["path"].string_or("");
      const json_value &smp = an["samplers"][static_cast<usize>(ch["sampler"].int_or(0))];
      module_rig::track tr;
      tr.node = ch["target"]["node"].int_or(-1);
      tr.path = path == "translation" ? 0 : path == "rotation" ? 1 : path == "scale" ? 2 : -1;
      tr.step = std::string(smp["interpolation"].string_or("LINEAR")) == "STEP";
      tr.times = accessor_numbers(g, smp["input"].int_or(-1), nullptr);
      tr.values = accessor_numbers(g, smp["output"].int_or(-1), nullptr);
      if (tr.node >= 0 && tr.node < static_cast<i32>(nodes.size()) && tr.path >= 0 && !tr.times.empty())
        rig.tracks.push_back(std::move(tr));
    }
  }
  // Every skinned mesh node is a leaf, moved by the joint its vertices weigh on most.
  for (usize i = 0; i < nodes.size(); ++i) {
    const json_value &n = nodes[i];
    if (!n.has("skin") || !n.has("mesh"))
      continue;
    const i32 skin = n["skin"].int_or(0);
    const json_value &sk = g.json["skins"][static_cast<usize>(skin)];
    const json_value &prim = g.json["meshes"][static_cast<usize>(n["mesh"].int_or(0))]["primitives"][usize{0}];
    i32 jc = 0, wc = 0, pc = 0;
    const std::vector<f32> joints = accessor_numbers(g, prim["attributes"]["JOINTS_0"].int_or(-1), &jc);
    const std::vector<f32> weights = accessor_numbers(g, prim["attributes"]["WEIGHTS_0"].int_or(-1), &wc);
    const std::vector<f32> pos = accessor_numbers(g, prim["attributes"]["POSITION"].int_or(-1), &pc);
    std::vector<f32> sum(sk["joints"].size(), 0.0f);
    for (size_t v = 0; jc == 4 && wc == 4 && v + 3 < joints.size() && v + 3 < weights.size(); v += 4)
      for (size_t c = 0; c < 4; ++c) {
        const size_t j = static_cast<size_t>(joints[v + c]);
        if (j < sum.size())
          sum[j] += weights[v + c];
      }
    if (sum.empty())
      continue;
    const size_t best = static_cast<size_t>(std::max_element(sum.begin(), sum.end()) - sum.begin());
    f32 total = 0.0f;
    for (const f32 w : sum)
      total += w;
    if (total > 0.0f && sum[best] < total * 0.999f)
      NJIN_WARN("pbk: %s: leaf %s hangs on more than one bone; moved by its main one", module_id.c_str(),
                n["name"].string_or(""));
    const i32 joint = sk["joints"][best].int_or(-1);
    if (joint < 0)
      continue;
    i32 mc = 0;
    const std::vector<f32> ibm = accessor_numbers(g, sk["inverseBindMatrices"].int_or(-1), &mc);
    mat4 bind;
    if (mc == 16 && ibm.size() >= (best + 1) * 16)
      std::copy(ibm.begin() + static_cast<std::ptrdiff_t>(best * 16),
                ibm.begin() + static_cast<std::ptrdiff_t>(best * 16 + 16), bind.m);
    const mat4 rest = rest_world(nodes, parent, static_cast<i32>(i));
    rig_leaf leaf;
    leaf.node = n["name"].string_or("");
    leaf.skin = skin;
    leaf.joint = nodes[static_cast<usize>(joint)]["name"].string_or("");
    leaf.lo = {1e30f, 1e30f, 1e30f};
    leaf.hi = {-1e30f, -1e30f, -1e30f};
    for (size_t v = 0; pc == 3 && v + 2 < pos.size(); v += 3) {
      const vec3 q = mat4_point(rest, {pos[v], pos[v + 1], pos[v + 2]});
      leaf.lo = {std::min(leaf.lo.x, q.x), std::min(leaf.lo.y, q.y), std::min(leaf.lo.z, q.z)};
      leaf.hi = {std::max(leaf.hi.x, q.x), std::max(leaf.hi.y, q.y), std::max(leaf.hi.z, q.z)};
    }
    rig.leaves.push_back(leaf);
    rig.leaf_joint.push_back(joint);
    rig.leaf_bind.push_back(bind);
    rig.leaf_unload.push_back(mat4_affine_inverse(rest));
  }
  rig.ok = !rig.leaves.empty() && !rig.tracks.empty();
  if (!rig.ok)
    NJIN_WARN("pbk: %s: no skinned leaf moved by the clip %s", module_id.c_str(), mi->anim_name.c_str());
  else if (mi->rig_count > 0 && rig.rigs != mi->rig_count)
    NJIN_WARN("pbk: %s: %d rigs in the file, the manifest says %d", module_id.c_str(), rig.rigs, mi->rig_count);
  return rig;
}

} // namespace sandtable::city::pbk
