#include "game.h"
#include "audio.h"
#include "sprites.h"
#include "levels.h"
#include "render.h"
#include "sim.h"
#include "view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sandtable {

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

// --- Setting up: circle menus ---
//
// Right click on the table opens the arms a troop can be raised as there; a
// click on a troop opens its orders. Orders that need a place (post, way,
// move) then wait for the next click on the table.

void close_menu() { state.menu = {}; }

void cancel_command() {
  state.cmd = command::none;
  state.selected = -1;
}

// The arms a troop can be raised as at `at`; none, and the reason shows.
void open_arms_menu(context &ctx, vec2 at) {
  radial_menu m{};
  m.kind = menu_kind::arms;
  m.at = at;
  const char *why = nullptr;
  for (i32 k = 0; k < arm_count; ++k) {
    const char *err = troop_error(static_cast<arm>(k), at);
    if (err == nullptr) {
      m.items.push_back(k);
      m.enabled.push_back(true);
    } else if (why == nullptr) {
      why = err;
    }
  }
  if (m.items.empty()) {
    ui_toast_clear(ctx);
    ui_toast(ctx, why, {.seconds = 2.0f});
    return;
  }
  state.menu = std::move(m);
  audio_play(ctx, sfx_type::click, 0.6f);
}

void open_orders_menu(context &ctx, i32 index) {
  const troop &c = state.board[static_cast<usize>(index)];
  radial_menu m{};
  m.kind = menu_kind::orders;
  m.at = c.pos;
  m.troop = index;
  for (i32 k = 0; k < order_count; ++k) {
    bool on = true;
    switch (static_cast<order>(k)) {
    case order::grow:
      on = c.tier + 1 < tier_count;
      break;
    case order::shrink:
      on = c.tier > 0;
      break;
    default:
      break;
    }
    m.items.push_back(k);
    m.enabled.push_back(on);
  }
  state.menu = std::move(m);
  audio_play(ctx, sfx_type::click, 0.6f);
}

void pick_from_menu(context &ctx, i32 item) {
  const radial_menu m = state.menu;
  close_menu();
  if (item < 0 || !m.enabled[static_cast<usize>(item)])
    return;
  const i32 k = m.items[static_cast<usize>(item)];
  if (m.kind == menu_kind::arms) {
    add_troop(ctx, static_cast<arm>(k), state.new_tier, m.at);
    return;
  }
  const i32 index = m.troop;
  switch (static_cast<order>(k)) {
  case order::face:
    state.cmd = command::face;
    state.selected = index;
    break;
  case order::move:
    state.cmd = command::move;
    state.selected = index;
    break;
  case order::grow:
    set_troop_tier(ctx, index, state.board[static_cast<usize>(index)].tier + 1);
    break;
  case order::shrink:
    set_troop_tier(ctx, index, state.board[static_cast<usize>(index)].tier - 1);
    break;
  default:
    remove_troop(ctx, index);
    break;
  }
}

// A click on the table that finishes an order waiting for a place.
void finish_command(context &ctx, vec2 at) {
  const i32 index = state.selected;
  if (index < 0 || index >= static_cast<i32>(state.board.size())) {
    cancel_command();
    return;
  }
  troop &c = state.board[static_cast<usize>(index)];
  switch (state.cmd) {
  case command::face:
    if (mouse_pressed(ctx, mouse_left)) {
      const vec2 way = at - c.pos;
      // A click right on the flag keeps the way it faces.
      if (distance(mouse_pos(ctx), table_to_screen(ctx, c.pos)) >= 6.0f && length(way) > 0.001f) {
        c.face = normalize(way);
        audio_play(ctx, sfx_type::drum, 0.5f);
      }
      cancel_command();
    }
    break;
  case command::move:
    if (mouse_pressed(ctx, mouse_left) && move_troop(ctx, index, at))
      cancel_command();
    break;
  default:
    break;
  }
}

void deploy_mouse(context &ctx, bool in_hud) {
  vec2 at{};
  const bool on_table = mouse_on_table(ctx, &at);
  if (state.menu.kind != menu_kind::none) {
    if (mouse_pressed(ctx, mouse_left))
      pick_from_menu(ctx, radial_item_at(ctx));
    else if (mouse_pressed(ctx, mouse_right))
      close_menu();
    return;
  }
  if (state.cmd != command::none) {
    if (mouse_pressed(ctx, mouse_right))
      cancel_command();
    else if (!in_hud && on_table)
      finish_command(ctx, at);
    return;
  }
  if (in_hud)
    return;
  const i32 own = flag_at(ctx, side::player);
  if ((mouse_pressed(ctx, mouse_left) || mouse_pressed(ctx, mouse_right)) && own >= 0)
    open_orders_menu(ctx, own);
  else if (mouse_pressed(ctx, mouse_right) && on_table)
    open_arms_menu(ctx, at);
}

void handle_input(context &ctx) {
  fit_view(ctx);
  if (state.restart_requested) {
    state.restart_requested = false;
    redeploy(ctx);
    return;
  }

  const bool in_hud = ui_mouse_over(ctx);
  update_view(ctx, in_hud);

  if (key_pressed(ctx, key_f12))
    screenshot(ctx, "sandtable_screenshot.png");

  if (state.screen == phase::deploy) {
    if (key_pressed(ctx, key_escape)) {
      close_menu();
      cancel_command();
    }
    if (key_pressed(ctx, key_enter))
      start_battle(ctx);
    deploy_mouse(ctx, in_hud);
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

// --- Scripted test run (--test [--plan ISACREB]) ---

struct test_config {
  bool enabled = false;
  i32 frame = 0;
  bool shot_mid = false;
  i32 zoom_frame = 0;
  f32 battle_real = 0.0f; // real seconds and frames of battle up to the mid shot
  i32 battle_frames = 0;
  std::vector<arm> plan{arm::infantry, arm::archer, arm::spear, arm::cavalry, arm::artillery};
};
test_config test;

// Two troops of every arm in `plan`, a regiment each, their flags in two
// lines across the table just short of the river, melee in front, ranged
// behind, facing the enemy. They march there from home.
void auto_deploy(context &ctx, const std::vector<arm> &plan) {
  std::vector<arm> lines[2]; // front, back
  for (arm a : plan)
    lines[spec(a).range > 0.0f ? 1 : 0].insert(lines[spec(a).range > 0.0f ? 1 : 0].end(), 2, a);
  for (i32 row = 0; row < 2; ++row) {
    const i32 count = static_cast<i32>(lines[row].size());
    for (i32 i = 0; i < count; ++i) {
      const arm a = lines[row][static_cast<usize>(i)];
      vec2 pos{world_width * (0.1f + 0.8f * (static_cast<f32>(i) + 0.5f) / static_cast<f32>(count)),
               row == 0 ? 740.0f : 800.0f};
      // Off a hill or out of the river, a little toward home.
      for (i32 tries = 0; tries < 4 && troop_error(a, pos) != nullptr; ++tries)
        pos.y += 24.0f;
      if (const char *err = troop_error(a, pos))
        std::printf("[test] %s not raised: %s\n", spec(a).tag, err);
      add_troop(ctx, a, 4, pos);
    }
  }
}

void test_harness(context &ctx) {
  if (!test.enabled)
    return;
  test.frame++;
  if (test.frame == 2) {
    load_level(ctx);
    auto_deploy(ctx, test.plan);
    std::printf("[test] %d troops on the table\n", troop_count(side::player));
  }
  // The table at noon, to see the ground's own colours, then back to the
  // level's hour.
  if (test.frame == 3)
    state.hour = 12.0f;
  // The shot is taken as the frame is drawn, after this: the hour goes back
  // a frame later.
  if (test.frame == 4)
    screenshot(ctx, "sandtable_test_noon.png");
  if (test.frame == 5)
    state.hour = current_level().hour;
  if (test.frame == 6)
    screenshot(ctx, "sandtable_test_deploy.png");
  // A close look at the orders: the posts and the ways they face.
  if (test.frame == 7) {
    open_orders_menu(ctx, 0);
    view_focus(state.board.front().pos + vec2{120.0f, 60.0f}, 12.0f, true);
  }
  if (test.frame == 12)
    screenshot(ctx, "sandtable_test_orders.png");
  // And the arms that can be set down on open ground at home.
  if (test.frame == 13) {
    close_menu();
    // The first open ground, going up the table from the bottom left.
    vec2 spot{};
    for (f32 y = 1150.0f; y > 100.0f && spot.x == 0.0f; y -= 50.0f)
      for (f32 x = 100.0f; x < world_width - 100.0f && spot.x == 0.0f; x += 50.0f)
        if (troop_error(arm::infantry, {x, y}) == nullptr)
          spot = {x, y};
    open_arms_menu(ctx, spot);
    view_focus(spot, 12.0f, true);
  }
  if (test.frame == 18)
    screenshot(ctx, "sandtable_test_arms.png");
  // The meadows close up, at noon.
  if (test.frame == 19) {
    close_menu();
    state.hour = 12.0f;
    view_focus({world_width * 0.5f, 1000.0f}, 10.0f, true);
  }
  if (test.frame == 21)
    screenshot(ctx, "sandtable_test_meadow.png");
  if (test.frame == 22) {
    state.hour = current_level().hour;
    view_reset();
  }
  if (test.frame == 23) {
    start_battle(ctx);
    set_speed(ctx, 2);
    std::printf("[test] battle: %zu figures, men %.0f vs %.0f\n", state.soldiers.size(), state.men_start[0],
                state.men_start[1]);
    // First a while paused: the frames then cost only the drawing.
    time_set_paused(ctx, true);
  }
  if (test.frame > 24 && test.frame <= 84) {
    test.battle_real += delta_real(ctx);
    ++test.battle_frames;
  }
  if (test.frame == 85) {
    std::printf("[test] drawing only (paused, every man on the table): %.1f ms a frame\n",
                1000.0f * test.battle_real / static_cast<f32>(std::max(1, test.battle_frames)));
    test.battle_real = 0.0f;
    test.battle_frames = 0;
    time_set_paused(ctx, false);
  }
  if (state.screen == phase::battle && !test.shot_mid && test.frame > 85) {
    test.battle_real += delta_real(ctx);
    ++test.battle_frames;
  }
  if (state.screen == phase::battle && !test.shot_mid && state.battle_time > 14.0f) {
    test.shot_mid = true;
    std::printf("[test] battle at x4 (sim and drawing): %.1f ms a frame, %.0f fps\n",
                1000.0f * test.battle_real / static_cast<f32>(std::max(1, test.battle_frames)),
                static_cast<f32>(test.battle_frames) / std::max(0.001f, test.battle_real));
    screenshot(ctx, "sandtable_test_battle.png");
    std::printf("[test] t=%.1f men %.0f vs %.0f, fps %.0f\n", state.battle_time, state.men_now[0], state.men_now[1],
                1.0f / std::max(0.0001f, delta_real(ctx)));
    // The player's blocks hold their posts: how many anchors are there, and
    // how far past its guard the furthest man has gone (0 when none has).
    i32 at_post = 0, blocks = 0;
    f32 strayed = 0.0f;
    for (const group &g : state.groups) {
      if (!g.garrison || g.alive == 0)
        continue;
      ++blocks;
      at_post += distance(g.anchor, g.post) < 4.0f ? 1 : 0;
    }
    for (const soldier &s : state.soldiers) {
      const group &g = state.groups[static_cast<usize>(s.group)];
      if (s.alive && g.garrison)
        strayed = std::max(strayed, distance(s.pos, g.anchor) - (150.0f + g.span));
    }
    std::printf("[test] posts: %d/%d blocks at post, furthest man %.0f past the guard\n", at_post, blocks,
                std::max(0.0f, strayed));
    time_set_paused(ctx, true);
    test.zoom_frame = test.frame;
  }
  // Then a close look at where the fighting is, paused, to check the figures:
  // a frame later, once the picture above has been drawn at the old zoom.
  if (test.zoom_frame > 0 && test.frame == test.zoom_frame + 2) {
    for (const soldier &s : state.soldiers) {
      if (s.alive && s.fighting && s.owner == side::player) {
        view_focus(s.pos, 9.0f, true);
        break;
      }
    }
  }
  if (test.zoom_frame > 0 && test.frame == test.zoom_frame + 40)
    screenshot(ctx, "sandtable_test_zoom.png");
  // The screenshot is taken when this frame is drawn: zoom back out after it.
  if (test.zoom_frame > 0 && test.frame == test.zoom_frame + 42) {
    view_reset();
    time_set_paused(ctx, false);
  }
  if (state.screen == phase::result) {
    static i32 result_frames = 0;
    if (++result_frames == 5) {
      std::printf("[test] result: %s after %.1fs, men %.0f/%.0f vs %.0f/%.0f\n", state.won ? "WIN" : "LOSS",
                  state.battle_time, state.men_now[0], state.men_start[0], state.men_now[1], state.men_start[1]);
      screenshot(ctx, "sandtable_test_result.png");
      // Blocks still standing, and how far each is from the nearest of the other side.
      for (const group &g : state.groups) {
        if (g.alive == 0)
          continue;
        f32 nearest = 1e9f;
        for (const group &o : state.groups)
          if (o.owner != g.owner && o.alive > 0)
            nearest = std::min(nearest, distance(g.centroid, o.centroid));
        std::printf("[test]   %s %s: %d/%d figures, %.0f from the nearest foe\n",
                    g.owner == side::player ? "ta" : "dich", spec(g.type).tag, g.alive, g.figures, nearest);
      }
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

mod_desc module(bool test_mode, const char *test_plan) {
  test = {};
  test.enabled = test_mode;
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
