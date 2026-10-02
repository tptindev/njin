#include "pbk.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <map>

// --pbkcheck: the building kit without a window. Reads the manifest and
// every GLB's own JSON, the three example plans, and runs the generator over
// every archetype it builds with many seeds, checking each result against
// the rules; writes the plans it made to pbk_out/ to look at.

namespace sandtable::city::pbk {

namespace {

i32 failures = 0;

void fail(const char *what, const std::string &detail = {}) {
  ++failures;
  std::printf("  FAIL %s%s%s\n", what, detail.empty() ? "" : ": ", detail.c_str());
}

void report(const char *name, const check_report &r, bool verbose) {
  std::printf("  %s: %s, %d errors, %d warnings, score %.2f\n", name, r.ok() ? "ok" : "BROKEN",
              static_cast<i32>(r.errors.size()), static_cast<i32>(r.warnings.size()), r.score);
  for (const std::string &e : r.errors)
    std::printf("    error: %s\n", e.c_str());
  if (verbose)
    for (const std::string &w : r.warnings)
      std::printf("    warning: %s\n", w.c_str());
}

void check_manifest() {
  std::printf("[pbk] manifest\n");
  const manifest &m = load_manifest();
  if (!m.loaded) {
    fail("export_manifest.json did not load");
    return;
  }
  std::printf("  %d modules, kit %s, render scale %.4f\n", static_cast<i32>(m.modules.size()), m.kit_revision.c_str(),
              m.render_scale);
  std::printf("  visual %s; source %s; export %s\n", m.visual_variant.c_str(),
              m.source_revision.c_str(), m.export_pending ? "pending (using existing GLBs)" : "current");
  if (std::fabs(m.render_scale - units_per_metre / 32.0f) > 1e-5f)
    fail("engine_render_scale is not units_per_metre * unit3d");
  const rules &R = load_rules();
  if (R.kit_revision != m.kit_revision)
    fail("runtime rules revision does not match available GLBs");
  i32 doors = 0;
  i32 totals[2] = {0, 0};
  for (const module_info &mi : m.modules) {
    const glb_summary g = read_glb(kit_path(mi.path));
    if (!g.ok) {
      fail("GLB did not read", mi.path);
      continue;
    }
    if (!g.root_identity)
      fail("ModuleRoot is not identity", mi.id);
    if (g.mesh_nodes.empty() || g.materials < 1)
      fail("no meshes or materials", mi.id);
    const f32 tol = 1e-3f;
    if (!mi.animated &&
        (std::fabs(g.lo.x - mi.lo.x) > tol || std::fabs(g.lo.y - mi.lo.y) > tol || std::fabs(g.lo.z - mi.lo.z) > tol ||
         std::fabs(g.hi.x - mi.hi.x) > tol || std::fabs(g.hi.y - mi.hi.y) > tol || std::fabs(g.hi.z - mi.hi.z) > tol)) {
      char buf[200];
      std::snprintf(buf, sizeof(buf), "%s: GLB bounds (%.3f %.3f %.3f)-(%.3f %.3f %.3f), manifest (%.3f %.3f %.3f)-(%.3f %.3f %.3f)",
                    mi.id.c_str(), g.lo.x, g.lo.y, g.lo.z, g.hi.x, g.hi.y, g.hi.z, mi.lo.x, mi.lo.y, mi.lo.z, mi.hi.x,
                    mi.hi.y, mi.hi.z);
      fail("bounds", buf);
    }
    // The loader keeps UVs, normals and tangents (the normal maps need
    // them), the textures inside the file, and draws transmission as glass.
    if (g.incomplete > 0)
      fail("a primitive without normals, or the UVs and tangents its textures need", mi.id);
    if (g.embedded_images != g.images)
      fail("a texture outside the GLB", mi.id);
    totals[0] += g.glazing_materials.empty() ? 0 : 1;
    totals[1] += g.textured.empty() ? 0 : 1;
    // Wooden shutters close the window: no pane of glass behind them.
    if (mi.shutters && !g.glazing_materials.empty())
      fail("a window with wooden shutters has glass", mi.id);
    if (!mi.animated)
      continue;
    ++doors;
    if (g.skins != mi.rig_count)
      fail("the file's rigs are not the manifest's rig_count", mi.id);
    if (std::find(g.animations.begin(), g.animations.end(), mi.anim_name) == g.animations.end())
      fail("the clip is missing", mi.id + " " + mi.anim_name);
    const module_rig &rig = module_rig_of(mi.id);
    if (!rig.ok) {
      fail("no leaf moved by the clip", mi.id);
      continue;
    }
    // Every leaf: shut at its shut pose, turned the open angle at its open
    // pose and still there through the hold, shut again at the end of the
    // closing stretch.
    const door_clip c = clip_of(&mi);
    // Held open between the opening stretch and the next one that moves it.
    const f32 hold = (c.open_to + (c.close_from > c.open_to ? c.close_from : c.duration)) * 0.5f;
    i32 left = 0, right = 0;
    for (i32 k = 0; k < static_cast<i32>(rig.leaves.size()); ++k) {
      const f32 open = rig.turn(k, c.open_pose, c.shut_pose), back = rig.turn(k, c.close_to, c.shut_pose);
      const f32 held = rig.turn(k, hold, c.shut_pose);
      (open > 0.0f ? left : right) += 1;
      if (std::fabs(std::fabs(open) - mi.open_angle) > 1.5f || std::fabs(std::fabs(held) - mi.open_angle) > 1.5f ||
          std::fabs(back) > 0.5f) {
        char buf[200];
        std::snprintf(buf, sizeof(buf), "%s leaf %s (%s): open %.1f, held %.1f, shut again %.1f deg", mi.id.c_str(),
                      rig.leaves[static_cast<size_t>(k)].node.c_str(), rig.leaves[static_cast<size_t>(k)].joint.c_str(),
                      open, held, back);
        fail("clip timeline", buf);
      }
    }
    // A pair of shutters opens apart: as many turning one way as the other.
    if (mi.shutters && left != right)
      fail("shutters that do not open as pairs", mi.id);
    if (mi.style == "Indochine" || mi.family == "DoorRigged")
      std::printf("  %s: %s, %d rig(s), %d leaves, open %.0f deg at %.4f s, shut at %.4f s\n", mi.id.c_str(),
                  mi.anim_name.c_str(), rig.rigs, static_cast<i32>(rig.leaves.size()),
                  std::fabs(rig.turn(0, c.open_pose, c.shut_pose)), c.open_pose, c.shut_pose);
  }
  std::printf("  %d animated modules with their rigs and clip; %d with glass, %d with a wood texture\n", doors,
              totals[0], totals[1]);
  if (!R.loaded)
    fail("building_rules.json did not load");
}

// The checks a plan's assembly must pass beyond check_plan.
void check_assembly(const plan &p, const assembly &as, const char *name) {
  // A visual opening must be present in the plan used by daylight and
  // lighting. The former assembly-only random pass violated this contract.
  const auto facade = exterior_bays(p);
  for (const auto &m : as.modules) {
    if (m.id.find("Window") == std::string::npos && m.id.find("Balcony") == std::string::npos) continue;
    for (const auto &b : facade) {
      if (distance(vec2{m.pos.x, m.pos.y}, b.origin) > 0.05f) continue;
      bool planned = false;
      for (const auto &a : p.apertures)
        if (a.floor == m.floor && distance(a.center, b.center) < 0.05f) planned = true;
      if (!planned) fail("rendered window absent from aperture plan", name);
    }
  }
  const auto merged = collision_solids(as, false);
  const auto covers = [](const std::vector<solid> &boxes, vec2 at, f32 z) {
    for (const auto &s : boxes) {
      const vec2 u = from_angle(s.angle), v{-u.y, u.x}, d = at - s.c;
      if (z >= s.z0 && z <= s.z1 && std::fabs(dot(d,u)) <= s.half.x && std::fabs(dot(d,v)) <= s.half.y)
        return true;
    }
    return false;
  };
  std::vector<solid> original;
  for (const auto &s : as.solids) if (s.kind == sk_exterior || s.kind == sk_facade) original.push_back(s);
  for (const auto &bay : exterior_bays(p))
    for (const f32 across : {-0.31f, -0.1f, 0.03f})
      for (i32 f = 0; f < p.storeys(); ++f)
        for (const f32 height : {0.5f, 2.5f}) {
          const vec2 at = bay.center + bay.out * across;
          const f32 z = f * storey + height;
          if (covers(original, at, z) != covers(merged, at, z)) fail("merged collider changed a facade or doorway", name);
        }
  std::printf("  %s: facade colliders %zu -> %zu\n", name, original.size(), merged.size());
  // The street door: shut, nothing is reached; open, everything.
  std::vector<std::string> shut;
  for (const door &d : as.doors)
    if (d.rigged)
      shut.push_back(d.portal);
  if (shut.empty()) {
    fail("no rigged street door", name);
    return;
  }
  const std::vector<std::string> closed = unreachable_rooms(p, shut, &as);
  i32 rooms = 0;
  for (const floor_plan &f : p.floors)
    rooms += static_cast<i32>(f.rooms.size());
  if (static_cast<i32>(closed.size()) != rooms)
    fail("rooms reached through a shut street door", name);
  // The leaf turns inward: its open direction against the street.
  for (const door &d : as.doors) {
    const f32 r = (d.closed_angle + d.swing) * pi / 180.0f;
    const vec2 tip = d.hinge + vec2{std::cos(r), -std::sin(r)} * d.width;
    bool inside = false;
    for (const room &rm : p.floors[static_cast<size_t>(d.floor)].rooms)
      inside = inside || point_in_polygon(rm.poly, tip);
    if (!inside)
      fail("an open door's leaf ends outside every room", std::string(name) + " " + d.portal);
  }
  i32 n_modules = 0, n_solid = 0;
  for (const module_place &m : as.modules)
    n_modules += m.floor >= -1 ? 1 : 0;
  n_solid = static_cast<i32>(as.solids.size());
  std::printf("  %s: %d modules, %d solids, %d doors, %d props\n", name, n_modules, n_solid,
              static_cast<i32>(as.doors.size()), static_cast<i32>(as.furniture.size()));
  for (const std::string &n : as.notes)
    std::printf("    note: %s\n", n.c_str());
}

void check_examples(bool verbose) {
  std::printf("[pbk] example plans\n");
  for (const char *name : {"detached_spacious", "corner_shop_3_fronts", "corner_shop_rounded"}) {
    const std::string path = std::string(kit_dir) + "/rules/examples/" + name + ".plan.json";
    plan p;
    std::string error;
    if (!load_plan(path.c_str(), p, &error)) {
      fail("plan did not load", path + ": " + error);
      continue;
    }
    const assembly as = assemble(p);
    const check_report rep = check_plan(p, &as);
    report(name, rep, verbose);
    if (!rep.ok())
      ++failures;
    check_assembly(p, as, name);
    // The plan survives a trip through JSON unchanged.
    const std::string a = json_dump(plan_to_json(p));
    plan q;
    if (!plan_from_json(plan_to_json(p), q, &error) || json_dump(plan_to_json(q)) != a)
      fail("JSON round trip", name);
  }
}

struct case_ {
  const char *archetype;
  f32 w0, w1, d0, d1;
  i32 f0, f1;
};

void check_generator(i32 seeds, bool verbose) {
  std::printf("[pbk] generator\n");
  // The example requests: what they ask for comes out, valid.
  for (const char *name : {"detached_spacious", "corner_shop_3_fronts", "corner_shop_rounded"}) {
    json_value j;
    request req;
    if (!json_load((std::string(kit_dir) + "/rules/examples/" + name + ".request.json").c_str(), j) ||
        !request_from_json(j, req)) {
      fail("request did not load", name);
      continue;
    }
    const gen_result g = generate(req);
    if (!g.ok) {
      fail("example request NoFit", std::string(name) + ": " + (g.reasons.empty() ? "" : g.reasons.back()));
      continue;
    }
    const assembly as = assemble(g.p);
    const check_report rep = check_plan(g.p, &as);
    char label[96];
    std::snprintf(label, sizeof(label), "%s seed %u (%d tries)", name, req.seed, g.tries);
    report(label, rep, verbose);
    if (!rep.ok())
      ++failures;
    check_assembly(g.p, as, name);
    json_save((std::string("pbk_out/") + name + ".generated.plan.json").c_str(), plan_to_json(g.p));
    // The same request twice: the same house.
    const gen_result again = generate(req);
    if (!again.ok || json_dump(plan_to_json(again.p)) != json_dump(plan_to_json(g.p)))
      fail("the same seed made a different plan", name);
  }

  // Every archetype it builds, over its range of sizes and floors.
  const case_ cases[] = {{"l_wing_house", 14, 20, 14, 20, 1, 3}, {"t_wing_house", 18, 24, 14, 20, 1, 3},
                         {"detached_spacious", 10, 16, 10, 16, 1, 3}, {"townhouse", 6, 8, 12, 18, 2, 4},
                         {"shop_house", 8, 12, 10, 16, 2, 4},        {"corner_shop_house", 8, 12, 8, 14, 2, 4},
                         {"corner_shop_3_fronts", 10, 14, 10, 14, 2, 4}, {"corner_shop_rounded", 10, 14, 10, 14, 2, 4},
                         {"tube_house", 4, 6, 14, 22, 1, 5},           {"tube_shop_house", 4, 6, 16, 22, 1, 5},
                         {"apartment_block", 10, 26, 8, 20, 2, 8},     {"hotel", 8, 20, 8, 20, 2, 8},
                         {"school", 14, 26, 8, 14, 1, 3},              {"workshop_hall", 8, 24, 10, 18, 1, 2},
                         {"warehouse_hall", 12, 26, 10, 18, 1, 3},     {"market_hall", 18, 30, 14, 22, 1, 2}};
  for (const case_ &c : cases) {
    i32 ok = 0, nofit = 0, broken = 0;
    std::vector<std::string> sample;
    std::string first_reason;
    std::vector<std::string> dumps;
    for (i32 s = 1; s <= seeds; ++s) {
      rng r(static_cast<u64>(s) * 7919u + 17u);
      request req;
      req.seed = static_cast<u32>(s) * 2654435761u;
      req.archetype = c.archetype;
      req.width = std::round(r.range(c.w0, c.w1) / bay) * bay;
      req.depth = std::round(r.range(c.d0, c.d1) / bay) * bay;
      req.floors = r.range(c.f0, c.f1);
      req.district = (s % 3 == 0) ? "old_quarter" : (s % 3 == 1) ? "residential" : "new_urban";
      const gen_result g = generate(req);
      if (!g.ok) {
        ++nofit;
        if (first_reason.empty() && !g.reasons.empty()) {
          char buf[160];
          std::snprintf(buf, sizeof(buf), "%.0fx%.0f m %d fl: ", req.width, req.depth, req.floors);
          first_reason = buf + g.reasons.back();
        }
        continue;
      }
      const assembly as = assemble(g.p);
      const check_report rep = check_plan(g.p, &as);
      if (!rep.ok()) {
        ++broken;
        report(c.archetype, rep, verbose);
        continue;
      }
      ++ok;
      dumps.push_back(json_dump(plan_to_json(g.p), false));
      if (s <= 3) {
        char path[128];
        std::snprintf(path, sizeof(path), "pbk_out/%s_%d.plan.json", c.archetype, s);
        json_save(path, plan_to_json(g.p));
      }
    }
    std::sort(dumps.begin(), dumps.end());
    const i32 distinct = static_cast<i32>(std::unique(dumps.begin(), dumps.end()) - dumps.begin());
    std::printf("  %-22s %3d valid, %3d NoFit, %d broken, %d distinct%s%s\n", c.archetype, ok, nofit, broken, distinct,
                first_reason.empty() ? "" : "; e.g. ", first_reason.c_str());
    if (broken > 0)
      ++failures;
  }
  // What it refuses, and says why.
  for (const char *a : {"courtyard_house", "low_rise_apartment"}) {
    request req;
    req.archetype = a;
    req.width = 14;
    req.depth = 14;
    req.floors = 2;
    const gen_result g = generate(req);
    std::printf("  %-22s refused: %s\n", a, g.ok ? "(made a plan!)" : g.reasons.front().c_str());
    if (g.ok)
      fail("built a shape it does not support", a);
  }
  {
    request req;
    req.archetype = "townhouse";
    req.width = 6;
    req.depth = 12;
    req.floors = 2;
    const gen_result g = generate(req);
    std::printf("  townhouse 6x12 m: %s%s\n", g.ok ? "ok" : "NoFit: ", g.ok ? "" : g.reasons.back().c_str());
  }
}

} // namespace

void dump_nav(const plan &p, const assembly &as, const char *prefix) {
  // Walkable white, blocked dark, room outlines red; the plan's y up the picture.
  for (i32 f = 0; f < p.storeys(); ++f) {
    const nav_floor n = build_nav(p, f, {}, &as);
    std::vector<u8> rgb(static_cast<size_t>(n.nx * n.ny * 3));
    for (i32 i = 0; i < n.nx * n.ny; ++i) {
      const bool w = n.walk[static_cast<size_t>(i)] != 0;
      rgb[static_cast<size_t>(i) * 3] = w ? 235 : 40;
      rgb[static_cast<size_t>(i) * 3 + 1] = w ? 235 : 40;
      rgb[static_cast<size_t>(i) * 3 + 2] = w ? 235 : 60;
    }
    for (const room &r : p.floors[static_cast<size_t>(f)].rooms)
      for (size_t k = 0; k < r.poly.size(); ++k) {
        const vec2 a = r.poly[k], b = r.poly[(k + 1) % r.poly.size()];
        for (i32 s = 0; s <= 200; ++s) {
          const i32 i = n.index(a + (b - a) * (static_cast<f32>(s) / 200.0f));
          if (i >= 0) {
            rgb[static_cast<size_t>(i) * 3] = 220;
            rgb[static_cast<size_t>(i) * 3 + 1] = 60;
            rgb[static_cast<size_t>(i) * 3 + 2] = 50;
          }
        }
      }
    // Door leaves, open, in blue.
    for (const door &d : as.doors) {
      if (d.floor != f)
        continue;
      const f32 r = (d.closed_angle + d.swing) * pi / 180.0f;
      for (i32 s = 0; s <= 40; ++s) {
        const i32 i = n.index(d.hinge + vec2{std::cos(r), -std::sin(r)} * (d.width * static_cast<f32>(s) / 40.0f));
        if (i >= 0) {
          rgb[static_cast<size_t>(i) * 3] = 40;
          rgb[static_cast<size_t>(i) * 3 + 1] = 90;
          rgb[static_cast<size_t>(i) * 3 + 2] = 230;
        }
      }
    }
    std::string img = "P6\n" + std::to_string(n.nx) + " " + std::to_string(n.ny) + "\n255\n";
    for (i32 y = n.ny - 1; y >= 0; --y)
      img.append(reinterpret_cast<const char *>(rgb.data()) + static_cast<size_t>(y * n.nx * 3),
                 static_cast<size_t>(n.nx * 3));
    file_write((std::string(prefix) + "_f" + std::to_string(f) + ".ppm").c_str(), img);
  }
}

namespace {
// Times the steps of one request and writes each floor's walkable raster as a PPM.
void probe(const request &req) {
  using clk = std::chrono::steady_clock;
  const auto ms = [](clk::time_point a) { return std::chrono::duration<f64, std::milli>(clk::now() - a).count(); };
  auto t = clk::now();
  const gen_result g = generate(req);
  std::printf("[pbk] probe %s %.0fx%.0f %d fl seed %u: %s in %.1f ms, %d tries\n", req.archetype.c_str(), req.width,
              req.depth, req.floors, req.seed, g.ok ? "ok" : "NoFit", ms(t), g.tries);
  for (const std::string &r : g.reasons)
    std::printf("  %s\n", r.c_str());
  if (!g.ok)
    return;
  t = clk::now();
  const assembly as = assemble(g.p);
  std::printf("  assemble %.1f ms\n", ms(t));
  t = clk::now();
  const check_report rep = check_plan(g.p, &as);
  std::printf("  check %.1f ms\n", ms(t));
  report("probe", rep, true);
  dump_nav(g.p, as, "pbk_out/nav");
  json_save("pbk_out/probe.plan.json", plan_to_json(g.p));
}
} // namespace

// Every building of town `seed`: whether the kit lays it out in full, and why not.
void city_tally(u32 seed) {
  city_map map;
  city_desc desc;
  desc.seed = seed;
  city::generate(map, desc);
  std::map<std::string, i32> tally;
  i32 ok = 0;
  for (i32 i = 0; i < static_cast<i32>(map.buildings.size()); ++i) {
    vec2 c;
    std::string why;
    const std::vector<request> reqs = requests_for_building(map, i, c, &why);
    if (reqs.empty()) {
      ++tally["skip: " + why.substr(0, 60)];
      continue;
    }
    gen_result g;
    for (const request &rq : reqs) {
      g = generate(rq);
      if (g.ok) {
        ++ok;
        ++tally["ok: " + rq.archetype];
        for (const auto &a : g.p.apertures)
          if (a.module_id.find("Balcony") != std::string::npos) ++tally["balcony bays"];
        break;
      }
    }
    if (!g.ok) {
      std::string r = g.reasons.empty() ? "?" : g.reasons.back();
      // Numbers apart, the same reason counts once.
      for (char &ch : r)
        if ((ch >= '0' && ch <= '9') || ch == '.')
          ch = '#';
      ++tally["NoFit " + reqs.back().archetype + ": " + r.substr(0, 80)];
    }
  }
  std::printf("[pbk] city %u: %d buildings, %d laid out by the kit\n", seed, static_cast<i32>(map.buildings.size()), ok);
  std::vector<std::pair<i32, std::string>> rows;
  for (const auto &[k, v] : tally)
    rows.push_back({v, k});
  std::sort(rows.rbegin(), rows.rend());
  for (const auto &[v, k] : rows)
    std::printf("  %4d  %s\n", v, k.c_str());
}

i32 run_pbk_check(i32 seeds, bool verbose) {
  failures = 0;
  if (seeds >= 100000) {
    city_tally(static_cast<u32>(seeds - 100000));
    return 0;
  }
  if (seeds < 0) {
    request req;
    req.archetype = "detached_spacious";
    req.width = 12;
    req.depth = 10;
    req.floors = 3;
    req.seed = static_cast<u32>(-seeds);
    gen_trace = true;
    probe(req);
    return 0;
  }
  check_manifest();
  check_examples(verbose);
  check_generator(seeds, verbose);
  std::printf("[pbk] %s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
  return failures;
}

} // namespace sandtable::city::pbk
