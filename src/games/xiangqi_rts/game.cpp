#include "game.h"
#include "audio.h"
#include "render.h"
#include "sim.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace xiangqi {

namespace {

std::vector<entt::entity> selected_units(njin_ctx &ctx) {
  std::vector<entt::entity> selected;
  for (const auto [e, u] : world(ctx).view<const unit_component>().each()) {
    if (u.side == faction::red && u.selected)
      selected.push_back(e);
  }
  return selected;
}

// Ends a box or click selection at `end` (screen pixels).
void finish_selection(njin_ctx &ctx, vec2 end) {
  state.is_box_selecting = false;
  entt::registry &reg = world(ctx);
  const bool add = key_held(ctx, key_left_shift);

  if (distance(state.box_start_screen, end) > 12.0f) {
    // Marquee Box Select
    const vec2 w1 = scr2w(ctx, state.box_start_screen);
    const vec2 w2 = scr2w(ctx, end);
    const rect box{{std::min(w1.x, w2.x), std::min(w1.y, w2.y)},
                   {std::abs(w2.x - w1.x), std::abs(w2.y - w1.y)}};
    bool any_selected = false;
    for (const auto [e, tr, u] : reg.view<const transform, unit_component>().each()) {
      if (u.side != faction::red)
        continue;
      if (point_in_rect(tr.pos, box)) {
        u.selected = true;
        any_selected = true;
      } else if (!add) {
        u.selected = false;
      }
    }
    if (any_selected)
      audio_play(ctx, sfx_type::click, 0.75f);
    return;
  }

  // Single Click Select
  const vec2 wpos = scr2w(ctx, end);
  entt::entity clicked = entt::null;
  f32 closest = 9999.0f;
  for (const auto [e, tr, u] : reg.view<const transform, const unit_component>().each()) {
    if (u.side == faction::red) {
      const f32 d = distance(wpos, tr.pos);
      if (d <= u.radius + 6.0f && d < closest) {
        closest = d;
        clicked = e;
      }
    }
  }
  if (!add) {
    for (const auto [e, u] : reg.view<unit_component>().each()) {
      if (u.side == faction::red)
        u.selected = false;
    }
  }
  if (clicked != entt::null) {
    reg.get<unit_component>(clicked).selected = true;
    audio_play(ctx, sfx_type::click, 0.8f);
  }
}

void handle_input(njin_ctx &ctx) {
  if (state.restart_requested) {
    sim_reset(ctx);
    return;
  }
  // The result popup is a njin UI popup: it takes Enter and the mouse itself.
  if (state.screen != game_screen::playing)
    return;

  const f32 dt = delta(ctx);
  const vec2 mpos = mouse_pos(ctx);
  // Over the HUD (njin UI panels) or the minimap: clicks are not for the world.
  const bool on_minimap = point_in_rect(mpos, minimap_area);
  const bool in_hud = ui_mouse_over(ctx) || on_minimap;

  // --- 1. CAMERA CONTROLS ---
  f32 cam_speed = 700.0f;
  if (key_held(ctx, key_left_shift))
    cam_speed *= 2.0f;

  if (key_held(ctx, key_w) || key_held(ctx, key_up))
    state.camera_target.y -= cam_speed * dt;
  if (key_held(ctx, key_s) || key_held(ctx, key_down))
    state.camera_target.y += cam_speed * dt;
  if (key_held(ctx, key_a) || key_held(ctx, key_left))
    state.camera_target.x -= cam_speed * dt;
  if (key_held(ctx, key_d) || key_held(ctx, key_right))
    state.camera_target.x += cam_speed * dt;

  // Zoom with mouse wheel
  const f32 wheel = in_hud ? 0.0f : mouse_wheel(ctx);
  if (wheel != 0.0f) {
    state.camera_zoom_target = clamp(state.camera_zoom_target + wheel * 0.08f, 0.65f, 1.35f);
  }

  // Snap to General on Space
  if (key_pressed(ctx, key_space)) {
    if (state.red_general != entt::null && world(ctx).valid(state.red_general)) {
      state.camera_target = world(ctx).get<transform>(state.red_general).pos;
    }
  }

  // Minimap click or drag pans the camera
  if (on_minimap && !state.is_box_selecting && mouse_held(ctx, mouse_left)) {
    const f32 rx = clamp((mpos.x - minimap_area.pos.x) / minimap_area.size.x, 0.0f, 1.0f);
    const f32 ry = clamp((mpos.y - minimap_area.pos.y) / minimap_area.size.y, 0.0f, 1.0f);
    state.camera_target = {rx * world_width, ry * world_height};
  }

  // Camera bounds clamping
  state.camera_target.x = clamp(state.camera_target.x, 320.0f, world_width - 320.0f);
  state.camera_target.y = clamp(state.camera_target.y, 240.0f, world_height - 240.0f);

  state.camera_pos = lerp(state.camera_pos, state.camera_target, dt * 9.0f);
  state.camera_zoom = lerp(state.camera_zoom, state.camera_zoom_target, dt * 10.0f);

  if (state.camera_entity != entt::null && world(ctx).valid(state.camera_entity)) {
    world(ctx).get<transform>(state.camera_entity).pos = state.camera_pos;
    world(ctx).get<camera_2d>(state.camera_entity).zoom = state.camera_zoom;
  }

  // --- 2. HOTKEYS (the dock buttons are njin UI buttons, see render_ui) ---
  if (key_pressed(ctx, key_1))
    recruit_unit(ctx, piece_type::pawn);
  if (key_pressed(ctx, key_2))
    recruit_unit(ctx, piece_type::horse);
  if (key_pressed(ctx, key_3))
    recruit_unit(ctx, piece_type::cannon);
  if (key_pressed(ctx, key_4))
    recruit_unit(ctx, piece_type::chariot);
  if (key_pressed(ctx, key_5))
    recruit_unit(ctx, piece_type::elephant);
  if (key_pressed(ctx, key_6))
    recruit_unit(ctx, piece_type::advisor);

  // Hotkey R for Rally
  if (key_pressed(ctx, key_r)) {
    trigger_rally(ctx);
  }

  // Hotkey H to halt selected units (S pans the camera)
  if (key_pressed(ctx, key_h)) {
    const std::vector<entt::entity> selected = selected_units(ctx);
    if (!selected.empty()) {
      stop_units(ctx, selected);
      audio_play(ctx, sfx_type::click, 0.7f);
    }
  }

  // Hotkey Tab to select all military units
  if (key_pressed(ctx, key_tab)) {
    for (const auto [e, u] : world(ctx).view<unit_component>().each()) {
      if (u.side == faction::red) {
        u.selected = true;
      }
    }
    audio_play(ctx, sfx_type::click, 0.8f);
  }

  // --- 3. SELECTION & COMMANDS IN WORLD ---
  // Left button: the UI already took it over its panels, so a press here is on the world.
  if (!on_minimap && mouse_pressed(ctx, mouse_left)) {
    state.is_box_selecting = true;
    state.box_start_screen = mpos;
  }
  if (state.is_box_selecting) {
    state.box_end_screen = mpos;
    // Released, or dragged onto a panel (which hides the button from the game).
    if (!mouse_held(ctx, mouse_left))
      finish_selection(ctx, mpos);
  }

  if (!in_hud) {
    // Mouse Right Pressed: Issue Move or Attack Orders
    if (mouse_pressed(ctx, mouse_right)) {
      const vec2 wpos = scr2w(ctx, mpos);
      entt::registry &reg = world(ctx);

      const std::vector<entt::entity> selected = selected_units(ctx);

      if (!selected.empty()) {
        // Check if clicking on enemy unit
        entt::entity target_foe = entt::null;
        f32 closest_foe = 9999.0f;

        for (const auto [fe, ftr, fu] : reg.view<const transform, const unit_component>().each()) {
          if (fu.side == faction::black && fu.hp > 0.0f) {
            const f32 d = distance(wpos, ftr.pos);
            if (d <= fu.radius + 8.0f && d < closest_foe) {
              closest_foe = d;
              target_foe = fe;
            }
          }
        }

        if (target_foe != entt::null) {
          // Attack Target Order
          issue_attack_order(ctx, selected, target_foe);
        } else {
          // Move Order
          issue_move_order(ctx, selected, wpos);
        }
      }
    }
  }

  // F12 Screenshot
  if (key_pressed(ctx, key_f12)) {
    screenshot(ctx, "xiangqi_screenshot.png");
  }
}


static bool test_enabled = false;
static i32 test_frame = 0;

void test_harness(njin_ctx &ctx) {
  if (!test_enabled)
    return;
  test_frame++;

  if (test_frame == 3) {
    // Select all player units and issue move order towards river
    std::vector<entt::entity> units;
    for (const auto [e, u] : world(ctx).view<unit_component>().each()) {
      if (u.side == faction::red) {
        u.selected = true;
        units.push_back(e);
      }
    }
    issue_move_order(ctx, units, {1200.0f, 1000.0f});
  }

  if (test_frame == 25) {
    // Recruit a pawn
    recruit_unit(ctx, piece_type::pawn);
  }

  if (test_frame == 50) {
    // Trigger rally
    trigger_rally(ctx);
  }

  if (test_frame >= 75) {
    screenshot(ctx, "xiangqi_test_screenshot.png");
    njin_quit(ctx);
  }
}

void startup(njin_ctx &ctx) {
  sim_init(ctx);
  render_init(ctx);
}

void setup(njin_ctx &ctx) {
  ecs_register(ctx, phase_startup, startup, "xiangqi_startup");
  ecs_register(ctx, phase_pre_update, handle_input, "xiangqi_input");
  ecs_register(ctx, phase_update, sim_update, "xiangqi_sim");
  ecs_register(ctx, phase_post_update, test_harness, "xiangqi_test_harness");
  ecs_register(ctx, phase_render, render_world, "xiangqi_world_render");
  ecs_register(ctx, phase_post_render, render_ui, "xiangqi_ui_render");
}

} // namespace

mod_desc module(bool test_mode) {
  test_enabled = test_mode;
  test_frame = 0;
  return {.name = "xiangqi_rts", .setup = setup};
}

} // namespace xiangqi

