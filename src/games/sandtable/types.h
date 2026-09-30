#pragma once

#include <njin.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace sandtable {
using namespace njin;

// The sand table, in world units: 64 x 38 terrain tiles of 32 (levels.h). The
// player's home is the bottom band, where their troops start before marching
// to their flags; the enemy camps in the top band. The battle can go anywhere
// on the table.
inline constexpr f32 world_width = 2048.0f;
inline constexpr f32 world_height = 1216.0f;
inline constexpr f32 table_margin = 24.0f;
inline constexpr rect player_zone{{table_margin, 836.0f}, {world_width - table_margin * 2.0f, 356.0f}};
inline constexpr rect enemy_zone{{table_margin, table_margin}, {world_width - table_margin * 2.0f, 356.0f}};

constexpr rgba rgb(i32 r, i32 g, i32 b, i32 a = 255) {
  return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
}

// Palette
inline constexpr rgba col_bg_dark = rgb(20, 18, 16);
inline constexpr rgba col_sand = rgb(196, 170, 124);
inline constexpr rgba col_sand_dark = rgb(172, 146, 102);
inline constexpr rgba col_grid_lines = rgb(120, 96, 62, 70);
inline constexpr rgba col_frame = rgb(92, 60, 34);
inline constexpr rgba col_frame_light = rgb(140, 98, 58);
inline constexpr rgba col_river_deep = rgb(70, 118, 150);
inline constexpr rgba col_river_shallow = rgb(110, 158, 184);
inline constexpr rgba col_ford = rgb(170, 158, 120);
inline constexpr rgba col_forest = rgb(78, 110, 62);
inline constexpr rgba col_forest_dark = rgb(56, 84, 46);
inline constexpr rgba col_hill = rgb(170, 140, 96);
inline constexpr rgba col_hill_line = rgb(120, 92, 58, 150);

inline constexpr rgba col_player = rgb(205, 52, 44);
inline constexpr rgba col_player_light = rgb(245, 120, 100);
inline constexpr rgba col_player_dark = rgb(120, 24, 20);
inline constexpr rgba col_enemy = rgb(36, 110, 160);
inline constexpr rgba col_enemy_light = rgb(110, 180, 230);
inline constexpr rgba col_enemy_dark = rgb(18, 50, 80);

inline constexpr rgba col_gold = rgb(255, 208, 64);
inline constexpr rgba col_gold_light = rgb(255, 235, 130);
inline constexpr rgba col_white = rgb(245, 248, 250);
inline constexpr rgba col_muted = rgb(155, 168, 176);
inline constexpr rgba col_good = rgb(82, 215, 120);
inline constexpr rgba col_warn = rgb(240, 195, 55);
inline constexpr rgba col_bad = rgb(235, 65, 60);

enum class side : i32 { player = 0, enemy = 1 };

inline side other(side s) { return s == side::player ? side::enemy : side::player; }

// --- Đàn em -----------------------------------------------------------------
//
// A turf war between two gangs. Every man on either side is the same: a
// street fighter with his fists and feet. What a boss chooses is where to
// send them and how many at once.

struct fighter_spec {
  f32 hp;
  f32 damage;   // a punch; a kick lands harder
  f32 interval; // seconds between blows
  f32 reach;    // past touching
  f32 speed;    // walking, world units a second; running is faster
  f32 body;     // radius
};
inline constexpr fighter_spec fighter{100.0f, 12.0f, 0.8f, 3.0f, 30.0f, 4.2f};
inline constexpr f32 run_factor = 1.7f;   // running speed over walking
inline constexpr f32 blow_time = 0.45f;   // a punch or kick: wind-up, blow, back
inline constexpr f32 flinch_time = 0.25f; // reeling from a blow taken
inline constexpr f32 down_time = 1.2f;    // lying after being knocked down
inline constexpr f32 rise_time = 0.7f;    // getting back up

// --- Group sizes (tiers) -----------------------------------------------------
//
// One figure on the table is one man: gangs are small.

inline constexpr i32 tier_count = 5;

struct tier_spec {
  const char *name;
  const char *value; // men, short
  i32 men;
  rgba color; // its flag
};

inline constexpr std::array<tier_spec, tier_count> tiers{{
    {"Bộ ba", "3", 3, rgb(232, 228, 216)},
    {"Nhóm", "5", 5, rgb(196, 44, 40)},
    {"Toán", "8", 8, rgb(40, 140, 72)},
    {"Đám", "12", 12, rgb(34, 34, 38)},
    {"Băng", "20", 20, rgb(110, 50, 150)},
}};

struct troop {
  i32 tier = 0;
  side owner = side::player;
  // The flag. The enemy stands here when the fight starts; the player's
  // troops start at their home turf (troop_home) and walk here to hold it.
  vec2 pos{};
  vec2 face{0.0f, -1.0f}; // player: the way it faces at its flag, the way it attacks
};

// --- Turfs (địa bàn) ---------------------------------------------------------
//
// Places worth holding. Men standing on a turf with none of the other gang
// there claim it little by little; a full claim turns it theirs. When time
// runs out, the gang holding more turfs wins.

inline constexpr i32 nobody = -1; // a turf nobody holds

struct turf_def {
  const char *name;
  vec2 pos{};
  f32 radius = 150.0f;
  i32 held_by = nobody; // at the start: nobody, or a side
};

struct turf {
  const char *name = "";
  vec2 pos{};
  f32 radius = 150.0f;
  i32 held_by = nobody;
  f32 claim = 0.0f; // -1 the enemy's, 1 the player's; turns a turf at either end
  i32 men[2]{};     // on it now, by side
};

// --- Terrain -----------------------------------------------------------------

struct terrain_blob {
  vec2 pos{};
  f32 radius = 0.0f;
};

struct level_def {
  const char *name;
  const char *brief;
  std::vector<troop> enemy;
  std::vector<turf_def> turfs;
  i32 home_turf = 0; // the player's one turf at the start, where his men set out from
  // River: winds across the table near river_y, crossed only at the fords.
  bool river = false;
  f32 river_y = 600.0f;
  std::vector<rect> fords;
  // Features placed by hand, on top of the noise.
  std::vector<terrain_blob> forests;
  std::vector<terrain_blob> hills;
  // The noise: share of the table under mountains and hills, woods, and how
  // many streams; `seed` picks the map.
  u32 seed = 1;
  f32 mountains = 0.05f;
  f32 hill_amount = 0.1f;
  f32 woods = 0.08f;
  i32 streams = 1;
  // The hour the level starts at, 0 to 24; the clock runs during the battle.
  f32 hour = 10.0f;
};

// --- Battle ------------------------------------------------------------------

struct soldier {
  vec2 pos{};
  vec2 facing{0.0f, -1.0f};
  vec2 slot{}; // place in the formation, relative to the group anchor
  f32 hp = 1.0f;
  f32 max_hp = 1.0f;
  f32 radius = 4.0f;
  f32 cooldown = 0.0f;
  f32 think = 0.0f; // time to the next target search
  f32 flash = 0.0f; // hit flash
  f32 anim = 0.0f;  // animation clock, started at random so a group does not breathe in unison
  // What the body is doing, for its animation (render.cpp) and for what it
  // may do next.
  f32 stride = 0.0f; // distance walked, for the steps
  f32 pace = 0.0f;   // world units a second this step
  f32 act = 0.0f;    // time left in a blow being thrown
  i32 act_kind = 0;  // that blow: 0 left punch, 1 right punch, 2 kick
  f32 hurt = 0.0f;   // time left flinching from a blow taken
  f32 down = 0.0f;   // time left lying knocked down
  f32 rise = 0.0f;   // time left getting up
  f32 route = 0.0f; // time to the next look for a way round
  vec2 via{};       // where to walk when the goal is behind something that cannot be crossed
  bool use_via = false;
  i32 target = -1;
  i32 group = 0;
  side owner = side::player;
  bool alive = true;
  bool fighting = false;
  bool moving = false;
};

struct group {
  i32 tier = 0;
  side owner = side::player;
  vec2 anchor{};
  vec2 dir{0.0f, -1.0f};
  vec2 centroid{};
  i32 alive = 0;
  i32 figures = 0;
  nav_agent path; // the anchor's way, round mountains, rivers and cliffs
  f32 repath = 0.0f;
  vec2 goal{};    // where it is heading (an enemy group goes for turfs and men)
  // A garrison (the player's groups) does not hunt: it marches to `post`,
  // faces `face` and fights only what comes within its guard.
  bool garrison = false;
  vec2 post{};
  vec2 face{0.0f, -1.0f};
  f32 span = 0.0f; // how far the formation reaches from its anchor
};

struct corpse {
  vec2 pos{};
  vec2 facing{0.0f, -1.0f};
  f32 radius = 3.0f;
  side owner = side::player;
  f32 age = 0.0f; // seconds since he fell: he falls, then lies still
};

// What a particle is. Each kind has its own motion and colour over its life
// (sim.cpp moves them, render.cpp colours them).
enum class fx_kind : i32 {
  spark, // a bright chip of `color` that slows down
  dust,  // sand-coloured puff that drifts and grows
};

struct fx_particle {
  vec2 pos{};
  vec2 vel{};
  f32 life = 0.0f;
  f32 max_life = 0.5f;
  f32 size = 4.0f; // world units
  rgba color{};
  fx_kind kind = fx_kind::spark;
  f32 delay = 0.0f; // seconds before it shows (smoke after the fire)
};

struct popup_text {
  vec2 pos{};
  f32 timer = 1.0f;
  f32 max_time = 1.0f;
  rgba color = col_white;
  std::string label;
};

enum class phase { deploy, battle, result };

// Orders a troop on the table can be given, from its circle menu.
enum class order : i32 { face, move, grow, shrink, withdraw, count };
inline constexpr i32 order_count = static_cast<i32>(order::count);

// A circle menu of the orders for a troop, opened by a click on its flag.
enum class menu_kind : i32 { none, orders };

struct radial_menu {
  menu_kind kind = menu_kind::none;
  vec2 at{};              // world: where it was opened
  i32 troop = -1;         // orders: which troop on the table
  std::vector<i32> items; // order numbers, round from the top
  std::vector<bool> enabled;
};

// What the next click on the table does, after an order that needs a place.
enum class command : i32 {
  none,
  face, // click sets the way the block faces
  move, // click moves the troop's flag
};

struct game_state {
  phase screen = phase::deploy;
  bool won = false;
  bool restart_requested = false; // result popup button, handled next frame

  // Sending men is free: any size, anywhere, as many groups as wanted.
  std::vector<troop> board;
  i32 new_tier = 2;  // size of the next group sent: the last one chosen
  radial_menu menu;
  // An order waiting for a place on the table, and for which troop.
  command cmd = command::none;
  i32 selected = -1;
  // The table when the player pressed deploy, for "set up again".
  std::vector<troop> saved_board;

  // Battle
  std::vector<soldier> soldiers;
  std::vector<group> groups;
  std::vector<corpse> corpses;
  std::vector<turf> turfs;
  f32 battle_time = 0.0f;
  f32 hour = 10.0f; // time of day, 0 to 24
  f32 men_start[2]{};
  f32 men_now[2]{};
  i32 speed_index = 0;

  // The camera (view.h): it looks at `cam_target` (table coordinates) from
  // `cam_distance` 3D units away, turned `cam_yaw` degrees round it (0 is from
  // the player's side). The controls move the goals; the camera eases to them.
  vec2 cam_target{world_width * 0.5f, world_height * 0.56f};
  vec2 cam_target_goal{world_width * 0.5f, world_height * 0.56f};
  f32 cam_yaw = 0.0f;
  f32 cam_yaw_goal = 0.0f;
  f32 cam_distance = 52.0f;
  f32 cam_distance_goal = 52.0f;

  std::vector<popup_text> popups;
  std::vector<fx_particle> particles;
};

extern game_state state;

inline constexpr f32 battle_time_limit = 240.0f;
// Seconds of battle for one hour of the day: a long battle runs into the night.
inline constexpr f32 seconds_per_hour = 8.0f;
inline constexpr f32 speed_steps[3] = {1.0f, 2.0f, 4.0f};

} // namespace sandtable
