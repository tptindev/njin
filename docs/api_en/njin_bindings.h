#pragma once
#include "_types.h"
#include "njin_input.h"
#include "njin_json.h"
#include <initializer_list>
#include <vector>

namespace njin {
struct context;

/// @addtogroup grp_input
/// @{

/// An input source: a key, a mouse button, or a gamepad button. Used to rebind
/// keys in the game and to display the bound key.
struct input_source {
  /// Kind of source.
  enum kind_t : u8 {
    none,  ///< None.
    key,   ///< Key, `code` is a njin::key_code.
    mouse, ///< Mouse button, `code` is a njin::mouse_button.
    pad,   ///< Gamepad button, `code` is a njin::gamepad_button.
  };
  kind_t kind = none; ///< Kind.
  i32 code = 0;       ///< Code, depending on the kind.

  /// Compares two sources. @param o The other source. @return `true` if the kind and code are the same.
  bool operator==(const input_source &o) const { return kind == o.kind && code == o.code; }
};

/// Display name of a source, in short English as on a keyboard: "Space",
/// "A", "Left Shift", "Mouse Left", "Pad A" (by Xbox button position).
/// @param source The source.
/// @return A static string, never null ("?" if invalid).
const char *input_source_name(input_source source);

/// Display name of a key. See input_source_name().
/// @param key The key.
/// @return A static string.
const char *key_name(key_code key);

/// The source just pressed this frame, if any: for a "press a new key" screen.
/// Checks the keyboard first, then the mouse, then every gamepad.
/// @param ctx Engine context.
/// @param out Receives the source just pressed.
/// @return `true` if a source was just pressed.
bool input_any_pressed(const context &ctx, input_source &out);

/// The sources bound to an action, in order: keys, mouse, gamepad.
/// @param ctx Engine context.
/// @param action The action.
/// @return The list of sources (empty if the handle is invalid).
std::vector<input_source> action_sources(const context &ctx, action_handle action);

/// Binds a source to an action (like action_bind_key(), action_bind_mouse(),
/// action_bind_pad() depending on the kind).
/// @param ctx Engine context.
/// @param action The action.
/// @param source The source.
void action_bind(context &ctx, action_handle action, input_source source);

/// Removes a source from an action.
/// @param ctx Engine context.
/// @param action The action.
/// @param source The source.
void action_unbind(context &ctx, action_handle action, input_source source);

/// Binds `source` to `action` in place of the existing source of the **same
/// kind** (key replaces key, gamepad button replaces gamepad button), and
/// removes it from every other action so one key does not do two jobs. This is
/// what a key-rebinding screen needs.
/// @param ctx Engine context.
/// @param action The action.
/// @param source The new source.
void action_rebind(context &ctx, action_handle action, input_source source);

/// A source in shorthand for action_define(): pass a key, a mouse button or a
/// gamepad button directly, without building a njin::input_source.
struct binding {
  input_source source; ///< The source, converted to a njin::input_source.
  /// @param key The key.
  binding(key_code key) : source{input_source::key, (i32)key} {}
  /// @param button The mouse button.
  binding(mouse_button button) : source{input_source::mouse, (i32)button} {}
  /// @param button The gamepad button.
  binding(gamepad_button button) : source{input_source::pad, (i32)button} {}
};

/// Registers an action and binds all its sources in one call.
///
/// Replaces action_register() followed by action_bind_key(), action_bind_mouse(),
/// action_bind_pad() one after another:
/// @code
/// g.jump = njin::action_define(ctx, "jump", {njin::key_space, njin::key_w, njin::pad_face_down});
/// @endcode
/// If the name already exists, the sources are **added** to that action, like action_bind().
/// Call it before settings_load() or input_bindings_load(): keys the player has
/// rebound will replace these default keys.
/// @param ctx Engine context.
/// @param name Action name.
/// @param sources The keys, mouse buttons and gamepad buttons, in any order.
/// @return The action's handle, or an invalid handle (id 0) if `name` is null.
inline action_handle action_define(context &ctx, const char *name, std::initializer_list<binding> sources) {
  const action_handle handle = action_register(ctx, name);
  for (const binding &b : sources)
    action_bind(ctx, handle, b.source);
  return handle;
}

/// A key pair of an axis: the key for the negative direction and the key for the positive direction.
struct axis_keys {
  key_code negative; ///< Key for the negative direction (left, up).
  key_code positive; ///< Key for the positive direction (right, down).
};

/// Registers an axis and binds all its key pairs and gamepad axes in one call.
///
/// Replaces axis_register() followed by axis_bind_keys(), axis_bind_pad() one after another:
/// @code
/// g.move = njin::axis_define(ctx, "move",
///                            {{njin::key_left, njin::key_right}, {njin::key_a, njin::key_d}},
///                            {njin::pad_axis_left_x});
/// @endcode
/// If the name already exists, the sources are added to that axis. Call it before settings_load().
/// @param ctx Engine context.
/// @param name Axis name.
/// @param keys The key pairs (negative, positive).
/// @param pads The gamepad axes. May be left empty.
/// @return The axis's handle, or an invalid handle (id 0) if `name` is null.
inline axis_handle axis_define(context &ctx, const char *name, std::initializer_list<axis_keys> keys,
                               std::initializer_list<gamepad_axis> pads = {}) {
  const axis_handle handle = axis_register(ctx, name);
  for (const axis_keys &k : keys)
    axis_bind_keys(ctx, handle, k.negative, k.positive);
  for (const gamepad_axis a : pads)
    axis_bind_pad(ctx, handle, a);
  return handle;
}

/// All bound keys (actions and axes) as JSON, to save in the settings file:
/// `{"actions": {"jump": ["key:space", "pad:face_down"]}, "axes": {"move":
/// {"keys": [["left", "right"]], "pad": ["left_x"]}}}`.
/// @param ctx Engine context.
/// @return A JSON object.
json_value input_bindings_save(const context &ctx);

/// Loads keys from the JSON of input_bindings_save(). Actions and axes present
/// in the JSON have all their keys **replaced**; those not present are left
/// alone, so a new action added in a game update still has its default keys.
/// Unknown names are ignored (logged).
/// @param ctx Engine context.
/// @param json The data.
/// @return `false` if `json` is not an object.
bool input_bindings_load(context &ctx, const json_value &json);

/// Rumbles the gamepad.
/// @param ctx Engine context.
/// @param pad Gamepad index.
/// @param low Strength of the left motor (low rumble), 0..1.
/// @param high Strength of the right motor (fast rumble), 0..1.
/// @param seconds Rumble duration.
void pad_rumble(context &ctx, i32 pad, f32 low, f32 high, f32 seconds);
/// @}
} // namespace njin
