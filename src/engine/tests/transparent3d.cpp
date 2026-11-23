// Transparent 3D draws without post3d.oit: a half transparent red cube in front of an
// opaque blue one is alpha blended over it (both colours show where they overlap), on
// every renderer, instead of the bgfx renderer refusing the draw.
#include <njin.h>
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
unsigned frame = 0;
std::string capture;
void require(bool ok, const std::string &message) {
  if (!ok) throw std::runtime_error(message);
}
void render(njin::context &ctx) {
  njin::light3d light;
  light.direction = {-0.3f, -1.0f, -0.5f};
  light.ambient = {.6f, .6f, .6f, 1};
  njin::light3d_set(ctx, light);
  njin::camera3d look;
  look.position = {0, 1.5f, 6};
  look.target = {0, 0.5f, 0};
  look.fovy = 45;
  look.entities = false;
  njin::begin_3d(ctx, look);
  njin::draw_cube3d(ctx, {0.6f, 0.5f, -1.5f}, {2, 2, 2}, {0.1f, 0.2f, 1, 1});  // opaque, behind
  njin::draw_cube3d(ctx, {-0.6f, 0.5f, 0.5f}, {2, 2, 2}, {1, 0.1f, 0.1f, 0.5f}); // half transparent, in front
  njin::end_3d(ctx);
  if (frame == 5) njin::screenshot(ctx, (capture + ".png").c_str());
  if (++frame >= 10) njin::quit(ctx);
}
void shutdown(njin::context &) {
  const auto image = njin::renderer_load_image((capture + ".png").c_str());
  require(image.width > 0, "no capture");
  // Pixels that are both red and blue: the front cube blended over the back one.
  size_t blended = 0, red = 0;
  for (size_t p = 0; p + 3 < image.pixels.size(); p += 4) {
    const int r = image.pixels[p], g = image.pixels[p + 1], b = image.pixels[p + 2];
    if (r > 60 && b > 60 && g < r && g < b) ++blended;
    if (r > 200 && g > 80 && g < 180 && b > 80 && b < 180) ++red; // pink: red over the light grey
  }
  NJIN_TEST_LOG("transparent: %zu px blended red over blue, %zu px red over the background\n", blended, red);
  require(blended > 2000, "the transparent cube is not blended over the opaque one");
  require(red > 1000, "the transparent cube is missing");
}
void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_render, render);
  njin::ecs_register(ctx, njin::phase_shutdown, shutdown);
}
} // namespace
int main(int argc, char **argv) {
  try {
    njin::config cfg{.title = "Njin transparent 3D", .width = 320, .height = 240, .target_fps = 60};
    cfg.audio = false;
    cfg.clear_bg_color = {0.9f, 0.9f, 0.9f, 1};
    cfg.graphics_backend = njin::GraphicsBackend::Vulkan;
#if defined(__ANDROID__)
    cfg.virtual_size = {320, 240};
#endif
    for (int i = 1; i < argc; ++i) {
      if (std::strncmp(argv[i], "--renderer=", 11) == 0) require(njin::parse_graphics_backend(argv[i] + 11, cfg.graphics_backend), "invalid renderer");
      else if (std::strcmp(argv[i], "--gpu-validation") == 0) cfg.graphics_validation = true;
    }
    const auto dir = std::filesystem::absolute(argv[0]).parent_path() / "captures";
    std::filesystem::create_directories(dir);
    capture = (dir / ("transparent-" + std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();
    std::unique_ptr<njin::context, void (*)(njin::context *)> ctx(njin::create(cfg), njin::destroy);
    njin::mod_register(*ctx, {.name = "transparent 3D", .setup = setup});
    njin::run(*ctx);
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
}
