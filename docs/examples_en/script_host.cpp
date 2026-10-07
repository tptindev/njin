#include <njin.h>

namespace {
using namespace njin;

i32 score = 0;
entt::entity player = entt::null;

void load(context &ctx) {
  // C++ functions callable from Lua: parameter and return types convert by themselves.
  script_register(ctx, "add_score", [](i32 points) { score += points; });
  script_register(ctx, "game.score", [] { return score; });

  // Functions and globals: load a file, then call one of its functions.
  script_run_file(ctx, "scripts/rules.lua");
  const script_result r = script_call(ctx, "rules.coin_value", {3.0});
  if (r.ok)
    NJIN_INFO("a coin is worth %g points", std::get<f64>(r.value));

  // A script attached to an entity: on_start, on_update(dt), on_render every frame.
  entt::registry &w = world(ctx);
  player = w.create();
  w.emplace<transform>(player, transform{.pos = {32, 64}});
  w.emplace<collider>(player, collider{.size = {12, 14}});
  w.emplace<platformer_body>(player);
  script_attach(ctx, player, "scripts/player.lua");

#ifndef NDEBUG
  hot_reload_enable(ctx, true); // edit the .lua file, save, and the game follows right away
#endif
}

void update(context &ctx) {
  // Read state the script keeps in `self`.
  const script_value coins = script_field(ctx, player, "coins");
  if (const f64 *n = std::get_if<f64>(&coins); n != nullptr && *n >= 100)
    NJIN_INFO("100 coins reached");
}

void setup(context &ctx) {
  ecs_register(ctx, phase_startup, load, "load");
  ecs_register(ctx, phase_update, update, "update");
}
} // namespace

mod_desc scripted_game_module() { return {.name = "scripted_game", .setup = setup}; }
