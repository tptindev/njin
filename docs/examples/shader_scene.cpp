#include <njin.h>

#include <cmath>

namespace {
njin::shader_handle scene;
entt::entity hero = entt::null; // nhân vật, game tự tạo ở chỗ khác

void load(njin::context &ctx) {
  // nullptr: giữ vertex shader mặc định, chỉ thay fragment shader.
  scene = njin::shader_load(ctx, nullptr, "assets/scene.fs");

  // Ảnh phụ: gắn một lần, engine gắn lại mỗi lần shader chạy. Dùng texture_load,
  // không dùng ảnh trong atlas.
  njin::shader_set_texture(ctx, scene, "ramp", njin::texture_load(ctx, "assets/ramp.png"));
  njin::shader_set_texture(ctx, scene, "noise", njin::texture_load(ctx, "assets/noise.png"));

  // Áp lên cả thế giới mà camera vẽ. UI không bị ảnh hưởng.
  njin::camera_set_post_shader(ctx, scene);
}

// Đặt uniform mỗi frame, trước khi thế giới được vẽ.
void update_scene(njin::context &ctx) {
  const float t = njin::elapsed(ctx);
  njin::shader_set_f32(ctx, scene, "time", t);
  njin::shader_set_vec2(ctx, scene, "resolution", njin::screen_size(ctx));
  njin::shader_set_vec3(ctx, scene, "ambient", {0.10f, 0.13f, 0.24f}); // xanh đêm
  njin::shader_set_f32(ctx, scene, "night", 1.0f);
  njin::shader_set_f32(ctx, scene, "dusk", 0.0f);
  njin::shader_set_f32(ctx, scene, "haze", 0.0f);

  // Đèn tính bằng pixel màn hình: xy là vị trí, z là bán kính, w là độ sáng.
  // Màu của từng đèn nằm ở mảng thứ hai, cùng chỉ số.
  const njin::vec2 torch = njin::w2scr(ctx, njin::world(ctx).get<njin::transform>(hero).pos);
  const njin::vec4 lights[2] = {
      {torch.x, torch.y, 190.0f, 1.0f + 0.08f * std::sin(t * 11.0f)}, // đuốc, chập chờn
      {njin::mouse_pos(ctx).x, njin::mouse_pos(ctx).y, 130.0f, 0.9f}, // đèn ở con trỏ chuột
  };
  const njin::vec4 colors[2] = {
      {1.0f, 0.75f, 0.4f, 0.0f}, // vàng cam
      {0.4f, 0.6f, 1.0f, 0.0f},  // xanh lam
  };
  njin::shader_set_i32(ctx, scene, "light_count", 2);
  njin::shader_set_vec4_array(ctx, scene, "lights", lights, 2);
  njin::shader_set_vec4_array(ctx, scene, "light_colors", colors, 2);
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, load, "load");
  njin::ecs_register(ctx, njin::phase_pre_render, update_scene, "update_scene");
}
} // namespace

njin::mod_desc scene_shader_module() {
  return {.name = "scene_shader", .setup = setup};
}
