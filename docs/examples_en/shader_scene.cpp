#include <njin.h>

#include <cmath>

namespace {
njin::shader_handle scene;
entt::entity hero = entt::null; // the character, created by the game elsewhere

void load(njin::context &ctx) {
  // nullptr: keep the default vertex shader, only replace the fragment shader.
  scene = njin::shader_load(ctx, nullptr, "assets/scene.fs");

  // Extra images: attach once, the engine re-attaches them every time the shader runs. Use texture_load,
  // not an image inside the atlas.
  njin::shader_set_texture(ctx, scene, "ramp", njin::texture_load(ctx, "assets/ramp.png"));
  njin::shader_set_texture(ctx, scene, "noise", njin::texture_load(ctx, "assets/noise.png"));

  // Applies to the whole world the camera draws. The UI is not affected.
  njin::camera_set_post_shader(ctx, scene);
}

// Set the uniforms every frame, before the world is drawn.
void update_scene(njin::context &ctx) {
  const float t = njin::elapsed(ctx);
  njin::shader_set_f32(ctx, scene, "time", t);
  njin::shader_set_vec2(ctx, scene, "resolution", njin::screen_size(ctx));
  njin::shader_set_vec3(ctx, scene, "ambient", {0.10f, 0.13f, 0.24f}); // night blue
  njin::shader_set_f32(ctx, scene, "night", 1.0f);
  njin::shader_set_f32(ctx, scene, "dusk", 0.0f);
  njin::shader_set_f32(ctx, scene, "haze", 0.0f);

  // Lights are in screen pixels: xy is the position, z is the radius, w is the brightness.
  // Each light's color is in the second array, at the same index.
  const njin::vec2 torch = njin::w2scr(ctx, njin::world(ctx).get<njin::transform>(hero).pos);
  const njin::vec4 lights[2] = {
      {torch.x, torch.y, 190.0f, 1.0f + 0.08f * std::sin(t * 11.0f)}, // torch, flickering
      {njin::mouse_pos(ctx).x, njin::mouse_pos(ctx).y, 130.0f, 0.9f}, // light at the mouse cursor
  };
  const njin::vec4 colors[2] = {
      {1.0f, 0.75f, 0.4f, 0.0f}, // orange-yellow
      {0.4f, 0.6f, 1.0f, 0.0f},  // blue
  };
  njin::shader_set_i32(ctx, scene, "light_count", 2);
  njin::shader_set_vec4_array(ctx, scene, "lights", lights, 2);
  njin::shader_set_vec4_array(ctx, scene, "light_colors", colors, 2);
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, load, "load");
  njin::ecs_register(ctx, njin::phase_pre_render, update_scene, "update_scene");
}
} // namespace

njin::mod_desc scene_shader_module() {
  return {.name = "scene_shader", .setup = setup};
}
