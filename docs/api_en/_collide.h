#pragma once
#include "_math.h"

namespace njin {
/// @addtogroup grp_collide
/// @{

/// Result of a collision with push-out.
///
/// When `hit` is true, moving the first object by `normal * depth` makes the two
/// objects just touch at their edges, no longer overlapping.
struct contact {
  bool hit = false;  ///< Whether the two shapes overlap.
  vec2 normal{};     ///< Direction to push the first object out, length 1.
  f32 depth = 0.0f;  ///< Depth of overlap along the `normal` direction.
};

/// Whether a point is inside a rectangle. A point on the left/top edge counts
/// as inside, on the right/bottom edge it does not.
/// @param p The point.
/// @param r The rectangle.
/// @return `true` if `p` is inside `r`.
constexpr bool point_in_rect(vec2 p, rect r) {
  return p.x >= r.pos.x && p.x < r.pos.x + r.size.x && p.y >= r.pos.y &&
         p.y < r.pos.y + r.size.y;
}

/// Whether a point is inside a circle.
/// @param p The point.
/// @param c The circle.
/// @return `true` if `p` is no farther than the radius from the center.
constexpr bool point_in_circle(vec2 p, circle c) {
  return length_sq(p - c.center) <= c.radius * c.radius;
}

/// Whether two rectangles overlap. Merely touching at an edge does not count.
/// @param a The first value.
/// @param b The second value.
/// @return `true` if `a` and `b` overlap.
constexpr bool rects_overlap(rect a, rect b) {
  return a.pos.x < b.pos.x + b.size.x && b.pos.x < a.pos.x + a.size.x &&
         a.pos.y < b.pos.y + b.size.y && b.pos.y < a.pos.y + a.size.y;
}

/// Whether two circles overlap. Merely touching at an edge does not count.
/// @param a The first value.
/// @param b The second value.
/// @return `true` if `a` and `b` overlap.
constexpr bool circles_overlap(circle a, circle b) {
  const f32 r = a.radius + b.radius;
  return length_sq(b.center - a.center) < r * r;
}

/// The point in `r` closest to `p`.
/// @param r The rectangle.
/// @param p The point.
/// @return The point inside or on the edge of `r` closest to `p`.
constexpr vec2 closest_point(rect r, vec2 p) {
  return clamp(p, r.pos, r.pos + r.size);
}

/// Whether a circle and a rectangle overlap.
/// @param c The circle.
/// @param r The rectangle.
/// @return `true` if `c` and `r` overlap.
constexpr bool circle_rect_overlap(circle c, rect r) {
  return length_sq(c.center - closest_point(r, c.center)) < c.radius * c.radius;
}

/// The intersection of two rectangles.
/// @param a The first value.
/// @param b The second value.
/// @return The intersection, or a zero-size rectangle if they do not overlap.
constexpr rect rect_intersection(rect a, rect b) {
  const f32 x0 = a.pos.x > b.pos.x ? a.pos.x : b.pos.x;
  const f32 y0 = a.pos.y > b.pos.y ? a.pos.y : b.pos.y;
  const f32 x1 = a.pos.x + a.size.x < b.pos.x + b.size.x ? a.pos.x + a.size.x
                                                          : b.pos.x + b.size.x;
  const f32 y1 = a.pos.y + a.size.y < b.pos.y + b.size.y ? a.pos.y + a.size.y
                                                          : b.pos.y + b.size.y;
  if (x1 <= x0 || y1 <= y0)
    return rect{{x0, y0}, {0.0f, 0.0f}};
  return rect{{x0, y0}, {x1 - x0, y1 - y0}};
}

/// Collides two rectangles, pushing `a` out along the axis of least overlap.
/// @param a The first value.
/// @param b The second value.
/// @return Collision info; `normal` is the direction to push `a` out of `b`.
constexpr contact collide_rects(rect a, rect b) {
  if (!rects_overlap(a, b))
    return {};
  const vec2 ca = rect_center(a);
  const vec2 cb = rect_center(b);
  const f32 ox = (a.size.x + b.size.x) * 0.5f - (ca.x > cb.x ? ca.x - cb.x : cb.x - ca.x);
  const f32 oy = (a.size.y + b.size.y) * 0.5f - (ca.y > cb.y ? ca.y - cb.y : cb.y - ca.y);
  if (ox < oy)
    return {true, {ca.x < cb.x ? -1.0f : 1.0f, 0.0f}, ox};
  return {true, {0.0f, ca.y < cb.y ? -1.0f : 1.0f}, oy};
}

/// Collides two circles.
/// @param a The first value.
/// @param b The second value.
/// @return Collision info; `normal` is the direction to push `a` out of `b`.
inline contact collide_circles(circle a, circle b) {
  const vec2 d = a.center - b.center;
  const f32 r = a.radius + b.radius;
  const f32 dist_sq = length_sq(d);
  if (dist_sq >= r * r)
    return {};
  const f32 dist = std::sqrt(dist_sq);
  // Coincident centers: no direction is natural, pick up.
  const vec2 n = dist > 0.0f ? d / dist : vec2{0.0f, -1.0f};
  return {true, n, r - dist};
}

/// Collides a circle with a rectangle.
/// @param c The circle.
/// @param r The rectangle.
/// @return Collision info; `normal` is the direction to push the circle out of `r`.
inline contact collide_circle_rect(circle c, rect r) {
  const vec2 p = closest_point(r, c.center);
  const vec2 d = c.center - p;
  const f32 dist_sq = length_sq(d);
  if (dist_sq >= c.radius * c.radius)
    return {};
  if (dist_sq > 0.0f) {
    const f32 dist = std::sqrt(dist_sq);
    return {true, d / dist, c.radius - dist};
  }
  // Center is inside the rectangle: push out along the nearest edge.
  const f32 left = c.center.x - r.pos.x;
  const f32 right = r.pos.x + r.size.x - c.center.x;
  const f32 top = c.center.y - r.pos.y;
  const f32 bottom = r.pos.y + r.size.y - c.center.y;
  f32 best = left;
  vec2 n{-1.0f, 0.0f};
  if (right < best) {
    best = right;
    n = {1.0f, 0.0f};
  }
  if (top < best) {
    best = top;
    n = {0.0f, -1.0f};
  }
  if (bottom < best) {
    best = bottom;
    n = {0.0f, 1.0f};
  }
  return {true, n, best + c.radius};
}

/// Reflects a velocity about a normal, like a ball bouncing off a wall.
/// @param velocity Incoming velocity.
/// @param normal Surface normal, length 1.
/// @return Velocity after the bounce.
constexpr vec2 reflect(vec2 velocity, vec2 normal) {
  return velocity - normal * (2.0f * dot(velocity, normal));
}
/// @}
} // namespace njin
