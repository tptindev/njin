#pragma once
#include "_math.h"
#include "_tween.h"
#include "_types.h"
#include <entt/entity/entity.hpp>
#include <functional>

namespace njin {
struct context;

/// @addtogroup grp_timer
/// @{

/// Identifies a timer, from timer_after() or timer_every().
struct timer_handle {
  u32 id = 0; ///< 0 means invalid.
};

/// Identifies a tween running on an entity, from tween_move() and its siblings.
struct tween_handle {
  u32 id = 0; ///< 0 means invalid.
};

/// Options for a timer.
struct timer_desc {
  /// Bind to this entity: when the entity is destroyed the timer cancels itself,
  /// so the function is never called with a dead entity.
  entt::entity owner = entt::null;
  /// Measured in real time: keeps running while the game is paused, in hitstop
  /// or in slow motion (menus, UI). Defaults to game time.
  bool real_time = false;
  /// Keeps running across scene changes. By default a timer belongs to the scene
  /// that was running when it was created and is cancelled when that scene is left.
  bool keep_across_scenes = false;
};

/// Calls `fn` once after `seconds` seconds.
/// @code
/// njin::timer_after(ctx, 0.4f, [](njin::context &c) { njin::scene_fade(c, next); });
/// njin::timer_after(ctx, 1.5f, respawn, {.owner = player});
/// @endcode
/// The function runs in `phase_update` (before the game's systems), so it may add
/// and destroy entities and create new timers. Time is counted from the frame of the call.
/// @param ctx Engine context.
/// @param seconds Time to wait, in seconds. 0 means next frame.
/// @param fn Function to call.
/// @param desc Options.
/// @return Handle for cancelling with timer_cancel().
timer_handle timer_after(context &ctx, f32 seconds, std::function<void(context &)> fn,
                         const timer_desc &desc = {});

/// Calls `fn` every `interval` seconds: spawn monsters on a beat, heal gradually.
/// @param ctx Engine context.
/// @param interval Time between two calls, in seconds. Greater than 0.
/// @param fn Function to call.
/// @param count Number of calls, -1 means forever (until timer_cancel()).
/// @param desc Options.
/// @return Handle for cancelling with timer_cancel().
timer_handle timer_every(context &ctx, f32 interval, std::function<void(context &)> fn,
                         i32 count = -1, const timer_desc &desc = {});

/// Cancels a timer. An expired or invalid handle is ignored.
/// @param ctx Engine context.
/// @param timer Timer.
void timer_cancel(context &ctx, timer_handle timer);

/// Whether a timer is still pending.
/// @param ctx Engine context.
/// @param timer Timer.
/// @return `true` if it has not finished and has not been cancelled.
bool timer_active(const context &ctx, timer_handle timer);

/// Options for a tween.
struct tween_desc {
  f32 delay = 0.0f;       ///< How long to wait before starting, in seconds.
  /// Number of extra repeats after the first run. -1 means forever.
  i32 repeat = 0;
  /// On each repeat, go back the other way (there and back) instead of jumping to the start.
  bool yoyo = false;
  bool real_time = false; ///< Measured in real time, like timer_desc::real_time.
  /// Called when the tween finishes (not called when it is cancelled).
  std::function<void(context &)> done{};
};

/// Moves the entity's `transform.pos` to `to` over `seconds` seconds.
///
/// Every tween on an entity: starts from the current value (when `delay` ends),
/// cancels itself when the entity is destroyed, and replaces an older tween **of
/// the same kind** on the same entity (calling tween_move twice means the later
/// one wins). Runs in `phase_update`.
/// @code
/// njin::tween_move(ctx, door, door_pos + njin::vec2{0, -32}, 0.6f, njin::ease::out_cubic);
/// njin::tween_scale(ctx, coin, 1.4f, 0.1f, njin::ease::out_quad, {.repeat = 1, .yoyo = true});
/// @endcode
/// @param ctx Engine context.
/// @param entity Entity with a transform.
/// @param to Target position.
/// @param seconds Duration.
/// @param curve Curve.
/// @param desc Options.
/// @return Handle for cancelling with tween_cancel().
tween_handle tween_move(context &ctx, entt::entity entity, vec2 to, f32 seconds,
                        ease curve = ease::out_quad, const tween_desc &desc = {});

/// Changes the entity's `transform.scale` to `to`. See tween_move().
/// @param ctx Engine context. @param entity Entity with a transform.
/// @param to Target scale. @param seconds Duration. @param curve Curve.
/// @param desc Options. @return Handle.
tween_handle tween_scale(context &ctx, entt::entity entity, f32 to, f32 seconds,
                         ease curve = ease::out_quad, const tween_desc &desc = {});

/// Rotates the entity's `transform.rot` to `to` degrees. See tween_move().
/// @param ctx Engine context. @param entity Entity with a transform.
/// @param to Target angle, in degrees. @param seconds Duration. @param curve Curve.
/// @param desc Options. @return Handle.
tween_handle tween_rotate(context &ctx, entt::entity entity, f32 to, f32 seconds,
                          ease curve = ease::out_quad, const tween_desc &desc = {});

/// Changes the entity's `sprite.tint` to `to` (including opacity). See tween_move().
/// @param ctx Engine context. @param entity Entity with njin::sprite.
/// @param to Target color. @param seconds Duration. @param curve Curve.
/// @param desc Options. @return Handle.
tween_handle tween_tint(context &ctx, entt::entity entity, rgba to, f32 seconds,
                        ease curve = ease::linear, const tween_desc &desc = {});

/// Runs a value from `from` to `to` and passes it to `apply` every frame: a tween
/// for anything (volume, a slowly draining health bar, the radius of a blast ring).
/// @param ctx Engine context.
/// @param from Start value.
/// @param to End value.
/// @param seconds Duration.
/// @param apply Receives the value every frame, including the final value.
/// @param curve Curve.
/// @param desc Options.
/// @param owner Owning entity: when it is destroyed the tween stops. May be null.
/// @return Handle.
tween_handle tween_value(context &ctx, f32 from, f32 to, f32 seconds,
                         std::function<void(context &, f32)> apply, ease curve = ease::linear,
                         const tween_desc &desc = {}, entt::entity owner = entt::null);

/// Cancels a tween, keeping the current value. Does not call `done`.
/// @param ctx Engine context.
/// @param tween Tween.
void tween_cancel(context &ctx, tween_handle tween);

/// Cancels every tween running on an entity.
/// @param ctx Engine context.
/// @param entity Entity.
void tween_cancel_all(context &ctx, entt::entity entity);

/// Whether a tween is still running (including while waiting out `delay`).
/// @param ctx Engine context.
/// @param tween Tween.
/// @return `true` if it is still running.
bool tween_active(const context &ctx, tween_handle tween);
/// @}
} // namespace njin
