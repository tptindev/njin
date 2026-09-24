#include "njin_input.h"
#include "njin2rl.h"
#include <raylib.h>

void njin::input_key_poll(njin_input &input) {
  input.prev = input.cur;
  for (i32 k = 1; k < key_count; k++) {
    i32 rlkey = KEY_NULL;
    to_raylib((key_code)k, rlkey);
    input.cur.keys[k] = IsKeyDown(rlkey);
  }
}

bool njin::input_key_pressed(njin_input &input, key_code key) {
  return key_valid(key) && (!input.prev.keys[key] && input.cur.keys[key]);
}

bool njin::input_key_held(njin_input &input, key_code key) {
  return key_valid(key) && (input.prev.keys[key] && input.cur.keys[key]);
}

bool njin::input_key_released(njin_input &input, key_code key) {
  return key_valid(key) && (input.prev.keys[key] && !input.cur.keys[key]);
}

