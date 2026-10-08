#include <njin.h>

namespace {
using namespace njin;

vehicle3d_handle bike;
vehicle3d_handle tank;
bool riding_bike = true;
bool debug = false;

void load(context &ctx) {
  light3d_set(ctx, {.direction = {-0.5f, -1.0f, -0.3f}, .shadows = true});
  // Mặt đất: hộp tĩnh lớn, ma sát cao như mặt đường.
  body3d_create(ctx, {.position = {0.0f, -0.5f, 0.0f}, .size = {400.0f, 1.0f, 400.0f}, .friction = 1.0f});
  // Xe máy và xe tăng với số mặc định.
  bike = motorcycle3d_create(ctx, {.position = {0.0f, 1.0f, 0.0f}});
  tank = tracked3d_create(ctx, {.position = {6.0f, 2.0f, 0.0f}});
}

void drive(context &ctx) {
  // Tab đổi xe, G bật tắt hình debug.
  if (key_pressed(ctx, key_tab))
    riding_bike = !riding_bike;
  if (key_pressed(ctx, key_g))
    debug = !debug;
  // Mũi tên lên/xuống là ga và lùi, trái/phải là lái, phím cách là phanh.
  const f32 gas = (key_held(ctx, key_up) ? 1.0f : 0.0f) - (key_held(ctx, key_down) ? 1.0f : 0.0f);
  const f32 steer = (key_held(ctx, key_right) ? 1.0f : 0.0f) - (key_held(ctx, key_left) ? 1.0f : 0.0f);
  const f32 brake = key_held(ctx, key_space) ? 1.0f : 0.0f;
  // Xe không lái thì phanh lại cho đứng yên.
  vehicle3d_set_input(ctx, bike, riding_bike ? gas : 0.0f, riding_bike ? steer : 0.0f, riding_bike ? brake : 1.0f, 0.0f);
  vehicle3d_set_input(ctx, tank, riding_bike ? 0.0f : gas, riding_bike ? 0.0f : steer, riding_bike ? 1.0f : brake, 0.0f);
}

void render(context &ctx) {
  const transform3d at = body3d_transform(ctx, vehicle3d_body(ctx, riding_bike ? bike : tank));
  begin_3d(ctx, {.position = at.position + vec3{-6.0f, 4.0f, -8.0f}, .target = at.position});
  draw_plane3d(ctx, {0.0f, 0.0f, 0.0f}, {400.0f, 400.0f}, {0.55f, 0.6f, 0.5f, 1.0f});
  // Thân xe máy và xe tăng theo body của chúng.
  const transform3d b = body3d_transform(ctx, vehicle3d_body(ctx, bike));
  draw_shape3d(ctx, {.kind = shape3d_box, .position = b.position, .rotation = b.rotation, .size = {0.4f, 0.6f, 0.8f}},
               colors::red);
  const transform3d t = body3d_transform(ctx, vehicle3d_body(ctx, tank));
  draw_shape3d(ctx, {.kind = shape3d_box, .position = t.position, .rotation = t.rotation, .size = {3.4f, 1.0f, 6.4f}},
               {0.35f, 0.45f, 0.3f, 1.0f});
  // Mỗi bánh là một hình trụ theo transform của bánh (trục y là trục bánh).
  for (const vehicle3d_handle v : {bike, tank})
    for (i32 i = 0; i < vehicle3d_wheel_count(ctx, v); i++) {
      const transform3d w = vehicle3d_wheel_transform(ctx, v, i);
      const f32 radius = v.id == bike.id ? 0.31f : 0.3f;
      draw_shape3d(ctx, {.kind = shape3d_cylinder, .position = w.position, .rotation = w.rotation, .radius = radius,
                         .height = v.id == bike.id ? 0.05f : 0.1f},
                   {0.15f, 0.15f, 0.15f, 1.0f});
    }
  // Hình debug: thân, bánh, giảm xóc và chỗ bánh chạm đất.
  if (debug) {
    vehicle3d_draw_debug(ctx, bike);
    vehicle3d_draw_debug(ctx, tank);
  }
  end_3d(ctx);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, load, "load");
  ecs_register(ctx, phase_fixed_update, drive, "drive");
  ecs_register(ctx, phase_render, render, "render");
}
} // namespace

mod_desc physics3d_vehicles_module() { return {.name = "physics3d_vehicles", .setup = setup}; }
