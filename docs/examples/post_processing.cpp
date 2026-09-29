#include <njin.h>

namespace {
njin::texture_handle player_tex;
njin::render_texture_handle scene;
njin::shader_handle effect;

void load(njin::context &ctx) {
  const njin::vec2 size = njin::screen_size(ctx);
  scene = njin::render_texture_load(ctx, (njin::u32)size.x, (njin::u32)size.y);
  player_tex = njin::texture_load(ctx, "assets/player.png");
  effect = njin::shader_load(ctx, nullptr, "assets/shaders/crt.fs");
}

// Bước 1: vẽ cảnh vào render texture. Đặt ở phase_post_update vì
// render_texture_begin đặt lại phép biến đổi của camera.
void draw_scene(njin::context &ctx) {
  const njin::rgba black{0.0f, 0.0f, 0.0f, 1.0f};
  const njin::rgba white{1.0f, 1.0f, 1.0f, 1.0f};

  njin::render_texture_begin(ctx, scene, black); // xóa đen rồi vẽ
  njin::texture_draw(ctx, player_tex, {100.0f, 100.0f}, white);
  njin::render_texture_end(ctx);
}

// Bước 2: vẽ render texture ra màn hình qua shader hậu kỳ.
void present(njin::context &ctx) {
  const njin::rgba white{1.0f, 1.0f, 1.0f, 1.0f};

  njin::shader_set_f32(ctx, effect, "time", njin::elapsed(ctx));
  njin::shader_begin(ctx, effect);
  njin::render_texture_draw(ctx, scene, {0.0f, 0.0f}, white);
  njin::shader_end(ctx);
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, load);
  njin::ecs_register(ctx, njin::phase_post_update, draw_scene);
  njin::ecs_register(ctx, njin::phase_post_render, present); // không gian màn hình
}
} // namespace

njin::mod_desc post_fx_module() {
  return {.name = "post_fx", .setup = setup};
}
