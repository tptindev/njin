#include "game.h"
#include "audio.h"
#include "clock.h"
#include "crowd.h"
#include "gang.h"
#include "gang_ai.h"
#include "person.h"
#include "render.h"
#include "view.h"
#include "world.h"
#include "city/pbk_render.h"
#include "city/railings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sandtable {

game_state state;

namespace {

void handle_input(context &ctx) {
  update_view(ctx, ui_mouse_over(ctx));
  world_input(ctx);
  if (key_pressed(ctx, key_f12))
    screenshot(ctx, "sandtable_screenshot.png");
}

// --- Scripted test run (--test) ---
//
// The city from above by day and night and as districts, close looks at a
// few districts and inside buildings, then every motion of the men close up.

struct test_config {
  bool enabled = false;
  i32 frame = 0;
  // --pbk-city-test: the town's houses from the procedural building kit.
  bool pbk = false;
  i32 step = 0;
  f32 clock = 0.0f;
  vec2 at{};
  i32 house = -1;
  i32 frames = 0;
  f32 frame_ms = 0.0f;
};
test_config test;

struct test_shot {
  const char *file;
  city::district_kind near; // look close at the first district of this kind; count: the whole table
  f32 hour;
  city::overlay layer;
  bool pins;
  bool cut; // buildings round the middle cut open
};

const test_shot test_shots[] = {
    {"sandtable_test_city.png", city::district_kind::count, 12.0f, city::overlay::none, false, false},
    {"sandtable_test_city_night.png", city::district_kind::count, 21.0f, city::overlay::none, false, false},
    {"sandtable_test_districts.png", city::district_kind::count, 12.0f, city::overlay::districts, false, false},
    {"sandtable_test_old_quarter.png", city::district_kind::old_quarter, 15.0f, city::overlay::none, false, false},
    {"sandtable_test_alleys.png", city::district_kind::residential, 10.0f, city::overlay::none, false, false},
    {"sandtable_test_market.png", city::district_kind::market, 9.0f, city::overlay::none, true, false},
    {"sandtable_test_docks.png", city::district_kind::docks, 16.0f, city::overlay::none, false, false},
    {"sandtable_test_new_town.png", city::district_kind::new_urban, 13.0f, city::overlay::none, false, false},
    {"sandtable_test_street_night.png", city::district_kind::nightlife, 21.5f, city::overlay::none, false, false},
    {"sandtable_test_cut_old_quarter.png", city::district_kind::old_quarter, 11.0f, city::overlay::none, false, true},
    {"sandtable_test_cut_old_quarter_night.png", city::district_kind::old_quarter, 21.0f, city::overlay::none, false,
     true},
    {"sandtable_test_cut_new_town.png", city::district_kind::new_urban, 11.0f, city::overlay::none, false, true},
    {"sandtable_test_cut_docks.png", city::district_kind::docks, 11.0f, city::overlay::none, false, true},
    {"sandtable_test_cut_market.png", city::district_kind::market, 11.0f, city::overlay::none, false, true},
};

void frame_shot(const test_shot &s) {
  state.hour = s.hour;
  world_view().layer = s.layer;
  world_view().markers = s.pins;
  world_cut_around(s.cut);
  if (s.near == city::district_kind::count) {
    view_reset();
    return;
  }
  // The biggest block of the first district of that kind (a district's
  // middle can be in the river).
  vec2 at{world_width * 0.5f, world_height * 0.5f};
  for (const city::district &d : world().districts)
    if (d.kind == s.near && !d.blocks.empty()) {
      i32 best = d.blocks.front();
      for (const i32 b : d.blocks)
        if (world().blocks[static_cast<size_t>(b)].cells > world().blocks[static_cast<size_t>(best)].cells)
          best = b;
      at = world().blocks[static_cast<size_t>(best)].centroid;
      break;
    }
  view_focus(at, 14.0f, true);
}

// The kit's houses in the town: a residential district from above once the
// worker has made what is in view, closer, then one of its houses cut open
// on two floors, and its street door opened.
void pbk_city_harness(context &ctx) {
  test.clock += delta_real(ctx);
  world_view().hover_district = world_view().hover_building = -1;
  const city::pbk_stats st = city::pbk_last_stats();
  switch (test.step) {
  case 0:
    if (test.clock > 0.5f) {
      state.hour = 12.0f;
      test.at = {world_width * 0.5f, world_height * 0.5f};
      for (const city::district &d : world().districts)
        if (d.kind == city::district_kind::residential && !d.blocks.empty()) {
          i32 best = d.blocks.front();
          for (const i32 b : d.blocks)
            if (world().blocks[static_cast<size_t>(b)].cells > world().blocks[static_cast<size_t>(best)].cells)
              best = b;
          test.at = world().blocks[static_cast<size_t>(best)].centroid;
          break;
        }
      view_focus(test.at, 14.0f, true);
      test.step = 1;
      test.clock = 0.0f;
    }
    break;
  case 1: // Until the worker has caught up with the view.
    if (test.clock > 1.0f) {
      // Frame time and draw calls over the district, once it has settled.
      test.frames += 1;
      test.frame_ms += delta_real(ctx) * 1000.0f;
    }
    if ((test.clock > 3.0f && st.queued == 0) || test.clock > 90.0f) {
      const render_info ri = render_info_get(ctx);
      NJIN_INFO("[pbk-city] after %.1f s: %d houses with full plans, %d NoFit, %d queued, %d eligible",
                static_cast<f64>(test.clock), st.ready, st.nofit, st.queued, st.eligible);
      NJIN_INFO("[pbk-city] district view: %.2f ms a frame over %d frames, %u draw calls, %u instanced",
                static_cast<f64>(test.frame_ms / static_cast<f32>(std::max(1, test.frames))), test.frames,
                ri.draw_calls, ri.instanced_calls);
      screenshot(ctx, "sandtable_pbk_city.png");
      view_focus(test.at, 6.0f, true);
      test.step = 2;
      test.clock = 0.0f;
    }
    break;
  case 2:
    if ((test.clock > 3.0f && st.queued == 0) || test.clock > 60.0f) {
      NJIN_INFO("[pbk-city] close: %d made, %d NoFit, %d queued", st.ready, st.nofit, st.queued);
      screenshot(ctx, "sandtable_pbk_city_close.png");
      // The nearest house the kit made, picked as a click would.
      f32 bd = 1e30f;
      for (const i32 id : city::pbk_ready_ids()) {
        const f32 d = distance(world().buildings[static_cast<size_t>(id)].box.center, test.at);
        if (d < bd) {
          bd = d;
          test.house = id;
        }
      }
      if (test.house < 0) {
        NJIN_INFO("[pbk-city] FAIL no house made from the kit in view");
        quit(ctx);
        return;
      }
      world_view() = {};
      world_cut_around(false);
      world_focus(test.house);
      state.cam_target = state.cam_target_goal;
      state.cam_distance = state.cam_distance_goal;
      state.cam_yaw = state.cam_yaw_goal;
      state.cam_steep = state.cam_steep_goal = 1.0f;
      test.step = 3;
      test.clock = 0.0f;
    }
    break;
  case 3:
    if (test.clock > 0.6f) {
      screenshot(ctx, "sandtable_pbk_city_cut_floor0.png");
      world_view().floor = 1;
      test.step = 4;
      test.clock = 0.0f;
    }
    break;
  case 4:
    if (test.clock > 0.6f) {
      screenshot(ctx, "sandtable_pbk_city_cut_floor1.png");
      world_unfocus();
      const city::building &b = world().buildings[static_cast<size_t>(test.house)];
      const i32 door = city::pbk_door_toggle_near(test.house, b.box.center - b.box.axis_y() * b.box.half.y);
      NJIN_INFO("[pbk-city] house %d: street door %d opened", test.house, door);
      view_focus(b.box.center - b.box.axis_y() * (b.box.half.y + 4.0f), 3.0f, true);
      test.step = 5;
      test.clock = 0.0f;
    }
    break;
  case 5:
    if (test.clock > 2.0f) {
      screenshot(ctx, "sandtable_pbk_city_door_open.png");
      state.hour = 21.0f;
      state.speed = 0;
      view_focus(test.at, 10.0f, true);
      test.step = 6;
      test.clock = 0;
    }
    break;
  case 6:
    if (test.clock > 1.0f) {
      screenshot(ctx, "sandtable_pbk_city_windows_night.png");
      NJIN_INFO("[pbk-city] night near: %u instances",
                city::view_last_stats().instances);
      test.step = 7;
      test.clock = 0;
    }
    break;
  case 7:
    // screenshot() captures the next rendered frame: don't change the
    // camera in the same step that asks for the night screenshot.
    if (test.clock > 0.2f) {
      // Exercise the actual eye pass through a ground-floor glass window,
      // including cached interiors and its budgeted room lights.
      bool eye_queued = false;
      for (const i32 id : city::pbk_ready_ids()) {
        city::pbk::building3d *b = city::pbk_building(id);
        if (!b || distance(b->at.center, test.at) > 100) continue;
        for (const city::pbk::module_place &m : b->as.modules) {
          const auto *mi = city::pbk::load_manifest().find(m.id);
          if (!mi || mi->family != "Window" || mi->radius != 0 || m.shutter >= 0 || m.floor != 0)
            continue;
          capture_room_camera(id);
          eye_queued = true;
          break;
        }
        if (eye_queued) break;
      }
      if (!eye_queued) NJIN_WARN("[pbk-city] FAIL no ground-floor glass window for eye test");
      view_focus(test.at, 75.0f, true);
      test.step = 8;
      test.clock = 0;
    }
    break;
  case 8:
    if (test.clock > 1.0f) {
      screenshot(ctx, "sandtable_pbk_city_windows_lod.png");
      NJIN_INFO("[pbk-city] far LOD: %d detailed chunks, %u instances",
                city::view_last_stats().detailed, city::view_last_stats().instances);
      test.step = 9;
      test.clock = 0;
    }
    break;
  case 9:
    if (test.clock > 0.2f) {
      state.hour = 12.0f;
      state.speed = 0;
      view_focus(test.at, 75.0f, true);
      test.step = 10;
      test.clock = 0;
    }
    break;
  case 10:
    if (test.clock > 0.5f) {
      screenshot(ctx, "sandtable_hover_off_before.png");
      test.step = 11;
      test.clock = 0;
    }
    break;
  case 11:
    // Cross the HUD/world boundary repeatedly with a fixed camera and hour:
    // the highlight must not alter shadows, depth or other mesh materials.
    if (test.clock > 0.2f) {
      test.step = 12;
      test.clock = 0;
    }
    break;
  case 12:
    if (test.house >= 0)
      world_view().hover_district = world().buildings[static_cast<size_t>(test.house)].district;
    if (test.clock > 0.3f) {
      screenshot(ctx, "sandtable_hover_on.png");
      test.step = 13;
      test.clock = 0;
    }
    break;
  case 13:
    if (test.house >= 0 && test.clock < 0.2f)
      world_view().hover_district = world().buildings[static_cast<size_t>(test.house)].district;
    if (test.clock > 0.4f) {
      screenshot(ctx, "sandtable_hover_off_after.png");
      test.step = 14;
      test.clock = 0;
    }
    break;
  case 14:
    if (test.clock > 0.2f) {
      state.cam_yaw_goal = 10.0f;
      test.step = 15;
      test.clock = 0;
    }
    break;
  case 15:
    if (test.clock > 1.2f) {
      screenshot(ctx, "sandtable_ground_orbit.png");
      test.step = 16;
      test.clock = 0;
    }
    break;
  case 16:
    if (test.clock > 0.2f) {
      state.cam_yaw_goal = 0;
      test.step = 17;
      test.clock = 0;
    }
    break;
  case 17:
    if (test.clock > 1.2f) {
      screenshot(ctx, "sandtable_ground_orbit_return.png");
      test.step = 18;
      test.clock = 0;
    }
    break;
  case 18:
    if (test.clock > 0.2f) {
      for (const auto &e : city::railing_layout(world()))
        if (e.bridge) { view_focus((e.a + e.b) * 0.5f, 9.0f, true); break; }
      test.step = 19;
      test.clock = 0;
    }
    break;
  case 19:
    if (test.clock > 1.0f) {
      screenshot(ctx, "sandtable_bridge_railings.png");
      test.step = 20;
      test.clock = 0;
    }
    break;
  case 20:
    if (test.clock > 0.2f) {
      for (const auto &e : city::railing_layout(world()))
        if (!e.bridge && e.a.x > 200 && e.a.y > 200 && distance(e.a, e.b) > 20) {
          view_focus((e.a + e.b) * 0.5f, 9.0f, true); break;
        }
      test.step = 21;
      test.clock = 0;
    }
    break;
  case 21:
    if (test.clock > 1.0f) {
      screenshot(ctx, "sandtable_river_railings.png");
      test.step = 22;
      test.clock = 0;
    }
    break;
  case 22:
    if (test.clock > 0.2f) {
      NJIN_INFO("[pbk-city] done");
      quit(ctx);
      test.step = 23;
    }
    break;
  default: break;
  }
}

void test_harness(context &ctx) {
  if (test.pbk) {
    pbk_city_harness(ctx);
    return;
  }
  if (!test.enabled)
    return;
  test.frame++;
  // The mouse rests in the middle of the window: nothing lit up under it in
  // the shots.
  world_view().hover_district = world_view().hover_building = -1;
  // Each shot is set up on one frame and taken a few frames later (the shot
  // is taken as the frame is drawn).
  constexpr i32 per_shot = 4;
  constexpr i32 shots = static_cast<i32>(sizeof(test_shots) / sizeof(test_shots[0]));
  const i32 f = test.frame - 2;
  if (f >= 0 && f < shots * per_shot) {
    const test_shot &s = test_shots[f / per_shot];
    if (f % per_shot == 0)
      frame_shot(s);
    if (f % per_shot == per_shot - 1)
      screenshot(ctx, s.file);
    return;
  }
  const i32 single = f - shots * per_shot;
  constexpr i32 floor_shots = 4; // the ground floor, one between, the top, then from afar
  if (single >= 0 && single < per_shot * floor_shots) {
    const i32 k = single / per_shot, step = single % per_shot;
    if (step == 0 && k > 0 && world_view().selected >= 0) {
      const i32 floors = world().buildings[static_cast<size_t>(world_view().selected)].floors;
      world_view().floor = k == 1 ? std::min(1, floors - 1) : k == 2 ? floors - 1 : 0;
      if (k == 3) // the focus from afar: the town round it hazy
        view_focus(world().buildings[static_cast<size_t>(world_view().selected)].box.center, 34.0f, true);
    }
    if (step == per_shot - 1 && k > 0) {
      static const char *names[] = {"", "sandtable_test_cut_floor_mid.png", "sandtable_test_cut_floor_top.png",
                                    "sandtable_test_focus_wide.png"};
      screenshot(ctx, names[k]);
    }
    if (k > 0)
      return;
    // One building picked, as a click would: it alone is opened, and the
    // camera comes close from its front.
    if (single == 0) {
      // A tall tube house with a shop, so the layout shows a stair and a
      // shopfront row as well as the usual bedroom/kitchen rows.
      i32 bi = -1;
      for (i32 i = 0; i < static_cast<i32>(world().buildings.size()); ++i) {
        const city::building &c = world().buildings[static_cast<size_t>(i)];
        if (c.kind == city::building_kind::tube_house && c.business >= 0 && c.floors >= 4 && c.door_ok) {
          bi = i;
          break;
        }
      }
      if (bi < 0 && !world().hq_sites.empty())
        bi = world().hq_sites[0];
      world_view() = {};
      world_cut_around(false);
      state.hour = 12.0f;
      world_focus(bi);
      // Straight there, not eased: the shot is a few frames away.
      state.cam_target = state.cam_target_goal;
      state.cam_distance = state.cam_distance_goal;
      state.cam_yaw = state.cam_yaw_goal;
      state.cam_steep = state.cam_steep_goal = 1.0f;
    }
    if (single == per_shot - 1)
      screenshot(ctx, "sandtable_test_cut_single.png");
    return;
  }
  const i32 g = single - per_shot * floor_shots;
  if (g == 0) {
    // On the biggest open place, where no house is in the way.
    vec2 at{world_width * 0.5f, world_height * 0.5f};
    f32 best = 0.0f;
    for (const city::spot &s : world().spots) {
      const f32 area = s.box.half.x * s.box.half.y;
      const bool clear = s.kind == city::spot_kind::vacant_lot || s.kind == city::spot_kind::sports_field;
      if (clear && area > best) {
        best = area;
        at = s.box.center;
      }
    }
    state.hour = 12.0f;
    world_view() = {};
    world_cut_around(false);
    show_pose_row(true, at);
    view_focus(at, 7.0f, true);
  }
  if (g == 3)
    screenshot(ctx, "sandtable_test_poses.png");
  if (g == 4) {
    show_pose_row(false);
    view_reset();
  }
  // The player's headquarters: the lounge, the boss's office, then the men
  // mustered before the door (after a few seconds' walk), then everyone's turf.
  const i32 h = g - 5;
  const i32 hq = gangs().empty() ? -1 : gang().hq;
  if (h == 0 && hq >= 0) {
    world_view() = {};
    world_cut_around(false);
    world_focus(hq);
    state.cam_target = state.cam_target_goal;
    state.cam_distance = state.cam_distance_goal;
    state.cam_yaw = state.cam_yaw_goal;
    state.cam_steep = state.cam_steep_goal = 1.0f;
  }
  if (h == 3)
    screenshot(ctx, "sandtable_test_hq_ground.png");
  if (h == 4 && hq >= 0)
    world_view().floor = world().buildings[static_cast<size_t>(hq)].floors - 1;
  if (h == 7)
    screenshot(ctx, "sandtable_test_hq_top.png");
  if (h == 8 && hq >= 0) {
    world_unfocus();
    gang_muster(ctx, true);
    const city::building &b = world().buildings[static_cast<size_t>(hq)];
    view_focus(b.door + b.front() * 30.0f, 5.0f, true);
  }
  if (h == 600)
    screenshot(ctx, "sandtable_test_hq_muster.png");
  if (h == 601) {
    gang_muster(ctx, false);
    world_view().show_turf = true;
    view_reset();
  }
  if (h == 605)
    screenshot(ctx, "sandtable_test_turf.png");
  if (g == 5 + 610) {
    std::printf("[test] city %u: %s", world().desc.seed, world().report.ok() ? "ok" : "FAILED");
    for (const gang_state &gs : gangs())
      std::printf("[test] %s: hq %d, %d men, %d blocks", gs.name.c_str(), gs.hq, static_cast<i32>(gs.men.size()),
                  gs.turf);
    std::printf("[test] done");
    // How well the crowd walked, after the run's minute or so of it.
    const crowd_report cr = crowd_check();
    NJIN_INFO("crowd: %d walkers, %d standing in one another, %d in a building, %d repaths", cr.walkers,
              cr.overlapping, cr.in_buildings, cr.repaths);
    quit(ctx);
  }
}

void startup(context &ctx) {
  audio_init(ctx);
  render_init(ctx);
  person_init(ctx);
  city::view_init(ctx);
  world_generate(ctx, state.seed);
}

void update(context &ctx) {
  crowd_update(delta(ctx));
  gang_update(delta(ctx));
}

// Each hour of the town, a line in the log: how the town is living.
void log_hour() {
  const crowd_report cr = crowd_check();
  const city::city_map &m = world();
  i32 open = 0;
  for (i32 b = 0; b < static_cast<i32>(m.businesses.size()); ++b)
    open += business_open(b) ? 1 : 0;
  char men[200] = "";
  for (const gang_state &g : gangs()) {
    i32 out = 0;
    for (const lackey &l : g.men)
      out += l.inside ? 0 : 1;
    const size_t at = std::strlen(men);
    std::snprintf(men + at, sizeof(men) - at, " | %s: %d/%d out, %dk, %d blocks", g.name.c_str(), out,
                  static_cast<i32>(g.men.size()), g.money, g.turf);
  }
  NJIN_INFO("[clock] day %d %02d:00: %d/%d townsfolk out (%d asleep, %d at work), %d seated, %d/%d shops open%s",
            state.day, static_cast<i32>(state.hour), cr.walkers, cr.residents, cr.asleep, cr.at_work, cr.seated, open,
            static_cast<i32>(m.businesses.size()), men);
}

// Each new day, a line per gang: how its money and its men are holding up.
void log_day() {
  for (const gang_state &g : gangs()) {
    f32 mood = 0.0f;
    i32 owed = 0, hurt = 0, tired = 0, n = 0;
    for (const lackey &l : g.men) {
      if (l.rk == rank::boss)
        continue;
      ++n;
      mood += l.morale;
      owed += l.unpaid;
      hurt += l.health < hurt_limit ? 1 : 0;
      tired += l.fatigue >= tired_limit ? 1 : 0;
    }
    i32 paying = 0;
    for (const shop_state &s : shops())
      paying += &gangs()[static_cast<size_t>(s.owner < 0 ? 0 : s.owner)] == &g && s.owner >= 0 ? 1 : 0;
    NJIN_INFO("[econ] day %d %s: %dk cash, %d men + boss, owed %dk, morale %.0f, %d hurt, %d tired, %d shops, %d blocks",
              state.day, g.name.c_str(), g.money, n, owed, static_cast<f64>(n > 0 ? mood / static_cast<f32>(n) : 0.0f),
              hurt, tired, paying, g.turf);
  }
}

// Before the physics steps (njin steps it right after the game's own
// systems of this phase). The clock first: everything else keeps to it.
void fixed_update(context &ctx) {
  clock_step(delta(ctx));
  crowd_step(ctx, delta(ctx));
  gang_step(ctx, delta(ctx));
  gang_ai_step(ctx);
  if (clock_new_hour())
    log_hour();
  if (clock_new_day())
    log_day();
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, startup, "sandtable_startup");
  ecs_register(ctx, phase_pre_update, handle_input, "sandtable_input");
  ecs_register(ctx, phase_fixed_update, fixed_update, "sandtable_crowd_step");
  ecs_register(ctx, phase_update, update, "sandtable_update");
  ecs_register(ctx, phase_post_update, test_harness, "sandtable_test_harness");
  ecs_register(ctx, phase_render, render_world, "sandtable_world_render");
  ecs_register(ctx, phase_post_render, render_ui, "sandtable_ui_render");
}

} // namespace

mod_desc module(bool test_mode, u32 seed, bool pbk_test) {
  state.seed = seed;
  test = {};
  test.enabled = test_mode;
  test.pbk = pbk_test;
  return {.name = "sandtable", .setup = setup};
}

} // namespace sandtable
