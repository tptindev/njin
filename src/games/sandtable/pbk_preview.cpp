#include "pbk_preview.h"

#include "city/pbk_render.h"
#include "city/street_kit.h"
#include "physics.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

// The building kit's own scenes (pbk_preview.h). Everything here goes through
// the game's code: pbk_gen/pbk_assemble make the house, pbk_render draws it
// and drives its doors, physics.cpp's person walks into it.

namespace sandtable {

namespace {

using namespace city::pbk;

enum scene_t : i32 { scene_door = 0, scene_examples, scene_generated, scene_kit, scene_count };
const char *const scene_names[scene_count] = {"1 Wall + DoorRigged", "2 ba plan mau", "3 nha sinh theo seed",
                                              "4 cua so, canh chop, kinh, via he"};

struct preview {
  bool test = false;
  // --pbk-tour: the test's script slowed for watching, no shots, over and
  // over until the window is shut.
  bool tour = false;
  scene_t scene = scene_door;
  std::vector<building3d> houses;
  static_batch batch;
  view_cut cut;
  bool dirty = true;
  u32 gen_seed = 1;
  // The orbit camera: round `target`, render units.
  vec3 target{0.0f, 0.2f, 0.0f};
  f32 yaw = 210.0f, pitch = 32.0f, dist = 3.0f;
  body3d_handle ground{};
  // A person walked at a door.
  character3d_handle man{};
  vec3 push{};
  // The script.
  i32 step = 0;
  f32 clock = 0.0f, step_clock = 0.0f;
  i32 frame = 0;
  std::vector<std::string> log;
  i32 failed = 0;
  std::string shot; // screenshot to take at the end of this frame
  std::vector<std::string> notes;
  // Scene 4: the street kit's sidewalk_layout.json, in front of the house.
  struct street_piece {
    const city::street::asset *a = nullptr;
    vec2 at{};   // layout metres: x along the street, y from the curb to the houses
    f32 rot = 0.0f;
  };
  std::vector<street_piece> street;
  box2 walkway{};
  std::vector<std::pair<std::string, model_handle>> street_models;
};
preview pv;

void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char *fmt, ...) {
  char buf[512];
  va_list a;
  va_start(a, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, a);
  va_end(a);
  std::printf("[pbk-test] %s\n", buf);
  pv.log.push_back(buf);
}

void expect(bool ok, const char *what) {
  say("%s %s", ok ? "PASS" : "FAIL", what);
  if (!ok)
    ++pv.failed;
}

// --- Scenes --------------------------------------------------------------------------

// One bay of Wall and one DoorRigged, side by side, facing the camera's side
// of the table: the smallest thing that shows the loader, the clip and the
// leaf's collision.
building3d wall_and_door() {
  building3d b;
  plan &p = b.p;
  p.style = "Indochine";
  p.width = 4.0f;
  p.depth = 2.0f;
  p.outer = {{0, 0}, {4, 0}, {4, 2}, {0, 2}};
  floor_plan f;
  room in;
  in.id = "inside";
  in.type = "living";
  in.poly = {{0.2f, 0.2f}, {3.8f, 0.2f}, {3.8f, 1.8f}, {0.2f, 1.8f}};
  f.rooms.push_back(in);
  p.floors.push_back(f);
  b.at = {{0.0f, 0.0f}, 0.0f, p.width, p.depth};
  const f32 yaw = yaw_facing({0.0f, -1.0f}); // +Z of the modules toward the front
  const std::string wall = p.style + "/Wall", rig = p.style + "/DoorRigged";
  b.as.modules.push_back({wall, {4.0f, 0.0f, 0.0f}, yaw, 0, -1});
  b.as.modules.push_back({rig, {2.0f, 0.0f, 0.0f}, yaw, 0, 0});
  const module_info *mi = load_manifest().find(rig);
  door d;
  d.portal = "street";
  d.rigged = true;
  d.module_id = rig;
  const vec3 h = module_to_plan(mi ? mi->socket("hinge") : vec3{0.525f, 0, -0.1f}, {2.0f, 0.0f, 0.0f}, yaw);
  d.hinge = {h.x, h.y};
  d.closed_angle = yaw;
  const door_clip clip = clip_of(mi);
  d.swing = module_rig_of(rig).turn(0, clip.open_pose, clip.shut_pose);
  d.width = 0.95f;
  d.thickness = 0.06f;
  d.z0 = 0.03f;
  d.z1 = 2.32f;
  b.as.doors.push_back(d);
  // Collision as the assembly makes it: the Wall's bay solid, the door's two
  // jambs and lintel, its opening free.
  const rgba w{0.72f, 0.55f, 0.3f, 1.0f};
  const auto wall_box = [&](f32 x0, f32 x1, f32 z0, f32 z1) {
    b.as.solids.push_back({{(x0 + x1) * 0.5f, 0.1f}, {(x1 - x0) * 0.5f, 0.1f}, 0.0f, z0, z1, 0, sk_exterior, w});
  };
  wall_box(2.0f, 4.0f, 0.0f, 3.0f);
  wall_box(1.525f, 2.0f, 0.0f, 3.0f);
  wall_box(0.0f, 0.475f, 0.0f, 3.0f);
  wall_box(0.475f, 1.525f, 2.35f, 3.0f);
  b.doors.assign(1, door_state{});
  b.doors[0].t = clip.shut_pose;
  return b;
}

// Scene 4: one bay of each window the kit opens or glazes, side by side on
// the street front, the street door among them; the shutters start open.
building3d kit_row() {
  building3d b;
  plan &p = b.p;
  p.style = "Indochine";
  p.width = 14.0f;
  p.depth = 4.0f;
  p.outer = {{0, 0}, {14, 0}, {14, 4}, {0, 4}};
  floor_plan f;
  room in;
  in.id = "inside";
  in.type = "living";
  in.poly = {{0.2f, 0.2f}, {13.8f, 0.2f}, {13.8f, 3.8f}, {0.2f, 3.8f}};
  f.rooms.push_back(in);
  p.floors.push_back(f);
  b.at = {{0.0f, 0.0f}, 0.0f, p.width, p.depth};
  const f32 yaw = yaw_facing({0.0f, -1.0f});
  // Module +X runs along the front from right to left in the plan (yaw_facing
  // turns +Z out of the front): each origin is a bay's right end.
  const char *ids[] = {"Indochine/Window", "Indochine/CornerWindow90", "Modern/Window", "Brick/Window",
                       "Indochine/DoorRigged", "Indochine/Wall"};
  const f32 x[] = {2.0f, 6.0f, 8.0f, 10.0f, 12.0f, 14.0f};
  for (i32 i = 0; i < 6; ++i) {
    module_place m{ids[i], {x[i], 0.0f, 0.0f}, yaw, 0, -1};
    const module_info *mi = load_manifest().find(ids[i]);
    if (mi && mi->shutters) {
      m.shutter = static_cast<i32>(b.as.shutters.size());
      b.as.shutters.push_back({ids[i], static_cast<i32>(b.as.modules.size()), 0});
    }
    if (std::string(ids[i]) == "Indochine/DoorRigged") {
      m.door = 0;
      door d;
      d.portal = "street";
      d.rigged = true;
      d.module_id = ids[i];
      const vec3 h = module_to_plan(mi ? mi->socket("hinge") : vec3{0.525f, 0, -0.1f}, {x[i], 0.0f, 0.0f}, yaw);
      d.hinge = {h.x, h.y};
      d.closed_angle = yaw;
      const door_clip c = clip_of(mi);
      d.swing = module_rig_of(ids[i]).turn(0, c.open_pose, c.shut_pose);
      b.as.doors.push_back(d);
    }
    b.as.modules.push_back(m);
  }
  b.doors.assign(b.as.doors.size(), door_state{});
  b.shutters.assign(b.as.shutters.size(), door_state{});
  for (size_t i = 0; i < b.shutters.size(); ++i) {
    b.shutters[i].phase = door_state::open;
    b.shutters[i].t = clip_of(load_manifest().find(b.as.shutters[i].module_id)).open_pose;
  }
  return b;
}

// sidewalk_layout.json's pieces and its walkway.
void load_street() {
  pv.street.clear();
  json_value j;
  if (!json_load((std::string(city::street::kit_dir) + "/sidewalk_layout.json").c_str(), j)) {
    pv.notes.push_back("sidewalk_layout.json did not load");
    return;
  }
  for (usize i = 0; i < j["placements"].size(); ++i) {
    const json_value &e = j["placements"][i];
    preview::street_piece sp;
    sp.a = city::street::find(e["asset_id"].string_or(""));
    sp.at = {e["position_m"][usize{0}].f32_or(0), e["position_m"][usize{1}].f32_or(0)};
    sp.rot = e["rotation_z_degrees"].f32_or(0);
    if (sp.a)
      pv.street.push_back(sp);
    else
      pv.notes.push_back(std::string("no street asset ") + e["asset_id"].string_or(""));
  }
  const json_value &w = j["walkway"]["bounds_xy_m"];
  pv.walkway = {w[usize{0}][usize{0}].f32_or(0), w[usize{0}][usize{1}].f32_or(0), w[usize{1}][usize{0}].f32_or(0),
                w[usize{1}][usize{1}].f32_or(0)};
}

// The layout's frame on the table: its x along the house's front, its curb
// 6 m out from the front wall.
vec3 street_point(vec2 layout) {
  const building3d &b = pv.houses[0];
  return b.at.to_render({7.0f - layout.x, -(9.5f - layout.y), 0.0f});
}

// A street piece's ground footprint in the layout's metres (x, y), as a box.
box2 street_foot(const preview::street_piece &sp) {
  const f32 c = std::cos(sp.rot * pi / 180.0f), sn = std::sin(sp.rot * pi / 180.0f);
  // The asset's footprint in Blender axes: x, y = -z of glTF.
  const vec2 fc{sp.a->foot_c.x, -sp.a->foot_c.y}, h = sp.a->foot_half;
  box2 out{1e30f, 1e30f, -1e30f, -1e30f};
  for (i32 k = 0; k < 4; ++k) {
    const vec2 q{fc.x + ((k & 1) ? h.x : -h.x), fc.y + ((k & 2) ? h.y : -h.y)};
    const vec2 r{sp.at.x + q.x * c - q.y * sn, sp.at.y + q.x * sn + q.y * c};
    out = {std::min(out.x0, r.x), std::min(out.y0, r.y), std::max(out.x1, r.x), std::max(out.y1, r.y)};
  }
  return out;
}

model_handle street_model(context &ctx, const city::street::asset &a) {
  for (auto &[id, m] : pv.street_models)
    if (id == a.id)
      return m;
  const model_handle m = model_load(ctx, {.path = a.path.c_str(), .merge = true});
  for (i32 i = 0; m.id != 0 && i < model_material_count(ctx, m); ++i) {
    model_material mm = model_material_get(ctx, m, i);
    mm.color = linear_to_srgb(mm.color);
    model_material_set(ctx, m, i, mm);
  }
  pv.street_models.push_back({a.id, m});
  return m;
}

void clear_scene(context &ctx) {
  for (building3d &b : pv.houses)
    physics_remove(ctx, b);
  pv.houses.clear();
  pv.batch.destroy(ctx);
  if (pv.man.id != 0) {
    character3d_destroy(ctx, pv.man);
    pv.man = {};
  }
}

void load_scene(context &ctx, scene_t s) {
  clear_scene(ctx);
  pv.scene = s;
  pv.notes.clear();
  if (s == scene_door) {
    pv.houses.push_back(wall_and_door());
    pv.target = {0.0f, 0.2f, 0.0f};
    pv.dist = 1.6f;
  } else if (s == scene_examples) {
    const char *names[] = {"detached_spacious", "corner_shop_3_fronts", "corner_shop_rounded"};
    const f32 angles[] = {0.0f, 30.0f, -15.0f};
    for (i32 i = 0; i < 3; ++i) {
      plan p;
      std::string err;
      const std::string path = std::string(kit_dir) + "/rules/examples/" + names[i] + ".plan.json";
      if (!load_plan(path.c_str(), p, &err)) {
        pv.notes.push_back(std::string(names[i]) + ": " + err);
        continue;
      }
      pv.houses.push_back(make_building(p, {static_cast<f32>(i - 1) * 110.0f, 0.0f}, angles[i]));
    }
    pv.target = {0.0f, 0.2f, 0.0f};
    pv.dist = 9.0f;
  } else if (s == scene_generated) {
    // Each archetype the generator builds, at two sizes, from this seed on.
    struct ask {
      const char *a;
      f32 w, d;
      i32 floors;
    };
    const ask asks[] = {{"detached_spacious", 12, 10, 2}, {"detached_spacious", 14, 12, 3}, {"shop_house", 10, 12, 3},
                        {"corner_shop_3_fronts", 12, 12, 2}, {"corner_shop_rounded", 12, 10, 3}, {"corner_shop_house", 10, 10, 2}};
    i32 k = 0;
    for (const ask &q : asks) {
      request req;
      req.seed = pv.gen_seed * 977u + static_cast<u32>(k) * 31u;
      req.archetype = q.a;
      req.width = q.w;
      req.depth = q.d;
      req.floors = q.floors;
      req.district = k % 2 ? "old_quarter" : "new_urban";
      const gen_result g = generate(req);
      const vec2 at{static_cast<f32>(k % 3 - 1) * 120.0f, static_cast<f32>(k / 3) * 120.0f - 60.0f};
      if (g.ok) {
        pv.houses.push_back(make_building(g.p, at, static_cast<f32>(k * 13 % 40) - 20.0f));
      } else {
        pv.notes.push_back(std::string(q.a) + ": " + (g.reasons.empty() ? "NoFit" : g.reasons.back()));
      }
      ++k;
    }
    pv.target = {0.0f, 0.2f, 0.0f};
    pv.dist = 11.0f;
  } else {
    pv.houses.push_back(kit_row());
    load_street();
    pv.target = {0.0f, 0.3f, -0.4f};
    pv.dist = 4.2f;
  }
  for (building3d &b : pv.houses)
    physics_add(ctx, b);
  pv.dirty = true;
}

void rebuild(context &ctx) {
  pv.batch.clear();
  for (const building3d &b : pv.houses)
    add_static(ctx, b, pv.cut, pv.batch);
  pv.batch.upload(ctx);
  pv.dirty = false;
}

camera3d camera() {
  const f32 y = pv.yaw * pi / 180.0f, p = pv.pitch * pi / 180.0f;
  const vec3 off{std::cos(p) * std::sin(y), std::sin(p), std::cos(p) * std::cos(y)};
  return {.position = pv.target + off * pv.dist, .target = pv.target, .fovy = 45.0f, .near_plane = 0.02f,
          .far_plane = 200.0f, .entities = false};
}

// --- A man at a door ---------------------------------------------------------------------

// The middle of door `i`'s opening and the way in (table units, unit vector).
void door_frame(const building3d &b, i32 i, vec2 &mid, vec2 &in) {
  const door &d = b.as.doors[static_cast<size_t>(i)];
  const f32 r = d.closed_angle * pi / 180.0f;
  const vec2 along{std::cos(r), -std::sin(r)};
  const vec2 out{std::sin(r), std::cos(r)}; // the module's +Z
  const vec2 m = d.hinge + along * 0.475f;
  mid = b.at.to_table(m);
  in = normalize(b.at.dir_to_table(-out));
}

void man_at(context &ctx, const building3d &b, i32 i, f32 metres_out) {
  vec2 mid, in;
  door_frame(b, i, mid, in);
  if (pv.man.id != 0)
    character3d_destroy(ctx, pv.man);
  pv.man = physics_person(ctx, mid - in * (metres_out * city::units_per_metre));
  pv.push = {};
}

// How far in past the door's outer face the man is, metres.
f32 man_depth(context &ctx, const building3d &b, i32 i) {
  vec2 mid, in;
  door_frame(b, i, mid, in);
  const vec2 at = from_phys(character3d_position(ctx, pv.man));
  return dot(at - mid, in) / city::units_per_metre;
}

void walk_in(const building3d &b, i32 i, f32 speed) {
  vec2 mid, in;
  door_frame(b, i, mid, in);
  pv.push = {in.x * speed, 0.0f, in.y * speed};
}

// --- Systems --------------------------------------------------------------------------------

void startup(context &ctx) {
  pv.ground = body3d_create(ctx, {.shape = shape3d_box, .position = {0.0f, -0.25f, 0.0f}, .size = {400.0f, 0.5f, 400.0f}});
  load_scene(ctx, scene_door);
}

void fixed(context &ctx) {
  const f32 dt = delta(ctx);
  for (building3d &b : pv.houses)
    doors_update(ctx, b, dt);
  if (pv.man.id != 0) {
    const vec3 v = character3d_velocity(ctx, pv.man);
    character3d_set_velocity(ctx, pv.man, {pv.push.x, v.y, pv.push.z});
  }
}

// The test script, one step after another.
void script(context &ctx) {
  const f32 t = pv.step_clock;
  const auto next = [&]() {
    ++pv.step;
    pv.step_clock = 0.0f;
  };
  building3d *b0 = pv.houses.empty() ? nullptr : &pv.houses[0];
  switch (pv.step) {
  case 0: // Milestone 1: the Wall and the rigged door, shut.
    if (t > 0.6f) {
      const model_handle w = module_model(ctx, "Indochine/Wall"), d = module_model(ctx, "Indochine/DoorRigged");
      expect(w.id != 0 && d.id != 0, "Indochine/Wall and Indochine/DoorRigged load through model_load");
      // raylib puts a default material first, then the file's own.
      const glb_summary gw = read_glb(std::string(kit_dir) + "/modules/Indochine/Wall.glb");
      const glb_summary gd = read_glb(std::string(kit_dir) + "/modules/Indochine/DoorRigged.glb");
      say("  materials: Wall %d in the engine, %d in the file; DoorRigged %d, %d; door mesh nodes %d, skin joints %d",
          model_material_count(ctx, w), gw.materials, model_material_count(ctx, d), gd.materials, static_cast<i32>(gd.mesh_nodes.size()), gd.joints);
      expect(model_material_count(ctx, w) == gw.materials + 1, "Wall keeps every material of its file");
      expect(model_material_count(ctx, d) == gd.materials + 1, "DoorRigged keeps every material of its file");
      const i32 a = model_anim_find(ctx, d, "Door_OpenClose");
      expect(a >= 0, "the engine finds the clip Door_OpenClose");
      if (a >= 0) {
        const f32 dur = model_anim_duration(ctx, d, a);
        say("  clip duration in the engine %.4f s (manifest 3.9583)", dur);
        expect(std::fabs(dur - 95.0f / 24.0f) < 0.05f, "the clip's length matches the manifest");
      }
      pv.yaw = 200.0f;
      pv.pitch = 18.0f;
      pv.shot = "pbk_m1_shut.png";
      man_at(ctx, *b0, 0, 0.9f);
      walk_in(*b0, 0, 1.2f);
      next();
    }
    break;
  case 1: // Walk into the shut door for 2 s.
    if (t > 2.0f) {
      const f32 depth = man_depth(ctx, *b0, 0);
      say("  shut: the man stands %.2f m from the door's face", depth);
      expect(depth < -0.1f, "a shut door's leaf stops a man");
      expect(door_toggle(*b0, 0), "the door opens on request");
      next();
    }
    break;
  case 2: // Half way through the opening.
    if (t > 0.45f && t < 0.5f + 1.0f / 60.0f && pv.shot.empty()) {
      const door_state &s = b0->doors[0];
      say("  opening: clip time %.3f s, leaf %.1f deg", s.t, s.angle);
      expect(s.phase == door_state::opening && std::fabs(s.angle) > 20.0f && std::fabs(s.angle) < 75.0f,
             "half way, the leaf has turned part of the way");
      pv.shot = "pbk_m1_opening.png";
    }
    if (t > 1.3f) {
      const door_state &s = b0->doors[0];
      say("  open: clip time %.4f s (open at 23/24 = 0.9583), leaf %.1f deg", s.t, s.angle);
      expect(s.phase == door_state::open && std::fabs(s.t - 23.0f / 24.0f) < 1e-3f, "held at the clip's open time");
      expect(std::fabs(std::fabs(s.angle) - 90.0f) < 1.0f, "the leaf stands at 90 degrees");
      pv.shot = "pbk_m1_open.png";
      next();
    }
    break;
  case 3: // No loop: still open 3 s later; the man walks through.
    if (t > 3.0f) {
      const door_state &s = b0->doors[0];
      expect(s.phase == door_state::open && std::fabs(s.t - 23.0f / 24.0f) < 1e-3f, "3 s later the door is still open (no loop)");
      const f32 depth = man_depth(ctx, *b0, 0);
      say("  open: the man is %.2f m in past the door's face", depth);
      expect(depth > 0.5f, "through the open door");
      expect(door_toggle(*b0, 0), "the door shuts on request");
      next();
    }
    break;
  case 4:
    if (t > 1.2f) {
      const door_state &s = b0->doors[0];
      say("  shut again: phase %d, leaf %.1f deg", static_cast<i32>(s.phase), s.angle);
      expect(s.phase == door_state::shut && std::fabs(s.angle) < 1.0f, "played 47/24..71/24 and stands shut");
      load_scene(ctx, scene_examples);
      pv.yaw = 200.0f;
      pv.pitch = 35.0f;
      next();
    }
    break;
  case 5: // Milestone 2: the three example plans.
    if (t > 0.5f) {
      expect(pv.houses.size() == 3, "the three example plans are built");
      for (const building3d &b : pv.houses) {
        const check_report r = check_plan(b.p, &b.as);
        say("  %s: %d modules, %d solids, %d doors, check %s", b.p.archetype.c_str(),
            static_cast<i32>(b.as.modules.size()), static_cast<i32>(b.as.solids.size()),
            static_cast<i32>(b.as.doors.size()), r.ok() ? "ok" : r.errors.front().c_str());
        expect(r.ok(), (b.p.archetype + " passes every hard rule").c_str());
      }
      pv.shot = "pbk_m2_examples.png";
      next();
    }
    break;
  case 6:
    if (t > 0.3f) {
      pv.cut = {.floors = 0, .cut = true};
      pv.dirty = true;
      pv.pitch = 60.0f;
      next();
    }
    break;
  case 7:
    if (t > 0.3f) {
      pv.shot = "pbk_m2_examples_floor0.png";
      next();
    }
    break;
  case 8:
    if (t > 0.2f) {
      pv.cut = {.floors = 1, .cut = true};
      pv.dirty = true;
      next();
    }
    break;

  case 9:
    if (t > 0.3f) {
      pv.shot = "pbk_m2_examples_floor1.png";
      next();
    }
    break;
  case 10: // The detached house's stair core, close: the dogleg, the void, the rails.
    if (t > 0.2f) {
      building3d &d = pv.houses[0];
      const box2 core = bounds_of(d.p.find_room(0, d.p.stair.room_ids[0])->poly);
      pv.target = d.at.to_render({(core.x0 + core.x1) * 0.5f, (core.y0 + core.y1) * 0.5f, 1.0f});
      pv.dist = 2.2f;
      pv.pitch = 55.0f;
      pv.yaw = 160.0f;
      pv.cut = {.floors = 1, .cut = true};
      pv.dirty = true;
      next();
    }
    break;
  case 11:
    if (t > 0.3f) {
      pv.shot = "pbk_m2_stair_floor1.png";
      next();
    }
    break;
  case 12:
    if (t > 0.2f) {
      pv.cut = {.floors = 0, .cut = true};
      pv.dirty = true;
      next();
    }
    break;
  case 13:
    if (t > 0.3f) {
      pv.shot = "pbk_m2_stair_floor0.png";
      next();
    }
    break;
  case 14: // The rounded corner, close, and its street door walked through.
    if (t > 0.3f) {
      pv.cut = {};
      pv.dirty = true;
      building3d &r = pv.houses[2];
      i32 street = -1;
      for (i32 i = 0; i < static_cast<i32>(r.as.doors.size()); ++i)
        if (r.as.doors[static_cast<size_t>(i)].rigged)
          street = i;
      vec2 mid, in;
      door_frame(r, street, mid, in);
      pv.target = to3d(mid, 0.25f);
      pv.dist = 2.4f;
      pv.yaw = std::atan2(-in.x, -in.y) * 180.0f / pi;
      pv.pitch = 20.0f;
      man_at(ctx, r, street, 0.9f);
      walk_in(r, street, 1.2f);
      next();
    }
    break;
  case 15:
    if (t > 2.0f) {
      building3d &r = pv.houses[2];
      i32 street = 0;
      for (i32 i = 0; i < static_cast<i32>(r.as.doors.size()); ++i)
        if (r.as.doors[static_cast<size_t>(i)].rigged)
          street = i;
      const f32 shut = man_depth(ctx, r, street);
      say("  rounded corner door shut: man at %.2f m", shut);
      expect(shut < -0.1f, "the planar door on the curve keeps a man out while shut");
      pv.shot = "pbk_m2_rounded_door.png";
      door_toggle(r, street);
      next();
    }
    break;
  case 16:
    if (t > 3.0f) {
      building3d &r = pv.houses[2];
      i32 street = 0;
      for (i32 i = 0; i < static_cast<i32>(r.as.doors.size()); ++i)
        if (r.as.doors[static_cast<size_t>(i)].rigged)
          street = i;
      const f32 in = man_depth(ctx, r, street);
      say("  rounded corner door open: man at %.2f m", in);
      expect(in > 0.5f, "through the open door into the shop on the curve");
      pv.shot = "pbk_m2_rounded_open.png";
      pv.step = 100; // load the next scene a frame later, after the shot
      pv.step_clock = 0.0f;
    }
    break;
  case 100:
    if (t > 0.1f) {
      load_scene(ctx, scene_generated);
      pv.yaw = 200.0f;
      pv.pitch = 40.0f;
      pv.step = 17;
      pv.step_clock = 0.0f;
    }
    break;
  case 17: // Milestone 3: houses from seeds.
    if (t > 0.5f) {
      say("  seed %u: %d houses built", pv.gen_seed, static_cast<i32>(pv.houses.size()));
      for (const std::string &n : pv.notes)
        say("  NoFit: %s", n.c_str());
      expect(pv.houses.size() >= 5, "the generator builds the archetypes it supports");
      pv.shot = "pbk_m3_generated_seed1.png";
      next();
    }
    break;
  case 18:
    if (t > 0.3f) {
      pv.cut = {.floors = 0, .cut = true};
      pv.dirty = true;
      next();
    }
    break;
  case 19:
    if (t > 0.3f) {
      pv.shot = "pbk_m3_generated_seed1_floor0.png";
      pv.step = 101;
      pv.step_clock = 0.0f;
    }
    break;
  case 101:
    if (t > 0.1f) {
      pv.gen_seed = 2;
      pv.cut = {};
      load_scene(ctx, scene_generated);
      pv.step = 20;
      pv.step_clock = 0.0f;
    }
    break;
  case 20:
    if (t > 0.5f) {
      pv.shot = "pbk_m3_generated_seed2.png";
      next();
    }
    break;
  case 21:
    if (t > 0.3f) {
      load_scene(ctx, scene_kit);
      pv.yaw = 180.0f;
      pv.pitch = 16.0f;
      pv.step = 30;
      pv.step_clock = 0.0f;
    }
    break;
  case 30: // Milestone 4: shutters, the corner window's two rigs, glass, the sidewalk.
    if (t > 0.5f) {
      const std::string corner = "Indochine/CornerWindow90", window = "Indochine/Window";
      const module_rig &cw = module_rig_of(corner);
      say("  Indochine/CornerWindow90: %d rigs, %d leaves", cw.rigs, static_cast<i32>(cw.leaves.size()));
      expect(cw.rigs == 2 && cw.leaves.size() == 4, "the corner window's two rigs each move their two shutters");
      const module_rig &w = module_rig_of(window);
      for (const rig_leaf &l : w.leaves)
        say("  Indochine/Window leaf %s on bone %s", l.node.c_str(), l.joint.c_str());
      const glb_summary gi = read_glb(kit_path(load_manifest().find("Indochine/Window")->path));
      const glb_summary gm = read_glb(kit_path(load_manifest().find("Modern/Window")->path));
      expect(gi.glass == 0, "no glass behind the wooden shutters");
      expect(gm.glass > 0, "the Modern window has a pane of transmission glass");
      const model_handle mw = batch_model(ctx, "Modern/Window#d");
      bool see_through = false;
      for (i32 k = 0; k < model_material_count(ctx, mw); ++k)
        see_through = see_through || model_material_get(ctx, mw, k).color.a < 0.5f;
      expect(see_through, "the engine draws the Modern window's glass see-through");
      const model_handle wood = batch_model(ctx, "Indochine/Window#leaf0");
      expect(wood.id != 0 && model_material_count(ctx, wood) >= 2, "a shutter leaf loads on its own with its wood and metal");
      pv.shot = "pbk_m4_kit_open.png";
      next();
    }
    break;
  case 31: // Shut every shutter, the corner window's four leaves too.
    if (t > 0.3f) {
      building3d &b = pv.houses[0];
      for (i32 i = 0; i < static_cast<i32>(b.shutters.size()); ++i)
        expect(shutter_toggle(b, i), "a shutter shuts on request");
      next();
    }
    break;
  case 32:
    if (t > 0.5f && t < 0.5f + 1.0f / 60.0f) {
      say("  closing: shutter 0 at clip %.3f s, %.1f deg", pv.houses[0].shutters[0].t, pv.houses[0].shutters[0].angle);
      pv.shot = "pbk_m4_kit_closing.png";
    }
    if (t > 1.4f) {
      const building3d &b = pv.houses[0];
      bool all = true;
      for (const door_state &s : b.shutters)
        all = all && s.phase == door_state::shut && std::fabs(s.angle) < 1.0f;
      say("  shut: %d shutters, shutter 0 at clip %.4f s (manifest 0.9583), %.1f deg", static_cast<i32>(b.shutters.size()),
          b.shutters[0].t, b.shutters[0].angle);
      expect(all, "every shutter stands shut at the clip's shut pose");
      pv.shot = "pbk_m4_kit_shut.png";
      next();
    }
    break;
  case 33: // Open them again: the clip's last stretch, 71/24..95/24... held open.
    if (t > 0.3f) {
      building3d &b = pv.houses[0];
      for (i32 i = 0; i < static_cast<i32>(b.shutters.size()); ++i)
        shutter_toggle(b, i);
      next();
    }
    break;
  case 34:
    if (t > 1.4f) {
      const building3d &b = pv.houses[0];
      bool all = true;
      for (const door_state &s : b.shutters)
        all = all && s.phase == door_state::open && std::fabs(std::fabs(s.angle) - 115.0f) < 1.5f;
      say("  open again: shutter 0 %.1f deg", b.shutters[0].angle);
      expect(all, "every shutter opens again to 115 degrees");
      // The sidewalk: every piece at its real size, the walkway clear.
      i32 blocked = 0;
      for (const preview::street_piece &sp : pv.street) {
        const box2 f = street_foot(sp);
        const bool hit = f.x0 < pv.walkway.x1 && f.x1 > pv.walkway.x0 && f.y0 < pv.walkway.y1 && f.y1 > pv.walkway.y0;
        say("  %s at (%.1f, %.1f): %.2f x %.2f x %.2f m%s", sp.a->id.c_str(), sp.at.x, sp.at.y, sp.a->size.x, sp.a->size.z,
            sp.a->size.y, hit ? "  IN THE WALKWAY" : "");
        blocked += hit ? 1 : 0;
      }
      expect(pv.street.size() == 12 && blocked == 0, "sidewalk_layout.json: 12 pieces, none in the walkway");
      pv.target = street_point({0.0f, 0.0f}) + vec3{0.0f, 0.2f, 0.0f};
      pv.dist = 5.5f;
      pv.pitch = 30.0f;
      pv.yaw = 200.0f;
      next();
    }
    break;
  case 35:
    if (t > 0.3f) {
      pv.shot = "pbk_m4_sidewalk.png";
      next();
    }
    break;
  case 36:
    if (t > 0.3f) {
      say("%s: %d failed", pv.failed == 0 ? "ALL PASS" : "FAILURES", pv.failed);
      std::string all;
      for (const std::string &l : pv.log)
        all += l + "\n";
      if (pv.tour) {
        // Round again from the door.
        pv.log.clear();
        pv.failed = 0;
        pv.gen_seed = 1;
        pv.cut = {};
        load_scene(ctx, scene_door);
        pv.step = 0;
        pv.step_clock = 0.0f;
        break;
      }
      file_write("pbk_test_report.txt", all);
      quit(ctx);
      next();
    }
    break;
  default: break;
  }
}

void update(context &ctx) {
  const f32 dt = delta(ctx);
  pv.clock += dt;
  // The tour holds each step about three times as long as the test.
  pv.step_clock += pv.tour ? dt * 0.3f : dt;
  ++pv.frame;
  if (pv.test) {
    script(ctx);
  } else {
    if (key_pressed(ctx, key_tab))
      load_scene(ctx, static_cast<scene_t>((pv.scene + 1) % scene_count));
    if (key_pressed(ctx, key_r) && pv.scene == scene_generated) {
      ++pv.gen_seed;
      load_scene(ctx, scene_generated);
    }
    for (i32 k = 0; k <= 4; ++k)
      if (key_pressed(ctx, static_cast<key_code>(key_1 + k - 1)) && k > 0) {
        pv.cut = {.floors = k - 1, .cut = true};
        pv.dirty = true;
      }
    if (key_pressed(ctx, key_c)) {
      pv.cut = {};
      pv.dirty = true;
    }
    if (key_held(ctx, key_left) || key_held(ctx, key_a))
      pv.yaw -= 70.0f * dt;
    if (key_held(ctx, key_right) || key_held(ctx, key_d))
      pv.yaw += 70.0f * dt;
    if (key_held(ctx, key_up) || key_held(ctx, key_w))
      pv.pitch = std::min(85.0f, pv.pitch + 40.0f * dt);
    if (key_held(ctx, key_down) || key_held(ctx, key_s))
      pv.pitch = std::max(5.0f, pv.pitch - 40.0f * dt);
    pv.dist = std::clamp(pv.dist * (1.0f - mouse_wheel(ctx) * 0.1f), 0.5f, 40.0f);
    if (mouse_held(ctx, mouse_right)) {
      const vec2 d = mouse_delta(ctx);
      pv.yaw += d.x * 0.3f;
      pv.pitch = std::clamp(pv.pitch + d.y * 0.2f, 5.0f, 85.0f);
    }
    // Click a door (or E at the screen's middle) to open or shut it.
    const bool click = mouse_pressed(ctx, mouse_left);
    if (click || key_pressed(ctx, key_e)) {
      const vec2 at = click ? mouse_pos(ctx) : screen_size(ctx) * 0.5f;
      const ray3d ray = camera3d_ray(ctx, camera(), at);
      f32 best = 1e30f;
      building3d *hit = nullptr;
      i32 door = -1;
      for (building3d &b : pv.houses) {
        f32 dd = 0.0f;
        const i32 i = door_pick(b, ray, &dd);
        if (i >= 0 && dd < best) {
          best = dd;
          hit = &b;
          door = i;
        }
      }
      if (hit)
        door_toggle(*hit, door);
    }
  }
}

void render(context &ctx) {
  if (pv.tour)
    pv.shot.clear(); // watching, not testing: no screenshots
  if (pv.dirty)
    rebuild(ctx);
  light3d_set(ctx, {.direction = {-0.5f, -1.0f, -0.35f},
                    .color = {0.95f, 0.9f, 0.82f, 1.0f},
                    .ambient = {0.42f, 0.43f, 0.48f, 1.0f},
                    .shadows = true,
                    .shadow_range = pv.dist * 1.2f + 1.0f,
                    .shadow_size = 2048});
  begin_3d(ctx, camera());
  draw_plane3d(ctx, {pv.target.x, -0.001f, pv.target.z}, {80.0f, 80.0f}, rgb(150, 146, 136));
  pv.batch.draw(ctx);
  for (const building3d &b : pv.houses)
    draw_doors(ctx, b, pv.cut);
  if (pv.scene == scene_kit && !pv.houses.empty()) {
    const f32 s = city::units_per_metre * unit3d;
    const f32 front = pv.houses[0].at.yaw_of(yaw_facing({0.0f, -1.0f}));
    for (const preview::street_piece &sp : pv.street)
      draw_model(ctx, street_model(ctx, *sp.a),
                 {.position = street_point(sp.at), .rotation = {0.0f, front + sp.rot, 0.0f}, .scale = {s, s, s}});
    // The walkway, a pale strip on the ground.
    const vec3 a = street_point({pv.walkway.x0, pv.walkway.y0}), b = street_point({pv.walkway.x1, pv.walkway.y1});
    draw_plane3d(ctx, (a + b) * 0.5f + vec3{0.0f, 0.001f, 0.0f}, {std::fabs(b.x - a.x), std::fabs(b.z - a.z)},
                 rgb(200, 196, 186));
  }
  if (pv.man.id != 0) {
    const vec3 p = character3d_position(ctx, pv.man) * (city::units_per_metre * unit3d);
    draw_cube3d(ctx, p + vec3{0.0f, city::person_height * 0.5f * unit3d, 0.0f},
                {0.4f * 0.1875f, city::person_height * unit3d, 0.3f * 0.1875f}, rgb(177, 68, 54));
  }
  end_3d(ctx);
  if (pv.shot.empty() || !pv.test) {
    char line[160];
    std::snprintf(line, sizeof(line), "Canh %s  |  Tab: canh tiep  1-4: mo tang  C: ca nha  click/E: cua  R: seed moi",
                  scene_names[pv.scene]);
    draw_text(ctx, line, {12, 10}, 18, colors::white);
    i32 y = 34;
    for (const building3d &b : pv.houses) {
      std::snprintf(line, sizeof(line), "%s %s seed %u, %d tang", b.p.archetype.empty() ? "wall+door" : b.p.archetype.c_str(),
                    b.p.style.c_str(), b.p.seed, b.p.storeys());
      draw_text(ctx, line, {12, static_cast<f32>(y)}, 15, rgb(230, 230, 220));
      y += 18;
    }
    for (const std::string &n : pv.notes) {
      draw_text(ctx, ("NoFit: " + n).c_str(), {12, static_cast<f32>(y)}, 15, rgb(240, 170, 120));
      y += 18;
    }
  }
  if (!pv.shot.empty()) {
    screenshot(ctx, pv.shot.c_str());
    say("  screenshot %s", pv.shot.c_str());
    pv.shot.clear();
  }
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, startup, "pbk_start");
  ecs_register(ctx, phase_fixed_update, fixed, "pbk_doors");
  ecs_register(ctx, phase_update, update, "pbk_update");
  ecs_register(ctx, phase_render, render, "pbk_render");
}

} // namespace

mod_desc pbk_module(bool test, bool tour) {
  pv.test = test || tour;
  pv.tour = tour;
  return {.name = "pbk_preview", .setup = setup};
}

} // namespace sandtable
