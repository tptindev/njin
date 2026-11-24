// draw_instanced (2D) on the SDL3/bgfx renderer: a shader package with its own vertex
// stage places three quads from their instance data (i_data0: x, y, width, height;
// i_data1: colour) in one draw. Checks the screenshot: each quad in its colour where its
// data puts it, the frame untouched elsewhere, and a range (first, count) that skips one.
//   njin_instanced2d_test <package.njshader> [renderer]
#include "njin.h"
#include "njin_render.h"
#include "njin_camera.h"
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
std::string g_package, g_shot;
njin::shader_handle g_shader;
njin::instance_buffer_handle g_buffer;
int g_frame = 0;

bool g_camera = false;
void render(njin::context &ctx) {
  // Under a zoomed camera: the engine's own rectangle where instance 1 is, in green, then
  // the instances over it. The same world transform leaves no green showing.
  if (g_camera)
    njin::draw_rect(ctx, {{40, 40}, {200, 100}}, njin::rgba{0, 1, 0, 1});
  // Instances 0..2, but only 1 and 2 are drawn: the range skips the first.
  njin::draw_instanced(ctx, g_buffer, g_shader, 1, 2);
}
void step(njin::context &ctx) {
  ++g_frame;
  if (g_frame == 10)
    njin::screenshot(ctx, g_shot.c_str());
  if (g_frame == 12) {
    njin::camera_spawn(ctx, 2.0f, {320, 180});
    g_camera = true;
  }
  if (g_frame == 20)
    njin::screenshot(ctx, (g_shot + ".camera.png").c_str());
  if (g_frame == 24)
    njin::quit(ctx);
}

const unsigned char *pixel(const njin::RenderImageAsset &img, int x, int y) {
  return img.pixels.data() + ((size_t)y * img.width + (size_t)x) * 4;
}
bool near(const unsigned char *p, int r, int g, int b) {
  return std::abs(p[0] - r) < 10 && std::abs(p[1] - g) < 10 && std::abs(p[2] - b) < 10;
}
} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: njin_instanced2d_test <package.njshader> [renderer]\n");
    return 2;
  }
  g_package = argv[1];
  g_shot = (std::filesystem::temp_directory_path() / "njin-instanced2d.png").generic_string();
  std::filesystem::remove(g_shot);
  njin::config cfg{.title = "njin instanced 2D test", .width = 640.0f, .height = 360.0f, .target_fps = 60.0f};
  cfg.clear_bg_color = njin::rgba{0, 0, 0, 1};
  cfg.audio = false;
  if (argc > 2) {
    const std::string r = argv[2];
    cfg.graphics_backend = r == "vulkan"        ? njin::GraphicsBackend::Vulkan
                           : r == "d3d12"       ? njin::GraphicsBackend::Direct3D12
                           : r == "bgfx-opengl" ? njin::GraphicsBackend::BgfxOpenGL
                                                : njin::GraphicsBackend::Auto;
  }
  njin::context *ctx = njin::create(cfg);
  g_shader = njin::shader_load(*ctx, nullptr, g_package.c_str());
  expect(g_shader.id != 0, "the package with a vertex stage loads");
  g_buffer = njin::instance_buffer_create(*ctx, 8);
  const float data[] = {
      // x, y, w, h,          r, g, b, a
      300, 250, 60, 60,       1, 1, 1, 1, // skipped by the range
      40, 40, 200, 100,       1, 0, 0, 1, // red
      400, 60, 120, 200,      0, 0, 1, 1, // blue
  };
  njin::instance_buffer_upload(*ctx, g_buffer, data, 3);
  njin::mod_register(*ctx, njin::mod_desc{"instanced2d_test", [](njin::context &c) {
                                            njin::ecs_register(c, njin::phase_render, render, "render");
                                            njin::ecs_register(c, njin::phase_update, step, "step");
                                          }});
  njin::run(*ctx);
  njin::destroy(ctx);

  const njin::RenderImageAsset img = njin::renderer_load_image(g_shot.c_str());
  expect(img.width == 640 && img.height == 360, "screenshot at the window size");
  if (img.width == 640 && img.height == 360) {
    expect(near(pixel(img, 140, 90), 255, 0, 0), "instance 1 is a red quad where its data puts it");
    expect(near(pixel(img, 460, 160), 0, 0, 255), "instance 2 is a blue quad where its data puts it");
    expect(near(pixel(img, 330, 280), 0, 0, 0), "instance 0 is outside the range and not drawn");
    expect(near(pixel(img, 20, 20), 0, 0, 0) && near(pixel(img, 250, 90), 0, 0, 0), "outside the quads the frame is untouched");
    expect(near(pixel(img, 41, 41), 255, 0, 0) && near(pixel(img, 38, 38), 0, 0, 0), "the quad's corner is at its pixel");
  }
  const njin::RenderImageAsset cam = njin::renderer_load_image((g_shot + ".camera.png").c_str());
  if (cam.width == 640 && cam.height == 360) {
    int green = 0, red = 0;
    for (unsigned y = 0; y < cam.height; ++y)
      for (unsigned x = 0; x < cam.width; ++x) {
        const unsigned char *p = pixel(cam, (int)x, (int)y);
        green += p[1] > 128 && p[0] < 100;
        red += p[0] > 200 && p[1] < 60 && p[2] < 60;
      }
    std::printf("      camera: %d red pixels, %d green pixels\n", red, green);
    expect(red > 15500 && red < 16500 && green < 40, "under a camera the instances follow the engine's 2D transform");
    expect(near(pixel(cam, 600, 200), 0, 0, 255), "instance 2 zoomed where the camera puts it");
  } else {
    expect(false, "camera screenshot");
  }
  std::printf("instanced2d: %d failures\n", failures);
  return failures == 0 ? 0 : 1;
}
