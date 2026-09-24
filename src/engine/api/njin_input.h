#pragma once

#include "types.h"
namespace njin {
struct njin_ctx;
struct njin_input_frame {
  bool keys[key_count] = {};
};
struct njin_input {
  njin_input_frame prev;
  njin_input_frame cur;
};
inline bool key_valid(key_code key) {
  return key > key_none && key < key_count;
}
void input_key_poll(njin_input &input);
bool input_key_pressed(njin_input &input, key_code key);
bool input_key_held(njin_input &input, key_code key);
bool input_key_released(njin_input &input, key_code key);
}
