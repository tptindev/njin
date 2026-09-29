#include <njin.h>

#include <cmath>
#include <vector>

namespace {
using namespace njin;

std::vector<entt::entity> shapes; // to draw their outline

// An occluder: put light_occluder on an entity that has a transform.
entt::entity place(context &ctx, vec2 pos, light_occluder shape) {
  entt::registry &reg = world(ctx);
  const entt::entity e = reg.create();
  reg.emplace<transform>(e, transform{.pos = pos});
  reg.emplace<light_occluder>(e, std::move(shape));
  shapes.push_back(e);
  return e;
}

void load(context &ctx) {
  entt::registry &reg = world(ctx);

  // Turn lighting on: night, a dark blue sky.
  lighting_set(ctx, {.enabled = true, .ambient = {0.34f, 0.38f, 0.55f, 1.0f}});

  // A lamp in the middle. A color temperature of 2700 K is an incandescent bulb.
  const entt::entity lamp = reg.create();
  reg.emplace<transform>(lamp, transform{.pos = {400.0f, 300.0f}});
  reg.emplace<light_2d>(lamp, light_2d{.temperature = 2700.0f, .intensity = 14.0f, .radius = 380.0f, .size = 60.0f, .height = 90.0f});

  // Ready-made occluder shapes, arranged in a ring around the lamp.
  place(ctx, {400.0f, 150.0f}, light_occluder_box({40.0f, 40.0f}));               // rectangle
  place(ctx, {545.0f, 205.0f}, light_occluder_circle(22.0f));                     // circle
  place(ctx, {585.0f, 370.0f}, light_occluder_ellipse({34.0f, 14.0f}));           // ellipse
  place(ctx, {470.0f, 470.0f}, light_occluder_capsule({-20.0f, 0.0f}, {20.0f, 0.0f}, 9.0f)); // capsule
  place(ctx, {230.0f, 445.0f},
        light_occluder_line({{-40.0f, 20.0f}, {0.0f, -25.0f}, {45.0f, 10.0f}})); // thin wall, bent
  place(ctx, {215.0f, 235.0f},
        light_occluder{{{-25.0f, 25.0f}, {0.0f, -30.0f}, {28.0f, 22.0f}, {5.0f, 8.0f}}}); // concave polygon, points given by hand

  // Following the sprite's shape: the shadow has exactly the shape of the tree. It also changes with the animation frame and when flipped.
  const entt::entity tree = reg.create();
  reg.emplace<transform>(tree, transform{.pos = {320.0f, 380.0f}, .scale = 3.0f});
  reg.emplace<sprite>(tree, sprite{.texture = texture_load(ctx, "assets/sprites/tree.png"), .origin = {0.5f, 1.0f}});
  reg.emplace<light_occluder_sprite>(tree, light_occluder_sprite{.alpha = 0.5f, .simplify = 0.8f});
}

// Draw the shapes' outlines so they are easy to see. Lighting does not draw occluders itself.
void outline(context &ctx) {
  entt::registry &reg = world(ctx);
  for (const entt::entity e : shapes) {
    const transform &tr = reg.get<transform>(e);
    const light_occluder &o = reg.get<light_occluder>(e);
    for (usize i = 0; i + (o.closed ? 0 : 1) < o.points.size(); i++) {
      const vec2 a = o.points[i], b = o.points[(i + 1) % o.points.size()];
      draw_line(ctx, tr.pos + a, tr.pos + b, 2.0f, {0.85f, 0.85f, 0.9f, 1.0f});
    }
  }
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, load, "load");
  ecs_register(ctx, phase_render, outline, "outline");
}
} // namespace

mod_desc light_shapes_module() { return {.name = "light_shapes", .setup = setup}; }
