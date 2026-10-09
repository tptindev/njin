#include <njin.h>

namespace {
using namespace njin;

// Area numbers whose meaning the game sets; 0 is plain ground.
constexpr u8 area_road = 1;
constexpr u8 area_swamp = 2;
constexpr u8 area_door = 3;

// Filter 0: villagers (keep out of the swamp, take the road). Filter 1: guards
// with a key, like villagers but able to go through the door. Filter 0 forbids the door.
constexpr i32 villager = 0;
constexpr i32 guard = 1;

navmesh3d_handle small_nav, big_nav;
nav3d_agent_handle walker, troll;
i32 crate = 0; // obstacle: a crate pushed back and forth

void load(context &ctx) {
  small_nav = navmesh3d_create(ctx, {.agent_radius = 0.4f});
  navmesh3d_add_box(ctx, small_nav, {0.0f, -0.5f, 0.0f}, {40.0f, 1.0f, 30.0f});
  navmesh3d_add_box(ctx, small_nav, {6.0f, 1.5f, -6.0f}, {0.5f, 3.0f, 10.0f}); // wall with a doorway at z = -0.5
  navmesh3d_add_box(ctx, small_nav, {6.0f, 1.5f, 6.0f}, {0.5f, 3.0f, 12.0f});
  // Mark areas: a swamp in the middle of the map, a road along the north, the doorway.
  navmesh3d_add_area(ctx, small_nav, {-4.0f, 0.0f, 0.0f}, {10.0f, 4.0f, 14.0f}, 0.0f, area_swamp);
  navmesh3d_add_area(ctx, small_nav, {-4.0f, 0.0f, 9.0f}, {30.0f, 4.0f, 3.0f}, 0.0f, area_road);
  navmesh3d_add_area(ctx, small_nav, {6.0f, 0.0f, -0.5f}, {1.0f, 4.0f, 1.0f}, 0.0f, area_door);

  // Costs: a metre of swamp costs as much as eight metres of plain ground, the road less.
  nav3d_filter f{};
  f.cost[area_swamp] = 8.0f;
  f.cost[area_road] = 0.6f;
  f.excluded = 1u << area_door; // villagers have no key
  navmesh3d_set_filter(ctx, small_nav, villager, f);
  f.excluded = 0;
  navmesh3d_set_filter(ctx, small_nav, guard, f);

  // A big monster needs its own navmesh: same geometry, areas and filters, kept further from walls.
  big_nav = navmesh3d_clone(ctx, small_nav, {.agent_radius = 1.2f, .agent_height = 3.0f});
  navmesh3d_build(ctx, small_nav);
  navmesh3d_build(ctx, big_nav);

  walker = nav3d_agent_add(ctx, small_nav, {.position = {-14.0f, 0.0f, 0.0f}, .filter = guard});
  troll = nav3d_agent_add(ctx, big_nav, {.position = {-14.0f, 0.0f, -4.0f}, .radius = 1.2f, .max_speed = 2.5f});
  nav3d_agent_set_target(ctx, walker, {14.0f, 0.0f, 0.0f});
  nav3d_agent_set_target(ctx, troll, {14.0f, 0.0f, -4.0f});

  // A moving obstacle: the navmesh rebuilds the tiles it touches by itself, a few each frame.
  crate = navmesh3d_add_obstacle(ctx, small_nav, {.position = {0.0f, 1.0f, 9.0f}, .size = {2.0f, 2.0f, 2.0f}});
}

void update(context &ctx) {
  // Every 3 seconds the crate is pushed to the other side of the road; agents find a way round it.
  // Moving an obstacle every frame means rebuilding a few tiles every frame: move it when it
  // really changes place.
  static i32 side = 0;
  const i32 now = (i32)(elapsed(ctx) / 3.0f) % 2;
  if (now != side) {
    side = now;
    navmesh3d_move_obstacle(ctx, small_nav, crate, {0.0f, 1.0f, side == 0 ? 9.0f : 7.0f});
  }
  // Key K: the guard loses the key, finds paths like a villager and can no longer go through the door.
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
