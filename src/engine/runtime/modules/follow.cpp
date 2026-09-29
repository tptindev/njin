#include "follow.h"
#include "_comps.h"
#include "_math.h"
#include "njin_camera.h"
#include "njin_cfg.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_level.h"
#include <cmath>

namespace njin {
namespace {
// Fraction of the way to close this frame, for a time constant `tau`.
f32 approach(f32 dt, f32 tau) { return tau > 0.0f ? 1.0f - std::exp(-dt / tau) : 1.0f; }

f32 sign_of(f32 v, f32 dead) { return v > dead ? 1.0f : (v < -dead ? -1.0f : 0.0f); }

void follow(context &ctx) {
  entt::registry &reg = world(ctx);
  const f32 dt = delta(ctx);
  const vec2 screen = screen_size(ctx);
  for (auto [e, tr, cam, f] : reg.view<transform, camera_2d, camera_follow>().each()) {
    if (f.center)
      cam.offset = screen * 0.5f;
    const transform *target =
        reg.valid(f.target) ? reg.try_get<transform>(f.target) : nullptr;
    vec2 pos = tr.pos;
    if (target != nullptr) {
      const vec2 aim = target->pos + f.offset;
      if (!f.started) {
        f.last_target = target->pos;
        f.look = {};
      }
      // Look ahead the way the target is going; hold it while it stands.
      if (dt > 0.0f) {
        const vec2 vel = (target->pos - f.last_target) / dt;
        const vec2 want{sign_of(vel.x, 5.0f) * f.lookahead.x, sign_of(vel.y, 5.0f) * f.lookahead.y};
        const f32 k = approach(dt, f.lookahead_smoothing);
        if (want.x != 0.0f)
          f.look.x = lerp(f.look.x, want.x, k);
        if (want.y != 0.0f)
          f.look.y = lerp(f.look.y, want.y, k);
      }
      f.last_target = target->pos;
      const vec2 goal = aim + f.look;
      // The camera moves only as far as needed to bring the goal back into
      // the deadzone.
      vec2 want = pos;
      const vec2 half = f.deadzone * 0.5f;
      if (goal.x > pos.x + half.x) want.x = goal.x - half.x;
      if (goal.x < pos.x - half.x) want.x = goal.x + half.x;
      if (goal.y > pos.y + half.y) want.y = goal.y - half.y;
      if (goal.y < pos.y - half.y) want.y = goal.y + half.y;
      if (!f.started)
        pos = aim;
      else
        pos = lerp(pos, want, approach(dt, f.smoothing));
      f.started = true;
    }
    pos = camera_clamp(ctx, pos, cam, f.bounds);
    if (f.pixel_snap) {
      const f32 z = cam.zoom > 0.0f ? cam.zoom : 1.0f;
      pos = {std::round(pos.x * z) / z, std::round(pos.y * z) / z};
    }
    tr.pos = pos;
  }
}

void setup(context &ctx) { ecs_register(ctx, phase_post_update, follow, "follow"); }
} // namespace

mod_desc camera_follow_module() {
  return mod_desc{.name = "njin.camera_follow", .setup = setup};
}

entt::entity camera_spawn(context &ctx, f32 zoom, vec2 pos) {
  entt::registry &reg = world(ctx);
  const entt::entity e = reg.create();
  reg.emplace<transform>(e, transform{.pos = pos});
  reg.emplace<camera_2d>(e, camera_2d{.offset = screen_size(ctx) * 0.5f,
                                      .zoom = zoom > 0.0f ? zoom : 1.0f});
  reg.emplace<camera_on>(e);
  return e;
}

rect level_bounds(const context &ctx, level_handle level) {
  return rect{level_origin(ctx, level), level_size(ctx, level)};
}

vec2 camera_clamp(const context &ctx, vec2 pos, const camera_2d &cam, rect bounds) {
  if (bounds.size.x <= 0.0f || bounds.size.y <= 0.0f)
    return pos;
  const f32 z = cam.zoom > 0.0f ? cam.zoom : 1.0f;
  const vec2 screen = screen_size(ctx);
  // What is visible: from pos - offset/zoom to pos + (screen - offset)/zoom.
  const vec2 before = cam.offset / z;
  const vec2 after = (screen - cam.offset) / z;
  const auto axis = [](f32 p, f32 lo, f32 hi, f32 b, f32 a) {
    if (hi - lo <= b + a) // level smaller than the view: centre it
      return (lo + hi) * 0.5f + (b - a) * 0.5f;
    return clamp(p, lo + b, hi - a);
  };
  return {axis(pos.x, bounds.pos.x, bounds.pos.x + bounds.size.x, before.x, after.x),
          axis(pos.y, bounds.pos.y, bounds.pos.y + bounds.size.y, before.y, after.y)};
}
} // namespace njin
