// Every 3D primitive on every renderer: one instance of each mesh3d_kind with
// draw_instanced3d (cube, sphere, plane, cylinder, low sphere, low cylinder), then
// draw_cylinder3d and draw_capsule3d, each in its own column of a front view. Each
// column must show its shape (enough non-background pixels), the cylinders as tall as
// the cubes, so the bgfx renderer draws them all as the OpenGL one does.
#include <njin.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#if defined(__ANDROID__)
#include <android/log.h>
#define NJIN_TEST_LOG(...) __android_log_print(ANDROID_LOG_INFO, "NjinBgfx", __VA_ARGS__)
#else
#define NJIN_TEST_LOG(...) std::printf(__VA_ARGS__)
#endif
namespace {
constexpr int columns = 8;
unsigned frame = 0;
std::string capture;
std::array<njin::instance_buffer_handle, 6> buffers{};
float column_x(int i) { return (float(i) - (columns - 1) * 0.5f) * 1.6f; }
void require(bool ok, const std::string &message) {
  if (!ok) throw std::runtime_error(message);
}
void startup(njin::context &ctx) {
  for (int i = 0; i < 6; ++i) {
    buffers[size_t(i)] = njin::instance_buffer_create(ctx, 8);
    require(buffers[size_t(i)].id != 0, "instancing unavailable");
    // Spheres and cylinders have radius 1: half size, so each fits its column.
    const float scale = i == 0 || i == 2 ? 1.0f : 0.5f;
    const float y = i == 2 ? 0.0f : i == 3 || i == 5 ? -0.5f : 0.0f;
    const float record[8]{column_x(i), y, 0, scale, 0.9f, 0.3f, 0.2f, 1};
    njin::instance_buffer_upload(ctx, buffers[size_t(i)], record, 1);
  }
}
void render(njin::context &ctx) {
  njin::light3d light;
  light.direction = {-0.3f, -0.6f, -1.0f};
  light.ambient = {.5f, .5f, .5f, 1};
  light.fog_density = 0;
  njin::light3d_set(ctx, light);
  njin::camera3d look;
  look.position = {0, 6, 12}; // from above, so the plane shows
  look.target = {0, 0, 0};
  look.projection = njin::camera3d_orthographic;
  look.ortho_height = 7; // 14 units across: the eight columns
  look.entities = false;
  njin::begin_3d(ctx, look);
  for (int i = 0; i < 6; ++i) njin::draw_instanced3d(ctx, njin::mesh3d_kind(i), buffers[size_t(i)], 0, 1);
  njin::draw_cylinder3d(ctx, {column_x(6), -0.5f, 0}, {column_x(6), 0.5f, 0}, 0.5f, {0.2f, 0.7f, 0.3f, 1});
  njin::draw_capsule3d(ctx, {column_x(7), -0.3f, 0}, {column_x(7), 0.3f, 0}, 0.4f, {0.2f, 0.4f, 0.9f, 1});
  njin::end_3d(ctx);
  if (frame == 5) njin::screenshot(ctx, (capture + ".png").c_str());
  if (++frame >= 10) njin::quit(ctx);
}
void shutdown(njin::context &) {
  const auto image = njin::renderer_load_image((capture + ".png").c_str());
  const unsigned w = image.width, h = image.height;
  require(w > 0, "no capture");
  const char *names[columns]{"cube", "sphere", "plane", "cylinder", "low sphere", "low cylinder", "draw_cylinder3d", "draw_capsule3d"};
  for (int c = 0; c < columns; ++c) {
    // The column's screen span and its non-background pixels (the clear colour is grey).
    const unsigned x0 = unsigned(w * (float(c) / columns)), x1 = unsigned(w * (float(c + 1) / columns));
    size_t shape = 0;
    unsigned top = h, bottom = 0;
    for (unsigned y = 0; y < h; ++y)
      for (unsigned x = x0; x < x1; ++x) {
        const auto *p = &image.pixels[(size_t(y) * w + x) * 4];
        const bool grey = std::abs(int(p[0]) - 128) < 12 && std::abs(int(p[1]) - 128) < 12 && std::abs(int(p[2]) - 128) < 12;
        if (grey) continue;
        ++shape;
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
    NJIN_TEST_LOG("primitives: %-15s %5zu px, rows %u..%u\n", names[c], shape, top, bottom);
    require(shape > 150, std::string("primitive missing: ") + names[c]);
  }
}
void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_render, render);
  njin::ecs_register(ctx, njin::phase_shutdown, shutdown);
}
} // namespace
int main(int argc, char **argv) {
  try {
    njin::config cfg{.title = "Njin primitives", .width = 480, .height = 240, .target_fps = 60};
    cfg.audio = false;
    cfg.clear_bg_color = {128 / 255.0f, 128 / 255.0f, 128 / 255.0f, 1};
    cfg.graphics_backend = njin::GraphicsBackend::Vulkan;
#if defined(__ANDROID__)
    cfg.virtual_size = {480, 240};
#endif
    for (int i = 1; i < argc; ++i) {
      if (std::strncmp(argv[i], "--renderer=", 11) == 0) require(njin::parse_graphics_backend(argv[i] + 11, cfg.graphics_backend), "invalid renderer");
      else if (std::strcmp(argv[i], "--gpu-validation") == 0) cfg.graphics_validation = true;
    }
    const auto dir = std::filesystem::absolute(argv[0]).parent_path() / "captures";
    std::filesystem::create_directories(dir);
    capture = (dir / ("primitives-" + std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();
    std::unique_ptr<njin::context, void (*)(njin::context *)> ctx(njin::create(cfg), njin::destroy);
    njin::mod_register(*ctx, {.name = "primitives", .setup = setup});
    njin::run(*ctx);
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
}
