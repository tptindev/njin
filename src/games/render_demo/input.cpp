#include "demo.h"

namespace render_demo {
void input(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  vec2 move{};
  move.x = (key_held(ctx, key_d) || key_held(ctx, key_right) ? 1.0f : 0.0f) -
           (key_held(ctx, key_a) || key_held(ctx, key_left) ? 1.0f : 0.0f);
  move.y = (key_held(ctx, key_s) || key_held(ctx, key_down) ? 1.0f : 0.0f) -
           (key_held(ctx, key_w) || key_held(ctx, key_up) ? 1.0f : 0.0f);
  reg.get<topdown_body>(demo.hero).input.move = move;

  if (key_pressed(ctx, key_1)) {
    demo.images.use_atlas = !demo.images.use_atlas;
    apply_atlas(ctx);
  }
  if (key_pressed(ctx, key_2)) {
    demo.gpu_particles = !demo.gpu_particles;
    particles_set_backend(ctx, demo.gpu_particles ? particle_backend_auto : particle_backend_cpu);
    // An emitter changes where it runs only once it has no particles left, so
    // clear them to see the switch at once (a game would just let them fly out).
    for (auto [e, em] : reg.view<particle_emitter>().each())
      em.particles.clear();
  }
  if (key_pressed(ctx, key_3))
    window_set_vsync(ctx, !window_vsync(ctx));
  if (key_pressed(ctx, key_4)) {
    demo.blur = !demo.blur;
    apply_post(ctx);
  }
  if (key_pressed(ctx, key_5)) {
    demo.bloom = !demo.bloom;
    apply_post(ctx);
  }
  if (key_pressed(ctx, key_6)) {
    demo.crt = !demo.crt;
    apply_post(ctx);
  }
  if (key_pressed(ctx, key_7))
    demo.pass.night = !demo.pass.night;
  if (key_pressed(ctx, key_8))
    demo.pass.dusk = !demo.pass.dusk;
  if (key_pressed(ctx, key_9))
    demo.pass.haze = !demo.pass.haze;
  if (key_pressed(ctx, key_l)) {
    demo.lights.lit = !demo.lights.lit;
    build_lights(ctx);
  }
  if (key_pressed(ctx, key_n)) {
    demo.lights.scene_preset = (demo.lights.scene_preset + 1) % scene_count;
    build_lights(ctx);
  }
  if (key_pressed(ctx, key_o))
    demo.lights.shadows = !demo.lights.shadows;
  if (key_pressed(ctx, key_m)) {
    demo.images.normal_maps = !demo.images.normal_maps;
    apply_normals(ctx);
  }
  if (key_pressed(ctx, key_p))
    demo.lights.falloff = (demo.lights.falloff + 1) % 4;
  if (key_pressed(ctx, key_b)) {
    demo.lights.pixel_shadows = !demo.lights.pixel_shadows;
    apply_occluders(ctx);
  }
  // `key_t` alone is ambiguous on Linux: <sys/types.h> has a global one.
  if (key_pressed(ctx, njin::key_t)) {
    demo.lights.tonemap = (demo.lights.tonemap + 1) % 3;
    lighting_desc d = lighting_get(ctx);
    d.tonemap = (light_tonemap)demo.lights.tonemap;
    lighting_set(ctx, d);
  }
  if (key_pressed(ctx, key_f)) {
    particle_emitter &em = reg.get<particle_emitter>(demo.fountain);
    em.emitting = !em.emitting;
  }
  if (key_pressed(ctx, key_r)) {
    demo.rain_on = !demo.rain_on;
    reg.get<particle_emitter>(demo.rain).emitting = demo.rain_on;
  }
  if (key_pressed(ctx, key_space))
    explode(ctx, scr2w(ctx, mouse_pos(ctx)));
  if (key_pressed(ctx, key_equal))
    set_crowd(ctx, demo.crowd + crowd_step);
  if (key_pressed(ctx, key_minus))
    set_crowd(ctx, demo.crowd - crowd_step);
  if (key_pressed(ctx, key_tab))
    demo.hud = !demo.hud;
}
} // namespace render_demo
