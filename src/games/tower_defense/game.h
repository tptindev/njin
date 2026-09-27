#pragma once

#include <njin.h>

#include <array>

namespace defense {
using namespace njin;

inline constexpr f32 field_x = 24.0f;
inline constexpr f32 field_y = 92.0f;
inline constexpr f32 field_w = 896.0f;
inline constexpr f32 field_h = 568.0f;
inline constexpr f32 grid_step = 48.0f;
inline constexpr f32 road_width = 42.0f;
inline constexpr i32 wave_limit = 8;

constexpr rgba rgb(i32 r, i32 g, i32 b, i32 a = 255) {
  return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
}

inline constexpr rgba ink = rgb(17, 25, 36);
inline constexpr rgba paper = rgb(235, 239, 230);
inline constexpr rgba muted = rgb(151, 166, 171);
inline constexpr rgba gold = rgb(247, 190, 91);
inline constexpr rgba green = rgb(100, 208, 159);
inline constexpr rgba red = rgb(244, 111, 102);
inline constexpr rgba field_green = rgb(59, 112, 81);
inline constexpr rgba road_dark = rgb(48, 67, 60);
inline constexpr rgba road = rgb(155, 137, 105);
inline constexpr rgba road_light = rgb(186, 166, 129);

enum class game_screen { intro, playing, ended };
enum class tower_kind : i32 { archer, mage, cannon };
enum class foe_kind { scout, brute, regular };

struct tower_spec {
  const char *name;
  const char *role;
  i32 cost;
  f32 range;
  f32 damage;
  f32 reload;
  f32 splash;
  f32 projectile_speed;
  rgba color;
};

inline constexpr std::array<tower_spec, 3> specs{{
    {"CUNG THỦ", "Bắn nhanh, đơn mục tiêu", 75, 154.0f, 13.0f, 0.46f, 0.0f, 430.0f, rgb(94, 212, 183)},
    {"PHÁP SƯ", "Nổ lan, sát thương vùng", 120, 137.0f, 25.0f, 1.12f, 42.0f, 310.0f, rgb(176, 142, 255)},
    {"PHÁO THỦ", "Tầm xa, đạn nặng", 145, 190.0f, 39.0f, 1.46f, 52.0f, 275.0f, rgb(255, 174, 96)},
}};

struct enemy_component {
  f32 progress = 0.0f;
  f32 hp = 1.0f;
  f32 max_hp = 1.0f;
  f32 speed = 40.0f;
  f32 radius = 12.0f;
  i32 reward = 12;
  foe_kind kind = foe_kind::regular;
};

struct damage_number_component {
  i32 amount = 0;
  rgba color = paper;
  f32 alpha = 1.0f;
};

struct tower_component {
  tower_kind kind = tower_kind::archer;
  f32 cooldown = 0.0f;
  i32 level = 1;
};

struct projectile_component {
  entt::entity target = entt::null;
  vec2 last_target{};
  tower_kind kind = tower_kind::archer;
  f32 speed = 300.0f;
  f32 damage = 10.0f;
  f32 splash = 0.0f;
  rgba color{};
};

struct game_state {
  game_screen screen = game_screen::intro;
  i32 gold = 265;
  i32 lives = 20;
  i32 wave = 0;
  i32 spawned = 0;
  i32 wave_size = 0;
  i32 build_kind = -1;
  i32 speed = 1;
  f32 spawn_timer = 0.0f;
  bool wave_active = false;
  bool paused = false;
  bool end_popup_open = false;
  entt::entity selected = entt::null;
  std::array<f32, 12> path_lengths{};
  f32 path_total = 0.0f;
};

inline constexpr std::array<vec2, 12> path{{
    {field_x + 3.0f, field_y + 264.0f},
    {field_x + 126.0f, field_y + 264.0f},
    {field_x + 126.0f, field_y + 96.0f},
    {field_x + 276.0f, field_y + 96.0f},
    {field_x + 276.0f, field_y + 400.0f},
    {field_x + 426.0f, field_y + 400.0f},
    {field_x + 426.0f, field_y + 194.0f},
    {field_x + 576.0f, field_y + 194.0f},
    {field_x + 576.0f, field_y + 336.0f},
    {field_x + 736.0f, field_y + 336.0f},
    {field_x + 736.0f, field_y + 240.0f},
    {field_x + 886.0f, field_y + 240.0f},
}};

inline constexpr rect field{{field_x, field_y}, {field_w, field_h}};

extern game_state game;

bool inside(rect area, vec2 point);
void toast(njin_ctx &ctx, const char *message);
vec2 point_on_path(f32 progress);
f32 distance_to_road(vec2 point);
vec2 snapped(vec2 point);
bool placement_ok(njin_ctx &ctx, vec2 position);
void reset_game(njin_ctx &ctx);
void return_to_title(njin_ctx &ctx);
void launch_wave(njin_ctx &ctx);
void upgrade_selected(njin_ctx &ctx);
void sell_selected(njin_ctx &ctx);
void tower_fired_effect(njin_ctx &ctx, entt::entity tower);
void enemy_hit_effect(njin_ctx &ctx, entt::entity enemy, f32 damage, tower_kind source, bool killed);
void draw_damage_numbers(njin_ctx &ctx);
void text(njin_ctx &ctx, const char *value, f32 x, f32 y, f32 size, rgba color);

void input(njin_ctx &ctx);
void update_game(njin_ctx &ctx);
void draw_background(njin_ctx &ctx);
void draw_field(njin_ctx &ctx);
void draw_sidebar(njin_ctx &ctx);
void draw_overlay(njin_ctx &ctx);
mod_desc module();
} // namespace defense
