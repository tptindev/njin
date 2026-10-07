#pragma once
#include "_math.h"
#include "_types.h"
#include <vector>

namespace njin {
struct context;

/// @addtogroup grp_spline
/// @{

/// Kind of curve of a spline.
enum spline_kind {
  /// Catmull-Rom: the curve goes **through** every point. The easiest to place:
  /// dot the points of a rail, a flight path, a patrol route. With `alpha` = 0.5
  /// (centripetal) it makes no knots or loops when the points are unevenly spaced.
  spline_catmull_rom,
  /// Cubic Bezier: points 0, 3, 6... lie on the curve, the two points between each
  /// pair are handles pulling the curve (as a drawing program's pen tool). An open
  /// curve needs `3k + 1` points, a closed one `3k`; extra points are ignored.
  spline_bezier,
};

/// A 3D curve: a camera rail, a moving platform, a flight path, a patrol route.
///
/// After changing `points` (or `kind`, `closed`, `alpha`) call spline_bake() once:
/// it builds a length table for moving by distance (spline_point_at(),
/// spline_follow()) at an even speed. The functions by parameter `t`
/// (spline_point()) do not need the table.
struct spline3d {
  std::vector<vec3> points;              ///< Control points.
  spline_kind kind = spline_catmull_rom; ///< Kind of curve.
  bool closed = false;                   ///< Closed curve: the last point joins back to the first.
  /// Catmull-Rom only: 0 is uniform, 0.5 is centripetal (the default, no knots),
  /// 1 is chordal.
  f32 alpha = 0.5f;
  /// @name Length table (filled by spline_bake(), do not change)
  /// @{
  std::vector<f32> table; ///< Length from the start of the curve to each sample.
  i32 steps = 0;          ///< Samples per segment.
  /// @}
};

/// As njin::spline3d, on a plane: a 2D monster's route, a curving bullet.
struct spline2d {
  std::vector<vec2> points;              ///< Control points.
  spline_kind kind = spline_catmull_rom; ///< Kind of curve.
  bool closed = false;                   ///< Closed curve.
  f32 alpha = 0.5f;                      ///< As spline3d::alpha.
  /// @name Length table (filled by spline_bake(), do not change)
  /// @{
  std::vector<f32> table; ///< Length from the start of the curve to each sample.
  i32 steps = 0;          ///< Samples per segment.
  /// @}
};

/// Builds the length table after the points change. Without a table the
/// functions by distance build a temporary one at each call (correct but slow).
/// @param spline The curve.
/// @param steps Samples per segment, 2..1024: more is a more exact distance. 64 is
/// enough for an even speed within 1%.
void spline_bake(spline3d &spline, i32 steps = 64);
/// @copydoc spline_bake(spline3d&, i32)
void spline_bake(spline2d &spline, i32 steps = 64);

/// Number of segments: `t` of spline_point() runs from 0 to this.
/// @param spline The curve.
/// @return Number of segments, 0 without enough points (2 for Catmull-Rom, 4 for an open Bezier).
i32 spline_segment_count(const spline3d &spline);
/// @copydoc spline_segment_count(const spline3d&)
i32 spline_segment_count(const spline2d &spline);

/// The point on the curve at parameter `t`: the whole part is the segment, the
/// fraction the place in it. For Catmull-Rom, a whole `t` is exactly control
/// point `t`. Speed along `t` is not even (long segments go faster): to move by
/// distance use spline_point_at().
/// @param spline The curve.
/// @param t 0..spline_segment_count(); clamped outside (a closed curve wraps round).
/// @return The point, or the first point without enough points.
vec3 spline_point(const spline3d &spline, f32 t);
/// @copydoc spline_point(const spline3d&, f32)
vec2 spline_point(const spline2d &spline, f32 t);

/// Direction of the curve at parameter `t`, length 1.
/// @param spline The curve.
/// @param t As spline_point().
/// @return Direction, or 0 on a degenerate curve.
vec3 spline_tangent(const spline3d &spline, f32 t);
/// @copydoc spline_tangent(const spline3d&, f32)
vec2 spline_tangent(const spline2d &spline, f32 t);

/// Length of the whole curve.
/// @param spline The curve.
/// @return Length.
f32 spline_length(const spline3d &spline);
/// @copydoc spline_length(const spline3d&)
f32 spline_length(const spline2d &spline);

/// Parameter `t` at `distance` from the start of the curve.
/// @param spline The curve.
/// @param distance 0..spline_length(); clamped outside (a closed curve wraps round).
/// @return Parameter `t`.
f32 spline_t_at(const spline3d &spline, f32 distance);
/// @copydoc spline_t_at(const spline3d&, f32)
f32 spline_t_at(const spline2d &spline, f32 distance);

/// The point at `distance` from the start of the curve: moving evenly by distance.
/// @param spline The curve.
/// @param distance As spline_t_at().
/// @return The point.
vec3 spline_point_at(const spline3d &spline, f32 distance);
/// @copydoc spline_point_at(const spline3d&, f32)
vec2 spline_point_at(const spline2d &spline, f32 distance);

/// Direction at `distance`, length 1.
/// @param spline The curve.
/// @param distance As spline_t_at().
/// @return Direction.
vec3 spline_tangent_at(const spline3d &spline, f32 distance);
/// @copydoc spline_tangent_at(const spline3d&, f32)
vec2 spline_tangent_at(const spline2d &spline, f32 distance);

/// The place on the curve nearest `p`: to snap onto a rail, to know how far the
/// player has come along a race track.
/// @param spline The curve.
/// @param p Point.
/// @param out_point Receives the nearest point, or nullptr.
/// @return Distance along the curve to that point.
f32 spline_nearest(const spline3d &spline, vec3 p, vec3 *out_point = nullptr);
/// @copydoc spline_nearest(const spline3d&, vec3, vec3*)
f32 spline_nearest(const spline2d &spline, vec2 p, vec2 *out_point = nullptr);

/// What happens at the end of the curve.
enum spline_end {
  spline_stop,      ///< Stops at the end, `finished` becomes true.
  spline_loop,      ///< Back to the start (a closed curve goes on round again).
  spline_ping_pong, ///< Turns round and goes back, then turns again at the start.
};

/// State of something moving along a curve at an even speed: a camera on a
/// rail, a moving platform, a patrolling monster. Keep it in the game's
/// component and call spline_follow() every frame.
struct spline_follower {
  f32 distance = 0.0f;        ///< How far it has come from the start of the curve.
  f32 speed = 1.0f;           ///< Units per second. Negative goes backwards.
  spline_end end = spline_stop; ///< At the end of the curve.
  bool finished = false;      ///< Stopped at the end (spline_stop only).
};

/// Moves on `dt` seconds along the curve and returns the new place. The
/// direction there is `spline_tangent_at(spline, follower.distance)` (times -1
/// when going backwards).
/// @param spline The curve.
/// @param follower State, updated.
/// @param dt Time, seconds (usually delta()).
/// @return The new place.
vec3 spline_follow(const spline3d &spline, spline_follower &follower, f32 dt);
/// @copydoc spline_follow(const spline3d&, spline_follower&, f32)
vec2 spline_follow(const spline2d &spline, spline_follower &follower, f32 dt);

/// Draws the curve and its control points with gizmos (njin_gizmo.h) this frame.
/// @param ctx The engine context.
/// @param spline The curve.
/// @param color Colour.
void spline_draw_debug(context &ctx, const spline3d &spline, rgba color = {1.0f, 0.8f, 0.2f, 1.0f});
/// @copydoc spline_draw_debug(context&, const spline3d&, rgba)
void spline_draw_debug(context &ctx, const spline2d &spline, rgba color = {1.0f, 0.8f, 0.2f, 1.0f});
/// @}
} // namespace njin
