#include <njin.h>

namespace {
using namespace njin;

void startup(njin_ctx &ctx) {
  // 1. Phím: hai axis (ngang, dọc) và một action lướt.
  const axis_handle move_x = axis_define(ctx, "move_x", {{key_left, key_right}, {key_a, key_d}}, {pad_axis_left_x});
  const axis_handle move_y = axis_define(ctx, "move_y", {{key_up, key_down}, {key_w, key_s}}, {pad_axis_left_y});
  const action_handle dash = action_define(ctx, "dash", {key_space, pad_face_down});

  entt::registry &reg = world(ctx);

  // 2. Bản đồ 20 x 12 ô: sàn cỏ, tường đá bao quanh và một đoạn tường ở giữa.
  tilemap map;
  map.tileset = texture_load(ctx, "assets/tiles.png");
  texture_set_filter(ctx, map.tileset, filter_nearest); // pixel art: không làm mờ
  for (i32 y = 0; y < 12; y++)
    for (i32 x = 0; x < 20; x++)
      tilemap_set(map, x, y, (x == 0 || y == 0 || x == 19 || y == 11) ? 7 : 0);
  for (i32 x = 6; x < 10; x++)
    tilemap_set(map, x, 5, 7);
  tilemap_set_shape(map, 0, tile_none); // cỏ chỉ để nhìn, đi qua được

  const entt::entity level = reg.create();
  reg.emplace<transform>(level);
  reg.emplace<tilemap>(level, std::move(map));
  reg.emplace<collider>(level, collider{.shape = collider_tiles}); // đá thành vật cản

  // 3. Nhân vật: transform + collider + topdown_body. pos là chân.
  const entt::entity player = reg.create();
  reg.emplace<transform>(player, transform{.pos = {48.0f, 48.0f}});
  reg.emplace<collider>(player, collider{.size = {10.0f, 8.0f}, .offset = {0.0f, -4.0f}});
  reg.emplace<topdown_body>(player, topdown_body{.dash_speed = 230.0f}); // 0 là tắt lướt
  reg.emplace<topdown_input_map>(player, topdown_input_map{move_x, move_y, dash});

  // 4. Camera bám nhân vật và không bao giờ lộ ra ngoài bản đồ (320 x 192 pixel).
  const rect bounds{{0.0f, 0.0f}, {320.0f, 192.0f}};
  reg.emplace<camera_follow>(camera_spawn(ctx, 3.0f), camera_follow{.target = player, .bounds = bounds});
}

// Chưa có sprite: vẽ nhân vật bằng một hình chữ nhật.
void draw_player(njin_ctx &ctx) {
  world(ctx).view<transform, collider, topdown_body>().each(
      [&](const transform &tr, const collider &col, const topdown_body &) {
        draw_rect(ctx, rect_from_center(tr.pos + col.offset, col.size), colors::yellow);
      });
}

void setup(njin_ctx &ctx) {
  ecs_register(ctx, phase_startup, startup);
  ecs_register(ctx, phase_render, draw_player);
}
} // namespace

int main() {
  njin_ctx *ctx = njin_create({.title = "Đi bộ", .width = 960, .height = 540, .target_fps = 60});
  njin_mod_register(*ctx, {.name = "game", .setup = setup});
  njin_run(*ctx);
  njin_destroy(ctx);
}
