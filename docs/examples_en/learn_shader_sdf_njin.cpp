// Run an SDF shader in njin: draw an image stretched into a 400 x 225 frame, and the shader makes the shapes on it.
#include <njin.h>

namespace {
njin::texture_handle white_card; // any image: the shader ignores its colors and only uses the 0..1 coordinates
njin::shader_handle ui_shader;

void startup(njin::context &ctx) {
  white_card = njin::texture_load(ctx, "assets/sprites.png");
  ui_shader = njin::shader_load(ctx, nullptr, "assets/learn_shader_sdf_ui.fs");
}

void draw(njin::context &ctx) {
  const njin::vec2 size{400.0f, 225.0f};
  const njin::vec2 image = njin::texture_size(ctx, white_card);

  // Uniforms are set BEFORE shader_begin. `resolution` is the size of the frame being drawn, so circles are not squashed.
  njin::shader_set_vec2(ctx, ui_shader, "resolution", size);
  njin::shader_set_vec2(ctx, ui_shader, "mouse", {0.7f, 0.5f});

  njin::shader_begin(ctx, ui_shader);
  njin::texture_draw_ex(ctx, white_card,
                        njin::texture_draw_desc{.pos = {280.0f, 100.0f},
                                                .scale = {size.x / image.x, size.y / image.y}});
  njin::shader_end(ctx);
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_post_render, draw);
}
} // namespace

int main() {
  njin::context *ctx = njin::create({.title = "SDF in njin", .width = 960, .height = 540, .target_fps = 60,
                                           .clear_bg_color = {0.2f, 0.25f, 0.3f, 1.0f}});
  njin::mod_register(*ctx, {.name = "game", .setup = setup});
  njin::run(*ctx);
  njin::destroy(ctx);
}
