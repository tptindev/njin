// light3d::hdr on the SDL3/bgfx renderer keeps what was drawn before the 3D pass, as the
// legacy renderer does (render3d_hdr.cpp copies the target into the HDR image): a red 2D
// rectangle drawn before begin_3d still shows, tone mapped, beside the 3D cube, where the
// HDR image used to start from the clear colour and covered it.
//   njin_hdr_keep2d_test [renderer]
#include "njin.h"
#include "njin_3d.h"
#include "njin_draw_shapes.h"
#include "njin_renderer.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {
int failures = 0;
void expect(bool ok, const char *what) {
  std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok)
    ++failures;
}
std::string g_shot;
int g_frame = 0;

void render(njin::context &ctx) {
  njin::draw_rect(ctx, {{20, 20}, {160, 120}}, njin::rgba{1, 0, 0, 1});
  njin::begin_3d(ctx, njin::camera3d{.position = {0, 1.5f, 4}, .target = {0, 0, 0}});
  njin::draw_cube3d(ctx, {0, 0, 0}, {1, 1, 1}, njin::rgba{0.2f, 0.4f, 1.0f, 1});
  njin::end_3d(ctx);
}
void step(njin::context &ctx) {
  if (++g_frame == 10)
    njin::screenshot(ctx, g_shot.c_str());
  if (g_frame == 14)
    njin::quit(ctx);
}
const unsigned char *pixel(const njin::RenderImageAsset &img, int x, int y) {
  return img.pixels.data() + ((size_t)y * img.width + (size_t)x) * 4;
}
} // namespace

int main(int argc, char **argv) {
  g_shot = (std::filesystem::temp_directory_path() / "njin-hdr-keep2d.png").generic_string();
  std::filesystem::remove(g_shot);
  njin::config cfg{.title = "njin hdr keep 2d", .width = 640.0f, .height = 360.0f, .target_fps = 60.0f};
  cfg.clear_bg_color = njin::rgba{0.05f, 0.05f, 0.05f, 1};
  cfg.audio = false;
  if (argc > 1) {
    const std::string r = argv[1];
    cfg.graphics_backend = r == "vulkan"        ? njin::GraphicsBackend::Vulkan
                           : r == "d3d12"       ? njin::GraphicsBackend::Direct3D12
                           : r == "bgfx-opengl" ? njin::GraphicsBackend::BgfxOpenGL
                                                : njin::GraphicsBackend::Auto;
  }
  njin::context *ctx = njin::create(cfg);
  njin::light3d light = njin::light3d_get(*ctx);
  light.hdr = true;
  light.tonemap = njin::tonemap3d_aces; // bgfx: HDR needs a tone map
  njin::light3d_set(*ctx, light);
  njin::mod_register(*ctx, njin::mod_desc{"hdr_keep2d_test", [](njin::context &c) {
                                            njin::ecs_register(c, njin::phase_render, render, "render");
                                            njin::ecs_register(c, njin::phase_update, step, "step");
                                          }});
  njin::run(*ctx);
  njin::destroy(ctx);

  const njin::RenderImageAsset img = njin::renderer_load_image(g_shot.c_str());
  expect(img.width == 640 && img.height == 360, "screenshot at the window size");
  if (img.width == 640 && img.height == 360) {
    const unsigned char *red = pixel(img, 60, 60), *centre = pixel(img, 320, 200), *corner = pixel(img, 600, 340);
    std::printf("      2D rect %d %d %d, cube %d %d %d, background %d %d %d\n", red[0], red[1], red[2], centre[0],
                centre[1], centre[2], corner[0], corner[1], corner[2]);
    expect(red[0] > 150 && red[1] < 60 && red[2] < 60, "the 2D drawn before the HDR pass still shows");
    expect(centre[2] > centre[0] + 40, "the 3D cube draws over it");
    expect(corner[0] < 40 && corner[1] < 40 && corner[2] < 40, "elsewhere the clear colour stays");
  }
  std::printf("hdr_keep2d: %d failures\n", failures);
  return failures == 0 ? 0 : 1;
}
