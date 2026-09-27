// The other motes: not steered by keys but by mote_ai, whose only job is to
// pick a heading and hold it a while. Everything about how that heading turns
// into motion and how the creature reads it is exactly the player's own
// drive_player, since a mote's body does not care who is asking it to move.
#include "game.h"

#include <algorithm>
#include <cmath>

namespace moteswarm {
entt::entity spawn_npc(njin_ctx &ctx, vec2 pos) {
  entt::registry &reg = world(ctx);
  rng &r = random(ctx);
  const entt::entity e = reg.create();
  reg.emplace<transform>(e, transform{.pos = pos});
  // Nothing else on the floor collides, but topdown_body moves through
  // collision_move and wants a collider to do it.
  reg.emplace<collider>(e, collider{.shape = collider_circle, .radius = mote_radius * 0.8f});
  reg.emplace<topdown_body>(e, topdown_body{.speed = npc_speed});
  // No topdown_input_map: nothing but drive_npcs ever writes this body's
  // input, so the keyboard axes never touch it.
  reg.emplace<mote_ai>(e, mote_ai{.dir = r.direction(), .change_timer = r.range(npc_wander_change_min, npc_wander_change_max)});

  creature c;
  c.radius = mote_radius;
  // A seed of its own, so the ring of blobs is not dealt the same turn as
  // every other mote's.
  c.rng = r.next_u32();
  c.tail_count = r.range(1, 3);
  creature_spawn(c, pos);
  reg.emplace<creature>(e, c);
  return e;
}

// Runs alongside drive_player: each NPC holds mote_ai.dir until its timer runs
// out, then swaps it for a new one, nudged back toward the middle once it has
// wandered far enough that the floor would otherwise run out from under it.
// Writing body.input.move here is what moves these motes at all — the body
// module only overwrites it from the keyboard for entities that carry a
// topdown_input_map, which NPCs do not.
void drive_npcs(njin_ctx &ctx) {
  entt::registry &reg = world(ctx);
  rng &r = random(ctx);
  const f32 dt = delta(ctx);
  for (auto [e, ai, c, body, tr] : reg.view<mote_ai, creature, topdown_body, const transform>().each()) {
    ai.change_timer -= dt;
    if (ai.change_timer <= 0.0f) {
      ai.dir = r.direction();
      ai.change_timer = r.range(npc_wander_change_min, npc_wander_change_max);
    }

    const f32 dist = length(tr.pos);
    if (dist > npc_wander_radius) {
      const vec2 home = tr.pos * (-1.0f / dist);
      const f32 pull = std::min((dist - npc_wander_radius) / npc_wander_radius, 1.0f);
      ai.dir = normalize(ai.dir + home * pull);
    }

    body.input.move = ai.dir;
    c.drive = ai.dir * body.speed;
    creature_tick(c, tr.pos, body.velocity, dt);
  }
}
} // namespace moteswarm
