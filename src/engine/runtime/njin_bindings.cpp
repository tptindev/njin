#include "njin_bindings.h"
#include "njin_ctx.h"
#include "njin_input.h"
#include "njin_ctx_impl.h"
#include "njin_input_impl.h"
#include "njin_log.h"
#include <algorithm>
#include <cstring>
#include <raylib.h>
#include <string>

namespace njin {
namespace {
// Stable ids (for the settings file) and display names, in enum order from
// key_up on. Letters, digits and F keys are generated.
struct key_names {
  const char *id;
  const char *shown;
};
constexpr key_names named_keys[] = {
    {"up", "Up"}, {"down", "Down"}, {"left", "Left"}, {"right", "Right"},
    {"home", "Home"}, {"end", "End"}, {"page_up", "Page Up"}, {"page_down", "Page Down"},
    {"insert", "Insert"}, {"delete", "Delete"}, {"space", "Space"}, {"enter", "Enter"},
    {"tab", "Tab"}, {"escape", "Esc"}, {"backspace", "Backspace"}, {"caps_lock", "Caps Lock"},
    {"scroll_lock", "Scroll Lock"}, {"num_lock", "Num Lock"}, {"print_screen", "Print Screen"},
    {"pause", "Pause"}, {"apostrophe", "'"}, {"comma", ","}, {"minus", "-"}, {"period", "."},
    {"slash", "/"}, {"semicolon", ";"}, {"equal", "="}, {"left_bracket", "["},
    {"backslash", "\\"}, {"right_bracket", "]"}, {"grave", "`"},
    {"left_shift", "Left Shift"}, {"left_control", "Left Ctrl"}, {"left_alt", "Left Alt"},
    {"left_super", "Left Super"}, {"right_shift", "Right Shift"}, {"right_control", "Right Ctrl"},
    {"right_alt", "Right Alt"}, {"right_super", "Right Super"},
    {"kp_0", "Num 0"}, {"kp_1", "Num 1"}, {"kp_2", "Num 2"}, {"kp_3", "Num 3"}, {"kp_4", "Num 4"},
    {"kp_5", "Num 5"}, {"kp_6", "Num 6"}, {"kp_7", "Num 7"}, {"kp_8", "Num 8"}, {"kp_9", "Num 9"},
    {"kp_decimal", "Num ."}, {"kp_divide", "Num /"}, {"kp_multiply", "Num *"},
    {"kp_subtract", "Num -"}, {"kp_add", "Num +"}, {"kp_enter", "Num Enter"}, {"kp_equal", "Num ="},
};
static_assert(sizeof(named_keys) / sizeof(named_keys[0]) == (usize)(key_count - key_up),
              "named_keys must follow key_code");

constexpr key_names mouse_names[] = {
    {"left", "Mouse Left"},   {"right", "Mouse Right"},   {"middle", "Mouse Middle"},
    {"side", "Mouse Side"},   {"extra", "Mouse Extra"},   {"forward", "Mouse Forward"},
    {"back", "Mouse Back"},
};
constexpr key_names pad_names[] = {
    {"none", "?"},           {"dpad_up", "D-Pad Up"},   {"dpad_right", "D-Pad Right"},
    {"dpad_down", "D-Pad Down"}, {"dpad_left", "D-Pad Left"}, {"face_up", "Pad Y"},
    {"face_right", "Pad B"}, {"face_down", "Pad A"},    {"face_left", "Pad X"},
    {"l1", "LB"},            {"l2", "LT"},              {"r1", "RB"},
    {"r2", "RT"},            {"select", "Back"},        {"guide", "Guide"},
    {"start", "Start"},      {"left_thumb", "LS"},      {"right_thumb", "RS"},
};
constexpr const char *axis_ids[] = {"left_x", "left_y", "right_x", "right_y", "left_trigger",
                                    "right_trigger"};

// Generated names for a..z, 0..9, f1..f12: "a"/"A", "0"/"0", "f1"/"F1".
struct generated {
  std::string id[key_count];
  std::string shown[key_count];
  generated() {
    for (i32 k = key_a; k <= key_z; k++) {
      id[k] = std::string(1, (char)('a' + (k - key_a)));
      shown[k] = std::string(1, (char)('A' + (k - key_a)));
    }
    for (i32 k = key_0; k <= key_9; k++)
      id[k] = shown[k] = std::string(1, (char)('0' + (k - key_0)));
    for (i32 k = key_f1; k <= key_f12; k++) {
      id[k] = "f" + std::to_string(k - key_f1 + 1);
      shown[k] = "F" + std::to_string(k - key_f1 + 1);
    }
    for (i32 k = key_up; k < key_count; k++) {
      id[k] = named_keys[k - key_up].id;
      shown[k] = named_keys[k - key_up].shown;
    }
  }
};

const generated &names() {
  static const generated g;
  return g;
}

std::string source_id(input_source s) {
  switch (s.kind) {
  case input_source::key:
    return s.code > key_none && s.code < key_count ? "key:" + names().id[s.code] : "";
  case input_source::mouse:
    return s.code >= 0 && s.code < mouse_button_count ? std::string("mouse:") + mouse_names[s.code].id : "";
  case input_source::pad:
    return s.code > pad_none && s.code < gamepad_button_count ? std::string("pad:") + pad_names[s.code].id : "";
  default:
    return "";
  }
}

bool parse_source(const char *text, input_source &out) {
  if (text == nullptr)
    return false;
  const char *colon = std::strchr(text, ':');
  if (colon == nullptr)
    return false;
  const std::string kind(text, (usize)(colon - text));
  const std::string name(colon + 1);
  if (kind == "key") {
    for (i32 k = key_none + 1; k < key_count; k++)
      if (names().id[k] == name) {
        out = {input_source::key, k};
        return true;
      }
  } else if (kind == "mouse") {
    for (i32 b = 0; b < mouse_button_count; b++)
      if (name == mouse_names[b].id) {
        out = {input_source::mouse, b};
        return true;
      }
  } else if (kind == "pad") {
    for (i32 b = pad_none + 1; b < gamepad_button_count; b++)
      if (name == pad_names[b].id) {
        out = {input_source::pad, b};
        return true;
      }
  }
  return false;
}

i32 key_by_id(const char *id) {
  if (id == nullptr)
    return key_none;
  for (i32 k = key_none + 1; k < key_count; k++)
    if (names().id[k] == id)
      return k;
  return key_none;
}

i32 axis_by_id(const char *id) {
  if (id == nullptr)
    return -1;
  for (i32 a = 0; a < gamepad_axis_count; a++)
    if (std::strcmp(axis_ids[a], id) == 0)
      return a;
  return -1;
}
} // namespace

const char *key_name(key_code key) {
  return key > key_none && key < key_count ? names().shown[key].c_str() : "?";
}

const char *input_source_name(input_source s) {
  switch (s.kind) {
  case input_source::key:
    return key_name((key_code)s.code);
  case input_source::mouse:
    return s.code >= 0 && s.code < mouse_button_count ? mouse_names[s.code].shown : "?";
  case input_source::pad:
    return s.code > pad_none && s.code < gamepad_button_count ? pad_names[s.code].shown : "?";
  default:
    return "?";
  }
}

bool input_any_pressed(const njin_ctx &ctx, input_source &out) {
  // Raw edges, before any consume: a rebind screen sits inside the UI, which
  // swallows Enter, Esc and the arrows for the game.
  const input_store &in = ctx.input;
  for (i32 k = key_none + 1; k < key_count; k++)
    if (!in.prev.keys[k] && in.cur.keys[k]) {
      out = {input_source::key, k};
      return true;
    }
  for (i32 b = 0; b < mouse_button_count; b++)
    if (!in.prev.mouse[b] && in.cur.mouse[b]) {
      out = {input_source::mouse, b};
      return true;
    }
  for (i32 p = 0; p < gamepad_max; p++)
    for (i32 b = pad_none + 1; b < gamepad_button_count; b++)
      if (!in.prev.pad[p][b] && in.cur.pad[p][b]) {
        out = {input_source::pad, b};
        return true;
      }
  return false;
}

std::vector<input_source> action_sources(const njin_ctx &ctx, action_handle action) {
  std::vector<input_source> out;
  const action_slot *slot = action_slot_of(ctx.input, action);
  if (slot == nullptr)
    return out;
  for (const key_code k : slot->keys)
    out.push_back({input_source::key, k});
  for (const mouse_button b : slot->mouse)
    out.push_back({input_source::mouse, b});
  for (const gamepad_button b : slot->pad)
    out.push_back({input_source::pad, b});
  // Sets have no order; sort so the same bindings always read the same.
  std::sort(out.begin(), out.end(), [](const input_source &a, const input_source &b) {
    return a.kind != b.kind ? a.kind < b.kind : a.code < b.code;
  });
  return out;
}

void action_bind(njin_ctx &ctx, action_handle action, input_source s) {
  switch (s.kind) {
  case input_source::key: action_bind_key(ctx, action, (key_code)s.code); break;
  case input_source::mouse: action_bind_mouse(ctx, action, (mouse_button)s.code); break;
  case input_source::pad: action_bind_pad(ctx, action, (gamepad_button)s.code); break;
  default: break;
  }
}

void action_unbind(njin_ctx &ctx, action_handle action, input_source s) {
  action_slot *slot = action_slot_of(ctx.input, action);
  if (slot == nullptr)
    return;
  switch (s.kind) {
  case input_source::key: slot->keys.erase((key_code)s.code); break;
  case input_source::mouse: slot->mouse.erase((mouse_button)s.code); break;
  case input_source::pad: slot->pad.erase((gamepad_button)s.code); break;
  default: break;
  }
}

void action_rebind(njin_ctx &ctx, action_handle action, input_source s) {
  action_slot *slot = action_slot_of(ctx.input, action);
  if (slot == nullptr || s.kind == input_source::none)
    return;
  // One source, one job: take it off every other action first.
  for (usize i = 0; i < ctx.input.actions.size(); i++)
    action_unbind(ctx, action_handle{(u32)(i + 1)}, s);
  // Keyboard and mouse count as one device: a key replaces a mouse button.
  if (s.kind == input_source::pad) {
    slot->pad.clear();
  } else {
    slot->keys.clear();
    slot->mouse.clear();
  }
  action_bind(ctx, action, s);
}

json_value input_bindings_save(const njin_ctx &ctx) {
  json_value actions = json_value::make_object();
  for (usize i = 0; i < ctx.input.actions.size(); i++) {
    json_value list = json_value::make_array();
    for (const input_source &s : action_sources(ctx, action_handle{(u32)(i + 1)}))
      list.push(json_value(source_id(s)));
    actions.set(ctx.input.actions[i].name, std::move(list));
  }
  json_value axes = json_value::make_object();
  for (const axis_slot &a : ctx.input.axes) {
    json_value keys = json_value::make_array();
    for (const axis_key_pair &p : a.key_pairs) {
      const bool ok = p.negative > key_none && p.negative < key_count && p.positive > key_none &&
                      p.positive < key_count;
      if (ok)
        keys.push(json_value::make_array()
                      .push(json_value(names().id[p.negative]))
                      .push(json_value(names().id[p.positive])));
    }
    json_value pads = json_value::make_array();
    for (const gamepad_axis ax : a.pad_axes)
      pads.push(json_value(axis_ids[ax]));
    axes.set(a.name, json_value::make_object().set("keys", std::move(keys)).set("pad", std::move(pads)));
  }
  return json_value::make_object().set("actions", std::move(actions)).set("axes", std::move(axes));
}

bool input_bindings_load(njin_ctx &ctx, const json_value &json) {
  if (!json.is(json_value::object))
    return false;
  for (const auto &[name, list] : json["actions"].members) {
    const action_handle h = action_register(ctx, name.c_str());
    action_clear_binds(ctx, h);
    for (const json_value &item : list.items) {
      input_source s{};
      if (parse_source(item.string_or(nullptr), s))
        action_bind(ctx, h, s);
      else
        NJIN_WARN("input_bindings_load: unknown input '%s' for action '%s'",
                  item.string_or("?"), name.c_str());
    }
  }
  for (const auto &[name, def] : json["axes"].members) {
    const axis_handle h = axis_register(ctx, name.c_str());
    axis_clear_binds(ctx, h);
    for (const json_value &pair : def["keys"].items) {
      const i32 neg = key_by_id(pair[(usize)0].string_or(nullptr));
      const i32 pos = key_by_id(pair[(usize)1].string_or(nullptr));
      if (neg != key_none && pos != key_none)
        axis_bind_keys(ctx, h, (key_code)neg, (key_code)pos);
    }
    for (const json_value &ax : def["pad"].items) {
      const i32 a = axis_by_id(ax.string_or(nullptr));
      if (a >= 0)
        axis_bind_pad(ctx, h, (gamepad_axis)a);
    }
  }
  return true;
}

void pad_rumble(njin_ctx &, i32 pad, f32 low, f32 high, f32 seconds) {
  if (pad < 0 || pad >= gamepad_max || !IsGamepadAvailable(pad) || seconds <= 0.0f)
    return;
  const auto unit = [](f32 v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
  SetGamepadVibration(pad, unit(low), unit(high), seconds);
}
} // namespace njin
