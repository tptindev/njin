#include "njin2rl.h"
#include <raylib.h>

namespace njin {

void to_raylib(vec2 from, Vector2 &to) {
  to.x = from.x;
  to.y = from.y;
}

void to_raylib(vec3 from, Vector3 &to) {
  to.x = from.x;
  to.y = from.y;
  to.z = from.z;
}

void to_raylib(vec4 from, Vector4 &to) {
  to.x = from.x;
  to.y = from.y;
  to.z = from.z;
  to.w = from.w;
}

void to_raylib(vec4 from, Color &to) {
  to.r = from.x;
  to.g = from.y;
  to.b = from.z;
  to.a = from.w;
}
void to_raylib(rgba from, Color &to) {
  to.r = (unsigned char)(from.r * 255.0f);
  to.g = (unsigned char)(from.g * 255.0f);
  to.b = (unsigned char)(from.b * 255.0f);
  to.a = (unsigned char)(from.a * 255.0f);
}

void to_raylib(camera_view from, Camera2D &to) {
  to.zoom = from.zoom;
  to.rotation = from.rotation;
  to_raylib(from.offset, to.offset );
  to_raylib(from.target, to.target );
}

void to_raylib(key_code from, i32 &to) {
  switch (from) {
  case key_a: to = KEY_A; break;
  case key_b: to = KEY_B; break;
  case key_c: to = KEY_C; break;
  case key_d: to = KEY_D; break;
  case key_e: to = KEY_E; break;
  case key_f: to = KEY_F; break;
  case key_g: to = KEY_G; break;
  case key_h: to = KEY_H; break;
  case key_i: to = KEY_I; break;
  case key_j: to = KEY_J; break;
  case key_k: to = KEY_K; break;
  case key_l: to = KEY_L; break;
  case key_m: to = KEY_M; break;
  case key_n: to = KEY_N; break;
  case key_o: to = KEY_O; break;
  case key_p: to = KEY_P; break;
  case key_q: to = KEY_Q; break;
  case key_r: to = KEY_R; break;
  case key_s: to = KEY_S; break;
  case key_t: to = KEY_T; break;
  case key_u: to = KEY_U; break;
  case key_v: to = KEY_V; break;
  case key_w: to = KEY_W; break;
  case key_x: to = KEY_X; break;
  case key_y: to = KEY_Y; break;
  case key_z: to = KEY_Z; break;
  case key_0: to = KEY_ZERO; break;
  case key_1: to = KEY_ONE; break;
  case key_2: to = KEY_TWO; break;
  case key_3: to = KEY_THREE; break;
  case key_4: to = KEY_FOUR; break;
  case key_5: to = KEY_FIVE; break;
  case key_6: to = KEY_SIX; break;
  case key_7: to = KEY_SEVEN; break;
  case key_8: to = KEY_EIGHT; break;
  case key_9: to = KEY_NINE; break;
  case key_space: to = KEY_SPACE; break;
  case key_enter: to = KEY_ENTER; break;
  case key_tab: to = KEY_TAB; break;
  case key_escape: to = KEY_ESCAPE; break;
  case key_backspace: to = KEY_BACKSPACE; break;
  case key_up: to = KEY_UP; break;
  case key_down: to = KEY_DOWN; break;
  case key_left: to = KEY_LEFT; break;
  case key_right: to = KEY_RIGHT; break;
  case key_left_shift: to = KEY_LEFT_SHIFT; break;
  case key_left_control: to = KEY_LEFT_CONTROL; break;
  case key_left_alt: to = KEY_LEFT_ALT; break;
  case key_right_shift: to = KEY_RIGHT_SHIFT; break;
  case key_right_control: to = KEY_RIGHT_CONTROL; break;
  case key_right_alt: to = KEY_RIGHT_ALT; break;
  case key_f1: to = KEY_F1; break;
  case key_f2: to = KEY_F2; break;
  case key_f3: to = KEY_F3; break;
  case key_f4: to = KEY_F4; break;
  case key_f5: to = KEY_F5; break;
  case key_f6: to = KEY_F6; break;
  case key_f7: to = KEY_F7; break;
  case key_f8: to = KEY_F8; break;
  case key_f9: to = KEY_F9; break;
  case key_f10: to = KEY_F10; break;
  case key_f11: to = KEY_F11; break;
  case key_f12: to = KEY_F12; break;
  case key_home: to = KEY_HOME; break;
  case key_end: to = KEY_END; break;
  case key_page_up: to = KEY_PAGE_UP; break;
  case key_page_down: to = KEY_PAGE_DOWN; break;
  case key_insert: to = KEY_INSERT; break;
  case key_delete: to = KEY_DELETE; break;
  case key_caps_lock: to = KEY_CAPS_LOCK; break;
  case key_scroll_lock: to = KEY_SCROLL_LOCK; break;
  case key_num_lock: to = KEY_NUM_LOCK; break;
  case key_print_screen: to = KEY_PRINT_SCREEN; break;
  case key_pause: to = KEY_PAUSE; break;
  case key_apostrophe: to = KEY_APOSTROPHE; break;
  case key_comma: to = KEY_COMMA; break;
  case key_minus: to = KEY_MINUS; break;
  case key_period: to = KEY_PERIOD; break;
  case key_slash: to = KEY_SLASH; break;
  case key_semicolon: to = KEY_SEMICOLON; break;
  case key_equal: to = KEY_EQUAL; break;
  case key_left_bracket: to = KEY_LEFT_BRACKET; break;
  case key_backslash: to = KEY_BACKSLASH; break;
  case key_right_bracket: to = KEY_RIGHT_BRACKET; break;
  case key_grave: to = KEY_GRAVE; break;
  case key_left_super: to = KEY_LEFT_SUPER; break;
  case key_right_super: to = KEY_RIGHT_SUPER; break;
  case key_kp_0: to = KEY_KP_0; break;
  case key_kp_1: to = KEY_KP_1; break;
  case key_kp_2: to = KEY_KP_2; break;
  case key_kp_3: to = KEY_KP_3; break;
  case key_kp_4: to = KEY_KP_4; break;
  case key_kp_5: to = KEY_KP_5; break;
  case key_kp_6: to = KEY_KP_6; break;
  case key_kp_7: to = KEY_KP_7; break;
  case key_kp_8: to = KEY_KP_8; break;
  case key_kp_9: to = KEY_KP_9; break;
  case key_kp_decimal: to = KEY_KP_DECIMAL; break;
  case key_kp_divide: to = KEY_KP_DIVIDE; break;
  case key_kp_multiply: to = KEY_KP_MULTIPLY; break;
  case key_kp_subtract: to = KEY_KP_SUBTRACT; break;
  case key_kp_add: to = KEY_KP_ADD; break;
  case key_kp_enter: to = KEY_KP_ENTER; break;
  case key_kp_equal: to = KEY_KP_EQUAL; break;
  default: to = KEY_NULL; break;
  }
}

} // namespace njin
