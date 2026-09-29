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

void startup(njin::context &ctx) {
  // Game components: register them so the inspector shows values, not just names.
  njin::debug_component<health>(ctx, "health", [](const health &h) {
    return njin::json_value::make_object().set("hp", h.hp).set("max", h.max);
  });
  njin::debug_component<enemy_ai>(ctx, "enemy_ai", [](const enemy_ai &a) {
    return njin::json_value::make_object().set("state", a.state);
  });
}

void update(njin::context &ctx) {
  entt::registry &reg = njin::world(ctx);
  if (!reg.valid(player))
    return;
  // Values watched live, shown in the inspector's "Watches" table.
  njin::debug_watch(ctx, "player.pos", reg.get<njin::transform>(player).pos);
  njin::debug_watch(ctx, "enemies", (njin::i32)reg.view<enemy_ai>().size());
}

void setup(njin::context &ctx) {
  njin::ecs_register(ctx, njin::phase_startup, startup);
  njin::ecs_register(ctx, njin::phase_update, update);
}
} // namespace

int main() {
  njin::context *ctx = njin::create({.title = "Game", .width = 1280, .height = 720, .target_fps = 60});
  njin::mod_register(*ctx, {.name = "game", .setup = setup});
#ifndef NDEBUG
  // Only debug builds open the port for njin_inspector.
  njin::debug_server_start(*ctx);
#endif
  njin::run(*ctx);
  njin::destroy(ctx);
}
