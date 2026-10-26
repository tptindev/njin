#pragma once
#include "_types.h"

namespace njin {
// Opaque, see njin_ctx.h.
struct context;

/// @addtogroup grp_input
/// @{

/// True on the frame the key was just pressed down (one frame only).
/// @param ctx Engine context.
/// @param key Key to check.
/// @return `true` on the frame the key was just pressed.
bool key_pressed(const context &ctx, key_code key);

/// True while the key has been held since the previous frame and is still held.
///
/// Note: on the first frame of the press this function returns false (key_pressed()
/// returns true then). To know whether the key is currently down, use
/// `key_pressed(...) || key_held(...)`.
/// @param ctx Engine context.
/// @param key Key to check.
/// @return `true` if the key has been held since the previous frame and is still held.
bool key_held(const context &ctx, key_code key);

/// True on the frame the key was just released (one frame only).
/// @param ctx Engine context.
/// @param key Key to check.
/// @return `true` on the frame the key was just released.
bool key_released(const context &ctx, key_code key);

/// Registers a named action. If the name already exists, returns the existing handle.
///
/// An action is a logical name (for example "jump") that can be bound to one or more keys.
/// @param ctx Engine context.
/// @param name Action name.
/// @return Handle of the action, or an invalid handle (id 0) if `name` is null.
action_handle action_register(context &ctx, const char *name);

/// Finds an action by name.
/// @param ctx Engine context.
/// @param name Action name.
/// @return Handle of the action, or an invalid handle (id 0) if there is none.
action_handle action_find(const context &ctx, const char *name);

/// Binds a key to an action. Several keys can be bound to the same action.
/// @param ctx Engine context.
/// @param handle Action to bind to.
/// @param key Key to bind. An invalid key is ignored.
void action_bind_key(context &ctx, action_handle handle, key_code key);

/// Binds a mouse button to an action. Keys, mouse buttons and gamepad buttons can be
/// mixed on the same action: any source that is satisfied satisfies the action.
/// @param ctx Engine context.
/// @param handle Action to bind to.
/// @param button Mouse button to bind. An invalid button is ignored.
void action_bind_mouse(context &ctx, action_handle handle, mouse_button button);

/// Binds a gamepad button to an action. Any connected gamepad satisfies it.
/// @param ctx Engine context.
/// @param handle Action to bind to.
/// @param button Gamepad button to bind. An invalid button is ignored.
void action_bind_pad(context &ctx, action_handle handle, gamepad_button button);

/// Clears every key, mouse button and gamepad button bound to the action.
///
/// To rebind keys at runtime, call this and then bind again. It can be used at any
/// time, not only in `setup`.
/// @param ctx Engine context.
/// @param handle Action to clear.
void action_clear_binds(context &ctx, action_handle handle);

/// True on the frame one of the action's sources (key, mouse button, gamepad button)
/// was just pressed.
/// @param ctx Engine context.
/// @param handle Action to check.
/// @return `true` on the frame one of the action's sources was just pressed. `false` if the handle is invalid.
bool action_pressed(const context &ctx, action_handle handle);

/// True while one of the action's sources has been held since the previous frame and
/// is still held. Same rule as key_held().
/// @param ctx Engine context.
/// @param handle Action to check.
/// @return `true` if one of the action's sources is held. `false` if the handle is invalid.
bool action_held(const context &ctx, action_handle handle);

/// True on the frame one of the action's sources was just released.
/// @param ctx Engine context.
/// @param handle Action to check.
/// @return `true` on the frame one of the action's sources was just released. `false` if the handle is invalid.
bool action_released(const context &ctx, action_handle handle);

/// Mouse cursor position, in screen pixels.
///
/// Not affected by the camera. Use scr2w() to convert to a position in the world.
/// @param ctx Engine context.
/// @return Mouse cursor position.
vec2 mouse_pos(const context &ctx);

/// Mouse movement since the previous frame, in pixels.
/// @param ctx Engine context.
/// @return Mouse movement.
vec2 mouse_delta(const context &ctx);

/// Number of mouse wheel notches scrolled this frame. Positive is scrolling away from
/// the user. Most frames return 0.
/// @param ctx Engine context.
/// @return Number of notches scrolled.
f32 mouse_wheel(const context &ctx);

/// True on the frame the mouse button was just pressed down (one frame only).
/// @param ctx Engine context.
/// @param button Button to check.
/// @return `true` on the frame the button was just pressed.
bool mouse_pressed(const context &ctx, mouse_button button);

/// True while the mouse button has been held since the previous frame and is still
/// held. Same rule as key_held(): returns false on the first frame of the press.
/// @param ctx Engine context.
/// @param button Button to check.
/// @return `true` if the button has been held since the previous frame and is still held.
bool mouse_held(const context &ctx, mouse_button button);

/// True on the frame the mouse button was just released (one frame only).
/// @param ctx Engine context.
/// @param button Button to check.
/// @return `true` on the frame the button was just released.
bool mouse_released(const context &ctx, mouse_button button);

/// How many fingers njin tracks at once.
constexpr i32 touch_max = 10;

/// One finger on a touch screen, see touch_get().
struct touch_point {
  /// This finger's number, the same from touching down to lifting. A later finger gets a new one.
  i32 id = 0;
  vec2 pos{};      ///< Position, in the same units as mouse_pos().
  vec2 delta{};    ///< Movement since the previous frame; on the touch frame, since where it landed.
  vec2 start{};    ///< Where the finger touched down.
  f32 duration = 0.0f; ///< How long it has been touching, seconds (real time, not the time scale).
  bool pressed = false;  ///< Touched down this frame.
  bool released = false; ///< Lifted this frame (its last frame in the list).
};

/// Number of fingers touching, including one lifted this frame.
///
/// Touch screens on Windows 8 and later, several fingers at once. The first
/// finger also drives the mouse, as Windows always does (mouse_pos(),
/// mouse_pressed() with `mouse_left`), so games and UI that only use the mouse
/// can be touched; this function and gestures() are for what needs several
/// fingers (pinch, rotate) or needs to know it is a finger. Other platforms do
/// not read touch yet: always 0.
/// @param ctx Engine context.
/// @return Number of fingers, 0..touch_max.
i32 touch_count(const context &ctx);

/// Finger number `index` (0..touch_count() - 1), in the order they touched down.
/// @code
/// for (njin::i32 i = 0; i < njin::touch_count(ctx); i++) {
///   const njin::touch_point t = njin::touch_get(ctx, i);
///   if (t.pressed) spawn_ripple(t.pos);
/// }
/// @endcode
/// @param ctx Engine context.
/// @param index Finger's place.
/// @return That finger; for an `index` out of range a default touch_point (`id` 0).
touch_point touch_get(const context &ctx, i32 index);

/// This frame's gestures, worked out from the fingers (touch_get()).
struct gesture_state {
  i32 fingers = 0;       ///< Fingers touching (not counting one just lifted).
  /// A quick touch and lift: under 0.3 seconds, sliding no more than a few
  /// pixels. True on the frame the finger lifts.
  bool tap = false;
  vec2 tap_pos{};        ///< Where the tap was.
  /// One finger held still for 0.5 seconds: true once, on the frame the time is
  /// up (context menus, pick up to drag). That finger lifting afterwards is no `tap`.
  bool long_press = false;
  vec2 long_press_pos{}; ///< Where it is held.
  /// Average movement of the fingers touching since the previous frame: one
  /// finger drags the map, two drag together.
  vec2 pan{};
  /// With two fingers or more: the first two fingers' distance over last
  /// frame's. Above 1 is spreading (zoom in), 1 is unchanged.
  f32 pinch = 1.0f;
  /// With two fingers or more: how far the line through the first two fingers
  /// turned since the previous frame, degrees, clockwise on the screen.
  f32 rotate = 0.0f;
  vec2 center{};         ///< Midpoint of the first two fingers (or where one is), to zoom around.
};

/// This frame's touch gestures.
/// @code
/// const njin::gesture_state g = njin::gestures(ctx);
/// cam.zoom *= g.pinch;                  // spreading two fingers zooms in
/// cam_pos -= g.pan / cam.zoom;          // drag the map with the finger
/// if (g.tap) select_at(njin::scr2w(ctx, g.tap_pos));
/// @endcode
/// @param ctx Engine context.
/// @return Gestures.
gesture_state gestures(const context &ctx);

/// Whether gamepad number `pad` is connected.
///
/// While it is not connected, every pad_* function reads as not pressed, without
/// reporting an error.
/// @param ctx Engine context.
/// @param pad Gamepad index, from 0 to gamepad_max - 1.
/// @return `true` if the gamepad is connected.
bool pad_available(const context &ctx, i32 pad);

/// True on the frame the gamepad button was just pressed down (one frame only).
/// @param ctx Engine context.
/// @param pad Gamepad index, from 0 to gamepad_max - 1.
/// @param button Button to check.
/// @return `true` on the frame the button was just pressed. `false` if the gamepad is not connected.
bool pad_pressed(const context &ctx, i32 pad, gamepad_button button);

/// True while the gamepad button has been held since the previous frame and is still
/// held. Same rule as key_held().
/// @param ctx Engine context.
/// @param pad Gamepad index, from 0 to gamepad_max - 1.
/// @param button Button to check.
/// @return `true` if the button has been held since the previous frame and is still held.
bool pad_held(const context &ctx, i32 pad, gamepad_button button);

/// True on the frame the gamepad button was just released (one frame only).
/// @param ctx Engine context.
/// @param pad Gamepad index, from 0 to gamepad_max - 1.
/// @param button Button to check.
/// @return `true` on the frame the button was just released.
bool pad_released(const context &ctx, i32 pad, gamepad_button button);

/// Value of one analog axis of the gamepad.
///
/// Passed through the deadzone and rescaled, so it leaves 0 smoothly instead of
/// jumping in steps. Triggers rest at -1 and read 1 when fully pressed (raylib's
/// convention, not remapped).
/// @param ctx Engine context.
/// @param pad Gamepad index, from 0 to gamepad_max - 1.
/// @param axis Axis to read.
/// @return Value from -1 to 1, or 0 if the gamepad is not connected.
f32 pad_axis(const context &ctx, i32 pad, gamepad_axis axis);

/// Sets the deadzone for every stick on every gamepad. Default 0.15.
/// @param ctx Engine context.
/// @param deadzone Deadzone, clamped to the range 0 to 0.95.
void pad_set_deadzone(context &ctx, f32 deadzone);

/// Number of characters typed this frame.
///
/// Characters have passed through the operating system's keyboard layout and dead
/// keys. This is what a text input box needs; key_pressed() is what a shortcut key
/// needs, two different questions.
/// @param ctx Engine context.
/// @return Number of characters, at most 32 per frame.
i32 text_count(const context &ctx);

/// Unicode code point of the `index`-th character typed this frame, in typing order.
/// @param ctx Engine context.
/// @param index Index, from 0 to text_count() - 1.
/// @return Unicode code point, or 0 if `index` is out of range.
i32 text_char(const context &ctx, i32 index);

/// Clears the state of a key for the rest of the frame.
///
/// Systems that run afterwards do not see this key as pressed or held. This is how a
/// menu swallows a key before the world underneath handles the same key. The state is
/// read again on the next frame, so the effect does not outlast the frame that calls it.
/// @param ctx Engine context.
/// @param key Key to clear.
void key_consume(context &ctx, key_code key);

/// Clears the state of a mouse button for the rest of the frame.
/// See key_consume().
/// @param ctx Engine context.
/// @param button Button to clear.
void mouse_consume(context &ctx, mouse_button button);

/// Clears the mouse wheel scroll for the rest of the frame. See key_consume().
/// @param ctx Engine context.
void mouse_wheel_consume(context &ctx);

/// Registers a named axis. If the name already exists, returns the existing handle.
///
/// An axis reads a value from -1 to 1, unlike an action, which is true or false. For a
/// two-dimensional vector, read two axes and combine them: only the caller knows
/// whether that vector needs to be normalized.
/// @param ctx Engine context.
/// @param name Axis name.
/// @return Handle of the axis, or an invalid handle (id 0) if `name` is null.
axis_handle axis_register(context &ctx, const char *name);

/// Finds an axis by name.
/// @param ctx Engine context.
/// @param name Axis name.
/// @return Handle of the axis, or an invalid handle (id 0) if there is none.
axis_handle axis_find(const context &ctx, const char *name);

/// Binds a pair of keys to an axis, acting as an analog stick.
///
/// Only the negative key reads -1, only the positive key reads 1, both or neither
/// reads 0.
/// @param ctx Engine context.
/// @param handle Axis to bind to.
/// @param negative Key for the negative direction.
/// @param positive Key for the positive direction.
void axis_bind_keys(context &ctx, axis_handle handle, key_code negative,
                    key_code positive);

/// Binds a gamepad axis to an axis. Any connected gamepad is read.
/// @param ctx Engine context.
/// @param handle Axis to bind to.
/// @param axis Gamepad axis to bind. An invalid axis is ignored.
void axis_bind_pad(context &ctx, axis_handle handle, gamepad_axis axis);

/// Clears every key pair and gamepad axis bound to the axis.
/// @param ctx Engine context.
/// @param handle Axis to clear.
void axis_clear_binds(context &ctx, axis_handle handle);

/// Current value of the axis.
///
/// The source that deviates furthest from rest wins, so a half push is not flattened
/// by a key pair standing still at 0, and vice versa.
/// @param ctx Engine context.
/// @param handle Axis to read.
/// @return Value from -1 to 1, or 0 if the handle is invalid.
f32 axis_value(const context &ctx, axis_handle handle);
/// @}
} // namespace njin
