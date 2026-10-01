#pragma once

#include "clock.h"
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
// The day goes on by the town's clock (clock.h); each new day the men are
// paid and paying shops owe another day of protection. A shop only pays while
// it is open: a man at a shut door comes back with nothing.
//
// Upkeep (the gang's economy). The gang's cash comes from protection and
// goes on wages, the clinic and new men; it never goes below nothing: wages
// it cannot pay are owed to the men. Each man has his own pocket: his wage
// goes in each day, his food and rent come out. How he feels (morale) goes up
// when he is paid in full with something to spare, and down when his wages
// are owed, he cannot eat or pay rent, he is hurt and not treated, or worn
// out; grit slows the fall. Below `quit_morale` he may walk out, a little
// more likely each day. Out on jobs he tires; resting at the headquarters he
// recovers. Beaten (a squeeze that goes wrong, another gang's men) he is
// hurt; a clinic, paid by the day, heals him fast, otherwise slowly. Too
// tired or too hurt, he will not go out. The boss never leaves, takes no
// wage, and can be sent like anyone: with no one else, he does it all.
//
// A shop that no one of its gang has called at for days, and that has none
// of its men about, stops paying: a gang without the men to go round shrinks.
//
// A man can also be sent on a raid (errand::raid): to the shops of another
// gang's turf, to take their protection money there and then, shop after
// shop, until the raid has its quota. A shop pays unless the gang it pays has
// men close by. The rivals' bosses decide who goes where (gang_ai.h).

enum class rank : u8 {
  soldier, // lính
  captain, // tổ trưởng
  deputy,  // cánh tay phải
  boss,    // đại ca
};
const char *rank_name(rank r);

enum class errand : u8 {
  usual, // collect from a shop that pays us, squeeze one that does not
  raid,  // take another gang's protection money, shop after shop
};

enum class job : u8 {
  idle,       // at the headquarters: inside, or in the muster outside
  going,      // walking to a shop
  talking,    // at its door
  returning,  // walking back, maybe with the money
  patrolling, // walking a block of the turf from shop to shop, until tired or called back
};

// Upkeep, thousands of đồng.
inline constexpr i32 clinic_cost = 80;      // a day, for a hurt man the gang has treated
inline constexpr f32 quit_morale = 25.0f;   // below it he may walk out
inline constexpr f32 tired_limit = 80.0f;   // fatigue at which he will not go out
inline constexpr f32 hurt_limit = 40.0f;    // health below which he cannot work
inline constexpr i32 neglect_days = 3;      // days unvisited before a shop thinks of stopping

struct lackey {
  std::string name;
  rank rk = rank::soldier;
  i32 strength = 5; // Sức: how hard he leans on a shopkeeper
  i32 grit = 5;     // Lì: how long he stays loyal unpaid (later)
  i32 wits = 5;     // Lanh: how much he gets out of a shop (a bigger take)
  i32 wage = 100;   // thousands of đồng a day
  // His own life.
  i32 wallet = 0;       // his pocket: wages in, food and rent out
  i32 unpaid = 0;       // wages the gang owes him
  i32 unpaid_days = 0;  // days in a row not paid in full
  f32 morale = 70.0f;   // 0 to 100
  f32 fatigue = 0.0f;   // 0 to 100
  f32 health = 100.0f;  // 0 to 100
  bool treat = true;    // the gang pays the clinic while he is hurt
  bool treated = false; // the clinic has him today
  bool hungry = false;  // could not pay for food and rent today
  bool quitting = false; // walks out once he is back in

  job task = job::idle;
  i32 target = -1;  // the shop he was sent to (patrolling: the one he walks to next)
  i32 beat = -1;    // patrolling: the block he walks
  errand sent_on = errand::usual;
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

// A raid on another gang's turf: a few men, one block, a sum to take home.
struct raid_plan {
  bool on = false;      // men are still going from shop to shop
  i32 victim = -1;      // the gang raided
  i32 block = -1;
  i32 quota = 0;        // thousands: this much, then home
  i32 taken = 0;
  i32 day = 0;          // the day it was made
  std::vector<i32> hit; // shops already called at
  // When the raided gang's boss hears of it (clock_now()), from a shopkeeper,
  // once the raiders have gone home; < 0 none due.
  f64 report_at = -1.0;
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
  raid_plan raid;
};

// What each shop has with the gangs.
struct shop_state {
  i32 owner = -1; // the gang it pays, -1 none
  i32 owed = 0;   // protection built up since last collected, thousands
  // Last raided: by which gang (-1 never), on which day, for how much.
  i32 raided_by = -1;
  i32 raided_day = 0;
  i32 raided_amount = 0;
  i32 last_seen = 1; // the last day a man of its gang called (or stood near)
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

// Sends man `mi` of gang `gi` to shop `b` on `e`; false if he is busy, is the
// boss, or the shop cannot be reached.
bool gang_order(context &ctx, i32 gi, i32 mi, i32 b, errand e = errand::usual);
// Whether gang `gi` has men close enough to shop `b` to stop a raid there:
// out on the street near its door, or a few at home in a headquarters close by.
bool gang_guards(i32 gi, i32 b);

// The player's orders, the five things a man is sent to do (CONCEPT.md):
// - collect (thu tiền bảo kê): gang_order() to a shop that pays the gang;
// - patrol (tuần tra): gang_patrol(), round a block of the turf, shop to
//   shop, as long as he holds out: his being about keeps its shops paying
//   and stops raids there;
// - raid (gây hấn): gang_raid(), to a shop of another gang's: he takes its
//   money there and then and goes on to the next of that gang's shops in the
//   block, unless its men are about (then he may be beaten, and comes back);
// - expand (bành trướng): gang_order() to a shop that pays nobody (squeezed
//   into paying) or another gang (taken over, harder);
// - retreat (rút lui): gang_recall(), back to the headquarters now, with
//   whatever he has on him.
bool gang_patrol(context &ctx, i32 gi, i32 mi, i32 block);
bool gang_raid(context &ctx, i32 gi, i32 mi, i32 b);
// False if he is not out on anything.
bool gang_recall(context &ctx, i32 gi, i32 mi);

// Orders, from the HUD, for the player's gang.
// Sends man `m` to shop `b`; false if he cannot go or it cannot be reached.
bool gang_send(context &ctx, i32 m, i32 b);
// Takes on a new soldier for `recruit_cost`; false without the money.
inline constexpr i32 recruit_cost = 400;
bool gang_recruit(context &ctx);
// The same for any gang (the rivals' bosses hire too).
bool gang_hire(context &ctx, i32 gi);
// The idle men come out before the door in rows by rank, or go back in.
void gang_muster(context &ctx, bool out);

// Why man `m` cannot be sent now ("Mệt", "Bị thương", "Đang bận"), or
// nullptr if he can.
const char *gang_cannot_go(const lackey &m);
// What a man of rank `r` spends a day on food and rent.
i32 living_cost(rank r);
// A word for how he feels.
const char *mood_name(f32 morale);

// What the HUD shows.
i32 gang_income_per_day(); // protection of the shops paying the player, a day
i32 gang_wages_per_day();
i32 gang_clinic_per_day(); // the player's hurt men being treated, a day
i32 gang_wages_owed();     // wages the player's gang owes its men
const char *job_name(job j);
// The name of the man's current shop, or "".
const char *gang_target_name(const lackey &m);
// The gang whose headquarters building `b` is, or -1.
i32 gang_of_hq(i32 b);

} // namespace sandtable
