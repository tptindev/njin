// light3d::ray_traced_shadows on the bgfx renderer (compute ray tracing): the traced
// sun shadow falls where the shadow map's does, softens with sun_angle, reaches past
// shadow_range, and the trace really runs (RendererResourceStats::ray_traces); a
// lamp's traced shadow falls where its shadow map's does; an unchanged scene reuses
// its BVH (ray_bvh_builds); shadow_lod traces a coarser level of a model; a smooth
// metal floor mirrors the cube with ray_traced_reflections; a ray traced irradiance
// grid (irradiance3d_desc::ray_traced) bounces a red wall's light onto the floor.
#include <njin.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(__ANDROID__)
#include <android/log.h> // stdout does not reach logcat on Android
#define NJIN_TEST_LOG(...) __android_log_print(ANDROID_LOG_INFO, "NjinBgfx", __VA_ARGS__)
#else
#define NJIN_TEST_LOG(...) std::printf(__VA_ARGS__)
#endif
namespace {
unsigned frame = 0;
std::string capture;
std::uint64_t traces_before = 0, traces_shadow_map = 0, traces_after = 0;
bool hardware = false, ray_query = true; // raytracing_hardware(); --no-ray-query forces compute
std::uint64_t builds_static_first = 0, builds_static_last = 0, builds_lod_before = 0, builds_lod_after = 0;
njin::model_handle ball{};
constexpr unsigned stages = 15;
void require(bool ok, const std::string &message) {
  if (!ok) throw std::runtime_error(message);
}
void render(njin::context &ctx) {
  const unsigned stage = frame / 4;
  njin::light3d light;
  light.direction = {-0.5f, -1.0f, -0.3f};
  light.ambient = {.25f, .25f, .28f, 1};
  light.shadows = stage != 0 && (stage < 6 || stage > 8);
  light.shadow_range = stage >= 4 && stage <= 5 ? 5.f : 12.f;
  // 6..8: no sun; a lamp lights the scene without shadows, with its shadow map, traced.
  if (stage >= 6 && stage <= 8) light.color = {0, 0, 0, 1};
  light.ray_traced_shadows = (stage >= 2 && stage <= 4) || stage == 8 || (stage >= 9 && stage <= 10);
  // 11, 12: a mirror floor without and with ray traced reflections.
  light.ray_traced_reflections = stage == 12;
  // 13, 14: a red wall beside the floor without and with a ray traced irradiance grid.
  if (stage >= 13) light.ambient = {.15f, .15f, .15f, 1};
  if (stage == 14 && frame % 4 == 0) {
    njin::irradiance3d_desc grid;
    grid.center = {-1, 1, 0};
    grid.size = {9, 2.5f, 9};
    grid.count_x = 5;
    grid.count_y = 2;
    grid.count_z = 5;
    grid.ray_traced = true;
    grid.rays = 256;
    grid.history = .5f;
    njin::irradiance3d_set(ctx, grid);
  }
  light.shadow_lod = stage == 10 ? 2 : 0;
  light.sun_angle = stage == 3 ? 8.f : 0.f;
  light.shadow_rays = stage == 3 ? 8 : 1;
  light.shadow_denoise = stage == 3 ? 3 : 0;
  njin::light3d_set(ctx, light);
  njin::camera3d look;
  look.position = {0, 7, 7};
  look.target = {0, 0, 0};
  look.fovy = 55;
  look.entities = false;
  njin::begin_3d(ctx, look);
  if (stage >= 6 && stage <= 8) {
    njin::light3d_source lamp;
    lamp.position = {2.5f, 3.5f, 1.5f};
    lamp.radius = 14;
    lamp.intensity = 2.5f;
    lamp.shadows = stage >= 7;
    njin::light3d_add(ctx, lamp);
  }
  njin::material3d plain;
  njin::material3d mirror;
  mirror.pbr = true;
  mirror.metallic = 1;
  mirror.roughness = .05f;
  njin::material3d_set(ctx, stage == 11 || stage == 12 ? mirror : plain);
  njin::draw_plane3d(ctx, {0, 0, 0}, {14, 14}, {.85f, .85f, .85f, 1});
  njin::material3d_set(ctx, plain);
  njin::draw_cube3d(ctx, {0, 1, 0}, {1.5f, 2, 1.5f}, {.8f, .3f, .2f, 1});
  // Off screen and outside the shadow map's range in stage 4: a tall pillar whose
  // shadow reaches into the view only for the traced rays.
  njin::draw_cube3d(ctx, {11, 9, 2}, {1, 18, 1}, {.3f, .3f, .9f, 1});
  if (stage >= 9 && stage <= 12) njin::draw_model(ctx, ball, njin::transform3d{.position = {-2.5f, 1.2f, 1.5f}}, {.9f, .9f, .3f, 1});
  if (stage >= 13) njin::draw_cube3d(ctx, {-4, 2, 0}, {.5f, 4, 9}, {.95f, .1f, .1f, 1});
  njin::end_3d(ctx);
  const auto builds = njin::renderer_resource_stats(ctx).ray_bvh_builds;
  if (frame % 4 == 1 && stage == 2) builds_static_first = builds;
  if (frame % 4 == 3 && stage == 3) builds_static_last = builds;
  if (frame % 4 == 3 && stage == 9) builds_lod_before = builds;
  if (frame % 4 == 3 && stage == 10) builds_lod_after = builds;
  if (frame % 4 == 1 && stage == 1) traces_before = njin::renderer_resource_stats(ctx).ray_traces;
  if (frame % 4 == 3 && stage == 1) traces_shadow_map = njin::renderer_resource_stats(ctx).ray_traces;
  if (frame % 4 == 3 && stage == 3) traces_after = njin::renderer_resource_stats(ctx).ray_traces;
  if (frame % 4 == 2) njin::screenshot(ctx, (capture + "." + std::to_string(stage) + ".png").c_str());
  if (++frame == stages * 4) {
    njin::irradiance3d_clear(ctx);
    njin::quit(ctx);
  }
}
bool available = false;
// A smooth sphere with simplified levels (model_lod_build) for shadow_lod.
void make_ball(njin::context &ctx) {
  std::vector<njin::vec3> positions;
  std::vector<njin::u32> indices;
  const int rings = 24, segments = 48;
  for (int r = 0; r <= rings; ++r)
    for (int s = 0; s <= segments; ++s) {
      const float a = 3.14159265f * r / rings, b = 6.2831853f * s / segments;
      positions.push_back({std::sin(a) * std::cos(b), std::cos(a), std::sin(a) * std::sin(b)});
    }
  for (int r = 0; r < rings; ++r)
    for (int s = 0; s < segments; ++s) {
      const njin::u32 i = njin::u32(r * (segments + 1) + s), j = i + segments + 1;
      indices.insert(indices.end(), {i, i + 1, j, i + 1, j + 1, j});
    }
  njin::mesh3d_data mesh;
  mesh.positions = positions.data();
  mesh.vertex_count = njin::u32(positions.size());
  mesh.indices = indices.data();
  mesh.index_count = njin::u32(indices.size());
  ball = njin::model_create(ctx, mesh);
  require(njin::model_lod_build(ctx, ball, {.levels = 2, .ratio = .3f, .max_error = .1f, .screen = .01f}) >= 1, "the sphere has no simplified level");
}
void startup(njin::context &ctx) {
  make_ball(ctx);
  // Compute ray tracing needs a bgfx backend with compute shaders (not the GLES 3.0
  // context bgfx makes on Android); without it the shadow maps are used.
  available = njin::raytracing_available(ctx);
  NJIN_TEST_LOG("compute ray tracing %s\n", available ? "available" : "unavailable: shadow maps used");
  hardware = njin::raytracing_hardware(ctx);
  NJIN_TEST_LOG("ray tracing hardware (ray queries) %s\n", hardware ? "used" : "not used");
  require(ray_query || !hardware, "config::graphics_ray_query off still used ray queries");
}
void shutdown(njin::context &ctx) {
  const auto queries = njin::renderer_resource_stats(ctx).ray_query_dispatches;
  NJIN_TEST_LOG("ray query dispatches: %llu\n", (unsigned long long)queries);
  require(hardware ? queries > 0 : queries == 0, hardware ? "ray queries available but no dispatch used them" : "ray query dispatches without ray queries");
  require(traces_shadow_map == traces_before, "shadow-map frames ran ray tracing");
  if (!available) {
    require(traces_after == traces_before, "ray tracing dispatched without compute support");
    return;
  }
  require(traces_after > traces_shadow_map, "ray traced frames did not dispatch a trace");
  NJIN_TEST_LOG("BVH builds: static scene %llu -> %llu, shadow_lod %llu -> %llu\n", (unsigned long long)builds_static_first,
                (unsigned long long)builds_static_last, (unsigned long long)builds_lod_before, (unsigned long long)builds_lod_after);
  require(builds_static_last == builds_static_first, "an unchanged scene rebuilt its BVH");
  require(builds_lod_after > builds_lod_before, "shadow_lod did not change the traced casters");
  const auto load = [](unsigned stage) { return njin::renderer_load_image((capture + "." + std::to_string(stage) + ".png").c_str()); };
  const auto lit = load(0), mapped = load(1), traced = load(2), soft = load(3), far = load(4), short_map = load(5);
  // Shadowed: darker than the unshadowed frame by more than 25 (red channel).
  const auto shadowed = [&](const njin::RenderImageAsset &image, size_t p) { return int(lit.pixels[p]) - int(image.pixels[p]) > 25; };
  size_t both = 0, either = 0, penumbra_hard = 0, penumbra_soft = 0;
  for (size_t p = 0; p < lit.pixels.size(); p += 4) {
    const bool a = shadowed(mapped, p), b = shadowed(traced, p);
    both += a && b;
    either += a || b;
    const auto partial = [&](const njin::RenderImageAsset &image) {
      const int d = int(lit.pixels[p]) - int(image.pixels[p]);
      return d > 6 && d < 40;
    };
    penumbra_hard += partial(traced);
    penumbra_soft += partial(soft);
  }
  const double overlap = either ? double(both) / double(either) : 0;
  NJIN_TEST_LOG("ray traced shadow: overlap with shadow map %.3f (%zu px), penumbra hard %zu soft %zu\n", overlap, either, penumbra_hard, penumbra_soft);
  require(either > 1500, "no shadow found");
  require(overlap > 0.75, "ray traced shadow does not fall where the shadow map's does");
  require(penumbra_soft > penumbra_hard * 2 + 200, "sun_angle did not soften the ray traced shadow");
  // The pillar's shadow with a 5 m shadow range: traced (stage 4), not mapped (stage 5).
  size_t pillar = 0;
  float left = 0, top = 0, w = float(lit.width), h = float(lit.height);
#if defined(__ANDROID__)
  // The 320x240 virtual screen, fitted (integer scale) and centred on the device.
  const float fit = std::floor(std::min(w / 320, h / 240));
  left = (w - 320 * fit) * .5f; top = (h - 240 * fit) * .5f; w = 320 * fit; h = 240 * fit;
#endif
  for (unsigned y = unsigned(top + h / 4); y < unsigned(top + h * 3 / 5); ++y)
    for (unsigned x = unsigned(left + w * 3 / 5); x < unsigned(left + w); ++x) {
      const size_t p = (size_t(y) * lit.width + x) * 4;
      pillar += int(short_map.pixels[p]) - int(far.pixels[p]) > 25;
    }
  NJIN_TEST_LOG("shadow from beyond shadow_range: %zu px\n", pillar);
  require(pillar > 300, "ray traced shadow did not reach past shadow_range");
  // The lamp: its traced shadow falls where its shadow map's does.
  const auto lamp_lit = load(6), lamp_mapped = load(7), lamp_traced = load(8);
  size_t lamp_both = 0, lamp_either = 0;
  for (size_t p = 0; p < lamp_lit.pixels.size(); p += 4) {
    const bool a = int(lamp_lit.pixels[p]) - int(lamp_mapped.pixels[p]) > 25, b = int(lamp_lit.pixels[p]) - int(lamp_traced.pixels[p]) > 25;
    lamp_both += a && b;
    lamp_either += a || b;
  }
  const double lamp_overlap = lamp_either ? double(lamp_both) / double(lamp_either) : 0;
  NJIN_TEST_LOG("lamp ray traced shadow: overlap with shadow map %.3f (%zu px)\n", lamp_overlap, lamp_either);
  require(lamp_either > 1000, "no lamp shadow found");
  require(lamp_overlap > 0.7, "the lamp's ray traced shadow does not fall where its shadow map's does");
  // shadow_lod: the sphere's own traced shadow (not in stage 2, the same scene and
  // rays without it) from its simplified level differs at the edge but stays the same.
  const auto full = load(9), coarse = load(10);
  size_t differ = 0, ball_both = 0, ball_either = 0, ball_full = 0, ball_coarse = 0;
  for (size_t p = 0; p < lit.pixels.size(); p += 4) {
    if (shadowed(traced, p)) continue;
    const bool a = shadowed(full, p), b = shadowed(coarse, p);
    differ += a != b;
    ball_both += a && b;
    ball_either += a || b;
    ball_full += a;
    ball_coarse += b;
  }
  NJIN_TEST_LOG("shadow_lod: sphere shadow %zu px full, %zu px coarse\n", ball_full, ball_coarse);
  require(ball_full > 300 && ball_coarse > 300, "the sphere casts no traced shadow");
  // Reflections: the mirror floor shows the red cube (red rises over green and blue).
  const auto plain_floor = load(11), mirrored = load(12);
  size_t red = 0, changed = 0;
  for (size_t p = 0; p < plain_floor.pixels.size(); p += 4) {
    const int dr = int(mirrored.pixels[p]) - int(plain_floor.pixels[p]);
    changed += std::abs(dr) > 12;
    red += dr > 30 && int(mirrored.pixels[p]) > int(mirrored.pixels[p + 1]) + 40;
  }
  NJIN_TEST_LOG("ray traced reflections: %zu px changed, %zu px of mirrored cube\n", changed, red);
  require(red > 300, "the mirror floor does not reflect the cube");
  // The grid: the floor beside the red wall turns redder (red over green rises).
  const auto no_grid = load(13), with_grid = load(14);
  size_t redder = 0, greener = 0;
  for (size_t p = 0; p < no_grid.pixels.size(); p += 4) {
    const int before = int(no_grid.pixels[p]) - int(no_grid.pixels[p + 1]), after = int(with_grid.pixels[p]) - int(with_grid.pixels[p + 1]);
    redder += after - before > 6;
    greener += before - after > 6;
  }
  NJIN_TEST_LOG("ray traced irradiance grid: %zu px redder, %zu px greener\n", redder, greener);
  require(redder > 1000 && redder > greener * 4, "the irradiance grid did not bounce the red wall's light");
  NJIN_TEST_LOG("shadow_lod: %zu px differ, overlap %.3f\n", differ, ball_either ? double(ball_both) / double(ball_either) : 0.0);
  require(differ > 20, "shadow_lod traced the same casters");
  require(ball_either && double(ball_both) / double(ball_either) > 0.8, "shadow_lod changed the shadow too much");
}
void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_render, render);
  njin::ecs_register(ctx, njin::phase_shutdown, shutdown);
}
} // namespace
int main(int argc, char **argv) {
  try {
    njin::config cfg{.title = "Njin ray traced shadows", .width = 320, .height = 240, .target_fps = 60};
    cfg.audio = false;
    cfg.clear_bg_color = {0.1f, 0.1f, 0.12f, 1};
    cfg.graphics_backend = njin::GraphicsBackend::Vulkan;
#if defined(__ANDROID__)
    cfg.virtual_size = {320, 240}; // fixed regions; the device's screen letterboxes it
#endif
    for (int i = 1; i < argc; ++i) {
      if (std::strncmp(argv[i], "--renderer=", 11) == 0) require(njin::parse_graphics_backend(argv[i] + 11, cfg.graphics_backend), "invalid renderer");
      else if (std::strcmp(argv[i], "--gpu-validation") == 0) cfg.graphics_validation = true;
      else if (std::strcmp(argv[i], "--no-ray-query") == 0) cfg.graphics_ray_query = ray_query = false;
    }
    const auto dir = std::filesystem::absolute(argv[0]).parent_path() / "captures";
    std::filesystem::create_directories(dir);
    capture = (dir / ("raytrace-" + std::string(njin::graphics_backend_name(cfg.graphics_backend)) + (ray_query ? "" : "-compute"))).string();
    std::unique_ptr<njin::context, void (*)(njin::context *)> ctx(njin::create(cfg), njin::destroy);
    njin::mod_register(*ctx, {.name = "ray traced shadows", .setup = setup});
    njin::run(*ctx);
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }
}
