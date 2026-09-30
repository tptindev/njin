#include "view.h"

#include <algorithm>
#include <cmath>

namespace sandtable {

namespace {

constexpr f32 min_distance = 5.0f;
constexpr f32 max_distance = 100.0f;

// The slant: steep from afar, flatter up close, so a close look is along the
// ground and the whole table is a map.
f32 pitch_at(f32 distance) {
  const f32 k = clamp((distance - min_distance) / (max_distance - min_distance), 0.0f, 1.0f);
  return (32.0f + 30.0f * k) * pi / 180.0f;
}

// Easing toward the goal, the same at any frame rate.
f32 ease(f32 dt, f32 rate) { return 1.0f - std::exp(-rate * dt); }

} // namespace

camera3d table_camera() {
  const f32 yaw = state.cam_yaw * pi / 180.0f;
  const f32 pitch = pitch_at(state.cam_distance);
  const vec3 target = to3d(state.cam_target, state.cam_lift);
  const vec3 back{std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
  return {.position = target + back * state.cam_distance,
          .target = target,
          .fovy = 40.0f,
          .near_plane = 0.5f,
          .far_plane = 400.0f};
}

void update_view(context &ctx, bool in_hud) {
  const f32 dt = delta_real(ctx);
  const f32 yaw = state.cam_yaw_goal * pi / 180.0f;
  // Along the table, as the camera sees it: forward is away from the camera.
  const vec2 forward{-std::sin(yaw), -std::cos(yaw)};
  const vec2 right{std::cos(yaw), -std::sin(yaw)};
  f32 pan = state.cam_distance_goal * 0.9f / unit3d; // world units a second
  if (key_held(ctx, key_left_shift))
    pan *= 2.0f;
  vec2 move{};
  if (key_held(ctx, key_w) || key_held(ctx, key_up))
    move += forward;
  if (key_held(ctx, key_s) || key_held(ctx, key_down))
    move -= forward;
  if (key_held(ctx, key_d) || key_held(ctx, key_right))
    move += right;
  if (key_held(ctx, key_a) || key_held(ctx, key_left))
    move -= right;
  state.cam_target_goal += move * pan * dt;
  if (key_held(ctx, key_q))
    state.cam_yaw_goal -= 90.0f * dt;
  if (key_held(ctx, key_e))
    state.cam_yaw_goal += 90.0f * dt;
  // Dragging with the middle button: the table follows the mouse.
  if (mouse_held(ctx, mouse_middle)) {
    const vec2 d = mouse_delta(ctx);
    const f32 per_pixel = state.cam_distance / screen_size(ctx).y * 1.2f / unit3d;
    state.cam_target_goal -= (right * d.x - forward * d.y) * per_pixel;
    state.cam_target = state.cam_target_goal;
  }
  const f32 wheel = in_hud ? 0.0f : mouse_wheel(ctx);
  if (wheel != 0.0f)
    state.cam_distance_goal = clamp(state.cam_distance_goal * std::pow(0.85f, wheel), min_distance, max_distance);

  state.cam_target_goal.x = clamp(state.cam_target_goal.x, 0.0f, world_width);
  state.cam_target_goal.y = clamp(state.cam_target_goal.y, 0.0f, world_height);
  state.cam_target = lerp(state.cam_target, state.cam_target_goal, ease(dt, 12.0f));
  state.cam_yaw += (state.cam_yaw_goal - state.cam_yaw) * ease(dt, 12.0f);
  state.cam_distance += (state.cam_distance_goal - state.cam_distance) * ease(dt, 10.0f);
  state.cam_lift += (state.cam_lift_goal - state.cam_lift) * ease(dt, 8.0f);
}

void view_focus(vec2 at, f32 distance, bool snap) {
  state.cam_target_goal = at;
  state.cam_distance_goal = clamp(distance, min_distance, max_distance);
  if (snap) {
    state.cam_target = state.cam_target_goal;
    state.cam_distance = state.cam_distance_goal;
    state.cam_yaw = state.cam_yaw_goal;
  }
}

void view_reset() {
  state.cam_yaw_goal = 0.0f;
  view_focus({world_width * 0.5f, world_height * 0.5f}, 80.0f, true);
}

bool screen_to_table(context &ctx, vec2 screen, vec2 *at) {
  const ray3d ray = camera3d_ray(ctx, table_camera(), screen);
  const ray3d_hit hit = ray3d_plane(ray, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
  if (!hit.hit)
    return false;
  const vec2 p{hit.point.x / unit3d, hit.point.z / unit3d};
  if (at)
    *at = p;
  return p.x >= 0.0f && p.y >= 0.0f && p.x < world_width && p.y < world_height;
}

bool mouse_on_table(context &ctx, vec2 *at) { return screen_to_table(ctx, mouse_pos(ctx), at); }

vec2 table_to_screen(context &ctx, vec2 p, f32 lift, bool *visible) {
  return camera3d_to_screen(ctx, table_camera(), to3d(p, lift), visible);
}

} // namespace sandtable
