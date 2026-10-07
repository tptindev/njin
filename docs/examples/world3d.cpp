#include <njin.h>

namespace {
using namespace njin;

terrain3d_handle ground;
water3d_handle lake;
grass3d_handle grass;
scatter3d_handle rocks;
body3d_handle crate;
sky3d sky{.hour = 9.0f};

void load(context &ctx) {
  // Đồi 512 m sinh bằng nhiễu: cỏ khắp nơi, đá ở chỗ dốc, tuyết trên đỉnh.
  const texture_handle grass_tex = texture_load(ctx, "assets/grass.png");
  const texture_handle rock_tex = texture_load(ctx, "assets/rock.png");
  ground = terrain3d_create(ctx, {.origin = {-256.0f, 0.0f, -256.0f},
                                  .size = 512.0f,
                                  .resolution = 513,
                                  .height_scale = 45.0f,
                                  .layers = {{.albedo = grass_tex, .tile = 3.0f},
                                             {.albedo = rock_tex, .tile = 5.0f, .min_slope = 30.0f, .blend = 8.0f},
                                             {.color = {0.95f, 0.96f, 1.0f, 1.0f}, .min_height = 36.0f}},
                                  .layer_count = 3});
  // Hồ lấp chỗ trũng; bờ và độ sâu theo địa hình.
  lake = water3d_create(ctx, {.level = 8.0f, .size = {512.0f, 512.0f}, .terrain = ground});
  grass = grass3d_create(ctx, {.terrain = ground, .density = 8.0f, .layer = 0});
  rocks = scatter3d_create(ctx, {.terrain = ground,
                                 .model = model_load(ctx, "assets/rock.glb"),
                                 .density = 0.01f,
                                 .spacing = 4.0f,
                                 .min_height = 9.0f,
                                 .align = 0.7f,
                                 .sink = 0.15f});
  // Một thùng gỗ thả xuống hồ: nổi và dập dềnh theo sóng.
  crate = body3d_create(ctx, {.position = {0.0f, 12.0f, 0.0f}, .motion = body3d_dynamic, .mass = 120.0f});
  water3d_float(ctx, lake, crate);
}

void update(context &ctx) {
  // Một ngày trôi qua trong hai phút; phím R chuyển sang mưa.
  sky.hour += delta(ctx) * 24.0f / 120.0f;
  if (sky.hour >= 24.0f)
    sky.hour -= 24.0f;
  if (key_pressed(ctx, key_r))
    sky.weather = weather3d_preset(sky.weather.rain > 0.0f ? weather3d_clear : weather3d_rain);
  // Chuột trái đắp đất lên chỗ chuột trỏ tới.
  if (mouse_pressed(ctx, mouse_left)) {
    const camera3d cam{.position = {-60.0f, 40.0f, 60.0f}, .target = {0.0f, 10.0f, 0.0f}};
    const ray3d_hit hit = physics3d_raycast(ctx, camera3d_ray(ctx, cam, mouse_pos(ctx)), 1000.0f);
    if (hit.hit)
      terrain3d_edit(ctx, ground, {.kind = terrain3d_raise, .center = hit.point, .radius = 5.0f, .strength = 1.5f});
  }
}

void render(context &ctx) {
  light3d_set(ctx, {.shadows = true, .shadow_range = 60.0f});
  begin_3d(ctx, {.position = {-60.0f, 40.0f, 60.0f}, .target = {0.0f, 10.0f, 0.0f}, .far_plane = 2000.0f});
  draw_sky3d(ctx, sky); // trời, mặt trời, mây; đặt luôn ánh sáng theo giờ
  draw_terrain3d(ctx, ground);
  draw_grass3d(ctx, grass);
  draw_scatter3d(ctx, rocks);
  const transform3d t = body3d_transform(ctx, crate);
  draw_shape3d(ctx, {.kind = shape3d_box, .position = t.position, .rotation = t.rotation},
               rgba{0.6f, 0.4f, 0.2f, 1.0f});
  draw_water3d(ctx, lake); // sau cùng không bắt buộc: nước luôn vẽ sau hình đục
  end_3d(ctx);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, load, "load");
  ecs_register(ctx, phase_update, update, "update");
  ecs_register(ctx, phase_render, render, "render");
}
} // namespace

mod_desc outdoor_module() { return {.name = "outdoor", .setup = setup}; }
