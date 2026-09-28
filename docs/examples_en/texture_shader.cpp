#include <njin.h>

namespace {
njin::texture_handle player_tex;
njin::shader_handle gray;

void load(njin::njin_ctx &ctx) {
  player_tex = njin::texture_load(ctx, "assets/player.png");
  // nullptr: keep the default vertex shader, only replace the fragment shader.
  gray = njin::shader_load(ctx, nullptr, "assets/shaders/gray.fs");
}

void draw(njin::njin_ctx &ctx) {
  const njin::rgba white{1.0f, 1.0f, 1.0f, 1.0f};

  // Set the uniforms BEFORE shader_begin.
  njin::shader_set_f32(ctx, gray, "amount", 1.0f);
  njin::shader_begin(ctx, gray);
  njin::texture_draw(ctx, player_tex, {100.0f, 100.0f}, white);
  njin::shader_end(ctx);

  // Outside begin/end: drawn normally, with no shader.
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
