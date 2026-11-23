// draw_sky3d() parity: the legacy OpenGL renderer writes reference captures (overcast
// day, cloudy sunset, starry night, each lighting a cube through drive_light; then rain
// under a roof found by cover_auto, soft snow, and shaped sparkling snow beside a
// weather3d_cover_set box); every bgfx backend draws the same frames and must match
// them closely. Time is paused so the drops stand still at the same places. Stages 6/7
// draw a terrain dry and wet beside a cover box: the wet/dry brightness ratio in the open
// and under the box must match the legacy renderer's. Stages 8/9 rain on flat water
// without and with water3d_desc::rain_rings: the rings' difference image must correlate
// with the legacy one and be about as strong.
#include <njin.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <array>
#include <vector>
namespace {
unsigned frame = 0;
std::string capture, reference;
constexpr unsigned stages = 10, strict_stages = 6;
njin::texture_handle flakes, ground_albedo;
njin::terrain3d_handle ground;
njin::water3d_handle pond;
njin::water3d_desc pond_desc;
void require(bool ok, const std::string &message) {
  if (!ok) throw std::runtime_error(message);
}
void render(njin::context &ctx) {
  const unsigned stage = frame / 4;
  njin::light3d light;
  light.hdr = true;
  light.tonemap = njin::tonemap3d_linear;
  njin::light3d_set(ctx, light);
  njin::post3d post;
  post.oit = stage >= 8; // water is a transparent draw (bgfx needs OIT for it)
  njin::post3d_set(ctx, post);
  njin::camera3d look;
  look.position = {0, 1, 0};
  look.target = {0, 1.6f, 10};
  look.fovy = 60;
  look.entities = false;
  njin::begin_3d(ctx, look);
  njin::sky3d sky;
  if (stage == 0) { sky.hour = 10; sky.weather.clouds = 1; sky.weather.cloud_darkness = .5f; }
  if (stage == 1) { sky.hour = 18.6f; sky.weather.clouds = .5f; }
  if (stage == 2) { sky.hour = 0; sky.weather.clouds = .2f; }
  if (stage >= 3) { sky.hour = 11; sky.weather.clouds = .6f; sky.weather.wind = {3, 1}; }
  if (stage == 3) sky.weather.rain = 1;
  if (stage == 4 || stage == 5) sky.weather.snow = 1;
  if (stage == 7) sky.weather.wetness = .9f;
  if (stage >= 8) sky.weather.rain = 1;
  sky.weather.cover_auto = stage == 3;
  if (stage >= 8) {
    njin::weather3d_cover_set(ctx, nullptr, 0);
    njin::weather3d_snow_flakes_set(ctx, {}, 1, 1);
  } else if (stage >= 6) {
    const njin::weather3d_cover box{.center = {3, 1, 8}, .size = {6, 4, 10}};
    njin::weather3d_cover_set(ctx, &box, 1);
    njin::weather3d_snow_flakes_set(ctx, {}, 1, 1);
  } else if (stage == 5) {
    const njin::weather3d_cover box{.center = {1.5f, 1, 6}, .size = {3, 4, 3}};
    njin::weather3d_cover_set(ctx, &box, 1);
    njin::weather3d_snow_flakes_set(ctx, flakes, 2, 1, 2);
  } else {
    njin::weather3d_cover_set(ctx, nullptr, 0);
    njin::weather3d_snow_flakes_set(ctx, {}, 1, 1);
  }
  njin::draw_sky3d(ctx, sky);
  njin::material3d plain;
  njin::material3d_set(ctx, plain);
  njin::draw_cube3d(ctx, {0, 0.4f, 5}, {1.2f, 1.2f, 1.2f}, {.8f, .8f, .8f, 1});
  // A roof over the left of the view: cover_auto keeps the rain off what is under it.
  if (stage == 3) njin::draw_cube3d(ctx, {-3, 3.5f, 6}, {6, .3f, 8}, {.5f, .45f, .4f, 1});
  if (stage == 6 || stage == 7) njin::draw_terrain3d(ctx, ground);
  if (stage >= 8) {
    pond_desc.rain_rings = stage == 9 ? 1.f : 0.f;
    njin::water3d_set(ctx, pond, pond_desc);
    njin::draw_water3d(ctx, pond);
  }
  njin::end_3d(ctx);
  if (frame % 4 == 2) njin::screenshot(ctx, (capture + "." + std::to_string(stage) + ".png").c_str());
  if (++frame == stages * 4) njin::quit(ctx);
}
void shutdown(njin::context &) {
  if (reference.empty()) return;
  for (unsigned stage = 0; stage < strict_stages; ++stage) {
    const auto suffix = "." + std::to_string(stage) + ".png";
    require(std::filesystem::exists(reference + suffix), "missing legacy reference " + reference + suffix);
    const auto a = njin::renderer_load_image((reference + suffix).c_str()), b = njin::renderer_load_image((capture + suffix).c_str());
    require(a.width == b.width && a.height == b.height, "sky capture sizes differ");
    double sum = 0;
    size_t large = 0, channels = 0;
    for (size_t p = 0; p < a.pixels.size(); p += 4)
      for (int c = 0; c < 3; ++c, ++channels) {
        const int d = std::abs(int(a.pixels[p + c]) - int(b.pixels[p + c]));
        sum += d;
        large += d > 12;
      }
    const double mean = sum / double(channels), share = double(large) / double(channels);
    std::printf("sky parity stage %u: mean %.2f, >12 %.3f%%\n", stage, mean, share * 100);
    require(mean <= 3.0 && share <= 0.005, "draw_sky3d differs from the legacy renderer at stage " + std::to_string(stage));
  }
  // Wet terrain: brightness of a 9x9 patch, wet over dry, in the open (screen right)
  // and under the cover box (screen left); the box keeps the ground dry.
  const auto patch = [](const njin::RenderImageAsset &image, float x, float y) {
    double sum = 0;
    const int cx = int(x * image.width), cy = int(y * image.height);
    for (int dy = -4; dy <= 4; ++dy)
      for (int dx = -4; dx <= 4; ++dx) {
        const auto *p = &image.pixels[(size_t(cy + dy) * image.width + size_t(cx + dx)) * 4];
        sum += p[0] + p[1] + p[2];
      }
    return sum;
  };
  const auto ratios = [&](const std::string &base) {
    const auto dry = njin::renderer_load_image((base + ".6.png").c_str()), wet = njin::renderer_load_image((base + ".7.png").c_str());
    return std::array<double, 2>{patch(wet, .75f, .75f) / patch(dry, .75f, .75f), patch(wet, .25f, .75f) / patch(dry, .25f, .75f)};
  };
  const auto want = ratios(reference), got = ratios(capture);
  std::printf("wet terrain ratios: open %.3f (legacy %.3f), covered %.3f (legacy %.3f)\n", got[0], want[0], got[1], want[1]);
  require(want[0] < 0.9 && std::abs(want[1] - 1) < 0.05, "legacy reference does not show wet open ground and dry covered ground");
  require(std::abs(got[0] - want[0]) < 0.05 && std::abs(got[1] - want[1]) < 0.05, "wet terrain differs from the legacy renderer");
  // Rain rings: luminance change from stage 8 to 9 in the lower half (the water).
  const auto rings = [](const std::string &base) {
    const auto a = njin::renderer_load_image((base + ".8.png").c_str()), b = njin::renderer_load_image((base + ".9.png").c_str());
    std::vector<double> d;
    for (unsigned y = a.height / 2; y < a.height; ++y)
      for (unsigned x = 0; x < a.width; ++x) {
        const auto *p = &a.pixels[(size_t(y) * a.width + x) * 4], *q = &b.pixels[(size_t(y) * a.width + x) * 4];
        d.push_back(double(q[0] + q[1] + q[2]) - double(p[0] + p[1] + p[2]));
      }
    return d;
  };
  const auto ra = rings(reference), rb = rings(capture);
  double ma = 0, mb = 0, sa = 0, sb = 0, ab = 0;
  for (size_t i = 0; i < ra.size(); ++i) { ma += std::abs(ra[i]); mb += std::abs(rb[i]); }
  for (size_t i = 0; i < ra.size(); ++i) { sa += ra[i] * ra[i]; sb += rb[i] * rb[i]; ab += ra[i] * rb[i]; }
  const double correlation = ab / std::sqrt(std::max(sa * sb, 1e-9)), strength = mb / std::max(ma, 1e-9);
  std::printf("rain rings: legacy mean change %.2f, correlation %.3f, strength ratio %.3f\n", ma / ra.size(), correlation, strength);
  require(ma / ra.size() > 0.5, "legacy reference shows no rain rings");
  // Drop placement follows a float hash that SPIR-V/fxc and GL compilers round differently
  // (Vulkan/D3D12 agree with each other, not exactly with GL), and the water body is not
  // yet the legacy water model: require rings about as strong, at mostly the same places.
  require(correlation > 0.35 && strength > 0.6 && strength < 1.6, "water rain rings differ from the legacy renderer");
}
void startup(njin::context &ctx) {
  njin::time_set_paused(ctx, true); // world clock (and so every drop) stands still
  // Two 8x8 flake shapes side by side: a cross and a ring, alpha only.
  unsigned char pixels[16 * 8 * 4]{};
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 16; ++x) {
      const int cx = x % 8, cell = x / 8;
      const float dx = cx - 3.5f, dy = y - 3.5f, r = std::sqrt(dx * dx + dy * dy);
      const bool on = cell == 0 ? (std::abs(dx) < 1 || std::abs(dy) < 1) : (r > 2 && r < 3.5f);
      unsigned char *p = pixels + (y * 16 + x) * 4;
      p[0] = p[1] = p[2] = 255;
      p[3] = on ? 255 : 0;
    }
  flakes = njin::texture_from_pixels(ctx, pixels, 16, 8);
  const unsigned char grey[]{150, 140, 120, 255};
  ground_albedo = njin::texture_from_pixels(ctx, grey, 1, 1);
  static std::vector<float> heights(9 * 9, 0.0f);
  njin::terrain3d_desc t;
  t.origin = {-20, 0, -4};
  t.size = 40;
  t.resolution = 9;
  t.heights = heights.data();
  t.collision = false;
  t.layer_count = 1;
  t.layers[0].albedo = ground_albedo;
  t.layers[0].tile = 4;
  ground = njin::terrain3d_create(ctx, t);
  require(ground.id != 0, "terrain creation failed");
  pond_desc.level = 0.2f;
  pond_desc.center = {0, 10};
  pond_desc.size = {40, 30};
  pond_desc.wave_count = 0; // flat: only the rings move the surface
  pond = njin::water3d_create(ctx, pond_desc);
  require(pond.id != 0, "water creation failed");
}
void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_render, render);
  njin::ecs_register(ctx, njin::phase_shutdown, shutdown);
}
} // namespace
int main(int argc, char **argv) {
  try {
    njin::config cfg{.title = "Njin sky parity", .width = 320, .height = 240, .target_fps = 60};
    cfg.audio = false;
    cfg.graphics_backend = njin::GraphicsBackend::OpenGL;
    for (int i = 1; i < argc; ++i)
      if (std::strncmp(argv[i], "--renderer=", 11) == 0) require(njin::parse_graphics_backend(argv[i] + 11, cfg.graphics_backend), "invalid renderer");
    const auto dir = std::filesystem::absolute(argv[0]).parent_path() / "captures";
    std::filesystem::create_directories(dir);
    capture = (dir / ("sky-parity-" + std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();
    if (cfg.graphics_backend != njin::GraphicsBackend::OpenGL)
      reference = (dir / ("sky-parity-" + std::string(njin::graphics_backend_name(njin::GraphicsBackend::OpenGL)))).string();
    std::unique_ptr<njin::context, void (*)(njin::context *)> ctx(njin::create(cfg), njin::destroy);
    njin::mod_register(*ctx, {.name = "sky parity", .setup = setup});
    njin::run(*ctx);
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
}
