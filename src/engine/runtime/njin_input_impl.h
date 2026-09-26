#pragma once

#include "_types.h"
#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

namespace njin {
// How much of a stick's travel around the centre reads as zero. Past it the
// value is rescaled to reach a full 1, so motion starts from 0 instead of
// jumping.
inline constexpr f32 pad_deadzone_default = 0.15f;

// Codepoints one frame may carry. Typing faster than this in one frame is not
// something a human does; a paste is, and it is not text input.
inline constexpr i32 input_text_max = 32;

// The three questions asked of a button, all answered from two snapshots:
// pressed is the frame it goes down, held is a frame that follows one where it
// was already down, released is the frame it comes up.
enum class edge { pressed, held, released };

constexpr bool edge_match(edge e, bool prev, bool cur) {
  switch (e) {
  case edge::pressed:
    return !prev && cur;
  case edge::held:
    return prev && cur;
  case edge::released:
    return prev && !cur;
  }
  return false;
}

// One frame of the whole input surface, taken once at the top of the frame.
struct input_frame {
  bool keys[key_count] = {};
  bool mouse[mouse_button_count] = {};
  bool pad_ready[gamepad_max] = {};
  bool pad[gamepad_max][gamepad_button_count] = {};
  f32 pad_axis[gamepad_max][gamepad_axis_count] = {};
  vec2 mouse_pos{};
  vec2 mouse_delta{};
  f32 wheel = 0.0f;
};

// Bindings for one digital action. Any bound source satisfies it.
struct action_slot {
  std::string name;
  std::unordered_set<key_code> keys;
  std::unordered_set<mouse_button> mouse;
  std::unordered_set<gamepad_button> pad;
};

// A pair of keys standing in for an analog axis. Negative alone reads -1,
// positive alone reads +1, both down or neither reads 0.
struct axis_key_pair {
  key_code negative = key_none;
  key_code positive = key_none;
};

struct axis_slot {
  std::string name;
  std::vector<axis_key_pair> key_pairs;
  std::vector<gamepad_axis> pad_axes;
};

struct input_store {
  input_frame prev;
  input_frame cur;
  // Consumed this frame: every query about them answers "not down". Kept
  // apart from prev/cur so consuming a held key does not rewrite its history
  // (which would make it read as a fresh press on every following frame).
  bool key_hidden[key_count] = {};
  bool mouse_hidden[mouse_button_count] = {};
  f32 pad_deadzone = pad_deadzone_default;
  i32 text[input_text_max] = {};
  i32 text_count = 0;
  std::vector<action_slot> actions;
  std::vector<axis_slot> axes;
};

inline bool key_valid(key_code key) {
  return key > key_none && key < key_count;
}

inline bool mouse_valid(mouse_button button) {
  return button >= mouse_left && button < mouse_button_count;
}

inline bool pad_button_valid(gamepad_button button) {
  return button > pad_none && button < gamepad_button_count;
}

inline bool pad_axis_valid(gamepad_axis axis) {
  return axis >= pad_axis_left_x && axis < gamepad_axis_count;
}

inline bool pad_index_valid(i32 pad) { return pad >= 0 && pad < gamepad_max; }

// Rescaled rather than clipped: a stick just past the deadzone reads near 0
// and reaches a full 1 at the rim, so there is no step at the threshold.
inline f32 pad_axis_deadzoned(f32 value, f32 deadzone) {
  const f32 magnitude = std::fabs(value);
  if (magnitude <= deadzone)
    return 0.0f;
  const f32 scaled = (magnitude - deadzone) / (1.0f - deadzone);
  return value < 0.0f ? -scaled : scaled;
}

// Handle id 0 is "invalid"; id N maps to actions[N - 1] / axes[N - 1].
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

inline const axis_slot *axis_slot_of(const input_store &input,
                                     axis_handle handle) {
  if (handle.id == 0 || handle.id > input.axes.size())
    return nullptr;
  return &input.axes[handle.id - 1];
}

inline axis_slot *axis_slot_of(input_store &input, axis_handle handle) {
  return const_cast<axis_slot *>(
      axis_slot_of(static_cast<const input_store &>(input), handle));
}

// Rolls cur into prev and refills cur from the backend. This is the one call
// that needs a window; everything else below only reads the two snapshots.
void input_key_poll(input_store &input);

bool input_key_pressed(const input_store &input, key_code key);
bool input_key_held(const input_store &input, key_code key);
bool input_key_released(const input_store &input, key_code key);

vec2 input_mouse_pos(const input_store &input);
vec2 input_mouse_delta(const input_store &input);
f32 input_mouse_wheel(const input_store &input);
bool input_mouse_pressed(const input_store &input, mouse_button button);
bool input_mouse_held(const input_store &input, mouse_button button);
bool input_mouse_released(const input_store &input, mouse_button button);

bool input_pad_available(const input_store &input, i32 pad);
bool input_pad_pressed(const input_store &input, i32 pad,
                       gamepad_button button);
bool input_pad_held(const input_store &input, i32 pad, gamepad_button button);
bool input_pad_released(const input_store &input, i32 pad,
                        gamepad_button button);
f32 input_pad_axis(const input_store &input, i32 pad, gamepad_axis axis);
void input_pad_set_deadzone(input_store &input, f32 deadzone);

i32 input_text_count(const input_store &input);
i32 input_text_char(const input_store &input, i32 index);

void input_key_consume(input_store &input, key_code key);
void input_mouse_consume(input_store &input, mouse_button button);
void input_mouse_wheel_consume(input_store &input);

action_handle input_action_find(const input_store &input, const char *name);
action_handle input_action_register(input_store &input, const char *name);
void input_action_bind_key(input_store &input, action_handle handle,
                           key_code key);
void input_action_bind_mouse(input_store &input, action_handle handle,
                             mouse_button button);
void input_action_bind_pad(input_store &input, action_handle handle,
                           gamepad_button button);
void input_action_clear_binds(input_store &input, action_handle handle);

bool input_action_pressed(const input_store &input, action_handle handle);
bool input_action_held(const input_store &input, action_handle handle);
bool input_action_released(const input_store &input, action_handle handle);

axis_handle input_axis_find(const input_store &input, const char *name);
axis_handle input_axis_register(input_store &input, const char *name);
void input_axis_bind_keys(input_store &input, axis_handle handle,
                          key_code negative, key_code positive);
void input_axis_bind_pad(input_store &input, axis_handle handle,
                         gamepad_axis axis);
void input_axis_clear_binds(input_store &input, axis_handle handle);
f32 input_axis_value(const input_store &input, axis_handle handle);
} // namespace njin
