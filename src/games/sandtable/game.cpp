#include "game.h"
#include "audio.h"
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

// --- Setting up ---
//
// Right click on the table sends a group of men there; a click on a group's
// flag opens its orders in a circle menu. Orders that need a place (the way
// to face, a new place for the flag) then wait for the next click.

void close_menu() { state.menu = {}; }

void cancel_command() {
  state.cmd = command::none;
  state.selected = -1;
}

// A group of the size last chosen, sent to `at`; if it cannot go there, why.
void send_men(context &ctx, vec2 at) {
  if (const char *err = troop_error(at)) {
    ui_toast_clear(ctx);
    ui_toast(ctx, err, {.seconds = 2.0f});
    return;
  }
  add_troop(ctx, state.new_tier, at);
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
    send_men(ctx, at);
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

// --- Scripted test run (--test) ---

struct test_config {
  bool enabled = false;
  i32 frame = 0;
  bool shot_mid = false;
  i32 zoom_frame = 0;
  f32 battle_real = 0.0f; // real seconds and frames of fight up to the mid shot
  i32 battle_frames = 0;
};
test_config test;

// A group to every turf nobody holds: a Toán (8) to those on
// the player's side of the river, an Đám (12) to those across it, each
// facing north.
void auto_deploy(context &ctx) {
  for (const turf &t : state.turfs) {
    if (t.held_by != nobody)
      continue; // the enemy's, or the home turf, held from the start
    const i32 tier = t.pos.y > 592.0f ? 2 : 3;
    if (!add_troop(ctx, tier, t.pos))
      std::printf("[test] no group sent to %s: %s\n", t.name, troop_error(t.pos));
  }
}

// The turfs, who holds them and how far each claim has gone.
void print_turfs(const char *when) {
  std::printf("[test] turfs %s:", when);
  for (const turf &t : state.turfs)
    std::printf(" %s=%s(%.2f)", t.name,
                t.held_by == static_cast<i32>(side::player)  ? "ta"
                : t.held_by == static_cast<i32>(side::enemy) ? "dich"
                                                             : "-",
                t.claim);
  std::printf("\n");
}

void test_harness(context &ctx) {
  if (!test.enabled)
    return;
  test.frame++;
  if (test.frame == 2) {
    load_level(ctx);
    auto_deploy(ctx);
    std::printf("[test] %d groups sent\n", troop_count(side::player));
    print_turfs("at the start");
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
  // The meadows and every pose of the men close up, at noon.
  if (test.frame == 13) {
    close_menu();
    state.hour = 12.0f;
    show_pose_row(true, {1400.0f, 1000.0f});
    view_focus({1400.0f, 1000.0f}, 9.0f, true);
  }
  if (test.frame == 16)
    screenshot(ctx, "sandtable_test_poses.png");
  if (test.frame == 19)
    view_focus({world_width * 0.5f, 1000.0f}, 10.0f, true);
  if (test.frame == 21)
    screenshot(ctx, "sandtable_test_meadow.png");
  if (test.frame == 22) {
    show_pose_row(false);
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
    std::printf("[test] posts: %d/%d groups at their flags, furthest man %.0f past the guard\n", at_post, blocks,
                std::max(0.0f, strayed));
    print_turfs("mid-fight");
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
      print_turfs("at the end");
      // Blocks still standing, and how far each is from the nearest of the other side.
      for (const group &g : state.groups) {
        if (g.alive == 0)
          continue;
        f32 nearest = 1e9f;
        for (const group &o : state.groups)
          if (o.owner != g.owner && o.alive > 0)
            nearest = std::min(nearest, distance(g.centroid, o.centroid));
        std::printf("[test]   %s %s: %d/%d men, %.0f from the nearest foe\n", g.owner == side::player ? "ta" : "dich",
                    tiers[static_cast<usize>(g.tier)].name, g.alive, g.figures, nearest);
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

mod_desc module(bool test_mode) {
  test = {};
  test.enabled = test_mode;
  return {.name = "sandtable", .setup = setup};
}

} // namespace sandtable
