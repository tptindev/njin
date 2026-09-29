#include <njin.h>

namespace {
using namespace njin;

void startup(context &ctx) {
  // 1. Phím: một axis cho trái/phải, một action cho nhảy.
  const axis_handle move = axis_define(ctx, "move", {{key_left, key_right}});
  const action_handle jump = action_define(ctx, "jump", {key_space, pad_face_down});

  entt::registry &reg = world(ctx);

  // 2. Bản đồ: một dải đất và một bục. Đặt ô ở đâu, bản đồ mở rộng tới đó.
  tilemap map;
  map.tileset = texture_load(ctx, "assets/tiles.png");
  texture_set_filter(ctx, map.tileset, filter_nearest); // pixel art: không làm mờ
  for (i32 x = -10; x < 40; x++) {
    tilemap_set(map, x, 10, 0); // mặt đất có cỏ
    tilemap_set(map, x, 11, 1); // đất bên dưới
  }
  for (i32 x = 8; x < 12; x++)
    tilemap_set(map, x, 8, 3); // bục
  tilemap_set_shape(map, 3, tile_one_way); // nhảy xuyên từ dưới lên được

  const entt::entity level = reg.create();
  reg.emplace<transform>(level);
  reg.emplace<tilemap>(level, std::move(map));
  reg.emplace<collider>(level, collider{.shape = collider_tiles}); // ô thành vật cản

  // 3. Nhân vật: transform + collider hộp + platformer_body.
  const entt::entity player = reg.create();
  reg.emplace<transform>(player, transform{.pos = {40.0f, 100.0f}}); // pos là chân
  reg.emplace<collider>(player, collider{.size = {10.0f, 14.0f}, .offset = {0.0f, -7.0f}});
  reg.emplace<platformer_body>(player);
  reg.emplace<platformer_input_map>(player, platformer_input_map{.move = move, .jump = jump});

  // 4. Camera bám theo nhân vật.
  reg.emplace<camera_follow>(camera_spawn(ctx, 3.0f), camera_follow{.target = player});
}

// Chưa có sprite: vẽ nhân vật bằng một hình chữ nhật.
void draw_player(context &ctx) {
  world(ctx).view<transform, collider, platformer_body>().each(
      [&](const transform &tr, const collider &col, const platformer_body &) {
        draw_rect(ctx, rect_from_center(tr.pos + col.offset, col.size), colors::yellow);
      });
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, startup);
  ecs_register(ctx, phase_render, draw_player);
}
} // namespace

int main() {
  context *ctx = create({.title = "Nhảy", .width = 960, .height = 540, .target_fps = 60});
  mod_register(*ctx, {.name = "game", .setup = setup});
  run(*ctx);
  destroy(ctx);
}
