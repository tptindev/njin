#include <njin.h>

#include <vector>

namespace {
// Dữ liệu của một instance: 8 số, shader đọc thành instance0 và instance1.
struct dot {
  float x, y, size, unused; // instance0: tâm, cạnh
  float r, g, b, a;         // instance1: màu
};

njin::shader_handle dots_shader;
njin::instance_buffer_handle dots;
std::vector<dot> data;

void load(njin::njin_ctx &ctx) {
  dots_shader = njin::shader_load(ctx, "assets/shaders/dots.vs", "assets/shaders/dots.fs");
  dots = njin::instance_buffer_create(ctx, 8); // 8 số một instance
  njin::rng &r = njin::random(ctx);
  for (int i = 0; i < 20000; ++i)
    data.push_back({r.range(0.0f, 1280.0f), r.range(0.0f, 720.0f), r.range(2.0f, 6.0f), 0.0f,
                    r.unit(), r.unit(), r.unit(), 1.0f});
}

void draw(njin::njin_ctx &ctx) {
  // Ghi lại mỗi frame (ở đây dữ liệu không đổi, nhưng thường thì có), rồi
  // vẽ cả 20.000 chấm bằng một lệnh vẽ.
  njin::instance_buffer_upload(ctx, dots, &data.data()->x, (njin::u32)data.size());
  njin::draw_instanced(ctx, dots, dots_shader, 0, (njin::u32)data.size());
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, load, "load");
  njin::ecs_register(ctx, njin::phase_render, draw, "dots");
}
} // namespace

int main() {
  njin::njin_ctx *ctx = njin::njin_create({.title = "Instancing", .width = 1280.0f, .height = 720.0f,
                                           .target_fps = 60.0f});
  njin::njin_mod_register(*ctx, {.name = "dots", .setup = setup});
  njin::njin_run(*ctx);
  njin::njin_destroy(ctx);
}
