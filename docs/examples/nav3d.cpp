#include <njin.h>

#include <vector>

namespace {
using namespace njin;

const camera3d camera{.position = {0.0f, 22.0f, 18.0f}, .target = {0.0f, 0.0f, 0.0f}, .fovy = 50.0f};

// Sàn, hai bức tường và một bục có dốc lên.
struct box {
  vec3 center, size, rotation;
};
const box level[] = {
    {{0.0f, -0.5f, 0.0f}, {30.0f, 1.0f, 30.0f}, {}},
    {{-4.0f, 1.0f, 0.0f}, {1.0f, 2.0f, 12.0f}, {}},
    {{5.0f, 1.0f, -4.0f}, {8.0f, 2.0f, 1.0f}, {}},
    {{8.0f, 1.0f, 8.0f}, {6.0f, 2.0f, 6.0f}, {}},
    {{2.2f, 0.9f, 8.0f}, {6.0f, 0.2f, 3.0f}, {0.0f, 0.0f, 20.0f}}, // dốc lên bục
};

navmesh3d_handle nav;
std::vector<nav3d_agent_handle> crowd;
std::vector<vec3> preview; // đường từ con đầu tới chỗ chuột

void load(context &ctx) {
  // Navmesh cho tác tử bán kính 0.4 m, cao 1.8 m, bước lên bậc 0.4 m.
  nav = navmesh3d_create(ctx, {.agent_radius = 0.4f, .agent_height = 1.8f, .agent_climb = 0.4f});
  for (const box &b : level) {
    navmesh3d_add_box(ctx, nav, b.center, b.size, b.rotation);
    body3d_create(ctx, {.shape = shape3d_box, .position = b.center, .rotation = b.rotation, .size = b.size});
  }
  navmesh3d_build(ctx, nav);

  // Một nhóm tác tử đứng thành hàng.
  for (i32 i = 0; i < 8; i++)
    crowd.push_back(nav3d_agent_add(ctx, nav, {.position = {-10.0f + (f32)i, 0.0f, 10.0f}}));
}

void update(context &ctx) {
  // Chuột trỏ chỗ nào trên sàn, cả nhóm đi tới đó; chuột trái là ra lệnh.
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
  // Navmesh và đường đi, để xem engine thấy chỗ nào đi được.
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
