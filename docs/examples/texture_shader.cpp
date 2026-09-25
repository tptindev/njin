#include <njin.h>

namespace {
njin::texture_handle player_tex;
njin::shader_handle gray;

void load(njin::njin_ctx &ctx) {
  player_tex = njin::texture_load(ctx, "assets/player.png");
  // nullptr: giữ vertex shader mặc định, chỉ thay fragment shader.
  gray = njin::shader_load(ctx, nullptr, "assets/shaders/gray.fs");
}

void draw(njin::njin_ctx &ctx) {
  const njin::rgba white{1.0f, 1.0f, 1.0f, 1.0f};

  // Đặt uniform TRƯỚC shader_begin.
  njin::shader_set_f32(ctx, gray, "amount", 1.0f);
  njin::shader_begin(ctx, gray);
  njin::texture_draw(ctx, player_tex, {100.0f, 100.0f}, white);
  njin::shader_end(ctx);

  // Ngoài begin/end: vẽ bình thường, không có shader.
  njin::texture_draw(ctx, player_tex, {300.0f, 100.0f}, white);
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, load);
  njin::ecs_register(ctx, njin::phase_render, draw);
}
} // namespace

njin::mod_desc texture_demo_module() {
  return {.name = "texture_demo", .setup = setup};
}
