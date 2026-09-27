// The camera: the whole sheet at zoom 1, or one person up close. Focusing
// eases the camera in on them and keeps them in the middle as they wander;
// letting go eases it back out to the whole sheet. Both directions are the
// same smoothing toward a target position and zoom, so switching from one
// person to another, or letting go halfway through zooming in, never jumps.
#include "game.h"

#include <cmath>

namespace paper_crowd {
namespace {
constexpr vec2 sheet_centre{world_w * 0.5f, world_h * 0.5f};
constexpr rect sheet{{0.0f, 0.0f}, {world_w, world_h}};
constexpr f32 move_smoothing = 0.18f; // seconds to cover ~63% of the way
constexpr f32 zoom_smoothing = 0.25f;

// Where a person reads as being: the middle of the figure, not the feet.
vec2 figure_centre(const person &p, vec2 feet) { return feet + vec2{0.0f, -10.0f * p.size}; }
} // namespace

void spawn_camera(njin_ctx &ctx) { g.camera = camera_spawn(ctx, 1.0f, sheet_centre); }

entt::entity person_at(njin_ctx &ctx, vec2 pos, f32 radius) {
  entt::entity best = entt::null;
  f32 best_d = radius * radius;
  for (auto [e, p, tr] : world(ctx).view<const person, const transform>().each()) {
    const f32 d = length_sq(figure_centre(p, tr.pos) - pos);
    if (d < best_d) {
      best_d = d;
      best = e;
    }
  }
  return best;
}

void focus_on(entt::entity e) { g.focus = e; }

void unfocus() { g.focus = entt::null; }

void drive_camera(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  if (!reg.valid(g.camera))
    return;
  if (g.focus != entt::null && !reg.valid(g.focus))
    unfocus();

  vec2 want_pos = sheet_centre;
  f32 want_zoom = 1.0f;
  if (g.focus != entt::null) {
    want_pos = figure_centre(reg.get<person>(g.focus), reg.get<transform>(g.focus).pos);
    want_zoom = g.focus_zoom;
  }

  // Frame-rate independent exponential smoothing. Zoom eases in log space, so
  // going 1 -> 4 feels as even as 4 -> 1.
  const f32 dt = delta_real(ctx);
  const f32 kp = 1.0f - std::exp(-dt / move_smoothing);
  const f32 kz = 1.0f - std::exp(-dt / zoom_smoothing);
  transform &tr = reg.get<transform>(g.camera);
  camera_2d &cam = reg.get<camera_2d>(g.camera);
  cam.zoom = std::exp(lerp(std::log(cam.zoom), std::log(want_zoom), kz));
  if (std::abs(cam.zoom - want_zoom) < 0.001f)
    cam.zoom = want_zoom;
  // The view never shows past the paper's edge; at zoom 1 that pins it to the
  // middle of the sheet, which is exactly where letting go should end up.
  tr.pos = camera_clamp(ctx, lerp(tr.pos, want_pos, kp), cam, sheet);
}
} // namespace paper_crowd
