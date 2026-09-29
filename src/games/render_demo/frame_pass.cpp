#include "demo.h"

#include <algorithm>
#include <cmath>

namespace render_demo {
namespace {
constexpr u32 max_lights = 8; // the size of the arrays in scene.fs

f32 approach(f32 value, f32 target, f32 step) {
  return value < target ? std::min(value + step, target) : std::max(value - step, target);
}
} // namespace

void apply_post(context &ctx) {
  post_fx fx = demo.crt ? post::crt() : post_fx{};
  if (demo.bloom) {
    fx.bloom = 1.0f;
    fx.bloom_threshold = 0.55f;
  }
  if (demo.blur)
    fx.blur = 8.0f;
  post_fx_set(ctx, fx);
}

// The whole-frame shader and the two images it reads besides the frame.
void load_frame_pass(context &ctx) {
  demo.pass.shader = shader_load(ctx, nullptr, "assets/scene.fs");
  shader_set_texture(ctx, demo.pass.shader, "ramp", texture_load(ctx, "assets/ramp.png"));
  shader_set_texture(ctx, demo.pass.shader, "noise", texture_load(ctx, "assets/noise.png"));
}

// Set every frame, before the world is drawn. The lights are in screen pixels:
// the hero's torch, one at the mouse, and four lamps standing in the forest.
void update_frame_pass(context &ctx) {
  const f32 step = delta_real(ctx) * 3.0f;
  demo.pass.night_amount = approach(demo.pass.night_amount, demo.pass.night ? 1.0f : 0.0f, step);
  demo.pass.dusk_amount = approach(demo.pass.dusk_amount, demo.pass.dusk ? 1.0f : 0.0f, step);
  demo.pass.haze_amount = approach(demo.pass.haze_amount, demo.pass.haze ? 1.0f : 0.0f, step);
  // Only pay for the extra pass while something is on.
  const bool on = demo.pass.night_amount > 0.0f || demo.pass.dusk_amount > 0.0f || demo.pass.haze_amount > 0.0f;
  camera_set_post_shader(ctx, on ? demo.pass.shader : shader_handle{});
  if (!on)
    return;

  const f32 t = elapsed(ctx);
  shader_set_f32(ctx, demo.pass.shader, "time", t);
  shader_set_vec2(ctx, demo.pass.shader, "resolution", screen_size(ctx));
  shader_set_vec3(ctx, demo.pass.shader, "ambient", {0.10f, 0.13f, 0.24f});
  shader_set_f32(ctx, demo.pass.shader, "night", demo.pass.night_amount);
  shader_set_f32(ctx, demo.pass.shader, "dusk", demo.pass.dusk_amount);
  shader_set_f32(ctx, demo.pass.shader, "haze", demo.pass.haze_amount);

  vec4 lights[max_lights];
  vec4 colors[max_lights];
  u32 n = 0;
  const vec2 hero_at = world(ctx).get<transform>(demo.hero).pos;
  const auto add = [&](vec2 screen_pos, f32 radius, f32 strength, vec4 color) {
    if (n < max_lights) {
      lights[n] = {screen_pos.x, screen_pos.y, radius, strength};
      colors[n++] = color;
    }
  };
  add(w2scr(ctx, hero_at - vec2{0.0f, 8.0f}), 190.0f * camera_zoom * 0.5f, 1.0f + 0.08f * std::sin(t * 11.0f),
      {1.0f, 0.75f, 0.4f, 0.0f});
  add(mouse_pos(ctx), 130.0f, 0.9f, {0.4f, 0.6f, 1.0f, 0.0f});
  constexpr vec2 lamps[] = {{-140.0f, -70.0f}, {150.0f, -90.0f}, {-100.0f, 110.0f}, {170.0f, 90.0f}};
  for (i32 i = 0; i < 4; i++)
    add(w2scr(ctx, world_size * 0.5f + lamps[(usize)i]), 90.0f * camera_zoom, 0.85f + 0.1f * std::sin(t * 5.0f + (f32)i),
        {1.0f, 0.85f, 0.55f, 0.0f});
  shader_set_i32(ctx, demo.pass.shader, "light_count", (i32)n);
  shader_set_vec4_array(ctx, demo.pass.shader, "lights", lights, n);
  shader_set_vec4_array(ctx, demo.pass.shader, "light_colors", colors, n);
}
} // namespace render_demo
