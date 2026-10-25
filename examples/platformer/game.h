// Sprout's Climb: the platformer sample. Shared state and components.
#pragma once
#include <njin.h>
#include <string>

namespace plat {
using namespace njin;

// Collision layers.
constexpr u32 layer_world = layer_bit(0); // tiles, moving platforms (the level's default)
constexpr u32 layer_player = layer_bit(1);
constexpr u32 layer_enemy = layer_bit(2);
constexpr u32 layer_pickup = layer_bit(3);
constexpr u32 layer_hazard = layer_bit(4);

// Draw layers. The level's tile layers take 0 and up (level_desc::layer_base).
constexpr i32 draw_items = 10;
constexpr i32 draw_player = 12;
constexpr i32 draw_fx = 13;

struct player_tag {};
struct coin_tag {};
struct walker {
  f32 dir = -1.0f;
  f32 fall = 0.0f;
  bool squashed = false;
};
struct checkpoint {
  bool on = false;
};
struct exit_tag {};
struct hazard_tag {};
struct sign {
  std::string text; // string table key with '@', or plain text
};
struct npc {
  std::string dialog; // dialog file name without extension
};

struct game_state {
  // Resources.
  texture_handle sprites{};
  sound_handle s_jump{}, s_coin{}, s_stomp{}, s_hurt{}, s_land{}, s_check{}, s_blip{}, s_select{}, s_win{};
  music_handle m_title{}, m_level{};
  dialog_script owl{};

  // Controls.
  axis_handle move{};
  action_handle jump{}, down{}, interact{}, pause{};

  // Scenes.
  scene_handle title{}, play{}, win{};

  // The run.
  std::string level_file = "assets/level1.tmx";
  level_handle level{};
  entt::entity player = entt::null;
  entt::entity camera = entt::null;
  vec2 respawn{};
  i32 coins = 0;       // this level
  i32 coins_total = 0; // this level
  i32 run_coins = 0;   // levels already finished
  i32 run_total = 0;
  i32 deaths = 0;
  f32 run_time = 0.0f;
  bool dying = false;
  bool finished = false; // reached the exit, fading out
  entt::entity near_talk = entt::null; // sign or npc in reach

  // Menus.
  bool paused = false;
  bool settings_open = false;
};

extern game_state g;

// play.cpp
mod_desc play_module();
void register_prefabs(context &ctx);
void play_enter(context &ctx);
void play_exit(context &ctx);

// menus.cpp
mod_desc menus_module();
void title_enter(context &ctx);
void card_scene_enter(context &ctx);
void win_enter(context &ctx);
void draw_backdrop(context &ctx, vec2 camera_pos);
std::string format_time(f32 seconds);
} // namespace plat
