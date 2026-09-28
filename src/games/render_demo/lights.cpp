#include "demo.h"

#include <cmath>
#include <vector>

namespace render_demo {
namespace {
constexpr const char *scene_names[scene_count] = {"night torches", "low sun", "flashlight"};
constexpr const char *falloff_names[4] = {"physical", "linear", "smooth", "none"};

entt::entity add_light(njin_ctx &ctx, vec2 pos, const light_2d &light) {
  entt::registry &reg = world(ctx);
  const entt::entity e = reg.create();
  reg.emplace<transform>(e, transform{.pos = pos});
  reg.emplace<light_2d>(e, light);
  return e;
}
} // namespace

const char *scene_name(i32 preset) { return scene_names[preset]; }
const char *falloff_name(i32 falloff) { return falloff_names[falloff]; }
const char *tonemap_name(i32 tonemap) {
  constexpr const char *names[3] = {"shoulder", "Reinhard", "ACES"};
  return names[tonemap];
}

// Sets up the lights of the current scene from scratch.
void build_lights(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  std::vector<entt::entity> old;
  for (const entt::entity e : reg.view<light_2d>())
    old.push_back(e);
  reg.destroy(old.begin(), old.end());
  demo.lights.torch = demo.lights.mouse_light = demo.lights.flashlight = entt::null;

  lighting_desc d{};
  d.shadow_reach = 44.0f; // a tree is about this tall over the low sun: shadows as long as its height allows
  d.enabled = demo.lights.lit;
  d.tonemap = (light_tonemap)demo.lights.tonemap;
  const vec2 centre = world_size * 0.5f;
  if (demo.lights.scene_preset == 0) {
    // Night: a dark blue ambient, the hero's torch, four lamps and a cool light at the mouse.
    d.ambient = {0.20f, 0.24f, 0.40f, 1.0f};
    demo.lights.torch = add_light(ctx, centre, {.temperature = 2400.0f, .intensity = 12.0f, .radius = 240.0f, .size = 30.0f,
                                         .height = 30.0f});
    for (const vec2 at : {vec2{-140.0f, -70.0f}, vec2{150.0f, -90.0f}, vec2{-100.0f, 110.0f}, vec2{170.0f, 90.0f}})
      add_light(ctx, centre + at, {.temperature = 2700.0f, .intensity = 9.0f, .radius = 190.0f, .size = 22.0f,
                                   .height = 26.0f});
    demo.lights.mouse_light = add_light(ctx, centre, {.temperature = 7000.0f, .intensity = 7.0f, .radius = 160.0f, .size = 22.0f});
  } else if (demo.lights.scene_preset == 1) {
    // A low, warm sun from the upper left: one directional light whose shadows fall to the lower right.
    const rgba sky = light_color_kelvin(6000.0f);
    d.ambient = {sky.r * 0.50f, sky.g * 0.54f, sky.b * 0.68f, 1.0f};
    add_light(ctx, centre, {.kind = light_directional, .temperature = 3200.0f, .intensity = 3.4f, .size = 40.0f,
                            .angle = 35.0f, .elevation = 32.0f});
  } else {
    // A flashlight: a spot from the hero towards the mouse, in a very dark forest.
    d.ambient = {0.12f, 0.14f, 0.26f, 1.0f};
    demo.lights.flashlight = add_light(ctx, centre, {.kind = light_spot, .temperature = 5600.0f, .intensity = 16.0f,
                                              .radius = 360.0f, .size = 40.0f, .cone = 46.0f, .softness = 0.4f,
                                              .height = 40.0f});
    demo.lights.torch = add_light(ctx, centre, {.temperature = 2400.0f, .intensity = 4.0f, .radius = 90.0f, .size = 20.0f});
  }
  lighting_set(ctx, d);
}

// Every frame: the lights that follow the hero and the mouse, and the two settings the keys flip.
void update_lights(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  const vec2 hero_at = reg.get<transform>(demo.hero).pos - vec2{0.0f, 6.0f};
  const vec2 mouse_at = scr2w(ctx, mouse_pos(ctx));
  if (demo.lights.torch != entt::null)
    reg.get<transform>(demo.lights.torch).pos = hero_at;
  if (demo.lights.mouse_light != entt::null)
    reg.get<transform>(demo.lights.mouse_light).pos = mouse_at;
  if (demo.lights.flashlight != entt::null) {
    reg.get<transform>(demo.lights.flashlight).pos = hero_at;
    const vec2 aim = mouse_at - hero_at;
    reg.get<light_2d>(demo.lights.flashlight).angle = std::atan2(aim.y, aim.x) * (180.0f / 3.14159265f);
  }
  const f32 t = elapsed(ctx);
  for (auto [e, l] : reg.view<light_2d>().each()) {
    l.cast_shadows = demo.lights.shadows;
    l.falloff = (light_falloff)demo.lights.falloff;
  }
  // The torch flickers a little.
  if (demo.lights.torch != entt::null && demo.lights.scene_preset == 0)
    reg.get<light_2d>(demo.lights.torch).intensity = 12.0f + 0.9f * std::sin(t * 11.0f) + 0.45f * std::sin(t * 23.0f);
}
} // namespace render_demo
