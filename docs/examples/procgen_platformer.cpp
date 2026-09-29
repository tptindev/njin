#include <njin.h>

namespace {
using namespace njin;

// Trong lưới, mặt đất chỉ là ba loại "trên giấy": cỏ, đất, đá. Tileset terrain.png của game mẫu có hai bộ 47 ô
// bo góc (đất phủ cỏ ở ô 0..46, đá ở 47..93), rồi 94 bục một chiều, 95 bụi, 96 hoa.
constexpr i32 grass = 0, dirt = 1, stone = 2;
constexpr i32 ground_set = 0, stone_set = 47, plank = 94, bush = 95, flower = 96;

entt::entity level = entt::null, player = entt::null;
u32 seed = 1;

void build(context &ctx) {
  // 1. Tham số. max_step và platform_height giữ ở 2 và 3 ô: nhân vật mặc định nhảy cao gần 3 ô.
  platformer_gen_desc d;
  d.width = 160;
  d.height = 30;
  d.surface_noise.seed = seed;
  d.cave_noise.seed = seed + 100;
  d.surface_tile = grass;
  d.dirt_tile = dirt;
  d.deep_tile = stone;
  d.caves = 0.2f;
  d.pit_chance = 0.06f;
  d.platform_chance = 0.05f;
  d.platform_tile = plank;
  platformer_gen_result r = generate_platformer(d);

  // 2. Luật thêm: bụi và hoa mọc trên mặt cỏ (ô trống ngay trên cỏ), cách nhau ít nhất 3 ô.
  auto on_grass = [](const tile_grid &g, i32 x, i32 y) { return g.get(x, y) == -1 && g.get(x, y + 1) == grass; };
  grid_scatter(r.grid, bush, 0.15f, seed, on_grass, 3);
  grid_scatter(r.grid, flower, 0.2f, seed + 1, on_grass, 2);

  // 3. Bo góc: cỏ và đất là một địa hình, đá nối liền với nó. Ngoài lưới coi như đất chạy tiếp, trừ phía trên.
  const u8 outside = side_all & ~side_up;
  grid_autotile(r.grid, {{.tiles = {grass, dirt}, .joins = {stone}, .base = ground_set, .outside = outside},
                         {.tiles = {stone}, .joins = {grass, dirt}, .base = stone_set, .outside = outside}});

  entt::registry &reg = world(ctx);
  tilemap &map = reg.get<tilemap>(level);
  tilemap_clear(map);
  tilemap_from_grid(map, r.grid);

  const rect cell = tilemap_cell_rect(map, {0.0f, 0.0f}, r.spawn.x, r.spawn.y);
  reg.get<transform>(player).pos = {cell.pos.x + 8.0f, cell.pos.y + 16.0f}; // chân ở đáy ô
  reg.get<platformer_body>(player).velocity = {};
}

void startup(context &ctx) {
  entt::registry &reg = world(ctx);
  tilemap map;
  map.tileset = texture_load(ctx, "assets/terrain.png");
  texture_set_filter(ctx, map.tileset, filter_nearest);
  tilemap_set_shape(map, plank, tile_one_way);
  tilemap_set_shape(map, bush, tile_none);
  tilemap_set_shape(map, flower, tile_none);
  level = reg.create();
  reg.emplace<transform>(level);
  reg.emplace<tilemap>(level, std::move(map));
  reg.emplace<collider>(level, collider{.shape = collider_tiles});

  player = reg.create();
  reg.emplace<transform>(player);
  reg.emplace<collider>(player, collider{.size = {10.0f, 14.0f}, .offset = {0.0f, -7.0f}});
  reg.emplace<platformer_body>(player);
  reg.emplace<platformer_input_map>(
      player, platformer_input_map{.move = axis_define(ctx, "move", {{key_left, key_right}}),
                                   .jump = action_define(ctx, "jump", {key_space}),
                                   .down = action_define(ctx, "down", {key_down})});
  reg.emplace<camera_follow>(camera_spawn(ctx, 3.0f),
                             camera_follow{.target = player, .bounds = {{0.0f, 0.0f}, {160 * 16.0f, 30 * 16.0f}}});
  build(ctx);
}

void update(context &ctx) {
  if (key_pressed(ctx, key_r)) { // R: màn mới
    seed++;
    build(ctx);
  }
}

void draw_player(context &ctx) {
  const transform &tr = world(ctx).get<transform>(player);
  draw_rect(ctx, rect_from_center(tr.pos - vec2{0.0f, 7.0f}, {10.0f, 14.0f}), colors::yellow);
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, startup);
  ecs_register(ctx, phase_update, update);
  ecs_register(ctx, phase_render, draw_player);
}
} // namespace

int main() {
  context *ctx = create({.title = "Sinh màn platformer", .width = 960, .height = 540, .target_fps = 60,
                               .clear_bg_color = {0.37f, 0.80f, 0.89f, 1.0f}});
  mod_register(*ctx, {.name = "game", .setup = setup});
  run(*ctx);
  destroy(ctx);
}
