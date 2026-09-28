#pragma once
#include "_math.h"

namespace njin {
/// @addtogroup grp_tween
/// @{

/// Timer: signals when time is up.
///
/// Call tick() every frame with delta(). Use it for cooldowns, rhythmic
/// spawning, blinking.
struct timer {
  f32 duration = 1.0f; ///< Duration, in seconds.
  bool repeat = false; ///< Restarts automatically when time is up.
  f32 time = 0.0f;     ///< Time elapsed.
  bool finished = false; ///< Time is up (only used when not repeating).

  /// Lets time pass.
  /// @param dt Frame time, usually delta().
  /// @return `true` on the call where the time just reached `duration`. For a
  /// repeating timer, returns `true` each time a cycle ends.
  bool tick(f32 dt) {
    if (finished || duration <= 0.0f)
      return false;
    time += dt;
    if (time < duration)
      return false;
    if (repeat) {
      while (time >= duration)
        time -= duration;
    } else {
      time = duration;
      finished = true;
    }
    return true;
  }

  /// Progress from 0 to 1.
  /// @return `time / duration`, clamped to 0..1.
  f32 progress() const {
    return duration > 0.0f ? clamp(time / duration, 0.0f, 1.0f) : 1.0f;
  }

  /// Starts over from 0.
  void reset() {
    time = 0.0f;
    finished = false;
  }
};

/// Easing curve of a tween.
///
/// `in` is slow at the start, `out` is slow at the end, `in_out` is slow at both ends.
enum class ease {
  linear,       ///< Even.
  in_quad,      ///< Gentle acceleration.
  out_quad,     ///< Gentle deceleration.
  in_out_quad,  ///< Gentle acceleration, then deceleration.
  in_cubic,     ///< Strong acceleration.
  out_cubic,    ///< Strong deceleration.
  in_out_cubic, ///< Strong acceleration, then deceleration.
  in_sine,      ///< Sine-shaped acceleration.
  out_sine,     ///< Sine-shaped deceleration.
  in_out_sine,  ///< Sine-shaped acceleration, then deceleration.
  in_back,      ///< Pulls back a little before moving off.
  out_back,     ///< Overshoots the target a little, then comes back.
  out_bounce,   ///< Bounces at the end like a falling ball.
  out_elastic,  ///< Wobbles like a spring at the end.
};

/// Applies the curve to progress `t`.
/// @param curve The curve.
/// @param t Progress from 0 to 1.
/// @return Progress after applying the curve. `back` and `elastic` can go outside 0..1.
inline f32 ease_apply(ease curve, f32 t) {
  t = clamp(t, 0.0f, 1.0f);
  switch (curve) {
  case ease::linear:
    return t;
  case ease::in_quad:
    return t * t;
  case ease::out_quad:
    return t * (2.0f - t);
  case ease::in_out_quad:
    return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
  case ease::in_cubic:
    return t * t * t;
  case ease::out_cubic: {
    const f32 u = 1.0f - t;
    return 1.0f - u * u * u;
  }
  case ease::in_out_cubic: {
    const f32 u = 1.0f - t;
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - 4.0f * u * u * u;
  }
  case ease::in_sine:
    return 1.0f - std::cos(t * pi * 0.5f);
  case ease::out_sine:
    return std::sin(t * pi * 0.5f);
  case ease::in_out_sine:
    return 0.5f - 0.5f * std::cos(t * pi);
  case ease::in_back: {
    const f32 s = 1.70158f;
    return t * t * ((s + 1.0f) * t - s);
  }
  case ease::out_back: {
    const f32 s = 1.70158f;
    const f32 u = t - 1.0f;
    return 1.0f + u * u * ((s + 1.0f) * u + s);
  }
  case ease::out_bounce: {
    const f32 n = 7.5625f;
    const f32 d = 2.75f;
    if (t < 1.0f / d)
      return n * t * t;
    if (t < 2.0f / d) {
      t -= 1.5f / d;
      return n * t * t + 0.75f;
    }
    if (t < 2.5f / d) {
      t -= 2.25f / d;
      return n * t * t + 0.9375f;
    }
    t -= 2.625f / d;
    return n * t * t + 0.984375f;
  }
  case ease::out_elastic:
    if (t <= 0.0f || t >= 1.0f)
      return t;
    return std::pow(2.0f, -10.0f * t) *
               std::sin((t * 10.0f - 0.75f) * (2.0f * pi / 3.0f)) +
           1.0f;
  }
  return t;
}

/// Smoothly moves a value from `from` to `to` over `duration` seconds.
///
/// `T` is njin::f32, njin::vec2 or njin::rgba (any type that has lerp()).
/// Call tick() every frame and use the returned value.
template <class T> struct tween {
  T from{};                  ///< Starting value.
  T to{};                    ///< Ending value.
  f32 duration = 1.0f;       ///< Duration, in seconds.
  ease curve = ease::linear; ///< Easing curve.
  f32 time = 0.0f;           ///< Time elapsed.

  /// Current value.
  /// @return The value between `from` and `to` according to progress and curve.
  T value() const {
    const f32 t = duration > 0.0f ? time / duration : 1.0f;
    return lerp(from, to, ease_apply(curve, t));
  }

  /// Lets time pass, then returns the new value.
  /// @param dt Frame time, usually delta().
  /// @return The current value.
  T tick(f32 dt) {
    time = time + dt > duration ? duration : time + dt;
    return value();
  }

  /// Whether it has finished running.
  /// @return `true` if the time has reached `duration`.
  bool done() const { return time >= duration; }

  /// Starts over from `from`.
  void reset() { time = 0.0f; }
};
/// @}
} // namespace njin
