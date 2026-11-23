// The open sea (water3d_desc::size {0, 0}) reaches the horizon on every renderer: seen
// from 4 m up, the water covers the view below the horizon, including the band beyond
// 80 m that the old 160 m bgfx patch left empty; far, coarse rings show no aliasing
// (each wave fades where the rings are too coarse for it), measured as the mean change
// between neighbouring pixels along that band.
#include <njin.h>
#include <cmath>
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
njin::water3d_handle sea;
njin::render_texture_handle android_target;
void require(bool ok, const std::string &message) {
  if (!ok) throw std::runtime_error(message);
}
void startup(njin::context &ctx) {
#if defined(__ANDROID__)
  android_target=njin::render_texture_load(ctx,320,240);
  require(android_target.id!=0,"ocean target creation failed");
#endif
  njin::water3d_desc d;
  d.size = {0, 0}; // the open sea
  d.level = 0;
  sea = njin::water3d_create(ctx, d);
  njin::post3d p;
  p.oit = true; // transparent water on bgfx
  njin::post3d_set(ctx, p);
}
void render(njin::context &ctx) {
  njin::light3d light;
  light.direction = {-0.3f, -1.0f, -0.4f};
  light.ambient = {.3f, .3f, .35f, 1};
  light.fog_density = 0;
  njin::light3d_set(ctx, light);
  njin::camera3d look;
  look.position = {0, 4, 0};
  look.target = {0, 3.2f, -50}; // a little below the horizon
  look.fovy = 50;
  look.far_plane = 9000;
  look.entities = false;
  if(android_target.id) njin::begin_3d(ctx,look,android_target,{1,0,1,1});
  else njin::begin_3d(ctx, look);
  njin::draw_water3d(ctx, sea);
  njin::end_3d(ctx);
  if(android_target.id) njin::render_texture_draw(ctx,android_target,{},{1,1,1,1});
  if (frame == 6) {
    if(android_target.id) require(njin::render_texture_save(ctx,android_target,(capture+".png").c_str()),"ocean target capture failed");
    else njin::screenshot(ctx, (capture + ".png").c_str());
  }
  if (++frame >= 10) njin::quit(ctx);
}
bool background(const unsigned char *p) { return p[0] > 200 && p[1] < 60 && p[2] > 200; }
void shutdown(njin::context &ctx) {
  const auto image = njin::renderer_load_image((capture + ".png").c_str());
  const unsigned w = image.width, h = image.height;
  require(w > 0 && h > 0, "no capture");
  const auto at = [&](unsigned x, unsigned y) { return &image.pixels[(size_t(y) * w + x) * 4]; };
  // The horizon is 0.9 degrees above the centre; 80 m away is 2 degrees below it.
  const double per_degree = double(h) / 50.0;
  const unsigned horizon = unsigned(double(h) / 2 - 0.92 * per_degree);
  const unsigned beyond80 = unsigned(double(h) / 2 + 1.94 * per_degree);
  size_t far_water = 0, far_total = 0, near_water = 0, near_total = 0;
  double gradient = 0;
  for (unsigned y = horizon + 2; y < beyond80; ++y)
    for (unsigned x = 0; x < w; ++x) {
      ++far_total;
      if (!background(at(x, y))) ++far_water;
      if (x + 1 < w)
        for (int c = 0; c < 3; ++c) gradient += std::abs(int(at(x, y)[c]) - int(at(x + 1, y)[c]));
    }
  for (unsigned y = beyond80; y < h; ++y)
    for (unsigned x = 0; x < w; ++x) {
      ++near_total;
      if (!background(at(x, y))) ++near_water;
    }
  const double far_share = double(far_water) / double(far_total), near_share = double(near_water) / double(near_total);
  gradient /= double(far_total) * 3;
  NJIN_TEST_LOG("ocean: water beyond 80 m %.3f, nearer %.3f, far band neighbour change %.2f\n", far_share, near_share, gradient);
  require(near_share > 0.99, "the sea does not cover the near view");
  require(far_share > 0.95, "the sea does not reach the horizon");
  require(gradient < 12, "the far sea aliases");
  if(android_target.id) njin::render_texture_unload(ctx,android_target);
}
void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_render, render);
  njin::ecs_register(ctx, njin::phase_shutdown, shutdown);
}
} // namespace
int main(int argc, char **argv) {
  try {
    njin::config cfg{.title = "Njin open sea", .width = 320, .height = 240, .target_fps = 60};
    cfg.audio = false;
    cfg.clear_bg_color = {1, 0, 1, 1};
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
    capture = (dir / ("ocean-" + std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();
    std::unique_ptr<njin::context, void (*)(njin::context *)> ctx(njin::create(cfg), njin::destroy);
    njin::mod_register(*ctx, {.name = "open sea", .setup = setup});
    njin::run(*ctx);
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
}
