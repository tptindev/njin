#include <njin.h>

namespace {
using namespace njin;

softbody3d_handle flag;
softbody3d_handle ball;
vehicle3d_handle car;

void load(context &ctx) {
  light3d_set(ctx, {.direction = {-0.5f, -1.0f, -0.3f}, .shadows = true});
  // Mặt đất: một hộp tĩnh lớn, mặt trên ở y = 0.
  body3d_create(ctx, {.position = {0.0f, -0.5f, 0.0f}, .size = {200.0f, 1.0f, 200.0f}});
  // Cột cờ và lá cờ ghim cạnh trái vào cột, gió thổi chéo.
  body3d_create(ctx, {.shape = shape3d_cylinder, .position = {-4.0f, 2.0f, 0.0f}, .radius = 0.05f, .height = 4.0f});
  flag = cloth3d_create(ctx, {.position = {-3.0f, 3.4f, 0.0f}, .size = {2.0f, 1.2f}, .columns = 20, .rows = 12,
                              .pin_edges = cloth3d_left});
  softbody3d_set_wind(ctx, flag, {6.0f, 0.0f, 2.0f});
  model_material_set(ctx, softbody3d_model(ctx, flag), 0, {.color = {0.85f, 0.15f, 0.15f, 1.0f}});
  // Quả bóng: mặt cầu có áp suất bên trong, nảy và lún khi rơi.
  ball = softbody3d_create(ctx, {.kind = softbody3d_sphere, .position = {0.0f, 3.0f, 2.0f}, .radius = 0.5f,
                                 .pressure = 200.0f});
  // Xe bốn bánh mặc định: hai bánh trước lái, cả bốn bánh kéo.
  car = vehicle3d_create(ctx, {.position = {4.0f, 1.0f, 0.0f}});
}

void drive(context &ctx) {
  // Mũi tên lên/xuống là ga và lùi, trái/phải là lái, phím cách là phanh tay.
  const f32 throttle = (key_held(ctx, key_up) ? 1.0f : 0.0f) - (key_held(ctx, key_down) ? 1.0f : 0.0f);
  const f32 steer = (key_held(ctx, key_right) ? 1.0f : 0.0f) - (key_held(ctx, key_left) ? 1.0f : 0.0f);
  vehicle3d_set_input(ctx, car, throttle, steer, 0.0f, key_held(ctx, key_space) ? 1.0f : 0.0f);
  // Đá quả bóng lên khi nhấn B.
  if (key_pressed(ctx, key_b))
    softbody3d_add_impulse(ctx, ball, {0.0f, 6.0f, 0.0f});
}

void render(context &ctx) {
  const transform3d body = body3d_transform(ctx, vehicle3d_body(ctx, car));
  begin_3d(ctx, {.position = body.position + vec3{-6.0f, 5.0f, -8.0f}, .target = body.position});
  draw_plane3d(ctx, {0.0f, 0.0f, 0.0f}, {200.0f, 200.0f}, {0.55f, 0.6f, 0.5f, 1.0f});
  draw_cylinder3d(ctx, {-4.0f, 0.0f, 0.0f}, {-4.0f, 4.0f, 0.0f}, 0.05f, colors::gray);
  // Vải và bóng: model của vật mềm luôn có hình hiện tại, vẽ ở transform mặc định.
  draw_model(ctx, softbody3d_model(ctx, flag), {});
  draw_model(ctx, softbody3d_model(ctx, ball), {}, colors::yellow);
  // Thân xe theo body của nó, mỗi bánh là một hình trụ theo transform của bánh.
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
