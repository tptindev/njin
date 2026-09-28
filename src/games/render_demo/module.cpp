#include "demo.h"

namespace render_demo {
namespace {
void startup(njin_ctx &ctx) {
  load_images(ctx);
  load_frame_pass(ctx);
  draw_set_y_sort(ctx, layer_things, true);
  build_map(ctx);
  build_hero(ctx);
  build_emitters(ctx);
  set_crowd(ctx, demo.crowd);
  build_orbs(ctx);
  build_lights(ctx);
  particles_set_backend(ctx, particle_backend_auto);
  debug_watch(ctx, "gpu particles available", particles_gpu_available(ctx));
}
} // namespace

void setup(njin_ctx &ctx) {
  ecs_register(ctx, phase_startup, startup, "startup");
  ecs_register(ctx, phase_update, input, "input");
  ecs_register(ctx, phase_post_update, follow_emitters, "follow");
  ecs_register(ctx, phase_post_update, update_lights, "update_lights");
  ecs_register(ctx, phase_pre_render, update_frame_pass, "update_scene");
  ecs_register(ctx, phase_post_render, hud, "hud");
}
} // namespace render_demo
