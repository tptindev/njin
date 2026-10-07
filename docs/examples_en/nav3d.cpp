#include <njin.h>

#include <vector>

namespace {
using namespace njin;

const camera3d camera{.position = {0.0f, 22.0f, 18.0f}, .target = {0.0f, 0.0f, 0.0f}, .fovy = 50.0f};

// A floor, two walls and a platform with a ramp up to it.
struct box {
  vec3 center, size, rotation;
};
const box level[] = {
    {{0.0f, -0.5f, 0.0f}, {30.0f, 1.0f, 30.0f}, {}},
    {{-4.0f, 1.0f, 0.0f}, {1.0f, 2.0f, 12.0f}, {}},
    {{5.0f, 1.0f, -4.0f}, {8.0f, 2.0f, 1.0f}, {}},
    {{8.0f, 1.0f, 8.0f}, {6.0f, 2.0f, 6.0f}, {}},
    {{2.2f, 0.9f, 8.0f}, {6.0f, 0.2f, 3.0f}, {0.0f, 0.0f, 20.0f}}, // ramp up to the platform
};

navmesh3d_handle nav;
std::vector<nav3d_agent_handle> crowd;
std::vector<vec3> preview; // path from the first agent to the mouse

void load(context &ctx) {
  // A navmesh for agents 0.4 m in radius, 1.8 m tall, climbing 0.4 m steps.
  nav = navmesh3d_create(ctx, {.agent_radius = 0.4f, .agent_height = 1.8f, .agent_climb = 0.4f});
  for (const box &b : level) {
    navmesh3d_add_box(ctx, nav, b.center, b.size, b.rotation);
    body3d_create(ctx, {.shape = shape3d_box, .position = b.center, .rotation = b.rotation, .size = b.size});
  }
  navmesh3d_build(ctx, nav);

  // A group of agents standing in a row.
  for (i32 i = 0; i < 8; i++)
    crowd.push_back(nav3d_agent_add(ctx, nav, {.position = {-10.0f + (f32)i, 0.0f, 10.0f}}));
}

void update(context &ctx) {
  // Wherever the mouse points on the floor, the group goes there; the left button gives the order.
  const ray3d ray = camera3d_ray(ctx, camera, mouse_pos(ctx));
  const ray3d_hit hit = ray3d_plane(ray, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
  if (!hit.hit)
    return;
  navmesh3d_path(ctx, nav, nav3d_agent_position(ctx, crowd[0]), hit.point, preview);
  if (mouse_pressed(ctx, mouse_left))
    for (nav3d_agent_handle a : crowd)
      nav3d_agent_set_target(ctx, a, hit.point);
}

void render(context &ctx) {
  light3d_set(ctx, {.direction = {-0.4f, -1.0f, -0.3f}, .shadows = true, .shadow_range = 40.0f});
  begin_3d(ctx, camera);
  for (const box &b : level)
    draw_shape3d(ctx, {.kind = shape3d_box, .position = b.center, .rotation = b.rotation, .size = b.size},
                 rgba{0.7f, 0.68f, 0.62f, 1.0f});
  for (nav3d_agent_handle a : crowd) {
    const vec3 p = nav3d_agent_position(ctx, a);
    draw_capsule3d(ctx, p + vec3{0.0f, 0.4f, 0.0f}, p + vec3{0.0f, 1.4f, 0.0f}, 0.4f, colors::blue);
  }
  // The navmesh and the path, to see where the engine thinks one can walk.
  navmesh3d_draw_debug(ctx, nav);
  for (usize i = 0; i + 1 < preview.size(); i++)
    gizmo_line3d(ctx, preview[i] + vec3{0.0f, 0.1f, 0.0f}, preview[i + 1] + vec3{0.0f, 0.1f, 0.0f}, colors::yellow);
  end_3d(ctx);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, load, "load");
  ecs_register(ctx, phase_update, update, "update");
  ecs_register(ctx, phase_render, render, "render");
}
} // namespace

mod_desc crowd_module() { return {.name = "crowd", .setup = setup}; }
