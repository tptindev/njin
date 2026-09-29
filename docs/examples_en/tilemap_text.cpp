#include <njin.h>

namespace {
using namespace njin;

void startup(context &ctx) {
  const axis_handle move_x = axis_define(ctx, "move_x", {{key_left, key_right}, {key_a, key_d}}, {pad_axis_left_x});
  const axis_handle move_y = axis_define(ctx, "move_y", {{key_up, key_down}, {key_w, key_s}}, {pad_axis_left_y});
  const action_handle dash = action_define(ctx, "dash", {key_space, pad_face_down});

  entt::registry &reg = world(ctx);

  // 1. A map written as text, one character per tile: # is a stone wall (tile 7), . is grass (tile 0), P is where the character
  //    appears. P both places grass tile 0 underneath and is reported back (the third parameter is true).
  tilemap map;
  map.tileset = texture_load(ctx, "assets/tiles.png");
  texture_set_filter(ctx, map.tileset, filter_nearest);
  tilemap_set_shape(map, 0, tile_none); // grass is only for looking at, it can be walked through
  const std::vector<tile_marker> markers = tilemap_from_text(map, R"(
####################
#..................#
#..P...............#
#..................#
#..................#
#.....####.........#
#..................#
#..................#
#..................#
#..................#
#..................#
####################
)", {{'#', 7}, {'.', 0}, {'P', 0, true}});

  vec2 start{};
  for (const tile_marker &m : markers)
    if (m.symbol == 'P') {
      const rect r = tilemap_cell_rect(map, {0.0f, 0.0f}, m.at.x, m.at.y); // tile -> rectangle in the world
      start = {r.pos.x + r.size.x * 0.5f, r.pos.y + r.size.y};             // feet at the bottom of the tile
    }

  const entt::entity level = reg.create();
  reg.emplace<transform>(level);
  reg.emplace<tilemap>(level, std::move(map));
  reg.emplace<collider>(level, collider{.shape = collider_tiles}); // stone becomes an obstacle

  // 2. The character at exactly the P spot.
  const entt::entity player = reg.create();
  reg.emplace<transform>(player, transform{.pos = start});
  reg.emplace<collider>(player, collider{.size = {10.0f, 8.0f}, .offset = {0.0f, -4.0f}});
  reg.emplace<topdown_body>(player, topdown_body{.dash_speed = 230.0f});
  reg.emplace<topdown_input_map>(player, topdown_input_map{move_x, move_y, dash});

  // 3. The camera follows the character, never showing outside the map (20 x 12 tiles, each 16 pixels).
  const rect bounds{{0.0f, 0.0f}, {320.0f, 192.0f}};
  reg.emplace<camera_follow>(camera_spawn(ctx, 3.0f), camera_follow{.target = player, .bounds = bounds});
}

void draw_player(context &ctx) {
  world(ctx).view<transform, collider, topdown_body>().each(
      [&](const transform &tr, const collider &col, const topdown_body &) {
        draw_rect(ctx, rect_from_center(tr.pos + col.offset, col.size), colors::yellow);
      });
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, startup);
  ecs_register(ctx, phase_render, draw_player);
}
} // namespace

int main() {
  context *ctx = create({.title = "Text map", .width = 960, .height = 540, .target_fps = 60});
  mod_register(*ctx, {.name = "game", .setup = setup});
  run(*ctx);
  destroy(ctx);
}
