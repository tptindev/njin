// Chạy một shader SDF trong njin: vẽ một ảnh kéo giãn thành khung 400 x 225, shader tự tạo hình lên đó.
#include <njin.h>

namespace {
njin::texture_handle white_card; // bất kỳ ảnh nào: shader bỏ qua màu của nó, chỉ dùng toạ độ 0..1
njin::shader_handle ui_shader;

void startup(njin::njin_ctx &ctx) {
  white_card = njin::texture_load(ctx, "assets/sprites.png");
  ui_shader = njin::shader_load(ctx, nullptr, "assets/learn_shader_sdf_ui.fs");
}

void draw(njin::njin_ctx &ctx) {
  const njin::vec2 size{400.0f, 225.0f};
  const njin::vec2 image = njin::texture_size(ctx, white_card);

  // Uniform đặt TRƯỚC shader_begin. `resolution` là kích thước khung sẽ vẽ, để hình tròn không bị dẹt.
  njin::shader_set_vec2(ctx, ui_shader, "resolution", size);
  njin::shader_set_vec2(ctx, ui_shader, "mouse", {0.7f, 0.5f});

  njin::shader_begin(ctx, ui_shader);
  njin::texture_draw_ex(ctx, white_card,
                        njin::texture_draw_desc{.pos = {280.0f, 100.0f},
                                                .scale = {size.x / image.x, size.y / image.y}});
  njin::shader_end(ctx);
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_post_render, draw);
}
} // namespace

int main() {
  njin::njin_ctx *ctx = njin::njin_create({.title = "SDF trong njin", .width = 960, .height = 540, .target_fps = 60,
                                           .clear_bg_color = {0.2f, 0.25f, 0.3f, 1.0f}});
  njin::njin_mod_register(*ctx, {.name = "game", .setup = setup});
  njin::njin_run(*ctx);
  njin::njin_destroy(ctx);
}
