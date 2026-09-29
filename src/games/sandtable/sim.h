#pragma once

#include "types.h"

namespace sandtable {

void sim_init(context &ctx);
void sim_update(context &ctx);

// --- Deployment --------------------------------------------------------------

void load_level(context &ctx, i32 index);
i32 gold_left();
i32 chips_on_board(side owner);

bool shop_buy(context &ctx, arm a, i32 tier);
bool reserve_merge(context &ctx, arm a, i32 tier); // 3 of `tier` -> 1 of `tier + 1`
bool reserve_split(context &ctx, arm a, i32 tier); // 1 of `tier` -> 3 of `tier - 1`
bool reserve_sell(context &ctx, arm a, i32 tier);

// Takes a chip from the reserve onto the cursor.
bool hold_from_reserve(context &ctx, arm a, i32 tier);
// Puts the chip on the cursor back into the reserve.
void drop_held(context &ctx);
// Why the held chip cannot go at `pos`, or nullptr when it can.
const char *placement_error(vec2 pos);
bool place_held(context &ctx, vec2 pos);
// The chip under `pos` on the table, or -1.
i32 board_chip_at(vec2 pos, side owner);
// Lifts a placed chip onto the cursor.
void lift_board_chip(context &ctx, i32 index);
void return_board_chip(context &ctx, i32 index);
void clear_board(context &ctx);

// Formation slots of a chip, relative to its centre, front row first.
std::vector<vec2> formation_slots(arm a, i32 tier);
f32 figure_radius(arm a, f32 weight);

// --- Battle ------------------------------------------------------------------

bool start_battle(context &ctx);
void redeploy(context &ctx);   // back to the table with the last deployment
void next_level(context &ctx);
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
