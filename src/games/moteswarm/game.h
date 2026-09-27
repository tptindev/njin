#pragma once
// Mote Swarm: one mote the player steers among a swarm of others left to
// their own devices, on an empty floor. It is here to show the creature's two
// states, idle (it breathes, blinks and looks around) and move (it turns to
// face where it goes, strokes its tails and leans its eye into the way) —
// and that nothing about either one cares whether a mote is player-steered
// or wandering on its own, or how many tails it grew.
//
//   game.cpp    startup, the state, the module
//   player.cpp  the player's mote entity, and steering its creature from its body
//   npc.cpp     the other motes: spawning, and the wander that steers them
//   render.cpp  the floor, the shader pass that draws every mote, the hint
#include "creature/creature.h"

#include <njin.h>

namespace moteswarm {
inline constexpr f32 mote_speed = 150.0f; // top speed, world units per second
inline constexpr f32 mote_radius = 30.0f;

inline constexpr i32 npc_count = 20;
inline constexpr f32 npc_speed = 80.0f;          // slower drift than the player, so it reads as idle wandering
inline constexpr f32 npc_spawn_radius = 360.0f;  // scattered around the player's own start
inline constexpr f32 npc_wander_radius = 480.0f; // soft leash: beyond this, a mote leans back toward the middle
inline constexpr f32 npc_wander_change_min = 1.2f; // seconds a wander heading is held, at least
inline constexpr f32 npc_wander_change_max = 3.2f; // seconds a wander heading is held, at most

struct game_state {
  entt::entity player = entt::null;
  shader_handle shader{};
  axis_handle move_x{};
  axis_handle move_y{};
};

extern game_state g;

// A wandering NPC's steering: a heading it holds for a while, then swaps for
// a new one. Everything else about how it moves and how its creature reads
// that movement is exactly the player's own, in topdown_body and creature.
struct mote_ai {
  vec2 dir{1.0f, 0.0f};
  f32 change_timer = 0.0f;
};

// player.cpp
entt::entity spawn_player(njin_ctx &ctx, vec2 pos);
void drive_player(njin_ctx &ctx);

// npc.cpp
entt::entity spawn_npc(njin_ctx &ctx, vec2 pos);
void drive_npcs(njin_ctx &ctx);

// render.cpp
void draw_floor(njin_ctx &ctx);
void draw_mote(njin_ctx &ctx);
void draw_hint(njin_ctx &ctx);

mod_desc module();
} // namespace moteswarm
