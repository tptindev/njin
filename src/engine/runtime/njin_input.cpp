#include "njin_input.h"
#include "njin2rl.h"
#include <raylib.h>

namespace njin {
namespace {
using key_query = bool (*)(const njin_input &, key_code);

// True if any key bound to the action satisfies `query`.
bool any_bound_key(const njin_input &input, action_handle handle,
                   key_query query) {
  const action_slot *slot = action_slot_of(input, handle);
  if (slot == nullptr)
    return false;
  for (const key_code key : slot->keys) {
    if (query(input, key))
      return true;
  }
  return false;
}
} // namespace

void input_key_poll(njin_input &input) {
  input.prev = input.cur;
  for (i32 k = key_none + 1; k < key_count; k++) {
    i32 rl_key = KEY_NULL;
    to_raylib((key_code)k, rl_key);
    input.cur.keys[k] = IsKeyDown(rl_key);
  }
}

bool input_key_pressed(const njin_input &input, key_code key) {
  return key_valid(key) && !input.prev.keys[key] && input.cur.keys[key];
}

bool input_key_held(const njin_input &input, key_code key) {
  return key_valid(key) && input.prev.keys[key] && input.cur.keys[key];
}

bool input_key_released(const njin_input &input, key_code key) {
  return key_valid(key) && input.prev.keys[key] && !input.cur.keys[key];
}

action_handle input_action_find(const njin_input &input, const char *name) {
  if (name == nullptr)
    return action_handle{};
  for (usize i = 0; i < input.actions.size(); i++) {
    if (input.actions[i].name == name)
      return action_handle{.id = (u32)(i + 1)};
  }
  return action_handle{};
}

action_handle input_action_register(njin_input &input, const char *name) {
  if (name == nullptr)
    return action_handle{};
  const action_handle existing = input_action_find(input, name);
  if (existing.id != 0)
    return existing;
  input.actions.push_back(action_slot{.name = name, .keys = {}});
  return action_handle{.id = (u32)input.actions.size()};
}

void input_action_bind_key(njin_input &input, action_handle handle,
                           key_code key) {
  if (!key_valid(key))
    return;
  action_slot *slot = action_slot_of(input, handle);
  if (slot != nullptr)
    slot->keys.insert(key);
}

bool input_action_pressed(const njin_input &input, action_handle handle) {
  return any_bound_key(input, handle, input_key_pressed);
}

bool input_action_held(const njin_input &input, action_handle handle) {
  return any_bound_key(input, handle, input_key_held);
}

bool input_action_released(const njin_input &input, action_handle handle) {
  return any_bound_key(input, handle, input_key_released);
}
} // namespace njin
