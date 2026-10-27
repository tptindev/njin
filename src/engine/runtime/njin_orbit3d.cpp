// camera3d_orbit (njin_3d.h): a camera turning round a point, driven by the
// mouse as model viewers and njin_inspector do.
#include "njin_3d.h"
#include "njin_cfg.h"
#include "njin_input.h"
#include "njin_ui.h"
#include <algorithm>
#include <cmath>

namespace njin {
namespace {
constexpr f32 deg = 3.14159265f / 180.0f;

// From the target towards the camera, length 1.
vec3 back_of(const camera3d_orbit &o) {
  const f32 y = o.yaw * deg, p = o.pitch * deg;
  return {std::cos(p) * std::sin(y), std::sin(p), std::cos(p) * std::cos(y)};
}
} // namespace

camera3d camera3d_orbit_view(const camera3d_orbit &orbit) {
  camera3d cam;
  cam.position = orbit.target + back_of(orbit) * orbit.distance;
  cam.target = orbit.target;
  cam.fovy = orbit.fovy;
  return cam;
}

void camera3d_orbit_update(const context &ctx, camera3d_orbit &orbit, const camera3d_orbit_input &input) {
  if (!ui_mouse_over(ctx)) {
    const vec2 d = mouse_delta(ctx);
    const bool shift = key_held(ctx, key_left_shift) || key_held(ctx, key_right_shift);
    const bool pan = mouse_held(ctx, input.pan) || (input.shift_pans && shift && mouse_held(ctx, input.rotate));
    if (pan) {
      // One pixel of drag moves the target one pixel on screen at its distance.
      const vec3 forward = back_of(orbit) * -1.0f;
      const vec3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
      const vec3 up = cross(right, forward);
      const f32 h = std::max(screen_size(ctx).y, 1.0f);
      const f32 per_pixel = 2.0f * orbit.distance * std::tan(orbit.fovy * 0.5f * deg) / h * input.pan_speed;
      orbit.target = orbit.target - right * (d.x * per_pixel) + up * (d.y * per_pixel);
    } else if (mouse_held(ctx, input.rotate)) {
      orbit.yaw -= d.x * input.rotate_speed;
      orbit.pitch += d.y * input.rotate_speed;
    }
    if (input.zoom) {
      const f32 wheel = mouse_wheel(ctx);
      if (wheel != 0.0f)
        orbit.distance *= std::pow(1.0f - std::clamp(input.zoom_step, 0.0f, 0.9f), wheel);
    }
  }
  orbit.yaw = std::fmod(orbit.yaw, 360.0f);
  orbit.pitch = std::clamp(orbit.pitch, std::max(orbit.pitch_min, -89.0f), std::min(orbit.pitch_max, 89.0f));
  orbit.distance = std::clamp(orbit.distance, std::max(orbit.distance_min, 1e-3f), std::max(orbit.distance_max, orbit.distance_min));
}
} // namespace njin
