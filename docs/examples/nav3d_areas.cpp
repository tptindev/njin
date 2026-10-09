#include <njin.h>

namespace {
using namespace njin;

// Số vùng do game đặt nghĩa; 0 là mặt đất thường.
constexpr u8 area_road = 1;
constexpr u8 area_swamp = 2;
constexpr u8 area_door = 3;

// Bộ lọc 0: dân làng (tránh đầm lầy, đi đường cái). Bộ lọc 1: lính gác có chìa
// khóa, giống dân làng nhưng đi được qua cửa. Bộ lọc 0 cấm cửa.
constexpr i32 villager = 0;
constexpr i32 guard = 1;

navmesh3d_handle small_nav, big_nav;
nav3d_agent_handle walker, troll;
i32 crate = 0; // vật cản: một cái thùng bị đẩy qua lại

void load(context &ctx) {
  small_nav = navmesh3d_create(ctx, {.agent_radius = 0.4f});
  navmesh3d_add_box(ctx, small_nav, {0.0f, -0.5f, 0.0f}, {40.0f, 1.0f, 30.0f});
  navmesh3d_add_box(ctx, small_nav, {6.0f, 1.5f, -6.0f}, {0.5f, 3.0f, 10.0f}); // tường có ô cửa ở z = -0.5
  navmesh3d_add_box(ctx, small_nav, {6.0f, 1.5f, 6.0f}, {0.5f, 3.0f, 12.0f});
  // Đánh dấu vùng: đầm lầy giữa bản đồ, đường cái men theo phía bắc, ô cửa.
  navmesh3d_add_area(ctx, small_nav, {-4.0f, 0.0f, 0.0f}, {10.0f, 4.0f, 14.0f}, 0.0f, area_swamp);
  navmesh3d_add_area(ctx, small_nav, {-4.0f, 0.0f, 9.0f}, {30.0f, 4.0f, 3.0f}, 0.0f, area_road);
  navmesh3d_add_area(ctx, small_nav, {6.0f, 0.0f, -0.5f}, {1.0f, 4.0f, 1.0f}, 0.0f, area_door);

  // Chi phí: một mét đầm lầy tốn như tám mét đường thường, đường cái rẻ hơn.
  nav3d_filter f{};
  f.cost[area_swamp] = 8.0f;
  f.cost[area_road] = 0.6f;
  f.excluded = 1u << area_door; // dân làng không có chìa khóa
  navmesh3d_set_filter(ctx, small_nav, villager, f);
  f.excluded = 0;
  navmesh3d_set_filter(ctx, small_nav, guard, f);

  // Quái to cần navmesh riêng: cùng hình học, vùng và bộ lọc, chừa tường xa hơn.
  big_nav = navmesh3d_clone(ctx, small_nav, {.agent_radius = 1.2f, .agent_height = 3.0f});
  navmesh3d_build(ctx, small_nav);
  navmesh3d_build(ctx, big_nav);

  walker = nav3d_agent_add(ctx, small_nav, {.position = {-14.0f, 0.0f, 0.0f}, .filter = guard});
  troll = nav3d_agent_add(ctx, big_nav, {.position = {-14.0f, 0.0f, -4.0f}, .radius = 1.2f, .max_speed = 2.5f});
  nav3d_agent_set_target(ctx, walker, {14.0f, 0.0f, 0.0f});
  nav3d_agent_set_target(ctx, troll, {14.0f, 0.0f, -4.0f});

  // Vật cản di động: navmesh tự dựng lại các ô vuông nó chạm vào, vài ô mỗi frame.
  crate = navmesh3d_add_obstacle(ctx, small_nav, {.position = {0.0f, 1.0f, 9.0f}, .size = {2.0f, 2.0f, 2.0f}});
}

void update(context &ctx) {
  // Mỗi 3 giây thùng bị đẩy sang bên kia đường cái; tác tử tìm đường vòng qua nó.
  // Dời vật cản mỗi frame thì mỗi frame phải dựng lại vài ô vuông: dời khi nó thật
  // sự đổi chỗ.
  static i32 side = 0;
  const i32 now = (i32)(elapsed(ctx) / 3.0f) % 2;
  if (now != side) {
    side = now;
    navmesh3d_move_obstacle(ctx, small_nav, crate, {0.0f, 1.0f, side == 0 ? 9.0f : 7.0f});
  }
  // Phím K: lính gác mất chìa khóa, tìm đường như dân làng và không qua cửa được nữa.
  if (key_pressed(ctx, key_k))
    nav3d_agent_set_filter(ctx, walker, villager);
}

void render(context &ctx) {
  begin_3d(ctx, camera3d{.position = {0.0f, 30.0f, 22.0f}, .target = {0.0f, 0.0f, 0.0f}, .fovy = 50.0f});
  navmesh3d_draw_debug(ctx, small_nav);
  draw_sphere3d(ctx, nav3d_agent_position(ctx, walker) + vec3{0.0f, 0.5f, 0.0f}, 0.4f, colors::yellow);
  draw_sphere3d(ctx, nav3d_agent_position(ctx, troll) + vec3{0.0f, 1.2f, 0.0f}, 1.2f, colors::green);
  end_3d(ctx);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, load, "load");
  ecs_register(ctx, phase_update, update, "update");
  ecs_register(ctx, phase_render, render, "render");
}
} // namespace

mod_desc areas_module() { return {.name = "areas", .setup = setup}; }
