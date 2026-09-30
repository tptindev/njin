#include "game.h"
#include "audio.h"
#include "crowd.h"
#include "person.h"
#include "render.h"
#include "view.h"
#include "world.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sandtable {

game_state state;

namespace {

// The HUD is laid out in a virtual screen of the window divided by the UI
// pixel size (2, more on very tall screens so the text stays readable); the
// 3D table is drawn at twice that density (render_scale, main.cpp), so at the
// window's own resolution. Set only when the size changes.
void fit_view(context &ctx) {
  static vec2 last{};
  const vec2 win = window_size(ctx);
  if (win.x < 1.0f || win.y < 1.0f || (win.x == last.x && win.y == last.y))
    return;
  last = win;
  const f32 pixel = std::max(2.0f, std::floor(win.y / 540.0f));
  window_set_virtual_size(ctx, {std::floor(win.x / pixel), std::floor(win.y / pixel)}, true);
}

void handle_input(context &ctx) {
  fit_view(ctx);
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

void test_harness(context &ctx) {
  if (!test.enabled)
    return;
  test.frame++;
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
    // One building picked, as a click would: it and whatever stands between
    // it and the camera are opened.
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
      world_view().selected = bi;
      world_view().cut = {bi};
      world_cut_around(false);
      state.hour = 12.0f;
      state.cam_yaw_goal = 0.0f;
      view_focus(world().buildings[static_cast<size_t>(bi)].box.center, 11.0f, true);
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
  if (g == 7) {
    std::printf("[test] city %u: %s\n", world().desc.seed, world().report.ok() ? "ok" : "FAILED");
    std::printf("[test] done\n");
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

void update(context &ctx) { crowd_update(delta(ctx)); }

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, startup, "sandtable_startup");
  ecs_register(ctx, phase_pre_update, handle_input, "sandtable_input");
  ecs_register(ctx, phase_update, update, "sandtable_update");
  ecs_register(ctx, phase_post_update, test_harness, "sandtable_test_harness");
  ecs_register(ctx, phase_render, render_world, "sandtable_world_render");
  ecs_register(ctx, phase_post_render, render_ui, "sandtable_ui_render");
}

} // namespace

mod_desc module(bool test_mode, u32 seed) {
  state.seed = seed;
  test = {};
  test.enabled = test_mode;
  return {.name = "sandtable", .setup = setup};
}

} // namespace sandtable
