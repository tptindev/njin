#include "njin_smoke.h"
#include "njin_ctx.h"
#include "njin_log.h"
#include "njin_renderer.h"
#include "njin_window.h"
#include "njin_debug.h"
#include "njin_frame_impl.h"
#include "njin_ctx_get.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace njin {
namespace {
struct smoke_state {
  unsigned frames = 0, frame = 0;
  double update_ms = 0, render_ms = 0, present_ms = 0; // summed, for the averages logged
  RendererResourceStats first{}, last{};
  render_info info{}; // the last frame's
  std::string screenshot;
  bool captured = false;
} smoke;

void smoke_frame(context &ctx) {
  ++smoke.frame;
  const auto &f = ctx_ref<frame_state>(ctx);
  smoke.update_ms += f.cpu_update_ms;
  smoke.render_ms += f.cpu_render_ms;
  smoke.present_ms += f.cpu_present_ms;
  smoke.info = render_info_get(ctx);
  if (ctx_ref<config>(ctx).graphics_backend != GraphicsBackend::OpenGL) {
    (smoke.frame == 1 ? smoke.first : smoke.last) = renderer_resource_stats(ctx);
  }
  // A few frames before the end, so the capture has landed by shutdown on every backend.
  if (!smoke.screenshot.empty() && !smoke.captured && smoke.frame + 8 >= smoke.frames) {
    screenshot(ctx, smoke.screenshot.c_str());
    smoke.captured = true;
  }
  if (smoke.frame >= smoke.frames) quit(ctx);
}

void smoke_check(context &) {
  const double n = std::max(1u, smoke.frame);
  NJIN_INFO("NJIN_SMOKE timing: cpu update %.2f ms, render %.2f ms, present %.2f ms a frame", smoke.update_ms / n,
            smoke.render_ms / n, smoke.present_ms / n);
  NJIN_INFO("NJIN_SMOKE last frame: sprites %u (culled %u), tile chunks %u, particles %u, draw calls %u, post passes %u, lights %u",
            smoke.info.sprites, smoke.info.sprites_culled, smoke.info.tile_chunks, smoke.info.particles, smoke.info.draw_calls,
            smoke.info.post_passes, smoke.info.lights);
  if (smoke.frame > 1 && smoke.last.texture_uploads)
    NJIN_INFO("NJIN_SMOKE uploads a frame: textures %.1f, meshes %.1f, instanced draws %.1f",
              double(smoke.last.texture_uploads - smoke.first.texture_uploads) / (n - 1),
              double(smoke.last.mesh_uploads - smoke.first.mesh_uploads) / (n - 1),
              double(smoke.last.instanced_draws - smoke.first.instanced_draws) / (n - 1));
  if (smoke.screenshot.empty()) {
    NJIN_INFO("NJIN_SMOKE ok frames=%u", smoke.frame);
    return;
  }
  std::error_code error;
  if (!std::filesystem::exists(smoke.screenshot, error)) {
    NJIN_WARN("NJIN_SMOKE blank frames=%u: no capture at %s", smoke.frame, smoke.screenshot.c_str());
    return;
  }
  const auto image = renderer_load_image(smoke.screenshot.c_str());
  // Distinct colours, coarsely: a frame that drew nothing is one clear colour.
  std::unordered_set<unsigned> colours;
  for (size_t p = 0; p + 3 < image.pixels.size() && colours.size() < 64; p += 4 * 7)
    colours.insert((unsigned(image.pixels[p] >> 3) << 10) | (unsigned(image.pixels[p + 1] >> 3) << 5) | unsigned(image.pixels[p + 2] >> 3));
  if (colours.size() < 3)
    NJIN_WARN("NJIN_SMOKE blank frames=%u colours=%zu %ux%u", smoke.frame, colours.size(), image.width, image.height);
  else
    NJIN_INFO("NJIN_SMOKE ok frames=%u colours=%zu %ux%u", smoke.frame, colours.size(), image.width, image.height);
}

void smoke_setup(context &ctx) {
  ecs_register(ctx, phase_post_render, smoke_frame, "njin.smoke");
  ecs_register(ctx, phase_shutdown, smoke_check, "njin.smoke.check");
}
} // namespace

config smoke_config(const config &cfg) {
  config out = cfg;
  if (const char *backend = std::getenv("NJIN_SMOKE_BACKEND"); backend && *backend) {
    if (!parse_graphics_backend(backend, out.graphics_backend))
      throw std::invalid_argument(std::string("njin: NJIN_SMOKE_BACKEND is not a backend: ") + backend);
  }
  return out;
}

void smoke_install(context &ctx) {
  const char *frames = std::getenv("NJIN_SMOKE_FRAMES");
  if (!frames || !*frames) return;
  smoke = {};
  smoke.frames = unsigned(std::max(1, std::atoi(frames)));
  if (const char *path = std::getenv("NJIN_SMOKE_SCREENSHOT"); path && *path) {
    smoke.screenshot = path;
    std::error_code error;
    std::filesystem::remove(smoke.screenshot, error); // never check a stale capture
  }
  mod_register(ctx, {.name = "njin.smoke", .setup = smoke_setup});
}
} // namespace njin
