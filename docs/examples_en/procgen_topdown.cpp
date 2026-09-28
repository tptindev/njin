#include <njin.h>

namespace {
using namespace njin;

// In the grid, the terrain is only three kinds (and flowers) "on paper". The top-down sample game's terrain.png tileset has two 47-tile
// rounded-corner sets (grass edged with sand in tiles 0..46; stone in 47..93), then 94..97 water (animated) and 98 flower.
constexpr i32 water = 0, grass = 1, rock = 2, flowers = 3;
constexpr i32 land_set = 0, rock_set = 47, water_tile = 94, flower_tile = 98;

entt::entity ground = entt::null, props = entt::null, sea = entt::null, player = entt::null;
u32 seed = 1;

// A grid where the `keep` tiles become `tile` and the other tiles become empty.
tile_grid select(const tile_grid &g, i32 keep, i32 tile) {
  tile_grid out = tile_grid_make(g.width, g.height, -1);
  for (usize i = 0; i < g.cells.size(); i++)
    if (g.cells[i] == keep)
      out.cells[i] = tile;
  return out;
}

void build(njin_ctx &ctx) {
  // 1. Parameters: height from noise, split into zones by height (as percentages of the map).
  topdown_gen_desc d;
  d.width = 64;
  d.height = 48;
  d.height_noise = {.seed = seed, .frequency = 0.06f, .octaves = 5, .warp = 6.0f};
  d.biomes = {{water, 0.32f}, {grass, 0.85f}, {rock, 1.0f}};
  d.island = 0.7f;               // surrounded by water
  d.walkable = {grass};          // all walkable places form one connected block
  d.blocked_tile = rock;
  topdown_gen_result r = generate_topdown(d);

  // 2. Extra rules: flowers scattered on the grass.
  grid_scatter(r.grid, grass, flowers, 0.06f, seed, 2);

  // 3. Split into three layers. The ground layer is everything that is not water; the top layer has rocks and flowers. Each layer gets its
  //    own rounded corners, so the shoreline is round and the water layer below shows through at the corners.
  tile_grid land = tile_grid_make(r.grid.width, r.grid.height, -1);
  for (usize i = 0; i < land.cells.size(); i++)
    if (r.grid.cells[i] != water)
      land.cells[i] = grass;
  grid_autotile(land, {{.tiles = {grass}, .base = land_set, .outside = 0}});

  tile_grid top = select(r.grid, rock, rock);
  grid_autotile(top, {{.tiles = {rock}, .base = rock_set, .outside = 0}});
  for (usize i = 0; i < top.cells.size(); i++)
    if (r.grid.cells[i] == flowers)
      top.cells[i] = flower_tile;

  entt::registry &reg = world(ctx);
  for (const auto &[e, grid] : {std::pair{ground, &land}, std::pair{props, &top}}) {
    tilemap &map = reg.get<tilemap>(e);
    tilemap_clear(map);
    tilemap_from_grid(map, *grid);
  }
  tilemap &map = reg.get<tilemap>(sea);
  tilemap_clear(map);
  tilemap_from_grid(map, tile_grid_make(r.grid.width, r.grid.height, water_tile));

  const rect cell = tilemap_cell_rect(map, {0.0f, 0.0f}, r.spawn.x, r.spawn.y);
  reg.get<transform>(player).pos = {cell.pos.x + 8.0f, cell.pos.y + 12.0f};
}

// A tilemap entity sharing the tileset, drawn on layer `layer`; `collide` turns on collision with solid tiles.
entt::entity make_layer(njin_ctx &ctx, const tilemap &shared, i32 layer, bool collide) {
  entt::registry &reg = world(ctx);
  const entt::entity e = reg.create();
  reg.emplace<transform>(e);
  tilemap &map = reg.emplace<tilemap>(e, shared);
  map.layer = layer;
  if (collide)
    reg.emplace<collider>(e, collider{.shape = collider_tiles});
  return e;
}

void startup(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  tilemap map;
  map.tileset = texture_load(ctx, "assets/terrain.png");
  texture_set_filter(ctx, map.tileset, filter_nearest);
  tilemap_animate(map, water_tile, {94, 95, 96, 97}, 0.25f);
  tilemap_set_shape(map, flower_tile, tile_none);
  for (i32 i = 0; i < 47; i++)
    tilemap_set_shape(map, land_set + i, tile_none); // the ground can be walked through; only rocks block
  // Three layers drawn on top of each other: water at the bottom, then ground, then rocks and flowers.
  sea = make_layer(ctx, map, 0, false);
  ground = make_layer(ctx, map, 1, false);
  props = make_layer(ctx, map, 2, true);

  player = reg.create();
  reg.emplace<transform>(player);
  reg.emplace<collider>(player, collider{.size = {10.0f, 8.0f}, .offset = {0.0f, -4.0f}});
  reg.emplace<topdown_body>(player);
  reg.emplace<topdown_input_map>(
      player, topdown_input_map{axis_define(ctx, "x", {{key_left, key_right}, {key_a, key_d}}),
                                axis_define(ctx, "y", {{key_up, key_down}, {key_w, key_s}})});
  reg.emplace<camera_follow>(camera_spawn(ctx, 3.0f),
                             camera_follow{.target = player, .bounds = {{0.0f, 0.0f}, {64 * 16.0f, 48 * 16.0f}}});
  build(ctx);
}

void update(njin_ctx &ctx) {
  if (key_pressed(ctx, key_r)) { // R: new map
    seed++;
    build(ctx);
  }
}

void draw_player(njin_ctx &ctx) {
  const transform &tr = world(ctx).get<transform>(player);
  draw_rect(ctx, rect_from_center(tr.pos - vec2{0.0f, 4.0f}, {10.0f, 8.0f}), colors::yellow);
}

void setup(njin_ctx &ctx) {
  ecs_register(ctx, phase_startup, startup);
  ecs_register(ctx, phase_update, update);
  ecs_register(ctx, phase_render, draw_player);
}
} // namespace

int main() {
  njin_ctx *ctx = njin_create({.title = "Top-down map generation", .width = 960, .height = 540, .target_fps = 60});
  njin_mod_register(*ctx, {.name = "game", .setup = setup});
  njin_run(*ctx);
  njin_destroy(ctx);
}
