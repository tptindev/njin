#pragma once
#include "_comps.h"
#include "_types.h"
#include <entt/entity/entity.hpp>

namespace njin {
struct njin_ctx;
struct level_handle;

/// @addtogroup grp_camera
/// @{

/// Makes a camera follow an entity: smooth, with a dead zone, looking ahead in the
/// running direction, and never showing anything outside the level frame.
///
/// Attach it to the camera entity (with a transform, njin::camera_2d, njin::camera_on).
/// The engine's camera_follow module, in `phase_post_update` (after characters
/// have moved, before tilemaps are prepared for drawing), moves the camera's
/// `transform.pos`.
/// @code
/// const entt::entity cam = njin::camera_spawn(ctx, 3.0f);
/// reg.emplace<njin::camera_follow>(cam, njin::camera_follow{
///     .target = player,
///     .deadzone = {24, 32},
///     .lookahead = {40, 0},
///     .bounds = njin::level_bounds(ctx, level)});
/// @endcode
///
/// All distances are in world units (world pixels before zooming).
/// The screen shake of njin::fx is unaffected: it is added in when drawing.
struct camera_follow {
  entt::entity target = entt::null; ///< Entity to follow (with a transform). null means stand still.
  vec2 offset{}; ///< Added to the target position: `{0, -16}` to look higher than the character's head.
  /// Size of a rectangle around the screen center inside which the camera stays still
  /// while the target moves. `{0, 0}` means always follow.
  vec2 deadzone{};
  /// Lag, in seconds: it takes about this long to cover 63% of the distance to the new position.
  /// 0 follows tightly.
  f32 smoothing = 0.12f;
  /// Look ahead in the direction the target is moving, per axis: `{48, 0}` for a
  /// platformer (horizontal only), `{32, 32}` for top-down.
  vec2 lookahead{};
  /// Lag of the look-ahead part, in seconds. Larger than `smoothing` so turning around does not jerk.
  f32 lookahead_smoothing = 0.5f;
  /// World region the view may not leave. A size of 0 means unbounded. A level smaller than
  /// the screen is centered on the screen. See level_bounds().
  rect bounds{};
  /// Keep `camera_2d::offset` at the screen center (also when the window is resized or the
  /// virtual resolution changes). `false` to set the offset yourself.
  bool center = true;
  /// Round the camera position to screen pixels. Turn on for pixel art so tiles
  /// do not shimmer by one pixel when the camera drifts slowly.
  bool pixel_snap = false;

  vec2 look{};        ///< Current look-ahead part, written by the engine.
  vec2 last_target{}; ///< Target position on the previous frame, written by the engine.
  /// The position has been set for the first time. Reset to `false` to make the camera jump straight to the target,
  /// for example after the character teleports.
  bool started = false;
};

/// Creates an active camera entity (transform, njin::camera_2d, njin::camera_on)
/// looking at `pos`, with the offset at the screen center.
/// @param ctx Engine context.
/// @param zoom Zoom. 2 makes everything twice as big.
/// @param pos Initial look point in the world.
/// @return The camera entity. Add njin::camera_follow to make it follow a character.
entt::entity camera_spawn(njin_ctx &ctx, f32 zoom = 1.0f, vec2 pos = {});

/// Frame of a level in the world, for camera_follow::bounds.
/// @param ctx Engine context.
/// @param level A loaded level.
/// @return `{level_origin, level_size}`, or an empty rectangle if the handle is invalid.
rect level_bounds(const njin_ctx &ctx, level_handle level);

/// Limits a camera position so the view stays inside `bounds`.
/// @param ctx Engine context (to know the screen size).
/// @param pos Camera position (`transform.pos`).
/// @param cam Camera.
/// @param bounds World region. A size of 0 means unbounded.
/// @return The limited position.
vec2 camera_clamp(const njin_ctx &ctx, vec2 pos, const camera_2d &cam, rect bounds);

/// Returns the view used for this frame.
///
/// That is the entity with camera_on, camera_2d and transform (see _comps.h), or the
/// identity view (world matches screen pixels) if there is no such entity.
/// Read directly from the registry, so changes take effect immediately.
/// @param ctx Engine context.
/// @return The view in use.
camera_view camera_active(const njin_ctx &ctx);

/// Converts a point from the world to screen pixels, through camera_active().
/// @param ctx Engine context.
/// @param pos Point in the world.
/// @return The corresponding position on the screen.
vec2 w2scr(const njin_ctx &ctx, vec2 pos);

/// Converts a point from screen pixels to the world, through camera_active().
///
/// Commonly used to turn the mouse position, from mouse_pos(), into a position in the world.
/// @param ctx Engine context.
/// @param pos Point on the screen (pixels).
/// @return The corresponding position in the world.
vec2 scr2w(const njin_ctx &ctx, vec2 pos);

/// World region currently shown on the screen.
///
/// When the camera rotates, this is the axis-aligned rectangle enclosing the visible region.
/// Use it to skip drawing things that are off screen.
/// @param ctx Engine context.
/// @return Rectangle in the world.
rect camera_bounds(const njin_ctx &ctx);

/// Applies a post-processing shader to the whole world seen through the camera.
///
/// When on, everything drawn in `phase_pre_render` and `phase_render` (including sprites
/// and tilemaps) is drawn into an off-screen image, then drawn to the screen through
/// this shader. UI drawn in `phase_post_render` is unaffected. Set the shader's
/// uniforms as usual with the shader_set_*() functions.
/// @param ctx Engine context.
/// @param shader Post-processing shader. A handle with id 0 turns it off.
void camera_set_post_shader(njin_ctx &ctx, shader_handle shader);
/// @}
} // namespace njin
