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

// --- Arms (binh chủng) -------------------------------------------------------
//
// Counters: archers beat spears, cavalry beats archers and artillery, spears
// beat cavalry, infantry beats spears and shrugs off arrows, artillery breaks
// dense blocks. Elephants trample infantry, archers and horses (horses fear
// them) and fall to long spears and guns. Boats keep to the water and shoot
// from it; guns and fire arrows sink them.

enum class arm : i32 { infantry = 0, spear, archer, cavalry, artillery, elephant, boat, count };

// Boats sail; everyone else walks.
inline bool sails(arm a) { return a == arm::boat; }

inline constexpr i32 arm_count = static_cast<i32>(arm::count);

struct arm_spec {
  const char *name;
  const char *tag; // short name
  const char *desc;
  i32 men_per_figure; // artillery: one gun is a crew of several men
  // Per man; a figure that stands for several men multiplies hp and damage.
  f32 hp;
  f32 damage;
  f32 interval;  // seconds between blows or shots
  f32 range;     // 0 is melee
  f32 min_range; // ranged units will not fire closer than this
  f32 reach;     // melee reach past touching
  f32 speed;
  f32 splash;    // artillery shell radius, elephant trample radius
  f32 body;      // figure radius for one man
};

inline constexpr std::array<arm_spec, arm_count> arms{{
    {"Bộ binh", "BB", "Kiếm khiên. Bền, khiên đỡ nửa sát thương tên. Thắng thương binh khi giáp lá cà.",
     1, 100.0f, 11.0f, 1.0f, 0.0f, 0.0f, 3.0f, 40.0f, 0.0f, 4.2f},
    {"Thương binh", "TB", "Giáo dài. Chặn đứng kỵ binh (x3 sát thương, triệt xung phong). Sợ tên.",
     1, 90.0f, 9.0f, 1.1f, 0.0f, 0.0f, 9.0f, 38.0f, 0.0f, 4.2f},
    {"Cung thủ", "CT", "Bắn xa. Mưa tên diệt thương binh; yếu khi bị áp sát.",
     1, 60.0f, 7.0f, 1.5f, 250.0f, 0.0f, 3.0f, 40.0f, 0.0f, 4.0f},
    {"Kỵ binh", "KB", "Nhanh. Lao đủ đà thì xung phong x3; x2 vào cung thủ và pháo. Kỵ thương binh.",
     1, 150.0f, 13.0f, 1.0f, 0.0f, 0.0f, 4.0f, 92.0f, 0.0f, 5.2f},
    {"Pháo binh", "PB", "Mỗi khẩu 5 người. Bắn cực xa, nổ lan diệt đội hình dày. Chậm, không tự vệ.",
     5, 50.0f, 16.0f, 4.0f, 480.0f, 110.0f, 0.0f, 20.0f, 26.0f, 3.6f},
    {"Tượng binh", "VO", "Mỗi con voi 5 người. Rất trâu, giẫm đạp cả đám quanh nó. Ngựa sợ voi. Sợ giáo dài và pháo.",
     5, 170.0f, 13.0f, 1.6f, 0.0f, 0.0f, 6.0f, 56.0f, 16.0f, 5.0f},
    {"Chiến thuyền", "TH", "Mỗi thuyền 10 người. Chỉ đi trên sông suối, bắn tên từ dưới nước. Đặt gần sông. Sợ pháo.",
     10, 80.0f, 6.0f, 1.6f, 230.0f, 0.0f, 3.0f, 62.0f, 0.0f, 4.2f},
}};

inline const arm_spec &spec(arm a) { return arms[static_cast<i32>(a)]; }

// Damage multiplier: attacker arm (row) against defender arm (column).
inline constexpr f32 counter[arm_count][arm_count] = {
    //  inf   spear  arch  cav   art   ele   boat
    {1.0f, 1.4f, 1.0f, 1.0f, 1.3f, 0.7f, 1.0f}, // infantry
    {0.9f, 1.0f, 1.0f, 3.0f, 1.3f, 2.2f, 1.0f}, // spear
    {0.5f, 1.6f, 1.0f, 0.9f, 1.0f, 0.6f, 1.2f}, // archer (arrows)
    {1.0f, 0.6f, 2.0f, 1.0f, 2.0f, 0.5f, 0.5f}, // cavalry
    {1.0f, 1.2f, 1.0f, 1.0f, 1.0f, 1.6f, 1.8f}, // artillery
    {1.5f, 0.7f, 1.8f, 1.8f, 1.4f, 1.0f, 1.0f}, // elephant
    {0.8f, 1.3f, 1.0f, 1.0f, 1.0f, 0.8f, 1.0f}, // boat (arrows)
};

// --- Troop sizes (tiers) -----------------------------------------------------
//
// A troop of a tier is `men` soldiers, three times the tier below; past
// company size one figure stands for several men so a corps stays a few
// hundred figures.

inline constexpr i32 tier_count = 7;

struct tier_spec {
  const char *name;
  const char *value; // men, short
  i32 men;
  i32 max_figures;
  rgba color;  // its flag
  rgba stripe; // (unused art colour)
};

inline constexpr std::array<tier_spec, tier_count> tiers{{
    {"Tiểu đội", "10", 10, 10, rgb(232, 228, 216), rgb(60, 60, 64)},
    {"Trung đội", "30", 30, 30, rgb(196, 44, 40), rgb(245, 240, 230)},
    {"Đại đội", "90", 90, 90, rgb(40, 140, 72), rgb(245, 240, 230)},
    {"Tiểu đoàn", "270", 270, 135, rgb(34, 34, 38), rgb(230, 200, 90)},
    {"Trung đoàn", "810", 810, 162, rgb(110, 50, 150), rgb(245, 240, 230)},
    {"Sư đoàn", "2.4K", 2430, 243, rgb(230, 150, 30), rgb(40, 30, 20)},
    {"Quân đoàn", "7.3K", 7290, 270, rgb(200, 170, 70), rgb(110, 20, 20)},
}};

inline i32 figure_count(arm a, i32 tier) {
  const i32 by_crew = std::max(1, tiers[tier].men / spec(a).men_per_figure);
  return std::min(by_crew, tiers[tier].max_figures);
}

struct troop {
  arm type = arm::infantry;
  i32 tier = 0;
  side owner = side::player;
  // The flag. The enemy stands here when the battle starts; the player's
  // troops start at home (troop_home) and march here to hold it.
  vec2 pos{};
  vec2 face{0.0f, -1.0f}; // player: the way it faces at its flag, the way it attacks
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
  f32 weight = 1.0f; // men this figure stands for
  f32 radius = 4.0f;
  f32 cooldown = 0.0f;
  f32 think = 0.0f; // time to the next target search
  f32 run = 0.0f;   // distance charged in a straight line (cavalry)
  f32 flash = 0.0f; // hit flash
  f32 anim = 0.0f;  // animation clock, started at random so a block does not step in unison
  f32 route = 0.0f; // time to the next look for a way round
  vec2 via{};       // where to walk when the goal is behind something that cannot be crossed
  bool use_via = false;
  i32 target = -1;
  i32 group = 0;
  arm type = arm::infantry;
  side owner = side::player;
  bool alive = true;
  bool fighting = false;
  bool moving = false;
};

struct group {
  arm type = arm::infantry;
  i32 tier = 0;
  side owner = side::player;
  vec2 anchor{};
  vec2 dir{0.0f, -1.0f};
  vec2 centroid{};
  i32 alive = 0;
  i32 figures = 0;
  nav_agent path; // the anchor's way to the enemy, round mountains, rivers and cliffs
  f32 repath = 0.0f;
  // A garrison (the player's blocks) does not hunt: it marches to `post`,
  // faces `face` and fights only what comes within its guard.
  bool garrison = false;
  vec2 post{};
  vec2 face{0.0f, -1.0f};
  f32 span = 0.0f; // how far the formation reaches from its anchor
};

struct projectile {
  vec2 from{};
  vec2 to{};
  f32 t = 0.0f;
  f32 duration = 0.5f;
  f32 damage = 0.0f;
  f32 splash = 0.0f;
  f32 arc = 0.0f;
  i32 target = -1; // arrows hit this figure if it is still alive
  arm source = arm::archer;
  side owner = side::player;
};

struct corpse {
  vec2 pos{};
  vec2 facing{0.0f, -1.0f};
  f32 radius = 3.0f;
  arm type = arm::infantry;
  side owner = side::player;
};

// What a particle is. Each kind has its own motion and colour over its life
// (sim.cpp moves them, render.cpp colours them).
enum class fx_kind : i32 {
  spark,  // a bright chip of `color` that slows down
  fire,   // white-hot to yellow, orange, red, then gone; rises a little
  smoke,  // grey puff that rises, grows and thins
  dust,   // sand-coloured puff that drifts and grows
  debris, // dark bit thrown out of a blast
  ring,   // shockwave: a circle that grows to `size`
};

struct fx_particle {
  vec2 pos{};
  vec2 vel{};
  f32 life = 0.0f;
  f32 max_life = 0.5f;
  f32 size = 4.0f; // world units: side of the square, or the ring's final radius
  rgba color{};
  fx_kind kind = fx_kind::spark;
  f32 delay = 0.0f; // seconds before it shows (smoke after the fire)
};

// An invisible shockwave: it bends the picture behind its front as it runs
// out from a blast (assets/shaders/shockwave.fs).
struct shockwave {
  vec2 pos{};
  f32 radius = 60.0f;   // world units, where the front ends up
  f32 strength = 3.0f;  // screen pixels the front shifts the picture at first
  f32 time = 0.0f;
  f32 duration = 0.45f;
};

// A burnt patch where a shell landed; stays for the battle.
struct scorch {
  vec2 pos{};
  f32 radius = 10.0f;
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

// A circle menu round a point on the table: the arms a troop can be raised
// as there (right click on the table), or the orders for a troop (click on it).
enum class menu_kind : i32 { none, arms, orders };

struct radial_menu {
  menu_kind kind = menu_kind::none;
  vec2 at{};              // world: where it was opened
  i32 troop = -1;         // orders: which troop on the table
  std::vector<i32> items; // arm or order numbers, round from the top
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

  // Deployment is free: any arm, any size, anywhere, as many troops as wanted.
  std::vector<troop> board;
  i32 new_tier = 4;  // size of the next troop raised: the last one chosen
  radial_menu menu;
  // An order waiting for a place on the table, and for which troop.
  command cmd = command::none;
  i32 selected = -1;
  // The table when the player pressed deploy, for "set up again".
  std::vector<troop> saved_board;

  // Battle
  std::vector<soldier> soldiers;
  std::vector<group> groups;
  std::vector<projectile> projectiles;
  std::vector<corpse> corpses;
  std::vector<scorch> scorches;
  std::vector<shockwave> shockwaves;
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
