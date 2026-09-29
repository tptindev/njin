#include "demo.h"

namespace render_demo {
namespace {
particle_emitter glow_emitter() {
  particle_emitter e{};
  e.emitting = false;
  e.texture = demo.images.separate[k_spark];
  e.blend = blend_additive;
  e.layer = layer_things;
  return e;
}
} // namespace

void explode(context &ctx, vec2 at) {
  particle_emitter e = glow_emitter();
  e.max_particles = 600;
  e.life = {0.6f, 1.4f};
  e.speed = {40.0f, 220.0f};
  e.drag = 1.6f;
  e.gravity = {0.0f, 60.0f};
  e.size_start = 16.0f;
  e.size_end = 2.0f;
  e.color_start = {1.0f, 0.75f, 0.3f, 1.0f};
  e.color_end = {0.9f, 0.15f, 0.05f, 0.0f};
  particles_spawn(ctx, e, at, 500);
}

void build_emitters(context &ctx) {
  entt::registry &reg = world(ctx);

  particle_emitter fountain = glow_emitter();
  fountain.max_particles = 30000;
  fountain.rate = 6000.0f;
  fountain.life = {1.5f, 2.5f};
  fountain.speed = {60.0f, 220.0f};
  fountain.angle = -90.0f;
  fountain.spread = 110.0f;
  fountain.gravity = {0.0f, 180.0f};
  fountain.drag = 0.3f;
  fountain.spin = {-90.0f, 90.0f};
  fountain.size_start = 8.0f;
  fountain.size_end = 1.0f;
  fountain.color_start = {0.5f, 0.85f, 1.0f, 0.9f};
  fountain.color_end = {0.2f, 0.3f, 1.0f, 0.0f};
  // Plain circles, the default shape and the costliest on the CPU: raylib draws
  // each as a polygon. (A textured quad is much cheaper there.)
  fountain.texture = {};
  demo.fountain = reg.create();
  reg.emplace<transform>(demo.fountain, transform{.pos = world_size * 0.5f});
  reg.emplace<particle_emitter>(demo.fountain, fountain);

  particle_emitter rain = fx::rain();
  rain.emitting = false;
  rain.rate = 900.0f;
  rain.max_particles = 4000;
  rain.area = {screen_size(ctx).x / camera_zoom + 160.0f, 4.0f};
  rain.layer = layer_things + 1;
  demo.rain = reg.create();
  reg.emplace<transform>(demo.rain);
  reg.emplace<particle_emitter>(demo.rain, rain);
}

// The fountain stays at the hero's feet; the rain follows the camera.
void follow_emitters(context &ctx) {
  entt::registry &reg = world(ctx);
  reg.get<transform>(demo.fountain).pos = reg.get<transform>(demo.hero).pos;
  const vec2 view = screen_size(ctx) / camera_zoom;
  reg.get<transform>(demo.rain).pos = reg.get<transform>(demo.camera).pos - vec2{0.0f, view.y * 0.5f + 20.0f};
}
} // namespace render_demo
