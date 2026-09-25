#pragma once

#include <cstddef>
#include <cstdint>

namespace njin {
// Signed integers
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

// Unsigned integers
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

// Floating point
using f32 = float;
using f64 = double;

// Pointer-sized integers
using usize = std::size_t;
using isize = std::ptrdiff_t;

// Vector 2d
struct vec2 {
  f32 x;
  f32 y;
};

// Vector 4d
struct vec4 {
  f32 x;
  f32 y;
  f32 z;
  f32 w;
};

// Color with RED, GREEN, BLUE, ALPHA
struct rgba {
  f32 r;
  f32 g;
  f32 b;
  f32 a;
};

struct camera_view {
  f32 zoom;
  f32 rotation;
  vec2 offset;
  vec2 target;
};

struct action_handle { u32 id = 0; };

struct shader_handle { u32 id = 0; };

enum key_code {
  key_none = 0,

  // Letters
  key_a,
  key_b,
  key_c,
  key_d,
  key_e,
  key_f,
  key_g,
  key_h,
  key_i,
  key_j,
  key_k,
  key_l,
  key_m,
  key_n,
  key_o,
  key_p,
  key_q,
  key_r,
  key_s,
  key_t,
  key_u,
  key_v,
  key_w,
  key_x,
  key_y,
  key_z,

  // Numbers
  key_0,
  key_1,
  key_2,
  key_3,
  key_4,
  key_5,
  key_6,
  key_7,
  key_8,
  key_9,

  // Function keys
  key_f1,
  key_f2,
  key_f3,
  key_f4,
  key_f5,
  key_f6,
  key_f7,
  key_f8,
  key_f9,
  key_f10,
  key_f11,
  key_f12,

  // Navigation
  key_up,
  key_down,
  key_left,
  key_right,

  key_home,
  key_end,
  key_page_up,
  key_page_down,
  key_insert,
  key_delete,

  // General
  key_space,
  key_enter,
  key_tab,
  key_escape,
  key_backspace,

  key_caps_lock,
  key_scroll_lock,
  key_num_lock,
  key_print_screen,
  key_pause,

  // Symbols
  key_apostrophe,
  key_comma,
  key_minus,
  key_period,
  key_slash,
  key_semicolon,
  key_equal,
  key_left_bracket,
  key_backslash,
  key_right_bracket,
  key_grave,

  // Modifiers
  key_left_shift,
  key_left_control,
  key_left_alt,
  key_left_super,

  key_right_shift,
  key_right_control,
  key_right_alt,
  key_right_super,

  // Keypad
  key_kp_0,
  key_kp_1,
  key_kp_2,
  key_kp_3,
  key_kp_4,
  key_kp_5,
  key_kp_6,
  key_kp_7,
  key_kp_8,
  key_kp_9,

  key_kp_decimal,
  key_kp_divide,
  key_kp_multiply,
  key_kp_subtract,
  key_kp_add,
  key_kp_enter,
  key_kp_equal,

  key_count
};
} // namespace njin
