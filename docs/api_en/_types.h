#pragma once

#include <cstddef>
#include <cstdint>

namespace njin {
/// @addtogroup grp_types
/// @{

/// @name Signed integers
/// @{
using i8 = std::int8_t; ///< 8-bit signed integer.
using i16 = std::int16_t; ///< 16-bit signed integer.
using i32 = std::int32_t; ///< 32-bit signed integer.
using i64 = std::int64_t; ///< 64-bit signed integer.
/// @}

/// @name Unsigned integers
/// @{
using u8 = std::uint8_t; ///< 8-bit unsigned integer.
using u16 = std::uint16_t; ///< 16-bit unsigned integer.
using u32 = std::uint32_t; ///< 32-bit unsigned integer.
using u64 = std::uint64_t; ///< 64-bit unsigned integer.
/// @}

/// @name Floating point
/// @{
using f32 = float; ///< 32-bit floating point number.
using f64 = double; ///< 64-bit floating point number.
/// @}

/// @name Pointer-sized integers
/// @{
using usize = std::size_t; ///< Pointer-sized unsigned integer, used for sizes and indices.
using isize = std::ptrdiff_t; ///< Pointer-sized signed integer, used for the difference of two indices.
/// @}

/// 2D vector. Used for positions, sizes, offsets.
struct vec2 {
  f32 x; ///< Horizontal component.
  f32 y; ///< Vertical component (in screen space, y grows downward).
};

/// 3D vector. Used for positions and directions in the 3D world (njin_3d.h),
/// and to pass `vec3` values to shader uniforms: colors that need no alpha,
/// directions, positions with a height.
struct vec3 {
  f32 x; ///< First component.
  f32 y; ///< Second component.
  f32 z; ///< Third component.
};

/// 4D vector. Used to pass `vec4` values to shader uniforms.
struct vec4 {
  f32 x; ///< First component.
  f32 y; ///< Second component.
  f32 z; ///< Third component.
  f32 w; ///< Fourth component.
};

/// Color made of red, green, blue and alpha. Each channel is in the range 0..1.
///
/// `{1, 1, 1, 1}` is opaque white, `{0, 0, 0, 0}` is fully transparent.
struct rgba {
  f32 r; ///< Red.
  f32 g; ///< Green.
  f32 b; ///< Blue.
  f32 a; ///< Opacity (0 = transparent, 1 = fully opaque).
};

/// The 2D view currently used for drawing, obtained from camera_active().
///
/// The world point `target` is drawn at the screen position `offset`.
struct camera_view {
  f32 zoom;     ///< Magnification. 1 means no zoom.
  f32 rotation; ///< Rotation angle in degrees, clockwise.
  vec2 offset;  ///< Screen position (pixels) where `target` is drawn.
  vec2 target;  ///< World point the camera looks at.
};

/// Identifier of an input action, created by action_register().
///
/// `id == 0` is an invalid handle. Every function that receives an invalid
/// handle does nothing and returns `false`.
struct action_handle {
  u32 id = 0; ///< 0 means invalid.
};

/// Identifier of an input axis, created by axis_register().
///
/// `id == 0` is an invalid handle. Every function that receives an invalid
/// handle does nothing and axis_value() returns 0.
struct axis_handle {
  u32 id = 0; ///< 0 means invalid.
};

/// Identifier of a shader, created by shader_load().
///
/// `id == 0` is an invalid handle (for example a missing file or a compile error).
/// An unloaded handle is also ignored and never points to a new shader by mistake.
struct shader_handle {
  u32 id = 0; ///< 0 means invalid.
};

/// Identifier of an instance buffer, created by instance_buffer_create().
///
/// `id == 0` is an invalid handle. A destroyed handle is also ignored and never
/// points to a new buffer by mistake.
struct instance_buffer_handle {
  u32 id = 0; ///< 0 means invalid.
};

/// Identifier of a short sound (sound), created by sound_load().
///
/// `id == 0` is an invalid handle. An unloaded handle is also ignored.
struct sound_handle {
  u32 id = 0; ///< 0 means invalid.
};

/// Identifier of a streamed music track (music), created by music_load().
///
/// `id == 0` is an invalid handle. An unloaded handle is also ignored.
struct music_handle {
  u32 id = 0; ///< 0 means invalid.
};

/// Identifier of a font, created by font_load().
///
/// `id == 0` is the engine's default font (JetBrains Mono, with Vietnamese glyphs). An unloaded
/// font is also replaced by the default font.
struct font_handle {
  u32 id = 0; ///< 0 means the default font.
};

/// Identifier of a scene, created by scene_register().
///
/// `id == 0` is "no scene". In njin::sys_desc, scene 0 means the system
/// runs in every scene.
struct scene_handle {
  u32 id = 0; ///< 0 means no scene.
};

/// Identifier of a texture, created by texture_load().
///
/// `id == 0` is an invalid handle. An unloaded handle is also ignored.
struct texture_handle {
  u32 id = 0; ///< 0 means invalid.
};

/// Identifier of a 3D model, created by model_load().
///
/// `id == 0` is an invalid handle. An unloaded handle is also ignored.
struct model_handle {
  u32 id = 0; ///< 0 means invalid.
};

/// Identifier of a 3D physics body, created by body3d_create().
///
/// `id == 0` is an invalid handle. A destroyed handle is also ignored.
struct body3d_handle {
  u32 id = 0; ///< 0 means invalid.
};

/// Identifier of a 3D physics character, created by character3d_create().
///
/// `id == 0` is an invalid handle. A destroyed handle is also ignored.
struct character3d_handle {
  u32 id = 0; ///< 0 means invalid.
};

/// Identifier of a 3D physics joint, created by joint3d_create().
///
/// `id == 0` is an invalid handle. A destroyed handle is also ignored.
struct joint3d_handle {
  u32 id = 0; ///< 0 means invalid.
};

/// Identifier of an atlas, created by atlas_create().
///
/// `id == 0` is an invalid handle. A destroyed handle is also ignored.
struct atlas_handle {
  u32 id = 0; ///< 0 means invalid.
};

/// Identifier of a render texture, created by render_texture_load().
///
/// `id == 0` is an invalid handle. An unloaded handle is also ignored.
struct render_texture_handle {
  u32 id = 0; ///< 0 means invalid.
};

/// Identifier of a prefab, created by prefab_register().
///
/// `id == 0` is an invalid handle: prefab_spawn() with it creates nothing.
struct prefab_handle {
  u32 id = 0; ///< 0 means invalid.
};

/// Identifier of an animated sprite sheet, created by anim_sheet_load() or
/// anim_sheet_grid().
///
/// `id == 0` is an invalid handle. An unloaded handle is also ignored.
struct anim_sheet_handle {
  u32 id = 0; ///< 0 means invalid.
};

/// Identifier of an animation state machine, created by anim_graph_create().
///
/// `id == 0` is "no state machine": the animator then only runs the clip
/// chosen with animator_play().
struct anim_graph_handle {
  u32 id = 0; ///< 0 means none.
};
/// @}


/// @addtogroup grp_input
/// @{

/// Keyboard key code, used with key_pressed(), action_bind_key() and related functions.
enum key_code {
  key_none = 0, ///< No key. Invalid when binding or querying.

  // Letters
  key_a, ///< Key A
  key_b, ///< Key B
  key_c, ///< Key C
  key_d, ///< Key D
  key_e, ///< Key E
  key_f, ///< Key F
  key_g, ///< Key G
  key_h, ///< Key H
  key_i, ///< Key I
  key_j, ///< Key J
  key_k, ///< Key K
  key_l, ///< Key L
  key_m, ///< Key M
  key_n, ///< Key N
  key_o, ///< Key O
  key_p, ///< Key P
  key_q, ///< Key Q
  key_r, ///< Key R
  key_s, ///< Key S
  key_t, ///< Key T
  key_u, ///< Key U
  key_v, ///< Key V
  key_w, ///< Key W
  key_x, ///< Key X
  key_y, ///< Key Y
  key_z, ///< Key Z

  // Numbers
  key_0, ///< Key 0 (number row)
  key_1, ///< Key 1 (number row)
  key_2, ///< Key 2 (number row)
  key_3, ///< Key 3 (number row)
  key_4, ///< Key 4 (number row)
  key_5, ///< Key 5 (number row)
  key_6, ///< Key 6 (number row)
  key_7, ///< Key 7 (number row)
  key_8, ///< Key 8 (number row)
  key_9, ///< Key 9 (number row)

  // Function keys
  key_f1, ///< Key F1
  key_f2, ///< Key F2
  key_f3, ///< Key F3
  key_f4, ///< Key F4
  key_f5, ///< Key F5
  key_f6, ///< Key F6
  key_f7, ///< Key F7
  key_f8, ///< Key F8
  key_f9, ///< Key F9
  key_f10, ///< Key F10
  key_f11, ///< Key F11
  key_f12, ///< Key F12

  // Navigation
  key_up, ///< Up arrow
  key_down, ///< Down arrow
  key_left, ///< Left arrow
  key_right, ///< Right arrow

  key_home, ///< Home
  key_end, ///< End
  key_page_up, ///< Page Up
  key_page_down, ///< Page Down
  key_insert, ///< Insert
  key_delete, ///< Delete

  // General
  key_space, ///< Space bar
  key_enter, ///< Enter
  key_tab, ///< Tab
  key_escape, ///< Esc
  key_backspace, ///< Backspace

  key_caps_lock, ///< Caps Lock
  key_scroll_lock, ///< Scroll Lock
  key_num_lock, ///< Num Lock
  key_print_screen, ///< Print Screen
  key_pause, ///< Pause/Break

  // Symbols
  key_apostrophe, ///< Apostrophe
  key_comma, ///< Comma
  key_minus, ///< Minus sign
  key_period, ///< Period
  key_slash, ///< Slash
  key_semicolon, ///< Semicolon
  key_equal, ///< Equals sign
  key_left_bracket, ///< Left square bracket
  key_backslash, ///< Backslash
  key_right_bracket, ///< Right square bracket
  key_grave, ///< Grave accent

  // Modifiers
  key_left_shift, ///< Left Shift
  key_left_control, ///< Left Ctrl
  key_left_alt, ///< Left Alt
  key_left_super, ///< Left Windows/Super

  key_right_shift, ///< Right Shift
  key_right_control, ///< Right Ctrl
  key_right_alt, ///< Right Alt
  key_right_super, ///< Right Windows/Super

  // Keypad
  key_kp_0, ///< Keypad: 0
  key_kp_1, ///< Keypad: 1
  key_kp_2, ///< Keypad: 2
  key_kp_3, ///< Keypad: 3
  key_kp_4, ///< Keypad: 4
  key_kp_5, ///< Keypad: 5
  key_kp_6, ///< Keypad: 6
  key_kp_7, ///< Keypad: 7
  key_kp_8, ///< Keypad: 8
  key_kp_9, ///< Keypad: 9

  key_kp_decimal, ///< Keypad: decimal point
  key_kp_divide, ///< Keypad: divide
  key_kp_multiply, ///< Keypad: multiply
  key_kp_subtract, ///< Keypad: subtract
  key_kp_add, ///< Keypad: add
  key_kp_enter, ///< Keypad: Enter
  key_kp_equal, ///< Keypad: equals

  key_count ///< Number of key codes. Not a real key.
};

/// Maximum number of gamepads tracked. Valid gamepad indices are `0..gamepad_max-1`.
inline constexpr i32 gamepad_max = 4;

/// Mouse button, used with mouse_pressed(), action_bind_mouse() and related functions.
enum mouse_button {
  mouse_left,    ///< Left mouse button.
  mouse_right,   ///< Right mouse button.
  mouse_middle,  ///< Middle mouse button (wheel press).
  mouse_side,    ///< Side button.
  mouse_extra,   ///< Extra button.
  mouse_forward, ///< Forward button.
  mouse_back,    ///< Back button.
  mouse_button_count ///< Number of mouse buttons. Not a real button.
};

/// Gamepad button, used with pad_pressed(), action_bind_pad() and related functions.
///
/// Face buttons are named by position, not by label: `pad_face_down` is A on Xbox and
/// Cross (X) on PlayStation.
enum gamepad_button {
  pad_none, ///< No button. Invalid when binding or querying.
  pad_dpad_up,    ///< D-pad up.
  pad_dpad_right, ///< D-pad right.
  pad_dpad_down,  ///< D-pad down.
  pad_dpad_left,  ///< D-pad left.
  pad_face_up,    ///< Top face button (Y on Xbox, Triangle on PlayStation).
  pad_face_right, ///< Right face button (B on Xbox, Circle on PlayStation).
  pad_face_down,  ///< Bottom face button (A on Xbox, Cross on PlayStation).
  pad_face_left,  ///< Left face button (X on Xbox, Square on PlayStation).
  pad_l1,         ///< Left shoulder button.
  pad_l2,         ///< Left trigger, read as a button.
  pad_r1,         ///< Right shoulder button.
  pad_r2,         ///< Right trigger, read as a button.
  pad_select,     ///< Select / Back / Share button.
  pad_guide,      ///< Guide / Home button.
  pad_start,      ///< Start / Menu button.
  pad_left_thumb, ///< Left stick press.
  pad_right_thumb, ///< Right stick press.
  gamepad_button_count ///< Number of gamepad buttons. Not a real button.
};

/// Gamepad analog axis, used with pad_axis() and axis_bind_pad().
enum gamepad_axis {
  pad_axis_left_x,        ///< Left stick, horizontal axis (-1 is left, 1 is right).
  pad_axis_left_y,        ///< Left stick, vertical axis (-1 is up, 1 is down).
  pad_axis_right_x,       ///< Right stick, horizontal axis.
  pad_axis_right_y,       ///< Right stick, vertical axis.
  pad_axis_left_trigger,  ///< Left trigger. Reads -1 at rest, 1 when fully pressed.
  pad_axis_right_trigger, ///< Right trigger. Reads -1 at rest, 1 when fully pressed.
  gamepad_axis_count      ///< Number of axes. Not a real axis.
};
/// @}
} // namespace njin
