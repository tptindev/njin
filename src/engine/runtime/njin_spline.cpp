#include "njin_spline.h"
#include "njin_gizmo.h"
#include <algorithm>
#include <cmath>

// Catmull-Rom (Barry-Goldman form with a knot exponent, centripetal at 0.5)
// and cubic Bezier, in 2D and 3D, with an arc-length table for moving at an
// even speed. Small enough that no library was worth pulling in.
namespace njin {
namespace {
template <class V> struct curve {
  const std::vector<V> &p;
  spline_kind kind;
  bool closed;
  f32 alpha;
};

template <class V> i32 segments(const curve<V> &c) {
  const i32 n = (i32)c.p.size();
  if (c.kind == spline_catmull_rom)
    return n < 2 ? 0 : (c.closed ? n : n - 1);
  if (c.closed)
    return n < 3 ? 0 : n / 3;
  return n < 4 ? 0 : (n - 1) / 3;
}

template <class V> V cr_point(const curve<V> &c, i32 i) {
  const i32 n = (i32)c.p.size();
  if (c.closed)
    return c.p[(usize)(((i % n) + n) % n)];
  if (i < 0)
    return c.p[0] * 2.0f - c.p[1];
  if (i >= n)
    return c.p[(usize)(n - 1)] * 2.0f - c.p[(usize)(n - 2)];
  return c.p[(usize)i];
}

template <class V> f32 knot(V a, V b, f32 alpha) {
  const f32 d = std::pow(length_sq(b - a), alpha * 0.5f);
  return d < 1e-5f ? 1e-5f : d;
}

template <class V> V segment_point(const curve<V> &c, i32 seg, f32 u) {
  if (c.kind == spline_bezier) {
    const i32 n = (i32)c.p.size();
    const V p0 = c.p[(usize)(seg * 3)], p1 = c.p[(usize)(seg * 3 + 1)], p2 = c.p[(usize)(seg * 3 + 2)];
    const V p3 = c.p[(usize)((seg * 3 + 3) % (c.closed ? (n / 3) * 3 : n + 1))];
    const f32 v = 1.0f - u;
    return p0 * (v * v * v) + p1 * (3.0f * v * v * u) + p2 * (3.0f * v * u * u) + p3 * (u * u * u);
  }
  const V p0 = cr_point(c, seg - 1), p1 = cr_point(c, seg), p2 = cr_point(c, seg + 1), p3 = cr_point(c, seg + 2);
  const f32 t0 = 0.0f;
  const f32 t1 = t0 + knot(p0, p1, c.alpha);
  const f32 t2 = t1 + knot(p1, p2, c.alpha);
  const f32 t3 = t2 + knot(p2, p3, c.alpha);
  const f32 t = t1 + (t2 - t1) * u;
  const V a1 = p0 * ((t1 - t) / (t1 - t0)) + p1 * ((t - t0) / (t1 - t0));
  const V a2 = p1 * ((t2 - t) / (t2 - t1)) + p2 * ((t - t1) / (t2 - t1));
  const V a3 = p2 * ((t3 - t) / (t3 - t2)) + p3 * ((t - t2) / (t3 - t2));
  const V b1 = a1 * ((t2 - t) / (t2 - t0)) + a2 * ((t - t0) / (t2 - t0));
  const V b2 = a2 * ((t3 - t) / (t3 - t1)) + a3 * ((t - t1) / (t3 - t1));
  return b1 * ((t2 - t) / (t2 - t1)) + b2 * ((t - t1) / (t2 - t1));
}

template <class V> f32 wrap_t(const curve<V> &c, f32 t) {
  const f32 s = (f32)segments(c);
  if (!std::isfinite(t))
    return 0.0f;
  if (c.closed && s > 0.0f) {
    t = std::fmod(t, s);
    return t < 0.0f ? t + s : t;
  }
  return clamp(t, 0.0f, s);
}

template <class V> V point(const curve<V> &c, f32 t) {
  const i32 s = segments(c);
  if (s == 0)
    return c.p.empty() ? V{} : c.p[0];
  t = wrap_t(c, t);
  const i32 seg = std::min((i32)t, s - 1);
  return segment_point(c, seg, t - (f32)seg);
}

template <class V> V tangent(const curve<V> &c, f32 t) {
  const i32 s = segments(c);
  if (s == 0)
    return V{};
  constexpr f32 h = 1e-3f;
  f32 a = t - h, b = t + h;
  if (!c.closed) {
    a = clamp(a, 0.0f, (f32)s);
    b = clamp(b, 0.0f, (f32)s);
  }
  const V d = point(c, b) - point(c, a);
  return length_sq(d) > 1e-20f ? normalize(d) : V{};
}

template <class V> std::vector<f32> make_table(const curve<V> &c, i32 steps) {
  const i32 s = segments(c);
  std::vector<f32> table;
  if (s == 0)
    return table;
  table.resize((usize)(s * steps + 1));
  table[0] = 0.0f;
  V prev = segment_point(c, 0, 0.0f);
  for (i32 i = 1; i <= s * steps; i++) {
    const i32 seg = std::min((i - 1) / steps, s - 1);
    const f32 u = (f32)(i - seg * steps) / (f32)steps;
    const V q = segment_point(c, seg, u);
    table[(usize)i] = table[(usize)(i - 1)] + length(q - prev);
    prev = q;
  }
  return table;
}

// The table to use: the baked one, or a temporary one when the spline was not
// baked (or was changed since).
template <class S, class V> struct table_ref {
  std::vector<f32> local;
  const std::vector<f32> *table = nullptr;
  i32 steps = 64;
  table_ref(const S &s, const curve<V> &c) {
    const i32 segs = segments(c);
    if (s.steps > 0 && segs > 0 && s.table.size() == (usize)(segs * s.steps + 1)) {
      table = &s.table;
      steps = s.steps;
    } else {
      local = make_table(c, steps);
      table = &local;
    }
  }
  f32 total() const { return table->empty() ? 0.0f : table->back(); }
};

template <class V> f32 t_at(const curve<V> &c, const std::vector<f32> &table, i32 steps, f32 distance) {
  if (table.empty())
    return 0.0f;
  const f32 total = table.back();
  if (!std::isfinite(distance))
    distance = 0.0f;
  if (c.closed && total > 0.0f) {
    distance = std::fmod(distance, total);
    if (distance < 0.0f)
      distance += total;
  } else {
    distance = clamp(distance, 0.0f, total);
  }
  const auto it = std::upper_bound(table.begin(), table.end(), distance);
  const usize hi = std::min((usize)(it - table.begin()), table.size() - 1);
  const usize lo = hi == 0 ? 0 : hi - 1;
  const f32 span = table[hi] - table[lo];
  const f32 f = span > 1e-9f ? (distance - table[lo]) / span : 0.0f;
  return ((f32)lo + f) / (f32)steps;
}

template <class S, class V> curve<V> curve_of(const S &s) { return curve<V>{s.points, s.kind, s.closed, s.alpha}; }

template <class S, class V> f32 nearest(const S &s, V p, V *out) {
  const curve<V> c = curve_of<S, V>(s);
  const table_ref<S, V> tr(s, c);
  const i32 segs = segments(c);
  if (segs == 0) {
    if (out != nullptr)
      *out = s.points.empty() ? V{} : s.points[0];
    return 0.0f;
  }
  // Coarse: the closest table sample; then refine between its neighbours.
  const i32 samples = segs * tr.steps;
  i32 best = 0;
  f32 best_d = 1e30f;
  for (i32 i = 0; i <= samples; i++) {
    const f32 d = length_sq(point(c, (f32)i / (f32)tr.steps) - p);
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  f32 lo = (f32)std::max(best - 1, 0) / (f32)tr.steps;
  f32 hi = (f32)std::min(best + 1, samples) / (f32)tr.steps;
  for (i32 k = 0; k < 40; k++) {
    const f32 m1 = lo + (hi - lo) / 3.0f, m2 = hi - (hi - lo) / 3.0f;
    if (length_sq(point(c, m1) - p) < length_sq(point(c, m2) - p))
      hi = m2;
    else
      lo = m1;
  }
  const f32 t = (lo + hi) * 0.5f;
  if (out != nullptr)
    *out = point(c, t);
  // Distance along: the table at t.
  const std::vector<f32> &table = *tr.table;
  const f32 fi = t * (f32)tr.steps;
  const usize i0 = std::min((usize)fi, table.size() - 1);
  const usize i1 = std::min(i0 + 1, table.size() - 1);
  return table[i0] + (table[i1] - table[i0]) * (fi - (f32)i0);
}

template <class S, class V> V follow(const S &s, spline_follower &f, f32 dt) {
  const curve<V> c = curve_of<S, V>(s);
  const table_ref<S, V> tr(s, c);
  const f32 total = tr.total();
  if (!std::isfinite(dt))
    dt = 0.0f;
  if (total <= 0.0f)
    return point(c, 0.0f);
  if (!f.finished)
    f.distance += f.speed * dt;
  switch (f.end) {
  case spline_stop:
    if (f.distance >= total || f.distance <= 0.0f) {
      const bool past = (f.speed > 0.0f && f.distance >= total) || (f.speed < 0.0f && f.distance <= 0.0f);
      f.distance = clamp(f.distance, 0.0f, total);
      if (past)
        f.finished = true;
    }
    break;
  case spline_loop:
    f.distance = std::fmod(f.distance, total);
    if (f.distance < 0.0f)
      f.distance += total;
    break;
  case spline_ping_pong:
    for (i32 k = 0; k < 4 && (f.distance > total || f.distance < 0.0f); k++) {
      f.distance = f.distance > total ? 2.0f * total - f.distance : -f.distance;
      f.speed = -f.speed;
    }
    f.distance = clamp(f.distance, 0.0f, total);
    break;
  }
  return point(c, t_at(c, *tr.table, tr.steps, f.distance));
}

template <class S, class V> void bake(S &s, i32 steps) {
  steps = std::clamp(steps, 2, 1024);
  const curve<V> c = curve_of<S, V>(s);
  s.table = make_table(c, steps);
  s.steps = s.table.empty() ? 0 : steps;
}

} // namespace

void spline_bake(spline3d &spline, i32 steps) { bake<spline3d, vec3>(spline, steps); }
void spline_bake(spline2d &spline, i32 steps) { bake<spline2d, vec2>(spline, steps); }

i32 spline_segment_count(const spline3d &spline) { return segments(curve_of<spline3d, vec3>(spline)); }
i32 spline_segment_count(const spline2d &spline) { return segments(curve_of<spline2d, vec2>(spline)); }

vec3 spline_point(const spline3d &spline, f32 t) { return point(curve_of<spline3d, vec3>(spline), t); }
vec2 spline_point(const spline2d &spline, f32 t) { return point(curve_of<spline2d, vec2>(spline), t); }

vec3 spline_tangent(const spline3d &spline, f32 t) { return tangent(curve_of<spline3d, vec3>(spline), t); }
vec2 spline_tangent(const spline2d &spline, f32 t) { return tangent(curve_of<spline2d, vec2>(spline), t); }

f32 spline_length(const spline3d &spline) {
  const curve<vec3> c = curve_of<spline3d, vec3>(spline);
  return table_ref<spline3d, vec3>(spline, c).total();
}
f32 spline_length(const spline2d &spline) {
  const curve<vec2> c = curve_of<spline2d, vec2>(spline);
  return table_ref<spline2d, vec2>(spline, c).total();
}

f32 spline_t_at(const spline3d &spline, f32 distance) {
  const curve<vec3> c = curve_of<spline3d, vec3>(spline);
  const table_ref<spline3d, vec3> tr(spline, c);
  return t_at(c, *tr.table, tr.steps, distance);
}
f32 spline_t_at(const spline2d &spline, f32 distance) {
  const curve<vec2> c = curve_of<spline2d, vec2>(spline);
  const table_ref<spline2d, vec2> tr(spline, c);
  return t_at(c, *tr.table, tr.steps, distance);
}

vec3 spline_point_at(const spline3d &spline, f32 distance) { return spline_point(spline, spline_t_at(spline, distance)); }
vec2 spline_point_at(const spline2d &spline, f32 distance) { return spline_point(spline, spline_t_at(spline, distance)); }

vec3 spline_tangent_at(const spline3d &spline, f32 distance) {
  return spline_tangent(spline, spline_t_at(spline, distance));
}
vec2 spline_tangent_at(const spline2d &spline, f32 distance) {
  return spline_tangent(spline, spline_t_at(spline, distance));
}

f32 spline_nearest(const spline3d &spline, vec3 p, vec3 *out_point) { return nearest<spline3d, vec3>(spline, p, out_point); }
f32 spline_nearest(const spline2d &spline, vec2 p, vec2 *out_point) { return nearest<spline2d, vec2>(spline, p, out_point); }

vec3 spline_follow(const spline3d &spline, spline_follower &follower, f32 dt) {
  return follow<spline3d, vec3>(spline, follower, dt);
}
vec2 spline_follow(const spline2d &spline, spline_follower &follower, f32 dt) {
  return follow<spline2d, vec2>(spline, follower, dt);
}

void spline_draw_debug(context &ctx, const spline3d &spline, rgba color) {
  const i32 segs = spline_segment_count(spline);
  constexpr i32 per = 16;
  vec3 prev = spline_point(spline, 0.0f);
  for (i32 i = 1; i <= segs * per; i++) {
    const vec3 q = spline_point(spline, (f32)i / (f32)per);
    gizmo_line3d(ctx, prev, q, color);
    prev = q;
  }
  for (const vec3 &p : spline.points)
    gizmo_point3d(ctx, p, color);
}

void spline_draw_debug(context &ctx, const spline2d &spline, rgba color) {
  const i32 segs = spline_segment_count(spline);
  constexpr i32 per = 16;
  vec2 prev = spline_point(spline, 0.0f);
  for (i32 i = 1; i <= segs * per; i++) {
    const vec2 q = spline_point(spline, (f32)i / (f32)per);
    gizmo_line(ctx, prev, q, color);
    prev = q;
  }
  for (const vec2 &p : spline.points)
    gizmo_point(ctx, p, color);
}
} // namespace njin
