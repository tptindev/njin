#pragma once

#include "person.h"
#include "types.h"

#include <string>
#include <vector>

namespace sandtable {

// The gangs of the town (CONCEPT.md, "Gameplay v1"). The player is the boss
// of the first; the others are rivals. Each gang starts in a small patch of
// town: its headquarters (a building set up as one, city/interior.h: the men's
// lounge on the ground floor, meeting rooms above, the boss's office at the
// top) and the shops of that block, which pay it already.
//
// Its men have ranks. Idle, they sit about inside the headquarters, the
// higher on a higher floor (the boss and his right hand in the office, the
// captains in the meeting rooms, the soldiers in the lounge); mustered, they
// come out and stand before the door in rows by rank, facing the boss.
//
// A man is sent to a shop to squeeze it into paying protection (the first
// visit; harder if it pays another gang) or to collect what it owes: he walks
// there on the town's physics (physics.h), talks, walks the money back. A
// block is a gang's when at least half its shops pay that gang.
//
// The day goes on at the speed the player sets (state.speed); each new day the
// men are paid and paying shops owe another day of protection.

// The game clock: seconds of play for an hour of the town at speed 1.
inline constexpr f32 seconds_per_hour = 20.0f;

enum class rank : u8 {
  soldier, // lính
  captain, // tổ trưởng
  deputy,  // cánh tay phải
  boss,    // đại ca
};
const char *rank_name(rank r);

enum class job : u8 {
  idle,      // at the headquarters: inside, or in the muster outside
  going,     // walking to a shop
  talking,   // at its door
  returning, // walking back, maybe with the money
};

struct lackey {
  std::string name;
  rank rk = rank::soldier;
  i32 strength = 5; // Sức: how hard he leans on a shopkeeper
  i32 grit = 5;     // Lì: how long he stays loyal unpaid (later)
  i32 wits = 5;     // Lanh: how much he gets out of a shop (a bigger take)
  i32 wage = 100;   // thousands of đồng a day
  job task = job::idle;
  i32 target = -1;  // the shop he was sent to
  i32 carrying = 0; // money on the way back
  f32 timer = 0.0f; // seconds left at the door

  // On the table.
  vec2 pos{};
  f32 facing = 0.0f;
  bool inside = true; // in the headquarters (not on the physics), on `floor`
  i32 floor = 0;
  character3d_handle body{};
  nav_agent agent;
  act now = act::idle, was = act::idle;
  f32 time = 0.0f, was_time = 0.0f, blend = 0.0f;
  f32 speed = 0.0f;
};

struct ledger_line {
  i32 day = 1;
  f32 hour = 0.0f;
  i32 amount = 0; // + in, - out
  std::string what;
};

struct gang_state {
  std::string name;
  rgba colour{};
  i32 money = 1500; // thousands of đồng
  i32 hq = -1;      // building
  std::vector<lackey> men; // the boss first
  std::vector<ledger_line> ledger; // newest last
  i32 turf = 0;                    // blocks held
  bool mustered = false;           // the idle men out before the door, in rows
};

// What each shop has with the gangs.
struct shop_state {
  i32 owner = -1; // the gang it pays, -1 none
  i32 owed = 0;   // protection built up since last collected, thousands
};

// All of them; the player's is gangs()[0], also gang().
std::vector<gang_state> &gangs();
gang_state &gang();
std::vector<shop_state> &shops();
// Per block, the gang that holds it (-1 none); `turf_version` goes up when it
// changes.
const std::vector<i8> &turf_owner();
u32 turf_version();

// New gangs on the current city (world()): headquarters in small blocks far
// apart, their men, their first turf.
void gang_start(context &ctx, u32 seed);
// Each fixed step: the clock, the men walking and talking, the day's pay.
void gang_step(context &ctx, f32 dt);
// Each frame: the men's motions.
void gang_update(f32 dt);
// The men and the headquarters' flags. Between begin_3d() and end_3d().
void gang_draw(context &ctx);
// The men out on the street within `range` world units of `at`, for a second
// eye (feeds.h).
void gang_draw_around(context &ctx, vec2 at, f32 range);

// The man looked at (a click on him in the world, or "Xem" in the men
// popup): city/render_hover.cpp draws a ring under him, following him as he
// walks, until someone else is picked or gang_unfocus_man() is called.
void gang_focus_man(i32 gi, i32 mi);
void gang_unfocus_man();
// Where he stands now and how high (world units: his floor, inside), if
// anyone is focused and he is drawn (outside, or on the open floor).
bool gang_focused_pos(vec2 &out, f32 &lift);
// The gang man nearest `screen`, among the ones actually drawn this frame (in
// view, and on the floor open if he is inside); false if none within
// `max_px` screen pixels.
bool gang_pick_man(context &ctx, vec2 screen, f32 max_px, i32 &gi, i32 &mi);

// Orders, from the HUD, for the player's gang.
// Sends man `m` to shop `b`; false if he cannot go or it cannot be reached.
bool gang_send(context &ctx, i32 m, i32 b);
// Takes on a new soldier for `recruit_cost`; false without the money.
inline constexpr i32 recruit_cost = 400;
bool gang_recruit(context &ctx);
// The idle men come out before the door in rows by rank, or go back in.
void gang_muster(context &ctx, bool out);

// What the HUD shows.
i32 gang_income_per_day(); // protection of the shops paying the player, a day
i32 gang_wages_per_day();
const char *job_name(job j);
// The name of the man's current shop, or "".
const char *gang_target_name(const lackey &m);
// The gang whose headquarters building `b` is, or -1.
i32 gang_of_hq(i32 b);

} // namespace sandtable
