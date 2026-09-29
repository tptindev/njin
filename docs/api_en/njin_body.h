#pragma once
#include "_types.h"
#include <entt/entity/entity.hpp>
#include <vector>

namespace njin {
struct context;

/// @addtogroup grp_body
/// @{

/// Buttons and stick of a njin::platformer_body, written by the game every frame.
///
/// `jump` and `drop` are **requests**: set `true` on the frame of the press, and
/// the controller clears it once handled, so it is not missed even if that frame
/// has no physics step. njin::platformer_input_map writes these fields from
/// actions for you.
struct platformer_input {
  f32 move_x = 0.0f;      ///< Run direction, -1 (left) to 1 (right).
  bool jump = false;      ///< Jump request. Set on the frame the jump button is pressed.
  bool jump_held = false; ///< The jump button is being held. Releasing early gives a low jump.
  bool drop = false;      ///< Request to drop down through the one-way platform being stood on.
};

/// Platformer character: runs, jumps, falls, climbs slopes, stands on moving platforms.
///
/// Needs a transform and a box njin::collider on the same entity. The engine's
/// body module, in `phase_fixed_update`, reads `input`, computes `velocity` and
/// moves the entity with collision_move(), then writes back the state
/// (`grounded`, `on_wall`...). The game only needs to set the input and read the
/// state to choose animations.
///
/// The familiar platformer feel is all built in:
/// - **coyote time**: after leaving a ledge you can still jump for `coyote_time` seconds;
/// - **jump buffer**: pressing jump early, before landing, within `jump_buffer`
///   seconds still counts;
/// - **low jump on early release**: releasing the button while rising multiplies
///   the velocity by `jump_cut`;
/// - falling faster than rising (`fall_gravity`), a cap on fall speed (`max_fall`);
/// - air jumps (`air_jumps`), wall slide and wall jump (off by default);
/// - sticking to slopes when going downhill, dropping down through one-way
///   platforms (`input.drop`).
///
/// To knock the character back when hit, write directly to `velocity`.
///
/// All units are pixels and seconds. Sends the events njin::body_jumped and
/// njin::body_landed through events().
struct platformer_body {
  // --- tune the feel ---
  f32 run_speed = 110.0f;     ///< Maximum run speed.
  f32 ground_accel = 1200.0f; ///< Acceleration on the ground.
  f32 ground_decel = 1600.0f; ///< Deceleration on the ground when the button is released (or direction changes).
  f32 air_accel = 800.0f;     ///< Acceleration in the air.
  f32 air_decel = 500.0f;     ///< Deceleration in the air.
  f32 gravity = 1000.0f;      ///< Fall acceleration while rising.
  f32 fall_gravity = 1600.0f; ///< Fall acceleration while descending. Larger than `gravity` for a firm jump.
  f32 max_fall = 360.0f;      ///< Maximum fall speed.
  f32 jump_speed = 300.0f;    ///< Upward velocity at the start of a jump.
  f32 jump_cut = 0.45f;       ///< Multiplied into the upward velocity when the jump button is released early. 1 is off.
  f32 coyote_time = 0.09f;    ///< Seconds after leaving a ledge during which you can still jump.
  f32 jump_buffer = 0.12f;    ///< Seconds an early jump press is remembered.
  i32 air_jumps = 0;          ///< Number of extra jumps in the air. 1 is a double jump.
  f32 wall_slide_speed = 0.0f; ///< Slide speed when pressed against a wall while falling. 0 is off.
  vec2 wall_jump{0.0f, 0.0f};  ///< Velocity when jumping off a wall (x away from the wall, y up). 0 is off.
  f32 wall_jump_lock = 0.15f;  ///< Seconds to ignore `move_x` after a wall jump, so the character can kick off.

  platformer_input input{}; ///< Controls, written by the game.

  // --- state, written by the engine ---
  vec2 velocity{};       ///< Current velocity. The game may write it (knockback, springs).
  bool grounded = false; ///< Standing on ground, a slope or a platform.
  bool on_slope = false; ///< Standing on a slope.
  i32 on_wall = 0;       ///< -1 pressed against a wall on the left, 1 on the right, 0 none.
  i32 facing = 1;        ///< Facing direction: -1 left, 1 right. Follows `input.move_x`.
  entt::entity ground = entt::null; ///< Entity being stood on (tilemap or platform).
  bool jumped = false;   ///< Just jumped in the last physics step.
  bool landed = false;   ///< Just landed in the last physics step.

  // --- internal ---
  f32 coyote_timer = 0.0f;   ///< Remaining coyote time.
  f32 buffer_timer = 0.0f;   ///< Time the jump request is still remembered.
  f32 drop_timer = 0.0f;     ///< Time left for passing through one-way platforms.
  f32 wall_lock_timer = 0.0f; ///< Time left ignoring `move_x` after a wall jump.
  i32 air_jumps_left = 0;    ///< Number of air jumps remaining.
  bool rising = false;       ///< Rising from a jump with the button still held.
  bool ground_one_way = false; ///< The surface being stood on is a one-way platform.
};

/// Buttons and stick of a njin::topdown_body.
struct topdown_input {
  vec2 move{};       ///< Movement direction. Lengths above 1 are brought back to 1, so diagonal movement is not faster.
  bool dash = false; ///< Dash request. Set on the frame of the press; the controller clears it.
};

/// Top-down character: 8-way movement (or any direction with an analog stick),
/// sliding along walls, dashing.
///
/// Needs a transform and a njin::collider (box or circle). The engine's body
/// module moves it in `phase_fixed_update` with collision_move(). Write to
/// `velocity` for knockback. Sends njin::body_dashed when a dash starts.
struct topdown_body {
  f32 speed = 90.0f;   ///< Maximum movement speed.
  f32 accel = 900.0f;  ///< Acceleration.
  f32 decel = 1300.0f; ///< Deceleration when the button is released.
  f32 dash_speed = 0.0f;     ///< Dash speed. 0 turns dashing off.
  f32 dash_time = 0.14f;     ///< Duration of one dash, in seconds.
  f32 dash_cooldown = 0.35f; ///< Seconds to wait between two dashes, counted from the start.

  topdown_input input{}; ///< Controls, written by the game.

  vec2 velocity{};        ///< Current velocity. The game may write it.
  vec2 facing{0.0f, 1.0f}; ///< Facing direction, length 1: the most recent nonzero movement direction.
  bool moving = false;    ///< Moving (nonzero velocity).
  bool dashing = false;   ///< Dashing.
  f32 dash_timer = 0.0f;  ///< Remaining dash time.
  f32 cooldown_timer = 0.0f; ///< Remaining dash cooldown.
};

/// Binds actions and axes to a njin::platformer_body: the body module reads them
/// in `phase_pre_update` and writes `input`, so the game does not need to.
///
/// Dropping down through a one-way platform: hold `down` and press `jump`.
struct platformer_input_map {
  axis_handle move{};     ///< Horizontal axis (left/right keys, left stick).
  action_handle jump{};   ///< Jump.
  action_handle down{};   ///< Hold to drop down through a platform. May be left empty.
};

/// Binds actions and axes to a njin::topdown_body, like njin::platformer_input_map.
struct topdown_input_map {
  axis_handle move_x{}; ///< Horizontal axis.
  axis_handle move_y{}; ///< Vertical axis (positive is down).
  action_handle dash{}; ///< Dash. May be left empty.
};

/// Makes an entity follow a repeating polyline: moving platform, guard,
/// saw running on a rail.
///
/// The engine's body module, in `phase_fixed_update` (before the character),
/// moves the entity toward the next point at `speed`. An entity with a box
/// collider is moved with collision_move_platform(), so it carries a character
/// standing on it.
struct path_mover {
  std::vector<vec2> points; ///< The points, in the world. At least two.
  f32 speed = 40.0f;        ///< Speed, in pixels per second.
  f32 wait = 0.0f;          ///< Seconds to pause at each point.
  /// `true`: goes through all points, then returns to the first (closed loop).
  /// `false`: goes through all points, then goes back the other way (back and
  /// forth).
  bool loop = false;
  bool paused = false;      ///< Temporarily stopped.
  i32 target = 1;           ///< The point being moved toward.
  i32 direction = 1;        ///< Direction of travel through the list, when `loop` is `false`.
  f32 wait_timer = 0.0f;    ///< Remaining pause time.
};

/// Event: a njin::platformer_body just jumped (including air jumps and wall jumps).
struct body_jumped {
  entt::entity entity = entt::null; ///< The character.
  bool from_air = false;  ///< Jumped in the air (double jump).
  bool from_wall = false; ///< Jumped off a wall.
};

/// Event: a njin::platformer_body just landed.
struct body_landed {
  entt::entity entity = entt::null; ///< The character.
  f32 speed = 0.0f; ///< Fall speed at landing: for shaking the screen on a hard fall.
};

/// Event: a njin::topdown_body just started a dash.
struct body_dashed {
  entt::entity entity = entt::null; ///< The character.
  vec2 direction{}; ///< Dash direction, length 1.
};
/// @}
} // namespace njin
