#pragma once

#include "types.h"

namespace sandtable {

void sim_init(context &ctx);
void sim_update(context &ctx);

// --- Setting up --------------------------------------------------------------

// Sets the table up for the fight: the turfs as the level gives them, the
// enemy at its flags, none of the player's men sent yet.
void load_level(context &ctx);
i32 troop_count(side owner);

// Why a flag cannot be planted at `pos`, anywhere on the table, or nullptr
// when it can; the troop numbered `ignore` is the one being moved.
const char *troop_error(vec2 pos, i32 ignore = -1);
// Sending men is free: no shop and no limit on the groups.
bool add_troop(context &ctx, i32 tier, vec2 pos);
// A new place for a troop's flag; its orders stay.
bool move_troop(context &ctx, i32 index, vec2 pos);
// More or fewer men in a group; later groups are sent at that size.
bool set_troop_tier(context &ctx, i32 index, i32 tier);
void remove_troop(context &ctx, i32 index);

// Where a player's group starts the fight: in the home band straight below
// its flag (at the flag when that is at home), on open ground. It walks from
// there to its flag.
vec2 troop_home(const troop &t);
void clear_board(context &ctx);

// Formation slots of a group, relative to its centre, front row first.
std::vector<vec2> formation_slots(i32 tier);

// The turf at `pos`, or -1.
i32 turf_at(vec2 pos);
// Turfs held by each side, and by nobody.
void turf_counts(i32 &player, i32 &enemy, i32 &free);

// --- Fight -------------------------------------------------------------------

bool start_battle(context &ctx);
void redeploy(context &ctx);   // back to the table with the last setup
void set_speed(context &ctx, i32 index);

void add_popup(vec2 pos, rgba col, const char *text, f32 time = 1.0f);
void add_particle(fx_kind kind, vec2 pos, vec2 vel, f32 size, f32 life, rgba col = {1.0f, 1.0f, 1.0f, 1.0f},
                  f32 delay = 0.0f);
void fx_dust(vec2 pos, i32 puffs, f32 spread); // feet, blows

} // namespace sandtable
