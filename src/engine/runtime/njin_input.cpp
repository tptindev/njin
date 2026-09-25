#include "njin_input.h"
#include <algorithm>
#include <array>
#include <iterator>
#include "njin2rl.h"
#include "rl2njin.h"
#include <raylib.h>

namespace njin {
// mouse_button, gamepad_button and gamepad_axis mirror raylib's numbering, so
// converting one is a cast. These keep the two sides honest: a raylib upgrade
// that renumbers something stops the build instead of silently mapping a wrong
// button.
static constexpr bool same(int a, int b) { return a == b; }

static_assert(same(mouse_left, MOUSE_BUTTON_LEFT));
static_assert(same(mouse_right, MOUSE_BUTTON_RIGHT));
static_assert(same(mouse_middle, MOUSE_BUTTON_MIDDLE));
static_assert(same(mouse_side, MOUSE_BUTTON_SIDE));
static_assert(same(mouse_extra, MOUSE_BUTTON_EXTRA));
static_assert(same(mouse_forward, MOUSE_BUTTON_FORWARD));
static_assert(same(mouse_back, MOUSE_BUTTON_BACK));
static_assert(same(mouse_button_count, MOUSE_BUTTON_BACK + 1));

static_assert(same(pad_none, GAMEPAD_BUTTON_UNKNOWN));
static_assert(same(pad_dpad_up, GAMEPAD_BUTTON_LEFT_FACE_UP));
static_assert(same(pad_dpad_right, GAMEPAD_BUTTON_LEFT_FACE_RIGHT));
static_assert(same(pad_dpad_down, GAMEPAD_BUTTON_LEFT_FACE_DOWN));
static_assert(same(pad_dpad_left, GAMEPAD_BUTTON_LEFT_FACE_LEFT));
static_assert(same(pad_face_up, GAMEPAD_BUTTON_RIGHT_FACE_UP));
static_assert(same(pad_face_right, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT));
static_assert(same(pad_face_down, GAMEPAD_BUTTON_RIGHT_FACE_DOWN));
static_assert(same(pad_face_left, GAMEPAD_BUTTON_RIGHT_FACE_LEFT));
static_assert(same(pad_l1, GAMEPAD_BUTTON_LEFT_TRIGGER_1));
static_assert(same(pad_l2, GAMEPAD_BUTTON_LEFT_TRIGGER_2));
static_assert(same(pad_r1, GAMEPAD_BUTTON_RIGHT_TRIGGER_1));
static_assert(same(pad_r2, GAMEPAD_BUTTON_RIGHT_TRIGGER_2));
static_assert(same(pad_select, GAMEPAD_BUTTON_MIDDLE_LEFT));
static_assert(same(pad_guide, GAMEPAD_BUTTON_MIDDLE));
static_assert(same(pad_start, GAMEPAD_BUTTON_MIDDLE_RIGHT));
static_assert(same(pad_left_thumb, GAMEPAD_BUTTON_LEFT_THUMB));
static_assert(same(pad_right_thumb, GAMEPAD_BUTTON_RIGHT_THUMB));
static_assert(same(gamepad_button_count, GAMEPAD_BUTTON_RIGHT_THUMB + 1));

static_assert(same(pad_axis_left_x, GAMEPAD_AXIS_LEFT_X));
static_assert(same(pad_axis_left_y, GAMEPAD_AXIS_LEFT_Y));
static_assert(same(pad_axis_right_x, GAMEPAD_AXIS_RIGHT_X));
static_assert(same(pad_axis_right_y, GAMEPAD_AXIS_RIGHT_Y));
static_assert(same(pad_axis_left_trigger, GAMEPAD_AXIS_LEFT_TRIGGER));
static_assert(same(pad_axis_right_trigger, GAMEPAD_AXIS_RIGHT_TRIGGER));
static_assert(same(gamepad_axis_count, GAMEPAD_AXIS_RIGHT_TRIGGER + 1));

namespace {
bool key_edge(const input_store &input, key_code key, edge e) {
  return key_valid(key) && !input.key_hidden[key] &&
         edge_match(e, input.prev.keys[key], input.cur.keys[key]);
}

bool mouse_edge(const input_store &input, mouse_button button, edge e) {
  return mouse_valid(button) && !input.mouse_hidden[button] &&
         edge_match(e, input.prev.mouse[button], input.cur.mouse[button]);
}

bool pad_edge(const input_store &input, i32 pad, gamepad_button button,
              edge e) {
  return pad_index_valid(pad) && pad_button_valid(button) &&
         edge_match(e, input.prev.pad_ready[pad] && input.prev.pad[pad][button],
                    input.cur.pad_ready[pad] && input.cur.pad[pad][button]);
}

bool pad_down_any(const input_frame &frame, gamepad_button button) {
  for (i32 pad = 0; pad < gamepad_max; pad++) {
    if (frame.pad_ready[pad] && frame.pad[pad][button])
      return true;
  }
  return false;
}

// Any connected pad satisfies it. This is a single-player engine: asking which
// pad pressed a button would make every caller answer a question it does not
// have. The edge is taken on "any pad", so handing the button from one pad to
// another without a gap stays one continuous hold.
bool pad_edge_any(const input_store &input, gamepad_button button, edge e) {
  return pad_button_valid(button) &&
         edge_match(e, pad_down_any(input.prev, button),
                    pad_down_any(input.cur, button));
}

// The edge is taken per bound source, so a key and a button bound to the same
// action each answer for themselves and either one satisfies it.
bool action_edge(const input_store &input, action_handle handle, edge e) {
  const action_slot *slot = action_slot_of(input, handle);
  if (slot == nullptr)
    return false;
  for (const key_code key : slot->keys) {
    if (key_edge(input, key, e))
      return true;
  }
  for (const mouse_button button : slot->mouse) {
    if (mouse_edge(input, button, e))
      return true;
  }
  for (const gamepad_button button : slot->pad) {
    if (pad_edge_any(input, button, e))
      return true;
  }
  return false;
}
} // namespace

// Taken once per frame, at the top of the frame. raylib polls the OS inside
// EndDrawing, so by the time this runs the backend already holds this frame's
// truth and every system in the frame reads the same answer.
void input_key_poll(input_store &input) {
  input.prev = input.cur;
  std::fill(std::begin(input.key_hidden), std::end(input.key_hidden), false);
  std::fill(std::begin(input.mouse_hidden), std::end(input.mouse_hidden), false);

  // raylib key code -> njin key, built once.
  static const auto from_rl = [] {
    std::array<key_code, 512> table{};
    table.fill(key_none);
    for (i32 k = key_none + 1; k < key_count; k++) {
      i32 rl_key = KEY_NULL;
      to_raylib((key_code)k, rl_key);
      if (rl_key > 0 && rl_key < (i32)table.size())
        table[(usize)rl_key] = (key_code)k;
    }
    return table;
  }();

  for (i32 k = key_none + 1; k < key_count; k++) {
    i32 rl_key = KEY_NULL;
    to_raylib((key_code)k, rl_key);
    input.cur.keys[k] = IsKeyDown(rl_key);
  }
  // A key pressed and released between two frames reads as up here, and its
  // press would be lost. raylib queues every press, so a queued key that is
  // already up counts as down for this one frame: pressed now, released next.
  for (i32 rl_key = GetKeyPressed(); rl_key != 0; rl_key = GetKeyPressed()) {
    if (rl_key > 0 && rl_key < (i32)from_rl.size()) {
      const key_code key = from_rl[(usize)rl_key];
      if (key != key_none && !input.prev.keys[key])
        input.cur.keys[key] = true;
    }
  }
  for (i32 b = 0; b < mouse_button_count; b++)
    input.cur.mouse[b] = IsMouseButtonDown(b);

  for (i32 pad = 0; pad < gamepad_max; pad++) {
    const bool ready = IsGamepadAvailable(pad);
    input.cur.pad_ready[pad] = ready;
    for (i32 b = 0; b < gamepad_button_count; b++)
      input.cur.pad[pad][b] = ready && IsGamepadButtonDown(pad, b);
    for (i32 a = 0; a < gamepad_axis_count; a++)
      input.cur.pad_axis[pad][a] = ready ? GetGamepadAxisMovement(pad, a) : 0.0f;
  }

  from_raylib(GetMousePosition(), input.cur.mouse_pos);
  from_raylib(GetMouseDelta(), input.cur.mouse_delta);
  input.cur.wheel = GetMouseWheelMove();

  // GetCharPressed drains a queue, so it has to be emptied exactly once per
  // frame and kept. Read straight from a system, whichever ran first would eat
  // the text and the rest would see none of it.
  input.text_count = 0;
  for (i32 c = GetCharPressed(); c != 0; c = GetCharPressed()) {
    if (input.text_count < input_text_max)
      input.text[input.text_count++] = c;
  }
}

// Keyboard

bool input_key_pressed(const input_store &input, key_code key) {
  return key_edge(input, key, edge::pressed);
}

bool input_key_held(const input_store &input, key_code key) {
  return key_edge(input, key, edge::held);
}

bool input_key_released(const input_store &input, key_code key) {
  return key_edge(input, key, edge::released);
}

// Mouse

vec2 input_mouse_pos(const input_store &input) { return input.cur.mouse_pos; }

vec2 input_mouse_delta(const input_store &input) {
  return input.cur.mouse_delta;
}

f32 input_mouse_wheel(const input_store &input) { return input.cur.wheel; }

bool input_mouse_pressed(const input_store &input, mouse_button button) {
  return mouse_edge(input, button, edge::pressed);
}

bool input_mouse_held(const input_store &input, mouse_button button) {
  return mouse_edge(input, button, edge::held);
}

bool input_mouse_released(const input_store &input, mouse_button button) {
  return mouse_edge(input, button, edge::released);
}

// Gamepad

bool input_pad_available(const input_store &input, i32 pad) {
  return pad_index_valid(pad) && input.cur.pad_ready[pad];
}

bool input_pad_pressed(const input_store &input, i32 pad,
                       gamepad_button button) {
  return pad_edge(input, pad, button, edge::pressed);
}

bool input_pad_held(const input_store &input, i32 pad, gamepad_button button) {
  return pad_edge(input, pad, button, edge::held);
}

bool input_pad_released(const input_store &input, i32 pad,
                        gamepad_button button) {
  return pad_edge(input, pad, button, edge::released);
}

f32 input_pad_axis(const input_store &input, i32 pad, gamepad_axis axis) {
  if (!pad_index_valid(pad) || !pad_axis_valid(axis) ||
      !input.cur.pad_ready[pad])
    return 0.0f;
  return pad_axis_deadzoned(input.cur.pad_axis[pad][axis], input.pad_deadzone);
}

void input_pad_set_deadzone(input_store &input, f32 deadzone) {
  const f32 low = deadzone < 0.0f ? 0.0f : deadzone;
  input.pad_deadzone = low > 0.95f ? 0.95f : low;
}

// Text

i32 input_text_count(const input_store &input) { return input.text_count; }

i32 input_text_char(const input_store &input, i32 index) {
  if (index < 0 || index >= input.text_count)
    return 0;
  return input.text[index];
}

// Consume

void input_key_consume(input_store &input, key_code key) {
  if (!key_valid(key))
    return;
  input.key_hidden[key] = true;
}

void input_mouse_consume(input_store &input, mouse_button button) {
  if (!mouse_valid(button))
    return;
  input.mouse_hidden[button] = true;
}

void input_mouse_wheel_consume(input_store &input) { input.cur.wheel = 0.0f; }

// Actions

action_handle input_action_find(const input_store &input, const char *name) {
  if (name == nullptr)
    return action_handle{};
  for (usize i = 0; i < input.actions.size(); i++) {
    if (input.actions[i].name == name)
      return action_handle{.id = (u32)(i + 1)};
  }
  return action_handle{};
}

action_handle input_action_register(input_store &input, const char *name) {
  if (name == nullptr)
    return action_handle{};
  const action_handle existing = input_action_find(input, name);
  if (existing.id != 0)
    return existing;
  input.actions.push_back(action_slot{.name = name, .keys = {}, .mouse = {},
                                      .pad = {}});
  return action_handle{.id = (u32)input.actions.size()};
}

void input_action_bind_key(input_store &input, action_handle handle,
                           key_code key) {
  if (!key_valid(key))
    return;
  action_slot *slot = action_slot_of(input, handle);
  if (slot != nullptr)
    slot->keys.insert(key);
}

void input_action_bind_mouse(input_store &input, action_handle handle,
                             mouse_button button) {
  if (!mouse_valid(button))
    return;
  action_slot *slot = action_slot_of(input, handle);
  if (slot != nullptr)
    slot->mouse.insert(button);
}

void input_action_bind_pad(input_store &input, action_handle handle,
                           gamepad_button button) {
  if (!pad_button_valid(button))
    return;
  action_slot *slot = action_slot_of(input, handle);
  if (slot != nullptr)
    slot->pad.insert(button);
}

void input_action_clear_binds(input_store &input, action_handle handle) {
  action_slot *slot = action_slot_of(input, handle);
  if (slot == nullptr)
    return;
  slot->keys.clear();
  slot->mouse.clear();
  slot->pad.clear();
}

bool input_action_pressed(const input_store &input, action_handle handle) {
  return action_edge(input, handle, edge::pressed);
}

bool input_action_held(const input_store &input, action_handle handle) {
  return action_edge(input, handle, edge::held);
}

bool input_action_released(const input_store &input, action_handle handle) {
  return action_edge(input, handle, edge::released);
}

// Axes

axis_handle input_axis_find(const input_store &input, const char *name) {
  if (name == nullptr)
    return axis_handle{};
  for (usize i = 0; i < input.axes.size(); i++) {
    if (input.axes[i].name == name)
      return axis_handle{.id = (u32)(i + 1)};
  }
  return axis_handle{};
}

axis_handle input_axis_register(input_store &input, const char *name) {
  if (name == nullptr)
    return axis_handle{};
  const axis_handle existing = input_axis_find(input, name);
  if (existing.id != 0)
    return existing;
  input.axes.push_back(axis_slot{.name = name, .key_pairs = {}, .pad_axes = {}});
  return axis_handle{.id = (u32)input.axes.size()};
}

void input_axis_bind_keys(input_store &input, axis_handle handle,
                          key_code negative, key_code positive) {
  axis_slot *slot = axis_slot_of(input, handle);
  if (slot != nullptr)
    slot->key_pairs.push_back(
        axis_key_pair{.negative = negative, .positive = positive});
}

void input_axis_bind_pad(input_store &input, axis_handle handle,
                         gamepad_axis axis) {
  if (!pad_axis_valid(axis))
    return;
  axis_slot *slot = axis_slot_of(input, handle);
  if (slot != nullptr)
    slot->pad_axes.push_back(axis);
}

void input_axis_clear_binds(input_store &input, axis_handle handle) {
  axis_slot *slot = axis_slot_of(input, handle);
  if (slot == nullptr)
    return;
  slot->key_pairs.clear();
  slot->pad_axes.clear();
}

// Whichever bound source sits furthest from rest wins, so a stick pushed half
// way is not flattened by a key pair resting at 0, nor the other way round.
f32 input_axis_value(const input_store &input, axis_handle handle) {
  const axis_slot *slot = axis_slot_of(input, handle);
  if (slot == nullptr)
    return 0.0f;

  f32 best = 0.0f;
  for (const axis_key_pair &pair : slot->key_pairs) {
    const bool positive = key_valid(pair.positive) && input.cur.keys[pair.positive] &&
                          !input.key_hidden[pair.positive];
    const bool negative = key_valid(pair.negative) && input.cur.keys[pair.negative] &&
                          !input.key_hidden[pair.negative];
    const f32 value = (positive ? 1.0f : 0.0f) - (negative ? 1.0f : 0.0f);
    if (std::fabs(value) > std::fabs(best))
      best = value;
  }
  for (const gamepad_axis axis : slot->pad_axes) {
    for (i32 pad = 0; pad < gamepad_max; pad++) {
      const f32 value = input_pad_axis(input, pad, axis);
      if (std::fabs(value) > std::fabs(best))
        best = value;
    }
  }
  return best;
}
} // namespace njin
