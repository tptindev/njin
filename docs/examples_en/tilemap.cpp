#include <njin.h>

namespace {
constexpr njin::vec2 player_size{12, 14};
constexpr njin::f32 gravity = 900.0f;
constexpr njin::f32 jump_speed = 330.0f;

entt::entity level = entt::null;
njin::vec2 player_pos{40, 0};
njin::vec2 player_vel{};
bool on_ground = false;

void build(njin::njin_ctx &ctx) {
  entt::registry &reg = njin::world(ctx);
  njin::tilemap map;
  map.tileset = njin::texture_load(ctx, "assets/tiles.png");
  map.tile_size = {16, 16};

  // No need to declare a size: wherever a tile is set, the chunk there is created.
  for (int x = -20; x < 400; x++)
    njin::tilemap_set(map, x, 10, 1); // ground 420 tiles long, spanning 14 chunks
  for (int y = 6; y < 10; y++)
    njin::tilemap_set(map, 15, y, 2); // a wall
  njin::tilemap_set(map, 8, 7, 3);    // a small platform

  level = reg.create();
  reg.emplace<njin::transform>(level, njin::transform{.pos = {0, 0}});
  reg.emplace<njin::tilemap>(level, std::move(map));
}

// Platformer physics in the fixed phase: the result does not depend on FPS.
void physics(njin::njin_ctx &ctx) {
  entt::registry &reg = njin::world(ctx);
  const auto &map = reg.get<njin::tilemap>(level);
  const njin::vec2 origin = reg.get<njin::transform>(level).pos;
  const njin::f32 dt = njin::delta(ctx); // exactly one fixed step

  njin::f32 dir = 0;
  if (njin::key_pressed(ctx, njin::key_left) || njin::key_held(ctx, njin::key_left))
    dir -= 1;
  if (njin::key_pressed(ctx, njin::key_right) || njin::key_held(ctx, njin::key_right))
    dir += 1;
  player_vel.x = dir * 140.0f;
  player_vel.y += gravity * dt;
  if (on_ground && njin::key_pressed(ctx, njin::key_space))
    player_vel.y = -jump_speed;

  // Move horizontally first, then vertically: slide along walls instead of sticking to them.
  const njin::move_result mv = njin::tilemap_move(
      map, origin, njin::rect{player_pos, player_size}, player_vel * dt);
  player_pos = mv.pos;
  on_ground = mv.hit_y && player_vel.y > 0;
  if (mv.hit_y)
    player_vel.y = 0;
}

// Dig tiles with the mouse: tilemap_set marks the chunk for redrawing.
void dig(njin::njin_ctx &ctx) {
  if (!njin::mouse_pressed(ctx, njin::mouse_left))
    return;
  entt::registry &reg = njin::world(ctx);
  auto &map = reg.get<njin::tilemap>(level);
  const njin::vec2 origin = reg.get<njin::transform>(level).pos;
  const njin::vec2 world_pos = njin::scr2w(ctx, njin::mouse_pos(ctx));
  const njin::cell c = njin::tilemap_cell_at(map, origin, world_pos);
  njin::tilemap_set(map, c.x, c.y, -1);
}

void draw_player(njin::njin_ctx &ctx) {
  njin::draw_rect(ctx, njin::rect{player_pos, player_size}, njin::colors::yellow);
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, build);
  njin::ecs_register(ctx, njin::phase_fixed_update, physics);
  njin::ecs_register(ctx, njin::phase_update, dig);
  njin::ecs_register(ctx, njin::phase_render, draw_player);
}
} // namespace

njin::mod_desc platformer_module() { return {.name = "platformer", .setup = setup}; }
