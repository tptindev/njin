#include "camera.h"
#include "_comps.h"
#include "njin2rl.h"
#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "rl2njin.h"
#include <raylib.h>

namespace njin {
namespace {
// Identity view: world coordinates are screen pixels.
constexpr camera_view default_view{.zoom = 1.0f,
                                   .rotation = 0.0f,
                                   .offset = {0.0f, 0.0f},
                                   .target = {0.0f, 0.0f}};

Camera2D active_raylib_camera(const njin_ctx &ctx) {
  Camera2D camera{};
  to_raylib(camera_active(ctx), camera);
  return camera;
}

void begin_world_space(njin_ctx &ctx) {
  BeginMode2D(active_raylib_camera(ctx));
}

void end_world_space(njin_ctx &) { EndMode2D(); }

void setup(njin_ctx &ctx) {
  ecs_register(ctx, phase_pre_render, begin_world_space);
  ecs_register(ctx, phase_post_render, end_world_space);
}
} // namespace

mod_desc camera_module() {
  return mod_desc{.name = "njin.camera", .setup = setup};
}

camera_view camera_active(const njin_ctx &ctx) {
  const auto cameras =
      ctx.ecs.registry
          .view<const camera_on, const camera_2d, const transform>();
  for (const entt::entity entity : cameras) {
    const camera_2d &cam = cameras.get<const camera_2d>(entity);
    const transform &tr = cameras.get<const transform>(entity);
    return camera_view{.zoom = cam.zoom > 0.0f ? cam.zoom : 1.0f,
                       .rotation = tr.rot,
                       .offset = cam.offset,
                       .target = tr.pos};
  }
  return default_view;
}

vec2 w2scr(const njin_ctx &ctx, vec2 pos) {
  Vector2 point{};
  to_raylib(pos, point);
  vec2 result{};
  from_raylib(GetWorldToScreen2D(point, active_raylib_camera(ctx)), result);
  return result;
}

vec2 scr2w(const njin_ctx &ctx, vec2 pos) {
  Vector2 point{};
  to_raylib(pos, point);
  vec2 result{};
  from_raylib(GetScreenToWorld2D(point, active_raylib_camera(ctx)), result);
  return result;
}
} // namespace njin
