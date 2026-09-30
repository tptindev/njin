#pragma once

// Plane geometry the city is built from: segments, boxes, polylines.

#include "city.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sandtable::city {

// --- Geometry -----------------------------------------------------------------

inline vec2 perp(vec2 v) { return {-v.y, v.x}; }

// Distance from `p` to segment ab, and the parameter of the closest point.
inline f32 seg_dist(vec2 p, vec2 a, vec2 b, f32 *t_out = nullptr) {
  const vec2 ab = b - a;
  const f32 l2 = length_sq(ab);
  const f32 t = l2 > 1e-9f ? clamp(dot(p - a, ab) / l2, 0.0f, 1.0f) : 0.0f;
  if (t_out)
    *t_out = l2 > 1e-9f ? dot(p - a, ab) / l2 : 0.0f;
  return length(p - (a + ab * t));
}

inline bool seg_cross(vec2 a, vec2 b, vec2 c, vec2 d, f32 &t, f32 &u) {
  const vec2 r = b - a, s = d - c;
  const f32 den = cross(r, s);
  if (std::fabs(den) < 1e-6f)
    return false;
  t = cross(c - a, s) / den;
  u = cross(c - a, r) / den;
  constexpr f32 eps = 1e-4f;
  return t >= -eps && t <= 1.0f + eps && u >= -eps && u <= 1.0f + eps;
}

// Separating axes: true when the two boxes overlap by more than `slack`
// (walls that touch, as tube houses do, are not an overlap).
inline bool obb_overlap(const obb &a, const obb &b, f32 slack = 0.5f) {
  const vec2 axes[4] = {a.axis_x(), a.axis_y(), b.axis_x(), b.axis_y()};
  const vec2 d = b.center - a.center;
  for (const vec2 n : axes) {
    const f32 ra = a.half.x * std::fabs(dot(a.axis_x(), n)) + a.half.y * std::fabs(dot(a.axis_y(), n));
    const f32 rb = b.half.x * std::fabs(dot(b.axis_x(), n)) + b.half.y * std::fabs(dot(b.axis_y(), n));
    if (std::fabs(dot(d, n)) >= ra + rb - slack)
      return false;
  }
  return true;
}

// Walking along a polyline by distance.
struct polyline_walk {
  const std::vector<vec2> *pts = nullptr;
  std::vector<f32> acc;

  explicit polyline_walk(const std::vector<vec2> &p) : pts(&p) {
    acc.resize(p.size());
    if (p.empty())
      return;
    acc[0] = 0.0f;
    for (size_t i = 1; i < p.size(); ++i)
      acc[i] = acc[i - 1] + distance(p[i - 1], p[i]);
  }
  f32 length() const { return acc.empty() ? 0.0f : acc.back(); }
  // The point `s` along, and the direction there.
  void at(f32 s, vec2 &p, vec2 &t) const {
    const std::vector<vec2> &v = *pts;
    s = clamp(s, 0.0f, length());
    size_t i = static_cast<size_t>(std::upper_bound(acc.begin(), acc.end(), s) - acc.begin());
    i = i == 0 ? 0 : i - 1;
    if (i >= v.size() - 1)
      i = v.size() - 2;
    const f32 seg = acc[i + 1] - acc[i];
    const f32 k = seg > 1e-6f ? (s - acc[i]) / seg : 0.0f;
    p = lerp(v[i], v[i + 1], k);
    t = normalize(v[i + 1] - v[i]);
  }
  // The part from s0 to s1.
  std::vector<vec2> slice(f32 s0, f32 s1) const {
    std::vector<vec2> out;
    vec2 p, t;
    at(s0, p, t);
    out.push_back(p);
    for (size_t i = 0; i < acc.size(); ++i)
      if (acc[i] > s0 + 0.01f && acc[i] < s1 - 0.01f)
        out.push_back((*pts)[i]);
    at(s1, p, t);
    out.push_back(p);
    return out;
  }
};

// The closest point to `p` on a polyline, and its distance along it.
inline f32 closest_on(const std::vector<vec2> &pts, vec2 p, vec2 &q, f32 *s_out = nullptr) {
  f32 best = 1e30f, acc = 0.0f, best_s = 0.0f;
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    f32 t;
    seg_dist(p, pts[i], pts[i + 1], &t);
    t = clamp(t, 0.0f, 1.0f);
    const vec2 c = lerp(pts[i], pts[i + 1], t);
    const f32 d = distance(p, c);
    const f32 seg = distance(pts[i], pts[i + 1]);
    if (d < best) {
      best = d;
      q = c;
      best_s = acc + seg * t;
    }
    acc += seg;
  }
  if (s_out)
    *s_out = best_s;
  return best;
}

inline f32 wrap_deg(f32 a) {
  a = std::fmod(a, 360.0f);
  if (a > 180.0f)
    a -= 360.0f;
  if (a < -180.0f)
    a += 360.0f;
  return a;
}

} // namespace sandtable::city
