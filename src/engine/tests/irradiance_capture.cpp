// irradiance3d_set without ray tracing: the grid captured from the scene (legacy
// capture_probes on OpenGL, portable_grid_capture_step on bgfx). A red wall lit by the sun
// stands beside a white block whose front face (+z, towards the camera) the sun does not
// reach: with the grid that face takes the red the wall throws, without it only the grey
// ambient. Checks, on
// one renderer, that the grid tints the face red (and nothing when cleared); and, with
// `legacy-vs <renderer>`, that legacy OpenGL and <renderer> give that face the same colour.
//   njin_irradiance_capture_test [renderer] | legacy-vs <renderer>
#include "njin.h"
#include "njin_3d.h"
#include "njin_renderer.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

namespace {
int failures = 0;
void expect(bool ok, const std::string &what) {
  std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok)
    ++failures;
}
std::string g_shot;
int g_frame = 0;
bool g_grid = true;

void render(njin::context &ctx) {
  njin::begin_3d(ctx, njin::camera3d{.position = {3.5f, 2.0f, 5.0f}, .target = {0.0f, 0.6f, 0.0f}, .fovy = 45.0f});
  njin::draw_plane3d(ctx, {0, 0, 0}, {12, 12}, njin::rgba{0.5f, 0.5f, 0.5f, 1});
  njin::draw_cube3d(ctx, {-2.0f, 1.0f, 0.0f}, {0.2f, 2.0f, 4.0f}, njin::rgba{0.9f, 0.1f, 0.1f, 1}); // the red wall
  njin::draw_cube3d(ctx, {0.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, njin::rgba{1, 1, 1, 1});        // the white block
  njin::end_3d(ctx);
}
void step(njin::context &ctx) {
  if (++g_frame == 40)
    njin::screenshot(ctx, g_shot.c_str());
  if (g_frame == 44)
    njin::quit(ctx);
}

struct colour {
  int r = 0, g = 0, b = 0;
  bool ok = false;
};

njin::GraphicsBackend backend_of(const std::string &r) {
  return r == "opengl"        ? njin::GraphicsBackend::OpenGL
         : r == "vulkan"      ? njin::GraphicsBackend::Vulkan
         : r == "d3d12"       ? njin::GraphicsBackend::Direct3D12
         : r == "bgfx-opengl" ? njin::GraphicsBackend::BgfxOpenGL
                              : njin::GraphicsBackend::Auto;
}

// The block's front face (+z), left part, nearer the wall: its mean colour.
colour run_scene(njin::GraphicsBackend backend, bool grid) {
  g_frame = 0;
  g_grid = grid;
  std::filesystem::remove(g_shot);
  njin::config cfg{.title = "njin irradiance capture", .width = 640.0f, .height = 360.0f, .target_fps = 60.0f};
  cfg.clear_bg_color = njin::rgba{0, 0, 0, 1};
  cfg.audio = false;
  cfg.graphics_backend = backend;
  njin::context *ctx = njin::create(cfg);
  njin::light3d light = njin::light3d_get(*ctx);
  light.direction = {-0.8f, -1.0f, 0.2f}; // from +x: lights the wall's +x face, not the block's -x face
  light.ambient = njin::rgba{0.15f, 0.15f, 0.15f, 1};
  light.shadows = false;
  njin::light3d_set(*ctx, light);
  if (grid) {
    njin::irradiance3d_desc g{};
    g.center = {-0.5f, 0.6f, 0.0f};
    g.size = {2.4f, 1.0f, 3.0f};
    g.count_x = 2;
    g.count_y = 1;
    g.count_z = 2;
    g.blend = 0.3f;
    njin::irradiance3d_set(*ctx, g);
  }
  njin::mod_register(*ctx, njin::mod_desc{"irradiance_capture_test", [](njin::context &c) {
                                            njin::ecs_register(c, njin::phase_render, render, "render");
                                            njin::ecs_register(c, njin::phase_update, step, "step");
                                          }});
  njin::run(*ctx);
  njin::destroy(ctx);
  colour out;
  const njin::RenderImageAsset img = njin::renderer_load_image(g_shot.c_str());
  if (img.width != 640 || img.height != 360)
    return out;
  // A small patch on the block's front face, near its left edge.
  long r = 0, g = 0, b = 0, n = 0;
  for (int y = 205; y < 215; ++y)
    for (int x = 275; x < 285; ++x) {
      const unsigned char *p = img.pixels.data() + ((size_t)y * img.width + (size_t)x) * 4;
      r += p[0], g += p[1], b += p[2], ++n;
    }
  out = {int(r / n), int(g / n), int(b / n), true};
  std::printf("      %s grid: face %d %d %d\n", grid ? "with" : "without", out.r, out.g, out.b);
  return out;
}
} // namespace

int main(int argc, char **argv) {
  g_shot = (std::filesystem::temp_directory_path() / "njin-irradiance-capture.png").generic_string();
  if (argc > 2 && std::string(argv[1]) == "legacy-vs") {
    const colour legacy = run_scene(njin::GraphicsBackend::OpenGL, true);
    const colour other = run_scene(backend_of(argv[2]), true);
    expect(legacy.ok && other.ok, "both renderers took a screenshot");
    expect(std::abs(legacy.r - other.r) <= 12 && std::abs(legacy.g - other.g) <= 12 && std::abs(legacy.b - other.b) <= 12,
           std::string("the captured grid lights the face the same on legacy OpenGL and ") + argv[2]);
    std::printf("irradiance_capture: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
  }
  const njin::GraphicsBackend backend = backend_of(argc > 1 ? argv[1] : "");
  const colour with = run_scene(backend, true), without = run_scene(backend, false);
  expect(with.ok && without.ok, "screenshots");
  expect(with.r > with.g + 8, "the face near the red wall takes its red from the grid");
  expect(std::abs(without.r - without.g) <= 6, "without the grid it has only the grey ambient");
  std::printf("irradiance_capture: %d failures\n", failures);
  return failures == 0 ? 0 : 1;
}
