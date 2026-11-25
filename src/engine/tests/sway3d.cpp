// model_material::sway on draw_instanced3d, the same on every renderer: two 2 m tall
// columns in an 8 m/s wind with the world clock held at 0 (time_set_scale(0)), one with
// sway 0.3 and one still, seen by an orthographic camera. The swaying column's right edge
// and top are where the legacy instanced shader's formula (render3d.cpp instance_sway,
// computed here on the CPU) puts them; the still one stays upright. Sway 0.3 keeps the
// bent column inside the frame.
//   njin_sway3d_test [opengl|vulkan|d3d12|bgfx-opengl]
#include "njin.h"
#include "njin_3d.h"
#include "njin_world3d.h"
#include "njin_renderer.h"
#include <algorithm>
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
njin::model_handle g_sway, g_still;
njin::instance_buffer_handle g_left, g_right;
int g_frame = 0;
constexpr float px_per_m = 90.0f; // 360 px over an ortho height of 4 m

void render(njin::context &ctx) {
  njin::camera3d cam{.position = {0, 1, 10}, .target = {0, 1, 0}};
  cam.projection = njin::camera3d_orthographic;
  cam.ortho_height = 4.0f;
  njin::begin_3d(ctx, cam);
  njin::draw_instanced3d(ctx, g_sway, g_left, 0, 1);
  njin::draw_instanced3d(ctx, g_still, g_right, 0, 1);
  njin::end_3d(ctx);
}
void step(njin::context &ctx) {
  if (++g_frame == 10)
    njin::screenshot(ctx, g_shot.c_str());
  if (g_frame == 14)
    njin::quit(ctx);
}

float g_flutter = 0.0f; // legacy-vs mode: model_material::flutter on the swaying column too

njin::model_handle column(njin::context &ctx, float sway) {
  // A 0.2 x 2 x 0.2 box standing on y = 0.
  const float x = 0.1f, y = 2.0f, z = 0.1f;
  static const njin::vec3 corners[8] = {{-x, 0, -z}, {x, 0, -z}, {x, y, -z}, {-x, y, -z},
                                        {-x, 0, z},  {x, 0, z},  {x, y, z},  {-x, y, z}};
  static const njin::u32 faces[36] = {4, 5, 6, 4, 6, 7, 1, 0, 3, 1, 3, 2, 5, 1, 2, 5, 2, 6,
                                           0, 4, 7, 0, 7, 3, 7, 6, 2, 7, 2, 3, 0, 1, 5, 0, 5, 4};
  njin::mesh3d_data mesh{};
  mesh.positions = corners;
  mesh.vertex_count = 8;
  mesh.indices = faces;
  mesh.index_count = 36;
  // Flutter data: distance to the stalk (the corner's height), and a beat per corner.
  static const njin::vec2 leaf[8] = {{0, 0.1f}, {0, 0.3f}, {2, 0.5f}, {2, 0.7f}, {0, 0.2f}, {0, 0.4f}, {2, 0.6f}, {2, 0.8f}};
  if (sway > 0 && g_flutter > 0)
    mesh.texcoords2 = leaf;
  const njin::model_handle model = njin::model_create(ctx, mesh);
  njin::model_material m{};
  m.color = njin::rgba{1, 1, 1, 1};
  m.surface.unlit = true;
  m.double_sided = true;
  m.sway = sway;
  m.flutter = sway > 0 ? g_flutter : 0.0f;
  njin::model_material_set(ctx, model, 0, m);
  return model;
}

float fract(float v) { return v - std::floor(v); }

struct measure {
  int right_edge[2] = {-1, -1}, top[2] = {9999, 9999};
  bool ok = false;
};

njin::GraphicsBackend backend_of(const std::string &r) {
  return r == "opengl"        ? njin::GraphicsBackend::OpenGL
         : r == "vulkan"      ? njin::GraphicsBackend::Vulkan
         : r == "d3d12"       ? njin::GraphicsBackend::Direct3D12
         : r == "bgfx-opengl" ? njin::GraphicsBackend::BgfxOpenGL
                              : njin::GraphicsBackend::Auto;
}

// The scene on one renderer: where the swaying and the still column end up.
measure run_scene(njin::GraphicsBackend backend) {
  std::filesystem::remove(g_shot);
  g_frame = 0;
  njin::config cfg{.title = "njin sway 3d", .width = 640.0f, .height = 360.0f, .target_fps = 60.0f};
  cfg.clear_bg_color = njin::rgba{0, 0, 0, 1};
  cfg.audio = false;
  cfg.graphics_backend = backend;
  njin::context *ctx = njin::create(cfg);
  njin::time_set_scale(*ctx, 0.0f); // the grass clock stays at 0: one exact pose
  njin::wind3d_set(*ctx, {8.0f, 0.0f});
  g_sway = column(*ctx, 0.3f);
  g_still = column(*ctx, 0.0f);
  const float left[4] = {-1.5f, 0, 0, 0}, right[4] = {1.5f, 0, 0, 0};
  g_left = njin::instance_buffer_create(*ctx, 4);
  g_right = njin::instance_buffer_create(*ctx, 4);
  njin::instance_buffer_upload(*ctx, g_left, left, 1);
  njin::instance_buffer_upload(*ctx, g_right, right, 1);
  njin::mod_register(*ctx, njin::mod_desc{"sway3d_test", [](njin::context &c) {
                                            njin::ecs_register(c, njin::phase_render, render, "render");
                                            njin::ecs_register(c, njin::phase_update, step, "step");
                                          }});
  njin::run(*ctx);
  njin::destroy(ctx);
  measure m;
  const njin::RenderImageAsset img = njin::renderer_load_image(g_shot.c_str());
  if (img.width != 640 || img.height != 360)
    return m;
  m.ok = true;
  for (unsigned y = 0; y < img.height; ++y)
    for (unsigned x = 0; x < img.width; ++x) {
      const unsigned char *p = img.pixels.data() + ((size_t)y * img.width + x) * 4;
      if (p[0] + p[1] + p[2] < 384)
        continue;
      const int side = x < 400 ? 0 : 1; // the still column stands at x 446..464
      m.right_edge[side] = std::max(m.right_edge[side], (int)x);
      m.top[side] = std::min(m.top[side], (int)y);
    }
  std::printf("      sway: right edge %d, top %d; still: right %d, top %d\n", m.right_edge[0], m.top[0],
              m.right_edge[1], m.top[1]);
  return m;
}
} // namespace

int main(int argc, char **argv) {
  g_shot = (std::filesystem::temp_directory_path() / "njin-sway3d.png").generic_string();
  // `legacy-vs <renderer>`: the legacy OpenGL renderer, then <renderer>, in one process;
  // their columns must land on the same pixels.
  if (argc > 2 && std::string(argv[1]) == "legacy-vs") {
    g_flutter = 0.5f;
    const measure legacy = run_scene(njin::GraphicsBackend::OpenGL);
    const measure other = run_scene(backend_of(argv[2]));
    expect(legacy.ok && other.ok, "both renderers took a screenshot");
    bool same = true;
    for (int i = 0; i < 2; ++i)
      same &= std::abs(legacy.right_edge[i] - other.right_edge[i]) <= 1 && std::abs(legacy.top[i] - other.top[i]) <= 1;
    expect(same, std::string("the columns sway and flutter the same on legacy OpenGL and ") + argv[2]);
    std::printf("sway3d: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
  }
  const measure m = run_scene(backend_of(argc > 1 ? argv[1] : ""));
  expect(m.ok, "screenshot at the window size");
  // The legacy formula at time 0, wind (8, 0), origin (-1.5, 0, 0), sway 0.3. The phase
  // hash fract(sin(x) * 43758.5) depends on the GPU's sin, so allow a few pixels.
  const float phase = fract(std::sin(-1.5f * 12.9898f) * 43758.5453f) * 6.2832f;
  const float gust = std::sin(phase + -1.5f * 0.3f) * 0.5f + 0.5f;
  const float flutter = std::sin(phase * 3.0f) * 0.12f;
  const float bend = (1.0f * (0.35f + 0.65f * gust) + flutter * 1.0f) * 0.3f;
  const float dx = bend * 4.0f, dy = -0.5f * bend * bend * 4.0f * 2.0f; // at y = 2
  const float want_right = 320.0f + (-1.5f + 0.1f + dx) * px_per_m;
  const float want_top = 180.0f - (2.0f + dy - 1.0f) * px_per_m;
  std::printf("      formula: right edge %.1f, top %.1f\n", want_right, want_top);
  expect(std::abs(m.right_edge[0] - want_right) <= 8.0f, "the swaying column leans downwind as legacy's formula");
  expect(std::abs(m.top[0] - want_top) <= 8.0f, "and bends down as legacy's formula");
  expect(std::abs(m.right_edge[1] - (320 + 1.6f * px_per_m)) <= 2.0f && std::abs(m.top[1] - 90) <= 2,
         "the still column stands upright");
  std::printf("sway3d: %d failures\n", failures);
  return failures == 0 ? 0 : 1;
}
