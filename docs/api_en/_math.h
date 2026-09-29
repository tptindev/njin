#pragma once
#include "_types.h"
#include <cmath>

namespace njin {
/// @addtogroup grp_math
/// @{

/// The number pi.
inline constexpr f32 pi = 3.14159265358979f;

/// Axis-aligned rectangle: `pos` is the top-left corner, `size` is the width and height.
struct rect {
  vec2 pos{};  ///< Top-left corner.
  vec2 size{}; ///< Width (`x`) and height (`y`).
};

/// Circle.
struct circle {
  vec2 center{};     ///< Center.
  f32 radius = 0.0f; ///< Radius.
};

// vec2 operators: + - * / component-wise, * / with a number, negation,
// += -= *= /=, == and !=. Described on the Math page of the wiki.
/// @cond VEC2_OPERATORS
constexpr vec2 operator+(vec2 a, vec2 b) { return {a.x + b.x, a.y + b.y}; }
constexpr vec2 operator-(vec2 a, vec2 b) { return {a.x - b.x, a.y - b.y}; }
constexpr vec2 operator*(vec2 a, vec2 b) { return {a.x * b.x, a.y * b.y}; }
constexpr vec2 operator/(vec2 a, vec2 b) { return {a.x / b.x, a.y / b.y}; }
constexpr vec2 operator*(vec2 a, f32 s) { return {a.x * s, a.y * s}; }
constexpr vec2 operator*(f32 s, vec2 a) { return {a.x * s, a.y * s}; }
constexpr vec2 operator/(vec2 a, f32 s) { return {a.x / s, a.y / s}; }
constexpr vec2 operator-(vec2 a) { return {-a.x, -a.y}; }
constexpr vec2 &operator+=(vec2 &a, vec2 b) { return a = a + b; }
constexpr vec2 &operator-=(vec2 &a, vec2 b) { return a = a - b; }
constexpr vec2 &operator*=(vec2 &a, f32 s) { return a = a * s; }
constexpr vec2 &operator/=(vec2 &a, f32 s) { return a = a / s; }
constexpr bool operator==(vec2 a, vec2 b) { return a.x == b.x && a.y == b.y; }
constexpr bool operator!=(vec2 a, vec2 b) { return !(a == b); }
/// @endcond

/// Dot product.
/// @param a First value.
/// @param b Second value.
/// @return `a.x * b.x + a.y * b.y`.
constexpr f32 dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }

/// 2D cross product (the z component of the 3D cross product).
/// @param a First value.
/// @param b Second value.
/// @return Positive if `b` is clockwise from `a` on the screen.
constexpr f32 cross(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }

/// Squared length. Faster than length() when you only need to compare.
/// @param v Vector.
/// @return Squared length of `v`.
constexpr f32 length_sq(vec2 v) { return dot(v, v); }

/// Length.
/// @param v Vector.
/// @return Length of `v`.
inline f32 length(vec2 v) { return std::sqrt(length_sq(v)); }

/// Distance between two points.
/// @param a First value.
/// @param b Second value.
/// @return Distance from `a` to `b`.
inline f32 distance(vec2 a, vec2 b) { return length(b - a); }

/// Vector with the same direction, length 1.
/// @param v Vector.
/// @return `v` normalized, or `{0, 0}` if `v` has length 0.
inline vec2 normalize(vec2 v) {
  const f32 len = length(v);
  return len > 0.0f ? v / len : vec2{0.0f, 0.0f};
}

/// Rotates a vector clockwise on the screen (y axis pointing down).
/// @param v Vector to rotate.
/// @param degrees Rotation angle, in degrees.
/// @return Rotated vector.
inline vec2 rotate(vec2 v, f32 degrees) {
  const f32 r = degrees * (pi / 180.0f);
  const f32 c = std::cos(r);
  const f32 s = std::sin(r);
  return {v.x * c - v.y * s, v.x * s + v.y * c};
}

/// Length-1 vector for an angle, 0 degrees points right, increasing clockwise on
/// the screen.
/// @param degrees Angle, in degrees.
/// @return Unit vector.
inline vec2 from_angle(f32 degrees) { return rotate({1.0f, 0.0f}, degrees); }

/// Angle of a vector, same convention as from_angle().
/// @param v Vector.
/// @return Angle in degrees, in the range -180 to 180.
inline f32 angle_of(vec2 v) { return std::atan2(v.y, v.x) * (180.0f / pi); }

/// @cond VEC3_OPERATORS
constexpr vec3 operator+(vec3 a, vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr vec3 operator-(vec3 a, vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr vec3 operator*(vec3 a, f32 s) { return {a.x * s, a.y * s, a.z * s}; }
constexpr vec3 operator*(f32 s, vec3 a) { return {a.x * s, a.y * s, a.z * s}; }
constexpr vec3 operator/(vec3 a, f32 s) { return {a.x / s, a.y / s, a.z / s}; }
constexpr vec3 operator-(vec3 a) { return {-a.x, -a.y, -a.z}; }
constexpr vec3 &operator+=(vec3 &a, vec3 b) { return a = a + b; }
constexpr vec3 &operator-=(vec3 &a, vec3 b) { return a = a - b; }
constexpr vec3 &operator*=(vec3 &a, f32 s) { return a = a * s; }
constexpr bool operator==(vec3 a, vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
constexpr bool operator!=(vec3 a, vec3 b) { return !(a == b); }
/// @endcond

/// Dot product.
/// @param a First value.
/// @param b Second value.
/// @return `a.x * b.x + a.y * b.y + a.z * b.z`.
constexpr f32 dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/// 3D cross product.
/// @param a First value.
/// @param b Second value.
/// @return A vector perpendicular to both `a` and `b` (right-hand rule).
constexpr vec3 cross(vec3 a, vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/// Squared length. Faster than length() when only comparing.
/// @param v Vector.
/// @return Squared length of `v`.
constexpr f32 length_sq(vec3 v) { return dot(v, v); }

/// Length.
/// @param v Vector.
/// @return Length of `v`.
inline f32 length(vec3 v) { return std::sqrt(length_sq(v)); }

/// Distance between two points.
/// @param a First point.
/// @param b Second point.
/// @return Distance from `a` to `b`.
inline f32 distance(vec3 a, vec3 b) { return length(b - a); }

/// Vector in the same direction, length 1.
/// @param v Vector.
/// @return `v` divided by its length, or `{0, 0, 0}` if `v` is zero.
inline vec3 normalize(vec3 v) {
  const f32 len = length(v);
  return len > 0.0f ? v / len : vec3{0.0f, 0.0f, 0.0f};
}

/// Linear interpolation between two vectors.
/// @param a Value at `t = 0`.
/// @param b Value at `t = 1`.
/// @param t Ratio, usually in 0..1.
/// @return `a + (b - a) * t`.
constexpr vec3 lerp(vec3 a, vec3 b, f32 t) { return a + (b - a) * t; }

/// Clamps `v` to the range `[lo, hi]`.
/// @param v Vector.
/// @param lo Lower bound.
/// @param hi Upper bound.
/// @return `lo` if `v < lo`, `hi` if `v > hi`, otherwise `v`.
constexpr f32 clamp(f32 v, f32 lo, f32 hi) { return v < lo ? lo : (v > hi ? hi : v); }

/// Clamps each component of `v` to the range `[lo, hi]`.
/// @param v Vector.
/// @param lo Lower bound.
/// @param hi Upper bound.
/// @return Clamped `v`.
constexpr vec2 clamp(vec2 v, vec2 lo, vec2 hi) {
  return {clamp(v.x, lo.x, hi.x), clamp(v.y, lo.y, hi.y)};
}

/// Linear interpolation: `t = 0` gives `a`, `t = 1` gives `b`.
/// @param a First value.
/// @param b Second value.
/// @param t Progress, from 0 to 1.
/// @return `a + (b - a) * t`.
constexpr f32 lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
/// Linear interpolation between two vectors.
/// @param a First value.
/// @param b Second value.
/// @param t Progress, from 0 to 1.
/// @return `a + (b - a) * t`.
constexpr vec2 lerp(vec2 a, vec2 b, f32 t) { return a + (b - a) * t; }
/// Linear interpolation between two colors, per channel.
/// @param a First value.
/// @param b Second value.
/// @param t Progress, from 0 to 1.
/// @return Color between `a` and `b`.
constexpr rgba lerp(rgba a, rgba b, f32 t) {
  return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t),
          lerp(a.a, b.a, t)};
}

/// Moves `from` toward `to` by at most `max_step`, without overshooting.
/// @param from Current value.
/// @param to Target value.
/// @param max_step Maximum step, non-negative.
/// @return New value.
constexpr f32 move_toward(f32 from, f32 to, f32 max_step) {
  return to > from ? (from + max_step > to ? to : from + max_step)
                   : (from - max_step < to ? to : from - max_step);
}

/// Moves the point `from` toward `to` by at most `max_step`, without overshooting.
/// @param from Current value.
/// @param to Target value.
/// @param max_step Maximum step, non-negative.
/// @return New point.
inline vec2 move_toward(vec2 from, vec2 to, f32 max_step) {
  const vec2 d = to - from;
  const f32 len = length(d);
  return len <= max_step || len == 0.0f ? to : from + d / len * max_step;
}

/// Center of a rectangle.
/// @param r Rectangle.
/// @return Midpoint of `r`.
constexpr vec2 rect_center(rect r) { return r.pos + r.size * 0.5f; }

/// Creates a rectangle from a center and a size.
/// @param center Center.
/// @param size Size.
/// @return Rectangle centered on `center`.
constexpr rect rect_from_center(vec2 center, vec2 size) {
  return {center - size * 0.5f, size};
}

/// Commonly used colors.
namespace colors {
inline constexpr rgba white{1.0f, 1.0f, 1.0f, 1.0f};  ///< White.
inline constexpr rgba black{0.0f, 0.0f, 0.0f, 1.0f};  ///< Black.
inline constexpr rgba red{0.9f, 0.16f, 0.22f, 1.0f};  ///< Red.
inline constexpr rgba green{0.0f, 0.89f, 0.19f, 1.0f}; ///< Green.
inline constexpr rgba blue{0.0f, 0.47f, 0.95f, 1.0f}; ///< Blue.
inline constexpr rgba yellow{0.99f, 0.98f, 0.0f, 1.0f}; ///< Yellow.
inline constexpr rgba gray{0.51f, 0.51f, 0.51f, 1.0f}; ///< Gray.
inline constexpr rgba transparent{0.0f, 0.0f, 0.0f, 0.0f}; ///< Transparent.
} // namespace colors
/// @}
} // namespace njin
