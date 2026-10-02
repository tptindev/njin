#include "pbk.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// The generator: a request and a seed in, a BuildingPlan or NoFit out.
//
// Every house it makes has the band layout the rule package measures in its
// examples: public rooms along the front, a corridor across the middle with
// the stair core at one end, service and private rooms along the back, the
// same core and wet stack on every floor (rules/README.vi.md, "Bố cục kiến
// trúc"). What the seed decides, each from its own stream: the style, the
// side of the core, the depth of the front band, the corridor's width, the
// order and widths of the rooms, the entrance bay and which windows a room
// gets. Each try is checked against every hard rule (check_plan); the best
// valid one of up to rules::retry_count tries is kept. A minimum is never
// shrunk to make a house fit: then the answer is NoFit with the reasons.

namespace sandtable::city::pbk {

namespace {

constexpr f32 core_w = 2.88f;  // two 1.2 m flights, the 0.2 m gap, 0.14 m each side
constexpr f32 core_len = 4.84f; // landing 1.42 + flight 2.0 + turn landing 1.4 + wall
constexpr f32 grid = 0.02f;

f32 snap(f32 v) { return std::round(v / grid) * grid; }

bool has(const std::vector<std::string> &v, const std::string &s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

// A room to place in a band: its type, and the widths that band must give it.
struct want {
  std::string type;
  f32 min_w = 0.0f, target_w = 0.0f;
};

want want_of(const rules &R, const std::string &type, f32 depth, f32 extra_area = 0.0f) {
  const room_rule *rr = R.room(type);
  want w{type};
  if (!rr)
    return w;
  w.min_w = std::max(rr->min_width, (rr->min_area + extra_area) / std::max(depth, 0.1f));
  w.target_w = std::max(w.min_w, (rr->target_area + extra_area) / std::max(depth, 0.1f));
  return w;
}

// Widths for `rooms` across `total` metres, gaps of a partition between them:
// every room its minimum, then toward its target, what is left to `flex`.
bool distribute(const std::vector<want> &rooms, f32 total, i32 flex, rng &r, std::vector<f32> &out) {
  const f32 gaps = partition * static_cast<f32>(rooms.size() - 1);
  f32 need = gaps;
  for (const want &w : rooms)
    need += w.min_w;
  if (need > total + 1e-4f)
    return false;
  out.assign(rooms.size(), 0.0f);
  f32 spare = total - need;
  f32 want_more = 0.0f;
  for (const want &w : rooms)
    want_more += w.target_w - w.min_w;
  for (size_t i = 0; i < rooms.size(); ++i) {
    const f32 more = want_more > 0.0f ? std::min(rooms[i].target_w - rooms[i].min_w,
                                                 spare * (rooms[i].target_w - rooms[i].min_w) / want_more)
                                      : 0.0f;
    out[i] = rooms[i].min_w + more * r.range(0.85f, 1.0f);
  }
  f32 used = gaps;
  for (const f32 w : out)
    used += w;
  out[static_cast<size_t>(flex)] += total - used;
  // On the 2 cm grid, the rounding given to the flexible room.
  used = gaps;
  for (size_t i = 0; i < out.size(); ++i)
    if (static_cast<i32>(i) != flex) {
      out[i] = std::max(snap(out[i]), rooms[i].min_w);
      used += out[i];
    }
  out[static_cast<size_t>(flex)] = total - used;
  return out[static_cast<size_t>(flex)] + 1e-4f >= rooms[static_cast<size_t>(flex)].min_w;
}

const char *pick_weighted(const std::vector<std::pair<std::string, f32>> &w, u32 seed, std::string &out) {
  f32 sum = 0.0f;
  for (const auto &[k, v] : w)
    sum += v;
  if (w.empty() || sum <= 0.0f)
    return nullptr;
  f32 x = static_cast<f32>(seed % 100000u) / 100000.0f * sum;
  for (const auto &[k, v] : w) {
    if (x < v) {
      out = k;
      return out.c_str();
    }
    x -= v;
  }
  out = w.back().first;
  return out.c_str();
}

// The glazing a module gives (measured by the rule examples).
f32 glazing_of(const std::string &module_id) {
  return module_id.find("Shopfront") != std::string::npos ? 3.8f : 1.18f * 1.5f;
}

std::string arc_id(const std::string &style, const char *role, f32 radius) {
  const i32 a = radius > 3.0f ? 30 : 45;
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%s/Arc%s_R%d_A%d", style.c_str(), role, static_cast<i32>(radius), a);
  return buf;
}

struct band_rooms {
  std::vector<want> front, rear;
};

// The rooms each floor puts in the front band and along the back, from the
// archetype's program: what must be there, and optional rooms that fill the
// rest when they fit.
struct floor_program {
  std::vector<std::string> front;     // in order along the front, west to east
  std::vector<std::string> rear;      // west to east, bathroom first
  std::vector<bool> rear_optional;
};

bool shop_type(const std::string &archetype) { return archetype.find("shop") != std::string::npos; }

struct attempt {
  plan p;
  std::vector<std::string> fail;
  f32 score = 0.0f;
};

void add_portal(plan &p, const std::string &from, const std::string &to, i32 floor, vec2 c, const char *axis,
                const char *kind, f32 aperture, f32 clear, const std::string &module = {}) {
  portal q;
  char id[16];
  std::snprintf(id, sizeof(id), "p%02d", static_cast<i32>(p.portals.size()));
  q.id = id;
  q.from = from;
  q.to = to;
  q.floor_from = q.floor_to = floor;
  q.center = c;
  q.axis = axis;
  q.normal = std::string(axis) == "x" ? vec2{1.0f, 0.0f} : vec2{0.0f, 1.0f};
  q.kind = kind;
  q.aperture = aperture;
  q.clear = clear;
  q.height = 2.35f;
  q.module_id = module;
  p.portals.push_back(q);
}

// What every layout family shares once its rooms, doors between rooms and
// stair are laid: the street door into `entry_id`, the stairs' vertical
// links, a window for every room that needs daylight, the modules named.
bool finish_plan(attempt &at, const std::string &style, f32 radius, u32 facade_seed, rng &r,
                 const std::string &entry_id) {
  plan &p = at.p;
  const rules &R = load_rules();
  const i32 floors = p.storeys();
  const bool multi = floors > 1;
  const bool rnd = p.shape == "CornerShopHouseRounded";
  const f32 W = p.width;
  const f32 door_ap = 1.05f;
  bool shop = false;
  for (const room &rm : p.floors[0].rooms)
    shop = shop || rm.type == "shop" || rm.type == "market_floor";
  // The street door: into the front public room, in a bay of the front away
  // from the corners; on the rounded corner, planar on the curve's middle.
  const std::vector<bay_slot> bays = exterior_bays(p);
  if (entry_id.empty() || !p.find_room(0, entry_id)) {
    at.fail.push_back("no public room on the ground floor for the street door");
    return false;
  }
  const std::string door_module = style + "/DoorRigged";
  i32 door_bay = -1;
  if (rnd) {
    const vec2 c{W - radius, radius};
    const f32 a = -45.0f * pi / 180.0f;
    const vec2 at_arc = c + vec2{std::cos(a), std::sin(a)} * radius;
    add_portal(p, "outside", entry_id, 0, at_arc, "tangent", "door", door_ap, 0.95f, door_module);
    p.portals.back().normal = vec2{-std::cos(a), -std::sin(a)};
  } else {
    std::vector<i32> ok;
    for (i32 i = 0; i < static_cast<i32>(bays.size()); ++i) {
      const bay_slot &b = bays[static_cast<size_t>(i)];
      if (b.side != "south" || ((b.first || b.last) && b.count > 2))
        continue;
      const room *behind = room_behind(p, 0, b);
      if (behind && behind->id == entry_id)
        ok.push_back(i);
    }
    if (ok.empty()) {
      at.fail.push_back("no front bay for the street door into " + entry_id);
      return false;
    }
    // Toward the middle of the front room.
    door_bay = ok[static_cast<size_t>(r.range(0, static_cast<i32>(ok.size()) - 1))];
    const bay_slot &b = bays[static_cast<size_t>(door_bay)];
    add_portal(p, "outside", entry_id, 0, b.center - b.out * (ext_wall * 0.5f), "y", "door", door_ap, 0.95f,
               door_module);
  }
  // The vertical links.
  for (i32 f = 0; f + 1 < floors; ++f) {
    portal q;
    q.id = "vertical_" + std::to_string(f);
    q.from = "f" + std::to_string(f) + "_stair";
    q.to = "f" + std::to_string(f + 1) + "_stair";
    q.floor_from = f;
    q.floor_to = f + 1;
    q.center = {(p.stair.lower_landing.x0 + p.stair.lower_landing.x1) * 0.5f,
                (p.stair.lower_landing.y0 + p.stair.lower_landing.y1) * 0.5f};
    q.axis = "vertical";
    q.kind = "stair_link";
    q.aperture = q.clear = p.stair.clear_width;
    q.height = storey;
    p.portals.push_back(q);
  }

  // Daylight: a window (or the shop's glass) for every room that needs it,
  // until its glass is the rule's share of its floor; the shop gets every
  // bay of the street front on the ground floor.
  rng fr(facade_seed);
  for (i32 f = 0; f < floors; ++f) {
    floor_plan &fl = p.floors[static_cast<size_t>(f)];
    for (room &rm : fl.rooms) {
      const room_rule *rr = R.room(rm.type);
      const bool is_shop = rm.type == "shop";
      if (!rr || (!rr->daylight && !is_shop))
        continue;
      std::vector<const bay_slot *> cand;
      for (size_t i = 0; i < bays.size(); ++i) {
        const bay_slot &b = bays[i];
        if (f == 0 && static_cast<i32>(i) == door_bay)
          continue;
        if (rnd && f == 0 && b.arc && b.index == b.count / 2)
          continue; // the door's piece
        if (!side_glazable(p, b.side))
          continue;
        const room *behind = room_behind(p, f, b);
        if (behind && behind->id == rm.id)
          cand.push_back(&b);
      }
      // Reuse the elevation's axes across floors; room/daylight constraints
      // may use secondary bays, but never choose a new random grid per floor.
      std::stable_sort(cand.begin(), cand.end(), [&](const bay_slot *a, const bay_slot *b) {
        const auto rank = [&](const bay_slot *b) {
          f32 value = (has(p.frontages, b->side) ? 4.0f : 0.0f) + (facade_axis(*b) ? 16.0f : 0.0f);
          for (const auto &q : p.apertures)
            if (q.floor == f - 1 && distance(q.center, b->center) < 0.05f) value += 6.0f;
          // A centered pair/axis reads as a group; the extreme corner bays
          // are reserves, used only when the room needs extra glazing.
          value -= std::fabs(static_cast<f32>(b->index) - (b->count - 1) * 0.5f) * 0.1f;
          return value;
        };
        return rank(a) > rank(b);
      });
      f32 glass = 0.0f;
      const f32 need = R.glazing_ratio * rm.area;
      for (const bay_slot *b : cand) {
        const bool shop_front = is_shop && f == 0 && has(p.frontages, b->side);
        if (glass >= need && !shop_front)
          break;
        aperture a;
        a.floor = f;
        a.room = rm.id;
        a.center = b->center;
        a.inward = -b->out;
        if (b->arc)
          a.module_id = arc_id(style, shop_front ? "Shopfront" : "Window", radius);
        else
          a.module_id = style + (shop_front ? "/Shopfront" : "/Window");
        a.glazed = glazing_of(a.module_id);
        glass += a.glazed;
        p.apertures.push_back(a);
      }
      for (const bay_slot *b : cand)
        if (std::find(rm.daylight_edges.begin(), rm.daylight_edges.end(), b->side) == rm.daylight_edges.end())
          rm.daylight_edges.push_back(b->side);
    }
  }
  (void)fr;

  // The modules the plan names.
  const auto need_module = [&](const std::string &id) {
    if (!has(p.required_modules, id))
      p.required_modules.push_back(id);
  };
  for (const char *role : {"/Wall", "/Window", "/DoorRigged", "/Column", "/Parapet"})
    need_module(style + role);
  if (shop)
    need_module(style + "/Shopfront");
  if (rnd)
    for (const char *role : {"Wall", "Window", "Shopfront", "Parapet"})
      need_module(arc_id(style, role, radius));
  for (const aperture &a : p.apertures)
    need_module(a.module_id);
  p.runtime_geometry = {"interior_partitions", "interior_portal_frames_and_leaves", "ceiling", "slab_with_voids"};
  if (multi)
    p.runtime_geometry.push_back(p.stair.type == "straight" ? "straight_stair_kit_module" : "dogleg_stair");
  if (multi && p.stair.type == "straight")
    need_module(style + "/Stair");
  p.provenance = "Generated at runtime by sandtable pbk_gen.cpp from building_rules.json";
  p.kit_revision = R.kit_revision;

  return true;
}

// One try at a layout.
attempt try_layout(const request &req, const archetype_rule &A, const preset_rule &P, const std::string &style,
                   f32 radius, u32 layout_seed, u32 facade_seed) {
  const rules &R = load_rules();
  attempt at;
  plan &p = at.p;
  rng r(layout_seed);
  const f32 W = req.width, D = req.depth;
  const i32 floors = req.floors;
  const bool multi = floors > 1;
  const bool rnd = A.shape == "CornerShopHouseRounded";
  const bool wing = A.shape == "LShape" || A.shape == "TShape";
  const f32 rear_left = A.shape == "TShape" ? 4.0f : 0.0f, rear_right = wing ? 4.0f : 0.0f;
  const bool shop = shop_type(req.archetype);
  p.archetype = req.archetype;
  p.style = style;
  p.space_preset = req.space_preset.empty() ? A.preset : req.space_preset;
  p.width = W;
  p.depth = D;
  p.shape = A.shape;
  p.radius = rnd ? radius : 0.0f;
  p.frontages = A.frontages;
  p.outer = footprint_inset(p, 0.0f);
  const polygon inner = footprint_inset(p, ext_wall);

  // The bands, front to back.
  const f32 x0 = ext_wall, x1 = W - ext_wall, y_back = D - ext_wall;
  const f32 iw = x1 - x0, id = y_back - ext_wall;
  const bool core_west = rnd || wing || r.chance(0.5f);
  const f32 corridor = snap(r.range(P.corridor_min, std::max(P.corridor_min, P.corridor_target - 0.1f)));

  // The rooms of each floor.
  std::vector<floor_program> prog(static_cast<size_t>(floors));
  // A narrow lot has room for one room across the front: the kitchen or the
  // second bedroom goes to the back, at the end with the rear window.
  const bool narrow = iw < 9.0f;
  for (i32 f = 0; f < floors; ++f) {
    floor_program &fp = prog[static_cast<size_t>(f)];
    const std::vector<std::string> &need = f == 0 ? A.ground : A.upper;
    if (narrow && !(shop && f == 0)) {
      fp.front = {f == 0 ? "living" : shop ? "living_dining_kitchen" : "bedroom"};
      fp.rear = {"bathroom", "utility", f == 0 ? "kitchen" : "bedroom"};
      fp.rear_optional = {false, true, false};
    } else if (shop) {
      if (f == 0) {
        fp.front = {"shop"};
        fp.rear = {"bathroom", "storage", "office"};
        fp.rear_optional = {false, false, true};
      } else {
        fp.front = {"living_dining_kitchen", "bedroom"};
        fp.rear = {"bathroom", "utility", r.chance(0.5f) ? "office" : "bedroom"};
        fp.rear_optional = {false, true, true};
      }
    } else if (f == 0) {
      fp.front = {"living", "kitchen"};
      fp.rear = {"bathroom", "utility", "bedroom"};
      fp.rear_optional = {false, true, true};
    } else {
      fp.front = {r.chance(0.5f) ? "living" : "bedroom", "bedroom"};
      fp.rear = {"bathroom", "utility", r.chance(0.6f) ? "bedroom" : "office"};
      fp.rear_optional = {false, true, true};
    }
    for (const std::string &t : need)
      if (!has(fp.front, t) && !has(fp.rear, t)) {
        at.fail.push_back("program room " + t + " has no slot in the band layout");
        return at;
      }
    if (r.chance(0.5f) && fp.front.size() == 2)
      std::swap(fp.front[0], fp.front[1]);
  }

  // Depths: the front band, then the corridor and the back band (beside the core).
  f32 rear_min_w = 0.0f;
  for (const floor_program &fp : prog)
    for (size_t i = 0; i < fp.rear.size(); ++i)
      if (!fp.rear_optional[i])
        if (const room_rule *rr = R.room(fp.rear[i]))
          rear_min_w = std::max(rear_min_w, rr->min_width);
  f32 front_min = 0.0f;
  for (const floor_program &fp : prog)
    for (const std::string &t : fp.front)
      if (const room_rule *rr = R.room(t))
        front_min = std::max(front_min, rr->min_width);
  if (rnd)
    front_min = std::max(front_min, radius - ext_wall - partition); // the corridor clear of the curve
  const f32 back_need = std::max(multi ? core_len : 0.0f, corridor + partition + std::max(rear_min_w, 2.4f));
  const f32 front_max = id - partition - back_need;
  if (front_max < front_min) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "depth %.1f m: front band needs %.2f m and core/corridor/back %.2f m", D,
                  front_min, back_need);
    at.fail.push_back(buf);
    return at;
  }
  const f32 fd = wing ? snap(D - 4.0f - ext_wall - corridor - 2.0f * partition) :
      snap(front_min + (front_max - front_min) * r.range(0.45f, 0.85f));
  if (fd < front_min || fd > front_max) { at.fail.push_back("wing body cannot fit the front rooms and stair"); return at; }
  const f32 yf = ext_wall + fd;
  const f32 y_hall = yf + partition;
  const f32 y_rear = y_hall + corridor + partition;
  const f32 rd = y_back - y_rear;

  // Across: the core at one end of the corridor.
  const f32 cx0 = core_west ? x0 + rear_left : x1 - core_w;
  const f32 hall_x0 = multi ? (core_west ? x0 + rear_left + core_w + partition : x0) : x0 + rear_left;
  const f32 hall_x1 = multi ? (core_west ? x1 - rear_right : x1 - core_w - partition) : x1 - rear_right;
  const f32 hall_w = hall_x1 - hall_x0;

  // The back rooms, the same on every floor so the wet stack lines up.
  std::vector<std::vector<std::string>> rear_types(static_cast<size_t>(floors));
  std::vector<want> rear;
  {
    // Slots: what each floor puts in slot k; a slot's minimum is the most any floor needs.
    const size_t slots = 3;
    std::vector<bool> keep(slots, true);
    const auto slot_want = [&](size_t k) {
      want w{"slot"};
      for (const floor_program &fp : prog) {
        const want x = want_of(R, fp.rear[k], rd);
        w.min_w = std::max(w.min_w, x.min_w);
        w.target_w = std::max(w.target_w, x.target_w);
      }
      return w;
    };
    std::vector<f32> widths;
    for (i32 drop = 0; drop < 3; ++drop) {
      rear.clear();
      for (size_t k = 0; k < slots; ++k)
        if (keep[k])
          rear.push_back(slot_want(k));
      if (distribute(rear, hall_w, static_cast<i32>(rear.size()) - 1, r, widths))
        break;
      widths.clear();
      // Drop the last optional slot and try again (rules: reduce optional rooms).
      bool dropped = false;
      for (size_t k = slots; k-- > 0 && !dropped;) {
        bool optional = keep[k];
        for (const floor_program &fp : prog)
          optional = optional && fp.rear_optional[k];
        if (optional) {
          keep[k] = false;
          dropped = true;
        }
      }
      if (!dropped)
        break;
    }
    if (widths.empty()) {
      char buf[160];
      std::snprintf(buf, sizeof(buf), "back band %.2f x %.2f m cannot hold the bathroom and service rooms", hall_w, rd);
      at.fail.push_back(buf);
      return at;
    }
    if (core_west == false)
      std::reverse(widths.begin(), widths.end()); // bathroom next to the core either way
    f32 x = hall_x0;
    std::vector<box2> rects;
    for (const f32 w : widths) {
      rects.push_back({x, y_rear, x + w, y_back});
      x += w + partition;
    }
    if (!core_west)
      std::reverse(rects.begin(), rects.end());
    for (i32 f = 0; f < floors; ++f) {
      size_t ri = 0;
      for (size_t k = 0; k < slots; ++k)
        if (keep[k]) {
          room rm;
          rm.type = prog[static_cast<size_t>(f)].rear[k];
          rm.poly = rect_poly(rects[ri++]);
          rear_types[static_cast<size_t>(f)].push_back(rm.type);
          char id[32];
          std::snprintf(id, sizeof(id), "f%d_rear%d", f, static_cast<i32>(ri));
          rm.id = id;
          p.floors.resize(static_cast<size_t>(floors));
          p.floors[static_cast<size_t>(f)].rooms.push_back(rm);
        }
    }
  }
  p.floors.resize(static_cast<size_t>(floors));
  for (i32 f = 0; f < floors; ++f) {
    p.floors[static_cast<size_t>(f)].level = f;
    p.floors[static_cast<size_t>(f)].elevation = static_cast<f32>(f) * storey;
  }

  // The front band of each floor: one or two rooms, each opening onto the corridor.
  for (i32 f = 0; f < floors; ++f) {
    floor_program &fp = prog[static_cast<size_t>(f)];
    std::vector<f32> widths;
    std::vector<want> front;
    for (size_t k = 0; k < fp.front.size(); ++k) {
      // The room by the rounded corner loses the curve's corner to it.
      const bool by_curve = rnd && k + 1 == fp.front.size();
      const f32 lost = by_curve ? (radius - ext_wall) * (radius - ext_wall) * (1.0f - pi / 4.0f) : 0.0f;
      front.push_back(want_of(R, fp.front[k], fd, lost));
    }
    while (!distribute(front, iw, 0, r, widths)) {
      // Too narrow for both: the second room goes, if the program allows.
      const std::vector<std::string> &need = f == 0 ? A.ground : A.upper;
      if (front.size() < 2) {
        at.fail.push_back("width " + std::to_string(static_cast<i32>(W)) + " m cannot hold a " + fp.front[0]);
        return at;
      }
      const std::string gone = front.back().type;
      const bool needed_elsewhere = has(rear_types[static_cast<size_t>(f)], gone);
      if (has(need, gone) && !needed_elsewhere) {
        at.fail.push_back("width " + std::to_string(static_cast<i32>(W)) + " m: " + fp.front[0] + " and " + gone +
                          " do not fit side by side");
        return at;
      }
      front.pop_back();
      fp.front.pop_back();
    }
    if (wing && widths.size() == 2) {
      // Rear wings have a central access spine: balance the front rooms so
      // both meet it, rather than giving all spare width to the first room.
      widths[0] = std::max((iw - partition) * 0.5f, hall_x0 - x0 + 1.05f + 0.74f);
      widths[1] = iw - partition - widths[0];
    }
    // Each front room must meet the corridor wide enough for its doorway: the
    // narrow one goes beside the corridor, not over the stair core.
    const auto reaches = [&](const std::vector<f32> &ws, const std::vector<want> &) {
      f32 x = x0;
      for (size_t k = 0; k < ws.size(); ++k) {
        const f32 lo = std::max(x, hall_x0) + 0.35f, hi = std::min(x + ws[k], hall_x1) - 0.35f;
        if (hi - lo < 1.05f)
          return false;
        x += ws[k] + partition;
      }
      return true;
    };
    if (front.size() == 2 && !reaches(widths, front)) {
      std::swap(front[0], front[1]);
      std::swap(fp.front[0], fp.front[1]);
      std::swap(widths[0], widths[1]);
      if (rnd) {
        // The curve's corner went with the room by it: share it out again.
        if (!distribute(front, iw, 0, r, widths))
          widths.clear();
      }
    }
    if (widths.empty() || !reaches(widths, front)) {
      at.fail.push_back("floor " + std::to_string(f) + ": no order of the front rooms reaches the corridor");
      return at;
    }
    f32 x = x0;
    for (size_t k = 0; k < front.size(); ++k) {
      room rm;
      rm.type = front[k].type;
      rm.poly = rect_poly({x, ext_wall, x + widths[k], yf});
      if (rnd)
        rm.poly = clip_convex(rm.poly, inner);
      char id[32];
      std::snprintf(id, sizeof(id), "f%d_front%d", f, static_cast<i32>(k + 1));
      rm.id = id;
      p.floors[static_cast<size_t>(f)].rooms.push_back(rm);
      x += widths[k] + partition;
    }
  }

  // The corridor and the stair core.
  for (i32 f = 0; f < floors; ++f) {
    floor_plan &fl = p.floors[static_cast<size_t>(f)];
    room hall;
    hall.id = "f" + std::to_string(f) + "_hall";
    hall.type = "corridor";
    hall.poly = rect_poly({hall_x0, y_hall, hall_x1, y_hall + corridor});
    fl.rooms.push_back(hall);
    if (multi) {
      room core;
      core.id = "f" + std::to_string(f) + "_stair";
      core.type = "stair";
      core.poly = rect_poly({cx0, y_hall, cx0 + core_w, y_back});
      fl.rooms.push_back(core);
    }
  }
  if (multi) {
    stair_core &s = p.stair;
    s.on = true;
    const f32 cy = y_hall;
    // West core as measured by the examples; an east core is its mirror.
    const auto mx = [&](f32 xr) { return core_west ? cx0 + xr : cx0 + core_w - xr; };
    const auto bx = [&](f32 a, f32 b, f32 y0, f32 y1) {
      return box2{std::min(mx(a), mx(b)), y0, std::max(mx(a), mx(b)), y1};
    };
    s.lower_landing = bx(0.14f, 1.34f, cy, cy + 1.42f);
    s.lower_flight = bx(0.14f, 1.34f, cy + 1.42f, cy + 3.42f);
    s.turn_landing = bx(0.14f, 2.74f, cy + 3.42f, cy + 4.82f);
    s.upper_flight = bx(1.54f, 2.74f, cy + 1.67f, cy + 3.42f);
    s.upper_landing = bx(1.54f, 2.74f, cy + 0.27f, cy + 1.67f);
    s.walking_line = {{mx(0.74f), cy + 0.94f, 0.0f}, {mx(0.74f), cy + 1.42f, 0.0f}, {mx(0.74f), cy + 3.42f, 1.6f},
                      {mx(0.74f), cy + 4.12f, 1.6f}, {mx(2.14f), cy + 4.12f, 1.6f}, {mx(2.14f), cy + 3.42f, 1.6f},
                      {mx(2.14f), cy + 1.67f, 3.0f}, {mx(2.14f), cy + 0.94f, 3.0f}};
    for (i32 f = 0; f < floors; ++f)
      s.room_ids.push_back("f" + std::to_string(f) + "_stair");
    // The hole each upper slab needs over the flights and the turn landing.
    polygon v = {{mx(0.02f), cy + 1.30f}, {mx(1.42f), cy + 1.30f}, {mx(1.42f), cy + 1.67f},
                 {mx(2.86f), cy + 1.67f}, {mx(2.86f), cy + 4.94f}, {mx(0.02f), cy + 4.94f}};
    if (!core_west)
      std::reverse(v.begin(), v.end());
    for (i32 f = 1; f < floors; ++f)
      p.floors[static_cast<size_t>(f)].voids.push_back(v);
  }

  // Net areas: the stair room less the hole in its slab.
  for (floor_plan &fl : p.floors)
    for (room &rm : fl.rooms) {
      rm.area = polygon_area(rm.poly);
      if (rm.type == "stair")
        for (const polygon &v : fl.voids)
          rm.area -= polygon_area(v);
      rm.furniture_target = std::min(0.2f, P.furniture_max);
    }

  // Doors between rooms.
  const f32 door_ap = 1.05f;
  for (i32 f = 0; f < floors; ++f) {
    floor_plan &fl = p.floors[static_cast<size_t>(f)];
    const std::string hall_id = "f" + std::to_string(f) + "_hall";
    for (const room &rm : fl.rooms) {
      const box2 b = bounds_of(rm.poly);
      if (rm.type == "corridor" || rm.type == "stair")
        continue;
      const bool front = b.y1 <= y_hall;
      const f32 lo = std::max(b.x0, hall_x0) + 0.35f, hi = std::min(b.x1, hall_x1) - 0.35f;
      const bool public_room = rm.type == "living" || rm.type == "shop" || rm.type == "living_dining_kitchen";
      const bool open_way = front && public_room && std::min(b.x1, hall_x1) - std::max(b.x0, hall_x0) - 0.7f >= 1.6f;
      const f32 ap = open_way ? 1.6f : door_ap;
      if (hi - lo < ap) {
        at.fail.push_back(rm.id + " (" + rm.type + ") does not reach the corridor for a doorway");
        return at;
      }
      f32 cxr = (lo + hi) * 0.5f;
      if (!front && rm.type == "bathroom")
        cxr = core_west ? lo + ap * 0.5f + 0.05f : hi - ap * 0.5f - 0.05f;
      cxr = snap(cxr);
      if (front)
        add_portal(p, rm.id, hall_id, f, {cxr, yf + partition * 0.5f}, "y", open_way ? "open" : "door", ap,
                   open_way ? ap : 0.95f);
      else
        add_portal(p, hall_id, rm.id, f, {cxr, y_rear - partition * 0.5f}, "y", "door", ap, 0.95f);
    }
    if (multi) {
      const f32 xw = core_west ? cx0 + core_w + partition * 0.5f : cx0 - partition * 0.5f;
      add_portal(p, hall_id, "f" + std::to_string(f) + "_stair", f, {xw, y_hall + 0.84f}, "x", "door", door_ap, 0.95f);
    }
  }

  // The street door into the front public room, the stairs' links, the
  // windows, the modules.
  std::string entry_id;
  for (const room &rm : p.floors[0].rooms)
    if (rm.type == "shop" || rm.type == "living")
      entry_id = rm.id;
  if (!finish_plan(at, style, radius, facade_seed, r, entry_id))
    return at;

  // What was chosen, to make the same house again.
  json_value g = json_value::make_object();
  g.set("core_side", core_west ? "west" : "east").set("front_band_m", fd).set("corridor_m", corridor);
  g.set("back_band_m", rd).set("layout_seed", static_cast<f64>(layout_seed));
  g.set("facade_seed", static_cast<f64>(facade_seed));
  p.generator = g;
  return at;
}

// --- Shared by the layout families ------------------------------------------------------

// A plan's frame from a request: size, shape, frontages, its floors.
void begin_plan(plan &p, const request &req, const archetype_rule &A, const std::string &style) {
  p.archetype = req.archetype;
  p.style = style;
  p.space_preset = req.space_preset.empty() ? A.preset : req.space_preset;
  p.width = req.width;
  p.depth = req.depth;
  p.shape = A.shape;
  p.radius = 0.0f;
  p.frontages = A.frontages;
  p.outer = footprint_inset(p, 0.0f);
  p.floors.resize(static_cast<size_t>(req.floors));
  for (i32 f = 0; f < req.floors; ++f) {
    p.floors[static_cast<size_t>(f)].level = f;
    p.floors[static_cast<size_t>(f)].elevation = static_cast<f32>(f) * storey;
  }
}

std::string room_id(i32 f, const char *what, i32 k = -1) {
  return "f" + std::to_string(f) + "_" + what + (k >= 0 ? std::to_string(k) : std::string());
}

void add_room(plan &p, i32 f, const std::string &id, const std::string &type, const box2 &b) {
  room rm;
  rm.id = id;
  rm.type = type;
  rm.poly = rect_poly(b);
  p.floors[static_cast<size_t>(f)].rooms.push_back(rm);
}

// Net areas (a stair room less the hole in its slab) and the furniture share.
void finish_areas(plan &p, const preset_rule &P) {
  for (floor_plan &fl : p.floors)
    for (room &rm : fl.rooms) {
      rm.area = polygon_area(rm.poly);
      if (rm.type == "stair")
        for (const polygon &v : fl.voids)
          rm.area -= polygon_area(v);
      rm.furniture_target = std::min(0.2f, P.furniture_max);
    }
}

// Whether `type` can stand `width` metres across at all.
bool fits_across(const rules &R, const std::string &type, f32 width) {
  const room_rule *rr = R.room(type);
  return rr && width + 1e-3f >= rr->min_width;
}

// The least depth of a room of `type` across `width`: its area, and its clear
// width when the room is narrower one way than the other.
f32 least_depth(const rules &R, const std::string &type, f32 width) {
  const room_rule *rr = R.room(type);
  if (!rr)
    return 0.0f;
  return snap(std::max(rr->min_area / std::max(width, 0.1f), rr->min_width) + 0.02f);
}

// A dogleg core in the rectangle `core` (2.88 x 4.84 m or more): the
// examples' measured flights and landings, mirrored to put the landing at
// the core's west or east side and its front or back end. `door` is where
// the core's own door goes on its long side toward the rest of the floor
// (an x-axis portal), on the landing end.
struct dogleg_layout {
  stair_core s;
  polygon void_poly;
  vec2 side_door{}; // on the core's side wall, by the landing
  vec2 end_door{};  // on the core's end wall, onto the landing
};

dogleg_layout make_dogleg(const box2 &core, bool landing_west, bool landing_front, i32 floors) {
  dogleg_layout d;
  stair_core &s = d.s;
  s.on = true;
  s.type = "dogleg";
  const f32 W = 2.88f, L = 4.84f;
  const f32 cx = landing_west ? core.x0 : core.x1 - W;
  const f32 cy = landing_front ? core.y0 : core.y1 - L;
  const auto mx = [&](f32 xr) { return landing_west ? cx + xr : cx + W - xr; };
  const auto my = [&](f32 yr) { return landing_front ? cy + yr : cy + L - yr; };
  const auto bx = [&](f32 a, f32 b, f32 y0, f32 y1) {
    return box2{std::min(mx(a), mx(b)), std::min(my(y0), my(y1)), std::max(mx(a), mx(b)), std::max(my(y0), my(y1))};
  };
  s.lower_landing = bx(0.14f, 1.34f, 0.0f, 1.42f);
  s.lower_flight = bx(0.14f, 1.34f, 1.42f, 3.42f);
  s.turn_landing = bx(0.14f, 2.74f, 3.42f, 4.82f);
  s.upper_flight = bx(1.54f, 2.74f, 1.67f, 3.42f);
  s.upper_landing = bx(1.54f, 2.74f, 0.27f, 1.67f);
  s.walking_line = {{mx(0.74f), my(0.94f), 0.0f}, {mx(0.74f), my(1.42f), 0.0f}, {mx(0.74f), my(3.42f), 1.6f},
                    {mx(0.74f), my(4.12f), 1.6f}, {mx(2.14f), my(4.12f), 1.6f}, {mx(2.14f), my(3.42f), 1.6f},
                    {mx(2.14f), my(1.67f), 3.0f}, {mx(2.14f), my(0.94f), 3.0f}};
  for (i32 f = 0; f < floors; ++f)
    s.room_ids.push_back(room_id(f, "stair"));
  polygon v = {{mx(0.02f), my(1.30f)}, {mx(1.42f), my(1.30f)}, {mx(1.42f), my(1.67f)},
               {mx(2.86f), my(1.67f)}, {mx(2.86f), my(4.94f)}, {mx(0.02f), my(4.94f)}};
  d.void_poly = v;
  const f32 side_x = landing_west ? cx + W + partition * 0.5f : cx - partition * 0.5f;
  d.side_door = {side_x, my(0.84f)};
  d.end_door = {mx(0.74f), landing_front ? cy - partition * 0.5f : cy + L + partition * 0.5f};
  return d;
}

void add_vertical_voids(plan &p, const polygon &v) {
  for (i32 f = 1; f < p.storeys(); ++f)
    p.floors[static_cast<size_t>(f)].voids.push_back(v);
}

// Widths for `n` rooms across `total`, partitions between, all equal.
std::vector<f32> equal_widths(f32 total, i32 n) {
  std::vector<f32> out;
  const f32 w = (total - partition * static_cast<f32>(n - 1)) / static_cast<f32>(n);
  for (i32 i = 0; i < n; ++i)
    out.push_back(i + 1 < n ? snap(w) : 0.0f);
  f32 used = partition * static_cast<f32>(n - 1);
  for (i32 i = 0; i + 1 < n; ++i)
    used += out[static_cast<size_t>(i)];
  out.back() = total - used;
  return out;
}

// --- Tube: nhà ống ------------------------------------------------------------------------
//
// A narrow lot's floors, front to back: one room across the whole width;
// the stair room, a straight flight of the kit's Stair along one side wall
// with the way past it beside; behind it the bathroom (over the same spot
// on every floor) and the back room. Two bays wide, the way to the back room
// runs on beside the bathroom; three or four, the two sit side by side off
// the stair room.

attempt try_tube(const request &req, const archetype_rule &A, const preset_rule &P, const std::string &style,
                 u32 layout_seed, u32 facade_seed) {
  const rules &R = load_rules();
  attempt at;
  plan &p = at.p;
  rng r(layout_seed);
  begin_plan(p, req, A, style);
  const i32 floors = req.floors;
  const bool multi = floors > 1;
  const bool shop = has(A.ground, "shop");
  const f32 W = req.width, D = req.depth;
  const f32 x0 = ext_wall, x1 = W - ext_wall, iw = x1 - x0;
  const f32 y0 = ext_wall, y_back = D - ext_wall;
  const bool stair_west = r.chance(0.5f);
  const f32 flight_w = R.stair_flight;
  const f32 run = 0.25f * static_cast<f32>(R.risers); // 15 treads of 0.25 m: 3.75 m
  const f32 landing = R.stair_landing;
  // The rooms of each floor: the front one and the back one.
  std::vector<std::string> front(static_cast<size_t>(floors)), back(static_cast<size_t>(floors));
  for (i32 f = 0; f < floors; ++f) {
    if (f == 0) {
      front[0] = shop ? "shop" : "living";
      back[0] = "kitchen";
    } else {
      front[static_cast<size_t>(f)] = shop && f == 1 ? "living_dining_kitchen" : "bedroom";
      back[static_cast<size_t>(f)] = "bedroom";
    }
  }
  if (!multi && shop)
    back[0] = "kitchen";
  for (i32 f = 0; f < floors; ++f)
    if (!fits_across(R, front[static_cast<size_t>(f)], iw)) {
      at.fail.push_back("width " + std::to_string(static_cast<i32>(W)) + " m: a " + front[static_cast<size_t>(f)] +
                        " needs " + std::to_string(R.room(front[static_cast<size_t>(f)])->min_width) + " m across");
      return at;
    }
  const bool narrow = iw < 1.8f + partition + 2.4f + 0.01f; // two bays: the bathroom and the back room in line
  const f32 bath_w = 1.8f;
  const f32 side_w = narrow ? iw : iw - bath_w - partition; // the back room's width
  // Depths: front, the stair (or a hall on one floor), bathroom, back.
  f32 fd = 0.0f;
  for (i32 f = 0; f < floors; ++f)
    fd = std::max(fd, least_depth(R, front[static_cast<size_t>(f)], iw));
  const f32 sd = multi ? snap(landing + run + landing + 0.04f) : snap(std::max(P.corridor_min, 1.4f) + 0.2f);
  const f32 bath_d = least_depth(R, "bathroom", bath_w);
  f32 bd = 0.0f;
  for (i32 f = 0; f < floors; ++f)
    bd = std::max(bd, least_depth(R, back[static_cast<size_t>(f)], side_w));
  // Two bays: bathroom, then the back room behind it; wider: side by side.
  const f32 back_zone = narrow ? bath_d + partition + bd : std::max(bd, bath_d);
  const f32 need = fd + partition + sd + partition + back_zone;
  const f32 have = y_back - y0;
  if (need > have + 1e-3f) {
    char buf[200];
    std::snprintf(buf, sizeof(buf), "depth %.0f m: a %.0f m wide tube house needs %.2f m inside, has %.2f", D, W, need,
                  have);
    at.fail.push_back(buf);
    return at;
  }
  // What is left goes to the front room and the back, by the seed.
  const f32 spare = have - need;
  const f32 to_front = snap(spare * r.range(0.45f, 0.75f));
  const f32 yf = y0 + fd + to_front;
  const f32 ys0 = yf + partition, ys1 = ys0 + sd;
  const f32 yb0 = ys1 + partition;
  const f32 sx0 = stair_west ? x0 : x1 - flight_w, sx1 = sx0 + flight_w;
  for (i32 f = 0; f < floors; ++f) {
    add_room(p, f, room_id(f, "front"), front[static_cast<size_t>(f)], {x0, y0, x1, yf});
    add_room(p, f, room_id(f, multi ? "stair" : "hall"), multi ? "stair" : "corridor", {x0, ys0, x1, ys1});
    // The bathroom on the stair's side, the back room on the other (or behind).
    const f32 bx0 = stair_west ? x0 : x1 - bath_w, bx1 = bx0 + bath_w;
    if (narrow) {
      const f32 cx0 = stair_west ? bx1 + partition : x0, cx1 = stair_west ? x1 : bx0 - partition;
      const f32 yb1 = yb0 + bath_d;
      add_room(p, f, room_id(f, "bath"), "bathroom", {bx0, yb0, bx1, yb1});
      add_room(p, f, room_id(f, "way"), "corridor", {cx0, yb0, cx1, yb1});
      add_room(p, f, room_id(f, "back"), back[static_cast<size_t>(f)], {x0, yb1 + partition, x1, y_back});
    } else {
      const f32 kx0 = stair_west ? bx1 + partition : x0, kx1 = stair_west ? x1 : bx0 - partition;
      add_room(p, f, room_id(f, "bath"), "bathroom", {bx0, yb0, bx1, y_back});
      add_room(p, f, room_id(f, "back"), back[static_cast<size_t>(f)], {kx0, yb0, kx1, y_back});
    }
  }
  // The straight flight: up toward the back, its foot a landing deep in.
  if (multi) {
    stair_core &s = p.stair;
    s.on = true;
    s.type = "straight";
    s.split[0] = R.risers;
    s.split[1] = 0;
    const f32 fy0 = ys0 + landing, fy1 = fy0 + run;
    s.lower_landing = {sx0, ys0, sx1, fy0};
    s.lower_flight = {sx0, fy0, sx1, fy1};
    s.upper_landing = {sx0, fy1, sx1, std::min(ys1, fy1 + landing)};
    const f32 mx = (sx0 + sx1) * 0.5f;
    s.walking_line = {{mx, ys0 + 0.7f, 0.0f}, {mx, fy0, 0.0f}, {mx, fy1, storey}, {mx, fy1 + 0.7f, storey}};
    for (i32 f = 0; f < floors; ++f)
      s.room_ids.push_back(room_id(f, "stair"));
    // The slab above is open over the flight where a head would meet it.
    add_vertical_voids(p, rect_poly({sx0 - 0.02f, fy0 + 0.6f, sx1 + 0.02f, fy1}));
  }
  finish_areas(p, P);
  // Doors: front room to the stair room, the stair room to the back.
  const f32 pass0 = stair_west ? sx1 : x0, pass1 = stair_west ? x1 : sx0; // the way past the flight
  for (i32 f = 0; f < floors; ++f) {
    const std::string mid = room_id(f, multi ? "stair" : "hall");
    const room *fr = p.find_room(f, room_id(f, "front"));
    const bool pub = fr->type == "living" || fr->type == "shop" || fr->type == "living_dining_kitchen";
    const f32 cx = snap((pass0 + pass1) * 0.5f);
    add_portal(p, fr->id, mid, f, {cx, yf + partition * 0.5f}, "y", pub && pass1 - pass0 >= 2.4f ? "open" : "door",
               pub && pass1 - pass0 >= 2.4f ? 1.6f : 1.05f, pub && pass1 - pass0 >= 2.4f ? 1.6f : 0.95f);
    const f32 bx0 = stair_west ? x0 : x1 - bath_w, bx1 = bx0 + bath_w;
    if (narrow) {
      const f32 cx0 = stair_west ? bx1 + partition : x0, cx1 = stair_west ? x1 : bx0 - partition;
      const f32 yb1 = yb0 + bath_d;
      add_portal(p, mid, room_id(f, "way"), f, {snap((cx0 + cx1) * 0.5f), ys1 + partition * 0.5f}, "y", "open",
                 cx1 - cx0 - 0.1f, cx1 - cx0 - 0.1f);
      add_portal(p, room_id(f, "way"), room_id(f, "bath"), f,
                 {stair_west ? bx1 + partition * 0.5f : bx0 - partition * 0.5f, snap((yb0 + yb1) * 0.5f)}, "x", "door",
                 1.05f, 0.95f);
      add_portal(p, room_id(f, "way"), room_id(f, "back"), f, {snap((cx0 + cx1) * 0.5f), yb1 + partition * 0.5f}, "y",
                 "door", 1.05f, 0.95f);
    } else {
      const f32 kx0 = stair_west ? bx1 + partition : x0, kx1 = stair_west ? x1 : bx0 - partition;
      // The bathroom's door off the flight's top end, the back room's off the way past it.
      add_portal(p, mid, room_id(f, "bath"), f, {snap((bx0 + bx1) * 0.5f), ys1 + partition * 0.5f}, "y", "door", 1.05f,
                 0.95f);
      add_portal(p, mid, room_id(f, "back"), f, {snap((kx0 + kx1) * 0.5f), ys1 + partition * 0.5f}, "y", "door", 1.05f,
                 0.95f);
    }
  }
  if (!finish_plan(at, style, 0.0f, facade_seed, r, room_id(0, "front")))
    return at;
  json_value g = json_value::make_object();
  g.set("layout", "tube").set("stair_side", stair_west ? "west" : "east").set("front_m", yf - y0);
  g.set("layout_seed", static_cast<f64>(layout_seed)).set("facade_seed", static_cast<f64>(facade_seed));
  p.generator = g;
  return at;
}

// --- Corridor: flats, hotels, schools ------------------------------------------------------
//
// A corridor across the building, rooms of the floor's kind in front of it
// and behind it (or only in front, when the building is shallow), the
// dogleg core at one end of it running the building's whole depth.

attempt try_corridor(const request &req, const archetype_rule &A, const preset_rule &P, const std::string &style,
                     u32 layout_seed, u32 facade_seed) {
  const rules &R = load_rules();
  attempt at;
  plan &p = at.p;
  rng r(layout_seed);
  begin_plan(p, req, A, style);
  const i32 floors = req.floors;
  const bool multi = floors > 1;
  const f32 W = req.width, D = req.depth;
  const f32 x0 = ext_wall, x1 = W - ext_wall;
  const f32 y0 = ext_wall, y_back = D - ext_wall;
  const f32 cw = snap(std::max(P.corridor_min, P.corridor_target));
  const bool core_west = r.chance(0.5f);
  const f32 core_w = 2.88f;
  const f32 hx0 = multi ? (core_west ? x0 + core_w + partition : x0) : x0;
  const f32 hx1 = multi ? (core_west ? x1 : x1 - core_w - partition) : x1;
  const f32 len = hx1 - hx0;
  // The kind of room on each floor, front and back.
  const auto kinds = [&](i32 f, std::string &fk, std::string &bk) {
    const bool ground = f == 0;
    if (req.archetype == "school") {
      fk = "classroom";
      bk = ground ? "office" : "classroom";
    } else if (req.archetype == "hotel") {
      fk = ground ? "shop" : "hotel_room";
      bk = ground ? "office" : "hotel_room";
    } else {
      fk = ground ? "shop" : "apartment_unit";
      bk = ground ? "storage" : "apartment_unit";
    }
  };
  // Two rows if both fit their least depth, else rooms in front only.
  f32 fneed = 0.0f, bneed = 0.0f;
  for (i32 f = 0; f < floors; ++f) {
    std::string fk, bk;
    kinds(f, fk, bk);
    fneed = std::max(fneed, std::max(R.room(fk)->min_width, 3.0f));
    bneed = std::max(bneed, std::max(R.room(bk)->min_width, 3.0f));
  }
  const f32 inside = y_back - y0;
  const bool two_rows = fneed + partition + cw + partition + bneed <= inside + 1e-3f;
  if (!two_rows && fneed + partition + cw > inside + 1e-3f) {
    at.fail.push_back("depth " + std::to_string(static_cast<i32>(D)) + " m: no room row and corridor fit");
    return at;
  }
  f32 yc0;
  if (two_rows) {
    const f32 spare = inside - (fneed + partition + cw + partition + bneed);
    yc0 = snap(y0 + fneed + spare * r.range(0.35f, 0.65f) + partition);
  } else {
    yc0 = y_back - cw;
  }
  const f32 yc1 = yc0 + cw;
  // Rooms of a row: as many as their target width allows, all at least the minimum.
  const auto row = [&](i32 f, const std::string &kind, f32 ya, f32 yb, const char *tag, bool with_bath) {
    const room_rule *rr = R.room(kind);
    const f32 depth = yb - ya;
    f32 wmin = std::max(rr->min_width, rr->min_area / std::max(depth, 0.1f));
    if (depth + 1e-3f < rr->min_width)
      wmin = std::max(wmin, rr->min_width); // the depth is then the clear width, checked below
    const f32 wtarget = std::max(wmin, rr->target_area / std::max(depth, 0.1f));
    f32 room_len = len;
    f32 bath_x = 0.0f;
    if (with_bath) {
      // One washroom by the core, the rest the row's kind.
      const f32 bw = 1.8f;
      bath_x = core_west ? hx0 : hx1 - bw;
      add_room(p, f, room_id(f, "bath"), "bathroom", {bath_x, ya, bath_x + bw, yb});
      room_len -= bw + partition;
    }
    i32 n = std::max(1, static_cast<i32>((room_len + partition) / (wtarget + partition)));
    // A little over the minimum: the widths are rounded to the 2 cm grid.
    while (n > 1 && (room_len - partition * static_cast<f32>(n - 1)) / static_cast<f32>(n) < wmin + 0.04f)
      --n;
    if ((room_len - partition * static_cast<f32>(n - 1)) / static_cast<f32>(n) + 1e-3f < wmin)
      return false;
    const std::vector<f32> ws = equal_widths(room_len, n);
    f32 x = with_bath && core_west ? hx0 + 1.8f + partition : hx0;
    for (i32 i = 0; i < n; ++i) {
      add_room(p, f, room_id(f, tag, i), kind, {x, ya, x + ws[static_cast<size_t>(i)], yb});
      x += ws[static_cast<size_t>(i)] + partition;
    }
    return true;
  };
  for (i32 f = 0; f < floors; ++f) {
    std::string fk, bk;
    kinds(f, fk, bk);
    if (!row(f, fk, y0, yc0 - partition, "front", false)) {
      at.fail.push_back("floor " + std::to_string(f) + ": the front row cannot hold a " + fk);
      return at;
    }
    if (two_rows && !row(f, bk, yc1 + partition, y_back, "back", f == 0 && req.archetype != "apartment_block")) {
      at.fail.push_back("floor " + std::to_string(f) + ": the back row cannot hold a " + bk);
      return at;
    }
    add_room(p, f, room_id(f, "hall"), "corridor", {hx0, yc0, hx1, yc1});
    if (multi)
      add_room(p, f, room_id(f, "stair"), "stair", {core_west ? x0 : x1 - core_w, y0, core_west ? x0 + core_w : x1, y_back});
  }
  // The core: its landing by the corridor, on the corridor's side wall.
  dogleg_layout dl;
  if (multi) {
    const box2 core{core_west ? x0 : x1 - core_w, y0, core_west ? x0 + core_w : x1, y_back};
    // The landing's end whose side door lands on the corridor.
    // The landing's side door 0.84 m from the core's landing end: slide the
    // core along the depth until that door is on the corridor's wall.
    const f32 mid = (yc0 + yc1) * 0.5f;
    box2 c = core;
    bool front_end = true;
    const f32 lo = y0, hi = y_back - 4.84f;
    if (hi < lo - 1e-3f) {
      at.fail.push_back("the building is shallower than the dogleg core");
      return at;
    }
    c.y0 = std::clamp(mid - 0.84f, lo, hi);
    c.y1 = c.y0 + 4.84f;
    const auto on_corridor = [&](f32 y) { return y >= yc0 + 0.55f && y <= yc1 - 0.55f; };
    if (!on_corridor(c.y0 + 0.84f)) {
      front_end = false;
      c.y1 = std::clamp(mid + 0.84f, lo + 4.84f, y_back);
      c.y0 = c.y1 - 4.84f;
      if (!on_corridor(c.y1 - 0.84f)) {
        at.fail.push_back("the dogleg core's door does not reach the corridor in this depth");
        return at;
      }
    }
    dl = make_dogleg(c, core_west, front_end, floors); // the door on the side away from the landing: the corridor's
    p.stair = dl.s;
    add_vertical_voids(p, dl.void_poly);
  }
  finish_areas(p, P);
  for (i32 f = 0; f < floors; ++f) {
    const std::string hall = room_id(f, "hall");
    for (const room &rm : p.floors[static_cast<size_t>(f)].rooms) {
      if (rm.type == "corridor" || rm.type == "stair")
        continue;
      const box2 b = bounds_of(rm.poly);
      const bool in_front = b.y1 <= yc0;
      const bool lobby = rm.type == "shop" && b.w() >= 2.4f;
      add_portal(p, in_front ? rm.id : hall, in_front ? hall : rm.id, f,
                 {snap((b.x0 + b.x1) * 0.5f), in_front ? yc0 - partition * 0.5f : yc1 + partition * 0.5f}, "y",
                 lobby ? "open" : "door", lobby ? 1.6f : 1.05f, lobby ? 1.6f : 0.95f);
    }
    if (multi)
      add_portal(p, hall, room_id(f, "stair"), f, dl.side_door, "x", "door", 1.05f, 0.95f);
  }
  // In by the front row's middle room.
  std::string entry;
  f32 best = 1e30f;
  for (const room &rm : p.floors[0].rooms) {
    const box2 b = bounds_of(rm.poly);
    if (b.y0 > y0 + 1e-3f || rm.type == "corridor" || rm.type == "stair")
      continue;
    const f32 d = std::fabs((b.x0 + b.x1) * 0.5f - W * 0.5f);
    if (d < best) {
      best = d;
      entry = rm.id;
    }
  }
  if (!finish_plan(at, style, 0.0f, facade_seed, r, entry))
    return at;
  json_value g = json_value::make_object();
  g.set("layout", "corridor").set("core_side", core_west ? "west" : "east").set("rows", two_rows ? 2 : 1);
  g.set("layout_seed", static_cast<f64>(layout_seed)).set("facade_seed", static_cast<f64>(facade_seed));
  p.generator = g;
  return at;
}

// --- Hall: workshops, warehouses, the market ---------------------------------------------
//
// One open floor across the front; along the back an office, a store and
// the washroom, and the dogleg core in a back corner when there are floors
// above, its door onto the hall.

attempt try_hall(const request &req, const archetype_rule &A, const preset_rule &P, const std::string &style,
                 u32 layout_seed, u32 facade_seed) {
  const rules &R = load_rules();
  attempt at;
  plan &p = at.p;
  rng r(layout_seed);
  begin_plan(p, req, A, style);
  const i32 floors = req.floors;
  const bool multi = floors > 1;
  const f32 W = req.width, D = req.depth;
  const f32 x0 = ext_wall, x1 = W - ext_wall, iw = x1 - x0;
  const f32 y0 = ext_wall, y_back = D - ext_wall;
  const std::string floor_kind = has(A.ground, "market_floor") ? "market_floor" : "hall";
  const bool core_west = r.chance(0.5f);
  const f32 core_w = 2.88f;
  // The back strip: deep enough for the core, or for the office.
  const f32 strip = multi ? 4.84f : snap(std::max(3.0f, least_depth(R, "office", 3.2f)));
  const f32 hd = y_back - y0 - strip - partition;
  if (!fits_across(R, floor_kind, iw) || hd + 1e-3f < R.room(floor_kind)->min_width ||
      iw * hd + 1e-3f < R.room(floor_kind)->min_area) {
    at.fail.push_back("the " + floor_kind + " does not fit in front of the back rooms");
    return at;
  }
  const f32 yh = y0 + hd, ys = yh + partition;
  // Along the back, from the core's side: core, washroom, office, store.
  for (i32 f = 0; f < floors; ++f) {
    add_room(p, f, room_id(f, "hall"), floor_kind, {x0, y0, x1, yh});
    f32 x = core_west ? x0 : x1;
    const auto take = [&](f32 w) {
      box2 b = core_west ? box2{x, ys, x + w, y_back} : box2{x - w, ys, x, y_back};
      x = core_west ? x + w + partition : x - w - partition;
      return b;
    };
    if (multi)
      add_room(p, f, room_id(f, "stair"), "stair", take(core_w));
    const f32 left = core_west ? x1 - x : x - x0;
    if (f == 0) {
      const f32 bath_w = 1.8f, office_w = std::max(R.room("office")->min_width, snap(9.0f / strip + 0.02f));
      if (left < bath_w + office_w + 2.0f * partition + 1.4f) {
        at.fail.push_back("the back is too short for the washroom, the office and the store");
        return at;
      }
      add_room(p, f, room_id(f, "bath"), "bathroom", take(bath_w));
      add_room(p, f, room_id(f, "office"), "office", take(office_w));
      const f32 rest = core_west ? x1 - x : x - x0;
      add_room(p, f, room_id(f, "store"), "storage", take(rest));
    } else {
      add_room(p, f, room_id(f, "store"), "storage", take(left));
    }
  }
  dogleg_layout dl;
  if (multi) {
    const box2 core = core_west ? box2{x0, ys, x0 + core_w, y_back} : box2{x1 - core_w, ys, x1, y_back};
    dl = make_dogleg(core, core_west, true, floors);
    p.stair = dl.s;
    add_vertical_voids(p, dl.void_poly);
  }
  finish_areas(p, P);
  for (i32 f = 0; f < floors; ++f) {
    const std::string hall = room_id(f, "hall");
    for (const room &rm : p.floors[static_cast<size_t>(f)].rooms) {
      if (rm.id == hall)
        continue;
      const box2 b = bounds_of(rm.poly);
      const vec2 at_door = rm.type == "stair" ? dl.end_door : vec2{snap((b.x0 + b.x1) * 0.5f), yh + partition * 0.5f};
      add_portal(p, hall, rm.id, f, at_door, "y", "door", 1.05f, 0.95f);
    }
  }
  if (!finish_plan(at, style, 0.0f, facade_seed, r, room_id(0, "hall")))
    return at;
  json_value g = json_value::make_object();
  g.set("layout", "hall").set("core_side", core_west ? "west" : "east").set("hall_depth_m", hd);
  g.set("layout_seed", static_cast<f64>(layout_seed)).set("facade_seed", static_cast<f64>(facade_seed));
  p.generator = g;
  return at;
}

// Appearance is part of the saved plan, so glazing, lighting and rendering
// all know that a floor-level balcony door replaced an ordinary window.
void design_balconies(plan &p) {
  if (!p.generator["balconies_allowed"].bool_or(true)) return;
  const rules &R = load_rules();
  rng fr(sub_seed(p.seed, R.salt_facade));
  const f32 chance = fr.range(R.balcony_chance[0], R.balcony_chance[1]);
  // Choose the building's balcony stack, rather than independent floor
  // coins which leave arbitrary gaps in a primary vertical feature.
  if (static_cast<f32>(sub_seed(p.seed, 991u) % 10000u) / 10000.0f >= chance) return;
  const auto bays = exterior_bays(p);
  bool any = false;
  for (i32 f = 1; f < p.storeys(); ++f) {
    std::vector<const bay_slot *> candidates;
    for (const auto &b : bays) {
      const room *behind = room_behind(p, f, b);
      if (!b.arc && b.side == "south" && !b.first && !b.last && behind &&
          (behind->type == "living" || behind->type == "living_dining_kitchen" || behind->type == "bedroom"))
        candidates.push_back(&b);
    }
    if (candidates.empty()) continue;
    std::stable_sort(candidates.begin(), candidates.end(), [&](const bay_slot *a, const bay_slot *b) {
      const auto rank = [&](const bay_slot *q) {
        f32 value = facade_axis(*q) ? 4.0f : 0.0f;
        for (const auto &old : p.apertures)
          if (old.floor == f - 1 && old.module_id.find("Balcony") != std::string::npos &&
              distance(old.center, q->center) < 0.05f) value += 20.0f;
        for (const auto &door : p.portals)
          if (door.exterior()) value -= distance(door.center, q->center) * 0.2f;
        return value;
      };
      return rank(a) > rank(b);
    });
    const auto &b = *candidates.front();
    const room *behind = room_behind(p, f, b);
    aperture a;
    a.floor = f; a.room = behind->id; a.center = b.center; a.inward = -b.out;
    a.module_id = p.style + "/Balcony"; a.glazed = 1.6f * 2.4f;
    bool replaced = false;
    for (auto &old : p.apertures) if (old.floor == f && distance(old.center, a.center) < 0.05f) {
      old = a; replaced = true; break;
    }
    if (!replaced) p.apertures.push_back(a);
    // Keep daylight requirements while removing redundant windows in this room.
    f32 glass = 0;
    for (const auto &q : p.apertures) if (q.floor == f && q.room == a.room) glass += q.glazed;
    for (size_t i = p.apertures.size(); i-- > 0;) {
      const auto &q = p.apertures[i];
      if (q.floor == f && q.room == a.room && q.module_id.find("Window") != std::string::npos &&
          glass - q.glazed >= R.glazing_ratio * behind->area) {
        glass -= q.glazed; p.apertures.erase(p.apertures.begin() + static_cast<std::ptrdiff_t>(i));
      }
    }
    any = true;
  }
  if (any) p.required_modules.push_back(p.style + "/Balcony");
}

} // namespace

gen_result generate(const request &req) {
  gen_result res;
  const rules &R = load_rules();
  const auto nofit = [&](const std::string &why) {
    res.ok = false;
    res.reasons.push_back(why);
    return res;
  };
  if (!R.loaded)
    return nofit("building_rules.json did not load");
  if (req.rule_version != R.version)
    return nofit("request rule_version " + std::to_string(req.rule_version) + ", rules are version " +
                 std::to_string(R.version));
  const archetype_rule *A = R.archetype(req.archetype);
  if (!A)
    return nofit("unknown archetype " + req.archetype);
  std::string why;
  if (!shape_supported(A->shape, &why))
    return nofit(why);
  if (A->layout == "band" && has(A->ground, "apartment_unit"))
    return nofit("archetype " + req.archetype + ": its band layout has no shared core; use apartment_block");
  if (A->layout != "band" && A->layout != "tube" && A->layout != "corridor" && A->layout != "hall")
    return nofit("archetype " + req.archetype + ": unknown layout " + A->layout);
  const auto bays_of = [](f32 m, i32 &n) {
    n = static_cast<i32>(std::lround(m / bay));
    return std::fabs(m - static_cast<f32>(n) * bay) < 1e-3f;
  };
  i32 wb = 0, db = 0;
  if (!bays_of(req.width, wb) || !bays_of(req.depth, db))
    return nofit("width and depth must be whole 2 m bays");
  char buf[200];
  if (wb < A->width_bays[0] || wb > A->width_bays[1] || db < A->depth_bays[0] || db > A->depth_bays[1]) {
    std::snprintf(buf, sizeof(buf), "%d x %d bays is outside %s's %d-%d x %d-%d", wb, db, req.archetype.c_str(),
                  A->width_bays[0], A->width_bays[1], A->depth_bays[0], A->depth_bays[1]);
    return nofit(buf);
  }
  if (req.floors < A->floors[0] || req.floors > A->floors[1]) {
    std::snprintf(buf, sizeof(buf), "%d floors is outside %s's %d-%d", req.floors, req.archetype.c_str(),
                  A->floors[0], A->floors[1]);
    return nofit(buf);
  }
  const std::string preset_name = req.space_preset.empty() ? A->preset : req.space_preset;
  const preset_rule *P = R.preset(preset_name);
  if (!P)
    return nofit("unknown space preset " + preset_name);
  f32 radius = 0.0f;
  if (A->shape == "CornerShopHouseRounded") {
    radius = req.radius;
    if (std::find(A->radii.begin(), A->radii.end(), radius) == A->radii.end())
      radius = A->radii.empty() ? 4.0f : A->radii.front();
    if (radius * 2.0f > std::min(req.width, req.depth))
      return nofit("the rounded corner's radius does not fit the footprint");
  }
  // The style: the request's, or the district's weights.
  std::string style = req.style;
  if (style.empty()) {
    for (const auto &[d, w] : R.district_styles)
      if (d == req.district)
        pick_weighted(w, sub_seed(req.seed, R.salt_appearance), style);
    if (style.empty())
      style = "Modern";
  }
  if (!R.palette(style))
    return nofit("unknown style " + style);
  std::string roof = "Flat";
  pick_weighted(A->roofs, sub_seed(req.seed, R.salt_appearance + 1u), roof);

  const u32 layout_base = sub_seed(req.seed, R.salt_layout);
  const u32 facade_seed = sub_seed(req.seed, R.salt_facade);
  bool have = false;
  attempt best;
  std::vector<std::string> last_fail;
  i32 valid = 0;
  for (i32 k = 0; k < R.retry_count; ++k) {
    res.tries = k + 1;
    const u32 ls = sub_seed(layout_base, static_cast<u32>(k));
    attempt at = A->layout == "tube"       ? try_tube(req, *A, *P, style, ls, facade_seed)
                 : A->layout == "corridor" ? try_corridor(req, *A, *P, style, ls, facade_seed)
                 : A->layout == "hall"     ? try_hall(req, *A, *P, style, ls, facade_seed)
                                           : try_layout(req, *A, *P, style, radius, ls, facade_seed);
    if (at.fail.empty()) {
      at.p.seed = req.seed;
      at.p.generator.set("balconies_allowed", req.balconies);
      design_balconies(at.p);
      const assembly as = assemble(at.p);
      const check_report rep = check_plan(at.p, &as);
      if (!rep.ok())
        at.fail = rep.errors;
      if (gen_trace && !rep.ok() && last_fail.empty())
        dump_nav(at.p, as, "pbk_out/fail");
      at.score = rep.score;
    }
    if (!at.fail.empty()) {
      if (gen_trace)
        std::printf("  try %d: %s%s\n", k, at.fail.front().c_str(), at.fail.size() > 1 ? " (and more)" : "");
      last_fail = at.fail;
      continue;
    }
    ++valid;
    if (!have || at.score > best.score) {
      best = std::move(at);
      have = true;
    }
    if (valid >= 4 || best.score > 0.92f)
      break;
  }
  if (!have) {
    res.reasons = last_fail;
    res.reasons.insert(res.reasons.begin(), "NoFit after " + std::to_string(res.tries) + " tries");
    return res;
  }
  res.ok = true;
  res.p = std::move(best.p);
  res.p.generator.set("request", json_value::make_object()
                                     .set("seed", static_cast<f64>(req.seed))
                                     .set("archetype", req.archetype)
                                     .set("district", req.district)
                                     .set("style", req.style)
                                     .set("width_m", req.width)
                                     .set("depth_m", req.depth)
                                     .set("floors", req.floors));
  res.p.generator.set("tries", res.tries).set("roof", roof);
  if (roof != "Flat")
    res.p.generator.set("roof_note", "the kit's gable spans 4 m: built as a flat roof with a parapet");
  return res;
}

} // namespace sandtable::city::pbk
