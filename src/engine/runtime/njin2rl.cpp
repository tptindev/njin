#include "njin2rl.h"
#include <raylib.h>

void njin::to_raylib(vec2 from, Vector2 &to) {
  to.x = from.x;
  to.y = from.y;
}

void njin::to_raylib(vec4 from, Vector4 &to) {
  to.x = from.x;
  to.y = from.y;
  to.z = from.z;
  to.w = from.w;
}

void njin::to_raylib(vec4 from, Color &to) {
  to.r = from.x;
  to.g = from.y;
  to.b = from.z;
  to.a = from.w;
}
void njin::to_raylib(rgba from, Color &to) {
  to.r = (unsigned char)(from.r * 255.0f);
  to.g = (unsigned char)(from.g * 255.0f);
  to.b = (unsigned char)(from.b * 255.0f);
  to.a = (unsigned char)(from.a * 255.0f);
}

void njin::to_raylib(camera_view from, Camera2D &to) {
  to.zoom = from.zoom;
  to.rotation = from.rotation;
  to_raylib(from.offset, to.offset );
  to_raylib(from.target, to.target );
}

void njin::to_raylib(key_code from, i32 &to) {
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
  default: to = KEY_NULL; break;
  }
}
