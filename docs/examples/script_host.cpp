#include <njin.h>

namespace {
using namespace njin;

i32 score = 0;
entt::entity player = entt::null;

void load(context &ctx) {
  // Hàm C++ gọi được từ Lua: kiểu tham số và kiểu trả về tự đổi.
  script_register(ctx, "add_score", [](i32 points) { score += points; });
  script_register(ctx, "game.score", [] { return score; });

  // Hàm và biến toàn cục: nạp một file, rồi gọi hàm của nó.
  script_run_file(ctx, "scripts/rules.lua");
  const script_result r = script_call(ctx, "rules.coin_value", {3.0});
  if (r.ok)
    NJIN_INFO("một xu đáng %g điểm", std::get<f64>(r.value));

  // Script gắn trên entity: on_start, on_update(dt), on_render mỗi frame.
  entt::registry &w = world(ctx);
  player = w.create();
  w.emplace<transform>(player, transform{.pos = {32, 64}});
  w.emplace<collider>(player, collider{.size = {12, 14}});
  w.emplace<platformer_body>(player);
  script_attach(ctx, player, "scripts/player.lua");

#ifndef NDEBUG
  hot_reload_enable(ctx, true); // sửa file .lua, lưu, game đổi theo ngay
#endif
}

void update(context &ctx) {
  // Đọc trạng thái mà script giữ trong `self`.
  const script_value coins = script_field(ctx, player, "coins");
  if (const f64 *n = std::get_if<f64>(&coins); n != nullptr && *n >= 100)
    NJIN_INFO("đủ 100 xu");
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, load, "load");
  ecs_register(ctx, phase_update, update, "update");
}
} // namespace

mod_desc scripted_game_module() { return {.name = "scripted_game", .setup = setup}; }
