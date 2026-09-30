#include "view.h"
#include "levels.h"

#include <algorithm>
#include <cmath>

namespace sandtable {

namespace {

constexpr f32 min_distance = 5.0f;
constexpr f32 max_distance = 70.0f;

u32 hash2(u32 a, u32 b) {
  u32 h = (a * 0x9E3779B1u) ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  return h;
}

f32 unit_hash(i32 x, i32 y) { return static_cast<f32>(hash2(static_cast<u32>(x), static_cast<u32>(y)) & 0xFFFFu) / 65535.0f; }

// How many of the eight tiles round (x, y) are of kind `t`.
i32 same_round(i32 x, i32 y, terrain t) {
  i32 n = 0;
  for (i32 dy = -1; dy <= 1; ++dy)
    for (i32 dx = -1; dx <= 1; ++dx)
      if ((dx != 0 || dy != 0) && terrain_cell(x + dx, y + dy) == t)
        ++n;
  return n;
}

// The slant: steep from afar, flatter up close, so a close look is along the
// ground and the whole table is a map.
f32 pitch_at(f32 distance) {
  const f32 k = clamp((distance - min_distance) / (max_distance - min_distance), 0.0f, 1.0f);
  return (32.0f + 30.0f * k) * pi / 180.0f;
}

// Easing toward the goal, the same at any frame rate.
f32 ease(f32 dt, f32 rate) { return 1.0f - std::exp(-rate * dt); }

// The height each tile pulls the ground toward.
f32 tile_target(i32 x, i32 y) {
  x = clamp(x, 0, tiles_x - 1);
  y = clamp(y, 0, tiles_y - 1);
  const terrain t = terrain_cell(x, y);
  const f32 r = unit_hash(x, y);
  switch (t) {
  case terrain::hill:
    return 0.45f + 0.25f * r + 0.05f * static_cast<f32>(same_round(x, y, terrain::hill));
  case terrain::mountain:
    return 1.0f + 0.6f * r + 0.2f * static_cast<f32>(same_round(x, y, terrain::mountain));
  case terrain::river:
    return -0.4f;
  case terrain::stream:
    return -0.22f;
  case terrain::ford:
    return -0.13f;
  default:
    return 0.0f;
  }
}

height_field field{};
u32 field_version = ~0u;

void build_field() {
  const i32 n = height_samples;
  field.width = tiles_x * n + 1;
  field.depth = tiles_y * n + 1;
  const usize count = static_cast<usize>(field.width * field.depth);
  std::vector<f32> h(count), tmp(count);
  // Between the tiles' own heights, taken at their middles.
  for (i32 j = 0; j < field.depth; ++j)
    for (i32 i = 0; i < field.width; ++i) {
      const f32 u = static_cast<f32>(i) / static_cast<f32>(n) - 0.5f;
      const f32 v = static_cast<f32>(j) / static_cast<f32>(n) - 0.5f;
      const i32 x0 = static_cast<i32>(std::floor(u)), y0 = static_cast<i32>(std::floor(v));
      const f32 fu = u - static_cast<f32>(x0), fv = v - static_cast<f32>(y0);
      const f32 a = lerp(tile_target(x0, y0), tile_target(x0 + 1, y0), fu);
      const f32 b = lerp(tile_target(x0, y0 + 1), tile_target(x0 + 1, y0 + 1), fu);
      h[static_cast<usize>(j * field.width + i)] = lerp(a, b, fv);
    }
  // Rounded off: a few passes of a small blur.
  for (i32 pass = 0; pass < 3; ++pass) {
    for (i32 j = 0; j < field.depth; ++j)
      for (i32 i = 0; i < field.width; ++i) {
        f32 sum = 0.0f;
        i32 k = 0;
        for (i32 dj = -1; dj <= 1; ++dj)
          for (i32 di = -1; di <= 1; ++di) {
            const i32 x = clamp(i + di, 0, field.width - 1), y = clamp(j + dj, 0, field.depth - 1);
            sum += h[static_cast<usize>(y * field.width + x)];
            ++k;
          }
        tmp[static_cast<usize>(j * field.width + i)] = sum / static_cast<f32>(k);
      }
    h.swap(tmp);
  }
  // Rough where it is high, barely rippled where it is flat.
  const noise_desc rough{.seed = current_level().seed * 31u + 7u, .frequency = 0.35f, .octaves = 3};
  for (i32 j = 0; j < field.depth; ++j)
    for (i32 i = 0; i < field.width; ++i) {
      f32 &v = h[static_cast<usize>(j * field.width + i)];
      const f32 d = noise_2d(rough, static_cast<f32>(i), static_cast<f32>(j)) - 0.5f;
      v += d * (0.02f + 0.3f * std::max(0.0f, v));
    }
  field.height = h;
  // Normals from the slopes across each sample.
  const f32 step = 1.0f / static_cast<f32>(n);
  field.normal.assign(count, {0.0f, 1.0f, 0.0f});
  for (i32 j = 0; j < field.depth; ++j)
    for (i32 i = 0; i < field.width; ++i) {
      const f32 l = field.at(std::max(0, i - 1), j), r = field.at(std::min(field.width - 1, i + 1), j);
      const f32 d = field.at(i, std::max(0, j - 1)), u = field.at(i, std::min(field.depth - 1, j + 1));
      field.normal[static_cast<usize>(j * field.width + i)] = normalize(vec3{l - r, 2.0f * step, d - u});
    }
}

} // namespace

const height_field &ground_field() {
  if (field_version != terrain_version()) {
    field_version = terrain_version();
    build_field();
  }
  return field;
}

f32 ground_height(vec2 p) {
  const height_field &f = ground_field();
  const f32 u = clamp(p.x * unit3d * static_cast<f32>(height_samples), 0.0f, static_cast<f32>(f.width - 1));
  const f32 v = clamp(p.y * unit3d * static_cast<f32>(height_samples), 0.0f, static_cast<f32>(f.depth - 1));
  const i32 i = std::min(static_cast<i32>(u), f.width - 2), j = std::min(static_cast<i32>(v), f.depth - 2);
  const f32 fu = u - static_cast<f32>(i), fv = v - static_cast<f32>(j);
  return lerp(lerp(f.at(i, j), f.at(i + 1, j), fu), lerp(f.at(i, j + 1), f.at(i + 1, j + 1), fu), fv);
}

camera3d table_camera() {
  const f32 yaw = state.cam_yaw * pi / 180.0f;
  const f32 pitch = pitch_at(state.cam_distance);
  const vec3 target = to3d(state.cam_target, 0.0f);
  const vec3 back{std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
  return {.position = target + back * state.cam_distance,
          .target = target,
          .fovy = 40.0f,
          .near_plane = 0.1f,
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
  view_focus({world_width * 0.5f, world_height * 0.56f}, 52.0f, true);
}

bool screen_to_table(context &ctx, vec2 screen, vec2 *at) {
  const ray3d ray = camera3d_ray(ctx, table_camera(), screen);
  // On the flat ground first, then again at the height of the ground found
  // there, which is close enough on a table of flat-topped tiles.
  f32 h = 0.0f;
  vec2 p{};
  for (i32 i = 0; i < 3; ++i) {
    const ray3d_hit hit = ray3d_plane(ray, {0.0f, h, 0.0f}, {0.0f, 1.0f, 0.0f});
    if (!hit.hit)
      return false;
    p = {hit.point.x / unit3d, hit.point.z / unit3d};
    h = std::max(ground_height(p), water_level);
  }
  if (at)
    *at = p;
  return p.x >= 0.0f && p.y >= 0.0f && p.x < world_width && p.y < world_height;
}

bool mouse_on_table(context &ctx, vec2 *at) { return screen_to_table(ctx, mouse_pos(ctx), at); }

vec2 table_to_screen(context &ctx, vec2 p, f32 lift, bool *visible) {
  return camera3d_to_screen(ctx, table_camera(), to3d(p, std::max(ground_height(p), water_level) + lift), visible);
}

} // namespace sandtable
