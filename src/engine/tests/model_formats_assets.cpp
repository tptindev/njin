// IQM, M3D and VOX models through model_load on the SDL3/bgfx renderer, from the small
// files src/engine/tests/assets/make_model_formats.py and make_m3d.c write (no raylib):
// bend.iqm / bend.m3d: a quad on a root bone and its child one unit up, clip "bend"
// turning the child 90 degrees about Z: IQM at its second keyframe (1/60 s, the rate
// legacy played every clip at), M3D at its third (raylib samples M3D every 17 ms, so its
// second keyframe is part way); blocks.vox: three voxels. Checks bones, clips, the bent pose,
// and that each draws (posed) into the frame.
//   njin_model_formats_assets_test <assets folder> [renderer]
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
bool near(njin::vec3 a, njin::vec3 b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z) < 1e-3f; }

std::string g_dir, g_shot;
njin::model_handle g_iqm, g_m3d, g_vox;
int g_frame = 0;

void render(njin::context &ctx) {
  njin::begin_3d(ctx, njin::camera3d{.position = {0, 1.5f, 8}, .target = {0, 1, 0}});
  njin::model_pose bent{};
  bent.anim = 0;
  bent.time = 2.0f / 60.0f;
  bent.loop = false;
  njin::transform3d left{}, middle{}, right{};
  left.position = {-2.5f, 0, 0};
  right.position = {2.5f, 0, 0};
  right.scale = {0.3f, 0.3f, 0.3f};
  njin::draw_model_anim(ctx, g_iqm, left, bent);
  njin::draw_model_anim(ctx, g_m3d, middle, bent);
  njin::draw_model(ctx, g_vox, right);
  njin::end_3d(ctx);
}
void step(njin::context &ctx) {
  if (++g_frame == 10)
    njin::screenshot(ctx, g_shot.c_str());
  if (g_frame == 14)
    njin::quit(ctx);
}
} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: njin_model_formats_assets_test <assets folder> [renderer]\n");
    return 2;
  }
  g_dir = argv[1];
  g_shot = (std::filesystem::temp_directory_path() / "njin-model-formats.png").generic_string();
  std::filesystem::remove(g_shot);
  njin::config cfg{.title = "njin model formats", .width = 640.0f, .height = 360.0f, .target_fps = 60.0f};
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
  g_iqm = njin::model_load(*ctx, (g_dir + "/bend.iqm").c_str());
  g_m3d = njin::model_load(*ctx, (g_dir + "/bend.m3d").c_str());
  g_vox = njin::model_load(*ctx, (g_dir + "/blocks.vox").c_str());
  expect(g_iqm.id && g_m3d.id && g_vox.id, "IQM, M3D and VOX load");
  if (g_iqm.id && g_m3d.id) {
    // M3D skins keep raylib's extra "no bone" bone.
    expect(njin::model_bone_count(*ctx, g_iqm) == 2 && njin::model_bone_count(*ctx, g_m3d) == 3, "bone counts");
    expect(njin::model_anim_count(*ctx, g_iqm) == 1 && njin::model_anim_count(*ctx, g_m3d) == 1, "one clip each");
    expect(njin::model_anim_find(*ctx, g_iqm, "bend") == 0 && njin::model_anim_find(*ctx, g_m3d, "bend") == 0,
           "the clip keeps its name");
    for (const njin::model_handle model : {g_iqm, g_m3d}) {
      const char *what = model.id == g_iqm.id ? "IQM" : "M3D";
      njin::model_pose rest{}, bent{};
      rest.anim = bent.anim = 0;
      bent.time = (model.id == g_iqm.id ? 1.0f : 2.0f) / 60.0f;
      rest.loop = bent.loop = false;
      const njin::i32 child = njin::model_bone_find(*ctx, model, "child");
      expect(child >= 0, std::string(what) + ": the child bone");
      if (child < 0)
        continue;
      const njin::bone_pose3d a = njin::model_bone_pose(*ctx, model, rest, child);
      const njin::bone_pose3d b = njin::model_bone_pose(*ctx, model, bent, child);
      std::printf("      %s child: rest %.3f %.3f %.3f y %.3f %.3f %.3f; bent y %.3f %.3f %.3f\n", what, a.position.x,
                  a.position.y, a.position.z, a.y_axis.x, a.y_axis.y, a.y_axis.z, b.y_axis.x, b.y_axis.y, b.y_axis.z);
      expect(near(a.position, {0, 1, 0}) && near(a.y_axis, {0, 1, 0}), std::string(what) + ": the child at rest");
      expect(near(b.position, {0, 1, 0}) && near(b.y_axis, {-1, 0, 0}), std::string(what) + ": the child bent 90 degrees");
    }
  }
  njin::mod_register(*ctx, njin::mod_desc{"model_formats_assets", [](njin::context &c) {
                                            njin::ecs_register(c, njin::phase_render, render, "render");
                                            njin::ecs_register(c, njin::phase_update, step, "step");
                                          }});
  njin::run(*ctx);
  njin::destroy(ctx);
  const njin::RenderImageAsset img = njin::renderer_load_image(g_shot.c_str());
  int lit[3] = {0, 0, 0};
  if (img.width == 640 && img.height == 360)
    for (unsigned y = 0; y < img.height; ++y)
      for (unsigned x = 0; x < img.width; ++x) {
        const unsigned char *p = img.pixels.data() + ((size_t)y * img.width + x) * 4;
        if (p[0] + p[1] + p[2] > 30)
          ++lit[x < 213 ? 0 : x < 426 ? 1 : 2];
      }
  std::printf("      drawn pixels: IQM %d, M3D %d, VOX %d\n", lit[0], lit[1], lit[2]);
  expect(lit[0] > 200 && lit[1] > 200 && lit[2] > 50, "each model draws");
  std::printf("model_formats_assets: %d failures\n", failures);
  return failures == 0 ? 0 : 1;
}
