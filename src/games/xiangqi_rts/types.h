#pragma once

#include <njin.h>

#include <array>
#include <string>
#include <vector>

namespace xiangqi {
using namespace njin;

inline constexpr f32 world_width = 2400.0f;
inline constexpr f32 world_height = 1600.0f;
inline constexpr f32 river_top = 720.0f;
inline constexpr f32 river_bottom = 880.0f;
inline constexpr f32 river_center_y = 800.0f;

constexpr rgba rgb(i32 r, i32 g, i32 b, i32 a = 255) {
  return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
}

// Color Palette
inline constexpr rgba col_bg_dark = rgb(20, 24, 28);
inline constexpr rgba col_ground = rgb(45, 54, 48);
inline constexpr rgba col_ground_dark = rgb(36, 44, 39);
inline constexpr rgba col_river_deep = rgb(34, 78, 114);
inline constexpr rgba col_river_shallow = rgb(56, 115, 158);
inline constexpr rgba col_water_foam = rgb(190, 225, 245, 180);
inline constexpr rgba col_bridge_wood = rgb(135, 95, 60);
inline constexpr rgba col_bridge_light = rgb(175, 130, 85);
inline constexpr rgba col_grid_lines = rgb(90, 110, 95, 80);

// Red Faction (Sở Quân / Player)
inline constexpr rgba col_red_base = rgb(238, 230, 212); // Antique ivory wood
inline constexpr rgba col_red_rim = rgb(200, 48, 42);   // Vermilion rim
inline constexpr rgba col_red_text = rgb(215, 38, 30);  // Vermilion calligraphy
inline constexpr rgba col_red_glow = rgb(255, 80, 70, 140);

// Black Faction (Hán Quân / AI)
inline constexpr rgba col_black_base = rgb(38, 44, 48);   // Dark obsidian wood
inline constexpr rgba col_black_rim = rgb(32, 178, 170);  // Cyan jade rim
inline constexpr rgba col_black_text = rgb(64, 224, 208); // Cyan jade text
inline constexpr rgba col_black_glow = rgb(40, 210, 200, 140);

// UI & FX Colors
inline constexpr rgba col_gold = rgb(255, 208, 64);
inline constexpr rgba col_gold_light = rgb(255, 235, 130);
inline constexpr rgba col_white = rgb(245, 248, 250);
inline constexpr rgba col_muted = rgb(155, 168, 176);
inline constexpr rgba col_health_green = rgb(82, 215, 120);
inline constexpr rgba col_health_yellow = rgb(240, 195, 55);
inline constexpr rgba col_health_red = rgb(235, 65, 60);

enum class faction : i32 { red = 0, black = 1 };

enum class piece_type : i32 {
  general = 0,  // Tướng / Soái
  advisor = 1,  // Sĩ
  elephant = 2, // Tượng
  chariot = 3,  // Xe / Xa
  cannon = 4,   // Pháo
  horse = 5,    // Mã
  pawn = 6      // Tốt / Binh
};

enum class unit_state : i32 {
  idle = 0,
  moving,
  attacking,
  charging, // Xe charge
  leaping   // Mã leap
};

struct piece_spec {
  const char *name_red;
  const char *name_black;
  const char *tag_red;
  const char *tag_black;
  const char *title;
  const char *desc;
  i32 cost;
  f32 max_hp;
  f32 attack_damage;
  f32 attack_range;
  f32 attack_interval;
  f32 move_speed;
  f32 radius;
};

inline constexpr std::array<piece_spec, 7> specs{{
    // 0: General / Marshal
    {"TƯỚNG", "SOÁI", "TƯỚNG", "SOÁI", "Đại Tướng Quân",
     "Chỉ huy tối cao. Hào quang Long Uy tăng sát thương & tốc độ quân ta lân cận. Tuyệt kỹ Hiệu Lệnh (R).",
     0, 1800.0f, 65.0f, 110.0f, 1.2f, 70.0f, 30.0f},

    // 1: Advisor
    {"SĨ", "SĨ", "SĨ", "SĨ", "Hộ Vệ Cận Thần",
     "Lá chắn hộ vệ. Tạo vòng bảo hộ giảm 35% sát thương cho đồng đội và gánh đòn cho Tướng.",
     120, 420.0f, 30.0f, 60.0f, 0.9f, 85.0f, 22.0f},

    // 2: Elephant
    {"TƯỢNG", "TƯỢNG", "TƯỢNG", "TƯỢNG", "Hộ Quốc Cự Tượng",
     "Đấu sĩ hộ quốc. Trên đất nhà tăng thủ & hồi máu mạnh. Đòn đánh dậm đất Địa Chấn lan rộng.",
     200, 1100.0f, 55.0f, 75.0f, 1.5f, 55.0f, 26.0f},

    // 3: Chariot
    {"XE", "XA", "XE", "XA", "Thiết Xa Tiên Phong",
     "Cơ động hỏa lực. Chạy đường thẳng kích hoạt Thiết Xa Xung Kích húc văng và xuyên thủng đối phương.",
     180, 550.0f, 60.0f, 160.0f, 0.8f, 125.0f, 24.0f},

    // 4: Cannon
    {"PHÁO", "PHÁO", "PHÁO", "PHÁO", "Thần Cơ Hỏa Pháo",
     "Pháo binh siêu tầm xa. Bắn đạn cầu vồng nổ lan. Khi có quân làm 'giá pháo' sẽ nổ bạo kích +60% sát thương!",
     150, 260.0f, 95.0f, 480.0f, 2.4f, 52.0f, 23.0f},

    // 5: Horse
    {"MÃ", "MÃ", "MÃ", "MÃ", "Thiết Kỵ Tung Hoành",
     "Kỵ binh đột kích. Tốc độ cực cao, có thể nhảy vượt địa hình, chém chí mạng vào pháo binh và hậu tuyến.",
     110, 380.0f, 45.0f, 65.0f, 0.85f, 145.0f, 21.0f},

    // 6: Pawn
    {"TỐT", "BINH", "TỐT", "BINH", "Tiên Phong Dũng Sĩ",
     "Bộ binh cơ bản giá rẻ. Khi vượt sông Sở Hà Hán Giới sẽ thức tỉnh: tăng vọt công, thủ, tốc đánh và tốc chạy!",
     50, 210.0f, 26.0f, 55.0f, 0.8f, 75.0f, 19.0f},
}};

struct unit_component {
  piece_type type = piece_type::pawn;
  faction side = faction::red;
  f32 hp = 210.0f;
  f32 max_hp = 210.0f;
  f32 attack_timer = 0.0f;
  f32 charge_time = 0.0f;
  f32 anim_timer = 0.0f;
  f32 radius = 20.0f;
  vec2 move_target{};
  bool has_move_target = false;
  entt::entity attack_target = entt::null;
  unit_state state = unit_state::idle;
  vec2 facing{0.0f, -1.0f};
  bool selected = false;
  bool crossed_river = false;
  bool is_screened_attack = false; // Pháo screen bonus indicator
};

struct projectile_component {
  vec2 pos{};
  vec2 start_pos{};
  vec2 target_pos{};
  entt::entity target_entity = entt::null;
  faction side = faction::red;
  piece_type source_type = piece_type::cannon;
  f32 speed = 360.0f;
  f32 damage = 50.0f;
  f32 splash_radius = 0.0f;
  f32 progress = 0.0f;
  f32 arc_height = 0.0f; // for cannon ballistic curve
  bool is_screened = false;
  rgba color{};
};

struct outpost_component {
  vec2 pos{};
  f32 radius = 90.0f;
  i32 owner = -1; // -1: neutral, 0: red, 1: black
  f32 control = 0.0f; // -100 (Black) to +100 (Red)
  const char *name = "Tiền Tiêu";
};

struct damage_popup {
  vec2 pos{};
  f32 amount = 0.0f;
  f32 timer = 1.0f;
  f32 max_time = 1.0f;
  rgba color = col_white;
  bool is_crit = false;
  std::string label;
};

struct fx_particle {
  vec2 pos{};
  vec2 vel{};
  f32 life = 0.0f;
  f32 max_life = 0.5f;
  f32 size = 4.0f;
  rgba color{};
  bool is_ring = false;
};

struct click_ping {
  vec2 pos{};
  f32 timer = 0.0f;
  f32 max_time = 0.45f;
  rgba color{};
};

enum class game_screen { playing, victory, defeat };

struct bridge_rect {
  rect area{};
  const char *name;
};

inline constexpr std::array<bridge_rect, 3> bridges{{
    {{{380.0f, river_top - 10.0f}, {160.0f, river_bottom - river_top + 20.0f}}, "Cầu Tây"},
    {{{1100.0f, river_top - 15.0f}, {200.0f, river_bottom - river_top + 30.0f}}, "Đại Kiều Trung Tâm"},
    {{{1860.0f, river_top - 10.0f}, {160.0f, river_bottom - river_top + 20.0f}}, "Cầu Đông"},
}};

struct game_state {
  game_screen screen = game_screen::playing;
  bool restart_requested = false; // "Chơi lại" on the result popup, handled next frame

  // Economy & Pop
  i32 red_gold = 260;
  i32 black_gold = 260;
  i32 red_pop = 0;
  i32 black_pop = 0;
  inline static constexpr i32 max_pop = 32;

  // Timers & Rally
  inline static constexpr f32 rally_cooldown_max = 24.0f;
  f32 rally_cooldown = 0.0f;
  f32 rally_active_timer = 0.0f;
  f32 match_time = 0.0f;

  // AI timers
  f32 ai_decision_timer = 0.0f;
  f32 ai_wave_timer = 12.0f;

  // Camera
  vec2 camera_pos{1200.0f, 1300.0f};
  vec2 camera_target{1200.0f, 1300.0f};
  f32 camera_zoom = 0.95f;
  f32 camera_zoom_target = 0.95f;
  entt::entity camera_entity = entt::null;

  // Selection
  bool is_box_selecting = false;
  vec2 box_start_screen{};
  vec2 box_end_screen{};
  entt::entity inspect_entity = entt::null;

  // Stats
  i32 red_kills = 0;
  i32 black_kills = 0;

  // Lists
  std::vector<damage_popup> popups;
  std::vector<fx_particle> particles;
  std::vector<click_ping> pings;

  // General entities
  entt::entity red_general = entt::null;
  entt::entity black_general = entt::null;
};

extern game_state state;

} // namespace xiangqi
