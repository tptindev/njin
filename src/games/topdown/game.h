// Old Stone Forest: the top-down sample. Shared state and components.
#pragma once
#include <njin.h>
#include <string>
#include <vector>

namespace td {
using namespace njin;

constexpr u32 layer_world = layer_bit(0);
constexpr u32 layer_player = layer_bit(1);
constexpr u32 layer_enemy = layer_bit(2);
constexpr u32 layer_sword = layer_bit(3);
constexpr u32 layer_thing = layer_bit(4); // chest, sign, npc

// Everything that stands on the map is drawn in one layer sorted by y, so the
// player walks behind trees and in front of them.
constexpr i32 draw_things = 5;
constexpr i32 draw_fx = 6;

struct player_tag {
  f32 hurt_timer = 0.0f;  // invulnerable while > 0
  f32 attack_timer = 0.0f; // sword out while > 0
  f32 attack_cooldown = 0.0f;
  i32 hp = 5;
  vec2 last_dir{0.0f, 1.0f};
};
struct slime {
  i32 hp = 2;
  f32 stun = 0.0f;       // knocked back: no steering
  f32 repath = 0.0f;
  f32 wander = 0.0f;
  vec2 wander_dir{};
  nav_agent agent{};
  vec2 knock{};
  bool chasing = false;
};
struct chest_tag {
  bool open = false;
};
struct sign_tag {
  std::string text;
};
struct npc_tag {
  std::string dialog;
};
struct sword_tag {};

struct game_state {
  texture_handle sprites{};
  sound_handle s_swing{}, s_hit{}, s_hurt{}, s_dash{}, s_blip{}, s_select{}, s_win{};
  music_handle m_forest{};
  dialog_script elder{};

  axis_handle move_x{}, move_y{};
  action_handle attack{}, dash{}, interact{}, pause{};

  scene_handle title{}, play{}, win{}, over{};

  level_handle level{};
  entt::entity player = entt::null;
  entt::entity camera = entt::null;
  nav_grid nav{};
  i32 slimes_left = 0;
  i32 hits_taken = 0;
  f32 run_time = 0.0f;
  entt::entity near_talk = entt::null;
  bool paused = false;
  bool settings_open = false;
};

extern game_state g;

mod_desc play_module();
void register_prefabs(njin_ctx &ctx);
void play_enter(njin_ctx &ctx);
void play_exit(njin_ctx &ctx);
mod_desc menus_module();
void title_enter(njin_ctx &ctx);
void end_enter(njin_ctx &ctx);
std::string format_time(f32 seconds);

// Source rectangle of sprite-sheet cell `index` (8 cells per row, 16 x 16 each).
inline rect cell_of(i32 index) {
  return rect{{(f32)(index % 8) * 16.0f, (f32)(index / 8) * 16.0f}, {16.0f, 16.0f}};
}
} // namespace td
