#pragma once

#include "types.h"

namespace sandtable {

void sim_init(context &ctx);
void sim_update(context &ctx);

// --- Deployment --------------------------------------------------------------

// Sets the table up for the battle, empty on the player's side.
void load_level(context &ctx);
i32 troop_count(side owner);

// Why a troop of arm `a` cannot plant its flag at home at `pos`, anywhere on
// the table, or nullptr when it can; the troop numbered `ignore` is the one
// being moved.
const char *troop_error(arm a, vec2 pos, i32 ignore = -1);
// Deployment is free: no shop and no limit on the troops.
bool add_troop(context &ctx, arm a, i32 tier, vec2 pos);
// A new home for a troop; its orders stay.
bool move_troop(context &ctx, i32 index, vec2 pos);
// More or fewer men in a troop; later troops are raised at that size.
bool set_troop_tier(context &ctx, i32 index, i32 tier);
void remove_troop(context &ctx, i32 index);

// Where a player's troop starts the battle: in the home band straight below
// its flag (at the flag when that is at home), on open ground, or on the
// nearest water for boats. It marches from there to its flag.
vec2 troop_home(const troop &t);
void clear_board(context &ctx);

// Formation slots of a troop, relative to its centre, front row first.
std::vector<vec2> formation_slots(arm a, i32 tier);
f32 figure_radius(arm a, f32 weight);

// --- Battle ------------------------------------------------------------------

bool start_battle(context &ctx);
void redeploy(context &ctx);   // back to the table with the last deployment
void set_speed(context &ctx, i32 index);

void add_popup(vec2 pos, rgba col, const char *text, f32 time = 1.0f);
void add_particle(fx_kind kind, vec2 pos, vec2 vel, f32 size, f32 life, rgba col = {1.0f, 1.0f, 1.0f, 1.0f},
                  f32 delay = 0.0f);

// Effects made of particles.
void fx_explosion(context &ctx, vec2 pos, f32 radius); // a shell landing
void fx_muzzle(vec2 pos, vec2 dir);                    // a gun firing
void fx_dust(vec2 pos, i32 puffs, f32 spread);         // feet, hooves, impacts
void fx_shockwave(vec2 pos, f32 radius, f32 strength, f32 duration);

} // namespace sandtable
