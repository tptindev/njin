#include <njin.h>

namespace {
struct health {
  njin::i32 hp = 3;
  njin::i32 max = 3;
};
struct enemy_ai {
  const char *state = "idle";
};

entt::entity player = entt::null;

void startup(njin::njin_ctx &ctx) {
  // Component của game: đăng ký để inspector hiện giá trị, không chỉ tên.
  njin::debug_component<health>(ctx, "health", [](const health &h) {
    return njin::json_value::make_object().set("hp", h.hp).set("max", h.max);
  });
  njin::debug_component<enemy_ai>(ctx, "enemy_ai", [](const enemy_ai &a) {
    return njin::json_value::make_object().set("state", a.state);
  });
}

void update(njin::njin_ctx &ctx) {
  entt::registry &reg = njin::world(ctx);
  if (!reg.valid(player))
    return;
  // Giá trị theo dõi trực tiếp, hiện trong bảng "Watches" của inspector.
  njin::debug_watch(ctx, "player.pos", reg.get<njin::transform>(player).pos);
  njin::debug_watch(ctx, "enemies", (njin::i32)reg.view<enemy_ai>().size());
}

void setup(njin::njin_ctx &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_update, update);
}
} // namespace

int main() {
  njin::njin_ctx *ctx = njin::njin_create({.title = "Game", .width = 1280, .height = 720, .target_fps = 60});
  njin::njin_mod_register(*ctx, {.name = "game", .setup = setup});
#ifndef NDEBUG
  // Chỉ bản debug mới mở cổng cho njin_inspector.
  njin::debug_server_start(*ctx);
#endif
  njin::njin_run(*ctx);
  njin::njin_destroy(ctx);
}
