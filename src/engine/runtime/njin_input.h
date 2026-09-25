#pragma once

#include "types.h"
#include <string>
#include <unordered_set>
#include <vector>

namespace njin {
struct input_frame {
  bool keys[key_count] = {};
};

struct action_slot {
  std::string name;
  std::unordered_set<key_code> keys;
};

struct input_store {
  input_frame prev;
  input_frame cur;
  std::vector<action_slot> actions;
};

inline bool key_valid(key_code key) {
  return key > key_none && key < key_count;
}

// Handle id 0 is "invalid"; id N maps to actions[N - 1].
inline const action_slot *action_slot_of(const input_store &input,
                                         action_handle handle) {
  if (handle.id == 0 || handle.id > input.actions.size())
    return nullptr;
  return &input.actions[handle.id - 1];
}

inline action_slot *action_slot_of(input_store &input, action_handle handle) {
  return const_cast<action_slot *>(
      action_slot_of(static_cast<const input_store &>(input), handle));
}

void input_key_poll(input_store &input);

bool input_key_pressed(const input_store &input, key_code key);
bool input_key_held(const input_store &input, key_code key);
bool input_key_released(const input_store &input, key_code key);

action_handle input_action_find(const input_store &input, const char *name);
action_handle input_action_register(input_store &input, const char *name);
void input_action_bind_key(input_store &input, action_handle handle,
                           key_code key);

bool input_action_pressed(const input_store &input, action_handle handle);
bool input_action_held(const input_store &input, action_handle handle);
bool input_action_released(const input_store &input, action_handle handle);
} // namespace njin
