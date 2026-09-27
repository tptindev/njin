#include "game.h"

namespace moteswarm {
game_state g;

namespace {
void startup(njin_ctx &ctx) {
  g.shader = shader_load(ctx, "assets/mote.vs", "assets/mote.fs");

  g.move_x = axis_define(ctx, "move_x", {{key_left, key_right}, {key_a, key_d}}, {pad_axis_left_x});
  g.move_y = axis_define(ctx, "move_y", {{key_up, key_down}, {key_w, key_s}}, {pad_axis_left_y});

  g.player = spawn_player(ctx, {});

  rng &r = random(ctx);
  for (i32 i = 0; i < npc_count; i++)
    spawn_npc(ctx, r.point_in({{-npc_spawn_radius, -npc_spawn_radius}, {npc_spawn_radius * 2.0f, npc_spawn_radius * 2.0f}}));

  const entt::entity cam = camera_spawn(ctx, 1.0f);
  world(ctx).emplace<camera_follow>(cam, camera_follow{.target = g.player, .smoothing = 0.15f});
}

void setup(njin_ctx &ctx) {
  ecs_register(ctx, phase_startup, startup, "startup");
  ecs_register(ctx, phase_update, drive_player, "drive_player");
  ecs_register(ctx, phase_update, drive_npcs, "drive_npcs");
  ecs_register(ctx, phase_pre_render, draw_floor, "floor");
  ecs_register(ctx, phase_render, draw_mote, "mote");
  ecs_register(ctx, phase_post_render, draw_hint, "hint");
}
} // namespace

mod_desc module() { return {.name = "moteswarm", .setup = setup}; }
} // namespace moteswarm
