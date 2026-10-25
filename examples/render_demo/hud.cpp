#include "demo.h"

#include <cstdio>

namespace render_demo {
namespace {
void draw_line(context &ctx, const char *text, f32 y, rgba color = colors::white) {
  draw_text(ctx, text, {10.0f, y}, 16.0f, color);
}
} // namespace

void hud(context &ctx) {
  demo.frame_ms += (delta_real(ctx) * 1000.0f - demo.frame_ms) * 0.05f;
  if (!demo.hud)
    return;
  const render_info r = render_info_get(ctx);
  entt::registry &reg = world(ctx);
  const bool fountain_on = reg.get<particle_emitter>(demo.fountain).emitting;

  char line[256];
  draw_rect(ctx, rect{{0.0f, 0.0f}, {screen_size(ctx).x, 150.0f}}, {0.0f, 0.0f, 0.0f, 0.62f});
  std::snprintf(line, sizeof line, "%.0f FPS   %.2f ms   vsync %s   entities %zu", 1000.0f / demo.frame_ms, demo.frame_ms,
                window_vsync(ctx) ? "on" : "off", reg.view<entt::entity>().size());
  draw_line(ctx, line, 6.0f);
  std::snprintf(line, sizeof line, "sprites %u drawn, %u off screen   tile chunks %u   draw calls ~%u   atlas %s", r.sprites,
                r.sprites_culled, r.tile_chunks, r.draw_calls, demo.images.use_atlas ? "ON" : "off");
  draw_line(ctx, line, 26.0f, r.draw_calls > 100 ? rgba{1.0f, 0.7f, 0.5f, 1.0f} : colors::white);
  std::snprintf(line, sizeof line, "particles %u (%u on the GPU, %u instanced calls)   emitters %u drawn, %u off screen   post passes %u",
                r.particles, r.particles_gpu, r.instanced_calls, r.emitters, r.emitters_culled, r.post_passes);
  draw_line(ctx, line, 46.0f);
  std::snprintf(line, sizeof line, "particles run on the %s%s", demo.gpu_particles && particles_gpu_available(ctx) ? "GPU" : "CPU",
                demo.gpu_particles && !particles_gpu_available(ctx) ? " (no GPU support found)" : "");
  draw_line(ctx, line, 66.0f, {0.6f, 0.9f, 1.0f, 1.0f});
  std::snprintf(line, sizeof line,
                "1 atlas  2 GPU/CPU particles  3 vsync  4 blur  5 bloom  6 CRT  F fountain (%s)  R rain  Space explode  +/- crowd %d  Tab hide",
                fountain_on ? "on" : "off", demo.crowd);
  draw_line(ctx, line, 86.0f, {0.8f, 0.85f, 0.95f, 1.0f});
  std::snprintf(line, sizeof line, "7 night + lights (%s)  8 dusk ramp (%s)  9 haze (%s)   these three run scene.fs over the whole frame",
                demo.pass.night ? "on" : "off", demo.pass.dusk ? "on" : "off", demo.pass.haze ? "on" : "off");
  draw_line(ctx, line, 106.0f, {0.8f, 0.85f, 0.95f, 1.0f});
  std::snprintf(line, sizeof line, "L lights (%s)  N scene: %s  O shadows (%s)  B %s  M PBR maps (%s)  P falloff: %s  T tonemap: %s   lights drawn %u",
                demo.lights.lit ? "on" : "off", scene_name(demo.lights.scene_preset), demo.lights.shadows ? "on" : "off",
                demo.lights.pixel_shadows ? "pixel-perfect" : "polygons",
                demo.images.normal_maps ? "on" : "off", falloff_name(demo.lights.falloff), tonemap_name(demo.lights.tonemap), r.lights);
  draw_line(ctx, line, 126.0f, {1.0f, 0.9f, 0.7f, 1.0f});
}
} // namespace render_demo
