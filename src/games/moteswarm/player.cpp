// The mote as an entity. njin's topdown_body moves it; the creature only
// decides how that movement looks, so the two are joined here and nowhere else.
#include "game.h"

namespace moteswarm {
entt::entity spawn_player(njin_ctx &ctx, vec2 pos) {
  entt::registry &reg = world(ctx);
  const entt::entity e = reg.create();
  reg.emplace<transform>(e, transform{.pos = pos});
  // Nothing else on the floor collides, but topdown_body moves through
  // collision_move and wants a collider to do it.
  reg.emplace<collider>(e, collider{.shape = collider_circle, .radius = mote_radius * 0.8f});
  reg.emplace<topdown_body>(e, topdown_body{.speed = mote_speed});
  reg.emplace<topdown_input_map>(e, topdown_input_map{g.move_x, g.move_y, {}});

  creature c;
  c.radius = mote_radius;
  creature_spawn(c, pos);
  reg.emplace<creature>(e, c);
  return e;
}

// Runs after the body has moved this frame. The drive is what the player is
// asking for, not the velocity the body ended up with, so a mote that is
// slowing to a stop has already stopped stroking.
//
// Skips any entity without a topdown_input_map so this only ever picks up the
// player: an NPC mote has the same creature/topdown_body pair but no input
// map, and is ticked by drive_npcs instead. Without the check both would tick
// it once each, doubling every rate the creature has.
void drive_player(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  for (auto [e, c, body, tr] : reg.view<creature, topdown_body, const transform>().each()) {
    if (!reg.all_of<topdown_input_map>(e))
      continue;
    vec2 ask = body.input.move;
    if (length_sq(ask) > 1.0f)
      ask = normalize(ask);
    c.drive = ask * body.speed;
    creature_tick(c, tr.pos, body.velocity, delta(ctx));
  }
}
} // namespace moteswarm
