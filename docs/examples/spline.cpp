#include <njin.h>

namespace {
using namespace njin;

spline3d rail;             // đường ray kín của camera, đi qua từng điểm
spline3d lift;             // đường bay của bục, Bezier có tay cầm
spline_follower cam_mover{.speed = 4.0f, .end = spline_loop};
spline_follower lift_mover{.speed = 2.0f, .end = spline_ping_pong};
bool show_rails = true;

void load(context &) {
  rail.points = {{12, 4, 0}, {6, 6, 10}, {-8, 5, 9}, {-12, 3, -2}, {-4, 7, -12}, {8, 4, -9}};
  rail.closed = true;
  spline_bake(rail); // sau khi sửa điểm: lập bảng để đi đều theo khoảng cách

  lift.kind = spline_bezier;
  lift.points = {{-4, 1, 0}, {-4, 6, 0}, {4, 6, 0}, {4, 1, 0}}; // đầu, hai tay cầm, cuối
  spline_bake(lift);
}

void update(context &ctx) {
  if (key_pressed(ctx, key_tab))
    show_rails = !show_rails;
}

void render(context &ctx) {
  // Camera chạy đều trên ray và nhìn trước theo hướng ray.
  const vec3 eye = spline_follow(rail, cam_mover, delta(ctx));
  const vec3 ahead = spline_tangent_at(rail, cam_mover.distance);
  const camera3d camera{.position = eye, .target = eye + ahead * 4.0f + vec3{0.0f, -1.5f, 0.0f}, .fovy = 60.0f};

  // Bục đi tới đi lui dọc đường cong.
  const vec3 platform = spline_follow(lift, lift_mover, delta(ctx));

  light3d_set(ctx, {.direction = {-0.4f, -1.0f, -0.3f}, .shadows = true, .shadow_range = 40.0f});
  begin_3d(ctx, camera);
  draw_plane3d(ctx, {0.0f, 0.0f, 0.0f}, {40.0f, 40.0f}, rgba{0.55f, 0.6f, 0.5f, 1.0f});
  draw_cube3d(ctx, platform, {2.0f, 0.3f, 2.0f}, rgba{0.8f, 0.5f, 0.2f, 1.0f});
  if (show_rails) {
    spline_draw_debug(ctx, rail);
    spline_draw_debug(ctx, lift, colors::green);
  }
  end_3d(ctx);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, load, "load");
  ecs_register(ctx, phase_update, update, "update");
  ecs_register(ctx, phase_render, render, "render");
}
} // namespace

mod_desc rails_module() { return {.name = "rails", .setup = setup}; }
