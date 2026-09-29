#include <njin.h>

#include <vector>

namespace {
// Data of one instance: 8 numbers, which the shader reads as instance0 and instance1.
struct dot {
  float x, y, size, unused; // instance0: center, side length
  float r, g, b, a;         // instance1: color
};

njin::shader_handle dots_shader;
njin::instance_buffer_handle dots;
std::vector<dot> data;

void load(njin::context &ctx) {
  dots_shader = njin::shader_load(ctx, "assets/shaders/dots.vs", "assets/shaders/dots.fs");
  dots = njin::instance_buffer_create(ctx, 8); // 8 numbers per instance
  njin::rng &r = njin::random(ctx);
  for (int i = 0; i < 20000; ++i)
    data.push_back({r.range(0.0f, 1280.0f), r.range(0.0f, 720.0f), r.range(2.0f, 6.0f), 0.0f,
                    r.unit(), r.unit(), r.unit(), 1.0f});
}

void draw(njin::context &ctx) {
  // Upload every frame (here the data does not change, but usually it does), then
  // draw all 20,000 dots with a single draw call.
  njin::instance_buffer_upload(ctx, dots, &data.data()->x, (njin::u32)data.size());
  njin::draw_instanced(ctx, dots, dots_shader, 0, (njin::u32)data.size());
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, load, "load");
  njin::ecs_register(ctx, njin::phase_render, draw, "dots");
}
} // namespace

int main() {
  njin::context *ctx = njin::create({.title = "Instancing", .width = 1280.0f, .height = 720.0f,
                                           .target_fps = 60.0f});
  njin::mod_register(*ctx, {.name = "dots", .setup = setup});
  njin::run(*ctx);
  njin::destroy(ctx);
}
