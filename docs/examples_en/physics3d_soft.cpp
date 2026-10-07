#include <njin.h>

namespace {
using namespace njin;

softbody3d_handle flag;
softbody3d_handle ball;
vehicle3d_handle car;

void load(context &ctx) {
  light3d_set(ctx, {.direction = {-0.5f, -1.0f, -0.3f}, .shadows = true});
  // The ground: a large static box, its top at y = 0.
  body3d_create(ctx, {.position = {0.0f, -0.5f, 0.0f}, .size = {200.0f, 1.0f, 200.0f}});
  // A flagpole and a flag pinned to it by its left edge, the wind blowing across.
  body3d_create(ctx, {.shape = shape3d_cylinder, .position = {-4.0f, 2.0f, 0.0f}, .radius = 0.05f, .height = 4.0f});
  flag = cloth3d_create(ctx, {.position = {-3.0f, 3.4f, 0.0f}, .size = {2.0f, 1.2f}, .columns = 20, .rows = 12,
                              .pin_edges = cloth3d_left});
  softbody3d_set_wind(ctx, flag, {6.0f, 0.0f, 2.0f});
  model_material_set(ctx, softbody3d_model(ctx, flag), 0, {.color = {0.85f, 0.15f, 0.15f, 1.0f}});
  // A ball: a sphere with pressure inside, it bounces and squashes when it lands.
  ball = softbody3d_create(ctx, {.kind = softbody3d_sphere, .position = {0.0f, 3.0f, 2.0f}, .radius = 0.5f,
                                 .pressure = 200.0f});
  // The default four-wheeled car: the front wheels steer, all four are driven.
  car = vehicle3d_create(ctx, {.position = {4.0f, 1.0f, 0.0f}});
}

void drive(context &ctx) {
  // Up/down arrows are throttle and reverse, left/right steer, space is the handbrake.
  const f32 throttle = (key_held(ctx, key_up) ? 1.0f : 0.0f) - (key_held(ctx, key_down) ? 1.0f : 0.0f);
  const f32 steer = (key_held(ctx, key_right) ? 1.0f : 0.0f) - (key_held(ctx, key_left) ? 1.0f : 0.0f);
  vehicle3d_set_input(ctx, car, throttle, steer, 0.0f, key_held(ctx, key_space) ? 1.0f : 0.0f);
  // Kick the ball up when B is pressed.
  if (key_pressed(ctx, key_b))
    softbody3d_add_impulse(ctx, ball, {0.0f, 6.0f, 0.0f});
}

void render(context &ctx) {
  const transform3d body = body3d_transform(ctx, vehicle3d_body(ctx, car));
  begin_3d(ctx, {.position = body.position + vec3{-6.0f, 5.0f, -8.0f}, .target = body.position});
  draw_plane3d(ctx, {0.0f, 0.0f, 0.0f}, {200.0f, 200.0f}, {0.55f, 0.6f, 0.5f, 1.0f});
  draw_cylinder3d(ctx, {-4.0f, 0.0f, 0.0f}, {-4.0f, 4.0f, 0.0f}, 0.05f, colors::gray);
  // Cloth and ball: a soft body's model always has its current shape, drawn at the default transform.
  draw_model(ctx, softbody3d_model(ctx, flag), {});
  draw_model(ctx, softbody3d_model(ctx, ball), {}, colors::yellow);
  // The chassis follows its body, each wheel is a cylinder at the wheel's transform.
  draw_shape3d(ctx, {.kind = shape3d_box, .position = body.position, .rotation = body.rotation,
                     .size = {1.8f, 0.6f, 4.0f}, .rounding = 0.1f},
               colors::blue);
  for (i32 i = 0; i < vehicle3d_wheel_count(ctx, car); i++) {
    const transform3d w = vehicle3d_wheel_transform(ctx, car, i);
    draw_shape3d(ctx, {.kind = shape3d_cylinder, .position = w.position, .rotation = w.rotation, .radius = 0.35f,
                       .height = 0.25f},
                 {0.15f, 0.15f, 0.15f, 1.0f});
  }
  end_3d(ctx);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, load, "load");
  ecs_register(ctx, phase_fixed_update, drive, "drive");
  ecs_register(ctx, phase_render, render, "render");
}
} // namespace

mod_desc physics3d_soft_module() { return {.name = "physics3d_soft", .setup = setup}; }
