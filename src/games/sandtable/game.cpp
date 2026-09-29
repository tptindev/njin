#include "game.h"
#include "audio.h"
#include "sprites.h"
#include "levels.h"
#include "render.h"
#include "sim.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sandtable {

namespace {

// Zooms one step in (+1) or out (-1), keeping the point under the mouse put.
void zoom_by(context &ctx, i32 steps) {
  const i32 next = clamp(state.zoom_step + steps, 0, zoom_count - 1);
  if (next == state.zoom_step)
    return;
  const vec2 under = scr2w(ctx, mouse_pos(ctx));
  const f32 k = 1.0f - camera_zoom() / camera_zooms[next];
  state.zoom_step = next;
  state.camera_target += (under - state.camera_target) * k;
  state.camera_pos += (under - state.camera_pos) * k;
}

void update_camera(context &ctx, bool in_hud) {
  const f32 dt = delta_real(ctx);
  const f32 zoom = camera_zoom();
  f32 pan = 300.0f / zoom;
  if (key_held(ctx, key_left_shift))
    pan *= 2.0f;
  if (key_held(ctx, key_w) || key_held(ctx, key_up))
    state.camera_target.y -= pan * dt;
  if (key_held(ctx, key_s) || key_held(ctx, key_down))
    state.camera_target.y += pan * dt;
  if (key_held(ctx, key_a) || key_held(ctx, key_left))
    state.camera_target.x -= pan * dt;
  if (key_held(ctx, key_d) || key_held(ctx, key_right))
    state.camera_target.x += pan * dt;
  if (mouse_held(ctx, mouse_middle)) {
    state.camera_target -= mouse_delta(ctx) / zoom;
    state.camera_pos = state.camera_target;
  }

  const f32 wheel = in_hud ? 0.0f : mouse_wheel(ctx);
  if (wheel > 0.0f)
    zoom_by(ctx, 1);
  else if (wheel < 0.0f)
    zoom_by(ctx, -1);

  state.camera_target.x = clamp(state.camera_target.x, 0.0f, world_width);
  state.camera_target.y = clamp(state.camera_target.y, 0.0f, world_height);
  state.camera_pos = lerp(state.camera_pos, state.camera_target, clamp(dt * 12.0f, 0.0f, 1.0f));
  if (state.camera_entity != entt::null && world(ctx).valid(state.camera_entity)) {
    // On the pixel grid of the current zoom, or the whole picture shimmers
    // as it scrolls.
    const f32 z = camera_zoom();
    world(ctx).get<transform>(state.camera_entity).pos = {std::round(state.camera_pos.x * z) / z,
                                                           std::round(state.camera_pos.y * z) / z};
    world(ctx).get<camera_2d>(state.camera_entity).zoom = z;
  }
}

void handle_input(context &ctx) {
  if (state.restart_requested) {
    state.restart_requested = false;
    redeploy(ctx);
    return;
  }
  if (state.next_requested) {
    state.next_requested = false;
    next_level(ctx);
    return;
  }

  const bool in_hud = ui_mouse_over(ctx);
  update_camera(ctx, in_hud);

  if (key_pressed(ctx, key_f12))
    screenshot(ctx, "sandtable_screenshot.png");

  if (state.screen == phase::deploy) {
    if (key_pressed(ctx, key_escape))
      drop_held(ctx);
    if (key_pressed(ctx, key_tab))
      state.hide_panels = !state.hide_panels;
    if (in_hud)
      return;
    const vec2 at = scr2w(ctx, mouse_pos(ctx));
    if (mouse_pressed(ctx, mouse_left)) {
      if (state.held.active)
        place_held(ctx, at);
      else
        lift_board_chip(ctx, board_chip_at(at, side::player));
    }
    if (mouse_pressed(ctx, mouse_right)) {
      if (state.held.active)
        drop_held(ctx);
      else
        return_board_chip(ctx, board_chip_at(at, side::player));
    }
    if (key_pressed(ctx, key_enter))
      start_battle(ctx);
    return;
  }

  if (state.screen == phase::battle) {
    if (key_pressed(ctx, key_space))
      time_set_paused(ctx, !time_paused(ctx));
    if (key_pressed(ctx, key_1))
      set_speed(ctx, 0);
    if (key_pressed(ctx, key_2))
      set_speed(ctx, 1);
    if (key_pressed(ctx, key_3))
      set_speed(ctx, 2);
  }
}

// --- Scripted test run (--test [--level N]) ---

struct test_config {
  bool enabled = false;
  i32 level = 0;
  i32 frame = 0;
  bool shot_mid = false;
  i32 zoom_frame = 0;
  i32 wave_frame = 0;
  std::vector<arm> plan{arm::infantry, arm::archer, arm::spear, arm::cavalry, arm::artillery};
};
test_config test;

// Spends the budget round-robin on the arms in `plan`, biggest chip that
// fits first, and lays the chips out in two lines across the zone.
void auto_deploy(context &ctx, const std::vector<arm> &plan) {
  const level_def &lvl = current_level();
  bool bought = true;
  while (bought) {
    bought = false;
    for (arm a : plan) {
      i32 held = 0;
      for (i32 k = 0; k < arm_count; ++k)
        for (i32 t = 0; t < tier_count; ++t)
          held += state.reserve[k][t];
      if (held >= lvl.max_chips)
        break;
      for (i32 t = lvl.max_tier; t >= 0; --t) {
        if (chip_cost(a, t) <= gold_left() / 2 || (t == 0 && chip_cost(a, t) <= gold_left())) {
          bought |= shop_buy(ctx, a, t);
          break;
        }
      }
    }
  }
  std::vector<std::pair<arm, i32>> chips;
  for (i32 k = 0; k < arm_count; ++k)
    for (i32 t = 0; t < tier_count; ++t)
      for (i32 n = 0; n < state.reserve[k][t]; ++n)
        chips.push_back({static_cast<arm>(k), t});
  const i32 count = static_cast<i32>(chips.size());
  for (i32 i = 0; i < count; ++i) {
    const auto [a, t] = chips[static_cast<usize>(i)];
    const bool back = spec(a).range > 0.0f;
    // Massed in the middle half of the zone, which is how a battle is won.
    const f32 x0 = player_zone.pos.x + player_zone.size.x * 0.22f;
    const vec2 pos{x0 + player_zone.size.x * 0.56f * (static_cast<f32>(i) + 0.5f) / static_cast<f32>(count),
                   back ? 1100.0f : 930.0f};
    hold_from_reserve(ctx, a, t);
    if (!place_held(ctx, pos))
      drop_held(ctx);
  }
}

void test_harness(context &ctx) {
  if (!test.enabled)
    return;
  test.frame++;
  if (test.frame == 2) {
    load_level(ctx, test.level);
    state.unlocked = static_cast<i32>(levels().size()) - 1;
    auto_deploy(ctx, test.plan);
    std::printf("[test] level %d: %d chips, %d gold left\n", test.level + 1, chips_on_board(side::player),
                gold_left());
  }
  if (test.frame == 6)
    screenshot(ctx, "sandtable_test_deploy.png");
  if (test.frame == 8) {
    start_battle(ctx);
    set_speed(ctx, 2);
    std::printf("[test] battle: %zu figures, men %.0f vs %.0f\n", state.soldiers.size(), state.men_start[0],
                state.men_start[1]);
  }
  if (state.screen == phase::battle && !test.shot_mid && state.battle_time > 14.0f) {
    test.shot_mid = true;
    screenshot(ctx, "sandtable_test_battle.png");
    std::printf("[test] t=%.1f men %.0f vs %.0f, fps %.0f\n", state.battle_time, state.men_now[0], state.men_now[1],
                1.0f / std::max(0.0001f, delta_real(ctx)));
    time_set_paused(ctx, true);
    test.zoom_frame = test.frame;
  }
  // Then a close look at where the fighting is, paused, to check the figures:
  // a frame later, once the picture above has been drawn at the old zoom.
  if (test.zoom_frame > 0 && test.frame == test.zoom_frame + 2) {
    for (const soldier &s : state.soldiers) {
      if (s.alive && s.fighting && s.owner == side::player) {
        state.camera_target = s.pos;
        break;
      }
    }
    state.camera_pos = state.camera_target;
    state.zoom_step = 3; // x2: one sprite pixel is two screen pixels
  }
  if (test.zoom_frame > 0 && test.frame == test.zoom_frame + 40)
    screenshot(ctx, "sandtable_test_zoom.png");
  // A close look at a shockwave caught mid-run, the battle paused under it.
  if (test.zoom_frame > 0 && test.frame > test.zoom_frame + 45 && test.wave_frame == 0 &&
      !state.shockwaves.empty() && state.shockwaves.back().time > 0.12f) {
    state.camera_pos = state.camera_target = state.shockwaves.back().pos;
    state.zoom_step = 3;
    time_set_paused(ctx, true);
    test.wave_frame = test.frame;
  }
  if (test.wave_frame > 0 && test.frame == test.wave_frame + 3)
    screenshot(ctx, "sandtable_test_wave.png");
  if (test.wave_frame > 0 && test.frame == test.wave_frame + 5) {
    state.zoom_step = 0;
    time_set_paused(ctx, false);
  }
  // The screenshot is taken when this frame is drawn: zoom back out after it.
  if (test.zoom_frame > 0 && test.frame == test.zoom_frame + 42) {
    state.camera_target = {world_width * 0.5f, world_height * 0.5f - 30.0f};
    state.zoom_step = 0;
    time_set_paused(ctx, false);
  }
  if (state.screen == phase::result) {
    static i32 result_frames = 0;
    if (++result_frames == 5) {
      std::printf("[test] result: %s after %.1fs, men %.0f/%.0f vs %.0f/%.0f\n", state.won ? "WIN" : "LOSS",
                  state.battle_time, state.men_now[0], state.men_start[0], state.men_now[1], state.men_start[1]);
      screenshot(ctx, "sandtable_test_result.png");
    }
    if (result_frames == 8)
      quit(ctx);
  }
  if (test.frame > 60 * 120)
    quit(ctx);
}

void startup(context &ctx) {
  sim_init(ctx);
  sprites_init(ctx);
  render_init(ctx);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, startup, "sandtable_startup");
  ecs_register(ctx, phase_pre_update, handle_input, "sandtable_input");
  ecs_register(ctx, phase_update, sim_update, "sandtable_sim");
  ecs_register(ctx, phase_post_update, test_harness, "sandtable_test_harness");
  ecs_register(ctx, phase_render, render_world, "sandtable_world_render");
  ecs_register(ctx, phase_post_render, render_ui, "sandtable_ui_render");
}

} // namespace

mod_desc module(bool test_mode, i32 test_level, const char *test_plan) {
  test = {};
  test.enabled = test_mode;
  test.level = test_level;
  // Letters in buying order: I infantry, S spear, A archer, C cavalry, R artillery, E elephant, B boat.
  if (test_plan && *test_plan) {
    test.plan.clear();
    for (const char *c = test_plan; *c; ++c) {
      const char *letters = "ISACREB";
      for (i32 k = 0; k < arm_count; ++k)
        if (*c == letters[k])
          test.plan.push_back(static_cast<arm>(k));
    }
  }
  return {.name = "sandtable", .setup = setup};
}

} // namespace sandtable
