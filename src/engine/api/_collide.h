#pragma once
#include "_math.h"

namespace njin {
/// @addtogroup grp_collide
/// @{

/// Kết quả của một phép va chạm có đẩy ra.
///
/// Khi `hit` đúng, dời vật thứ nhất một đoạn `normal * depth` thì hai vật vừa
/// chạm mép, không còn chồng nhau.
struct contact {
  bool hit = false;  ///< Hai hình có chồng nhau không.
  vec2 normal{};     ///< Hướng đẩy vật thứ nhất ra, độ dài 1.
  f32 depth = 0.0f;  ///< Độ sâu chồng nhau theo hướng `normal`.
};

/// Điểm có nằm trong hình chữ nhật không. Điểm nằm trên cạnh trái/trên được
/// tính là trong, cạnh phải/dưới thì không.
/// @param p Điểm.
/// @param r Hình chữ nhật.
/// @return `true` nếu `p` nằm trong `r`.
constexpr bool point_in_rect(vec2 p, rect r) {
  return p.x >= r.pos.x && p.x < r.pos.x + r.size.x && p.y >= r.pos.y &&
         p.y < r.pos.y + r.size.y;
}

/// Điểm có nằm trong hình tròn không.
/// @param p Điểm.
/// @param c Hình tròn.
/// @return `true` nếu `p` cách tâm không quá bán kính.
constexpr bool point_in_circle(vec2 p, circle c) {
  return length_sq(p - c.center) <= c.radius * c.radius;
}

/// Hai hình chữ nhật có chồng nhau không. Chỉ chạm mép thì không tính.
/// @param a Giá trị thứ nhất.
/// @param b Giá trị thứ hai.
/// @return `true` nếu `a` và `b` chồng nhau.
constexpr bool rects_overlap(rect a, rect b) {
  return a.pos.x < b.pos.x + b.size.x && b.pos.x < a.pos.x + a.size.x &&
         a.pos.y < b.pos.y + b.size.y && b.pos.y < a.pos.y + a.size.y;
}

/// Hai hình tròn có chồng nhau không. Chỉ chạm mép thì không tính.
/// @param a Giá trị thứ nhất.
/// @param b Giá trị thứ hai.
/// @return `true` nếu `a` và `b` chồng nhau.
constexpr bool circles_overlap(circle a, circle b) {
  const f32 r = a.radius + b.radius;
  return length_sq(b.center - a.center) < r * r;
}

/// Điểm trong `r` gần `p` nhất.
/// @param r Hình chữ nhật.
/// @param p Điểm.
/// @return Điểm trong hoặc trên cạnh `r` gần `p` nhất.
constexpr vec2 closest_point(rect r, vec2 p) {
  return clamp(p, r.pos, r.pos + r.size);
}

/// Hình tròn và hình chữ nhật có chồng nhau không.
/// @param c Hình tròn.
/// @param r Hình chữ nhật.
/// @return `true` nếu `c` và `r` chồng nhau.
constexpr bool circle_rect_overlap(circle c, rect r) {
  return length_sq(c.center - closest_point(r, c.center)) < c.radius * c.radius;
}

/// Phần giao của hai hình chữ nhật.
/// @param a Giá trị thứ nhất.
/// @param b Giá trị thứ hai.
/// @return Phần giao, hoặc hình chữ nhật kích thước 0 nếu không chồng nhau.
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

/// Va chạm hai hình chữ nhật, đẩy `a` ra theo trục chồng ít nhất.
/// @param a Giá trị thứ nhất.
/// @param b Giá trị thứ hai.
/// @return Thông tin va chạm; `normal` là hướng đẩy `a` ra khỏi `b`.
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

/// Va chạm hai hình tròn.
/// @param a Giá trị thứ nhất.
/// @param b Giá trị thứ hai.
/// @return Thông tin va chạm; `normal` là hướng đẩy `a` ra khỏi `b`.
inline contact collide_circles(circle a, circle b) {
  const vec2 d = a.center - b.center;
  const f32 r = a.radius + b.radius;
  const f32 dist_sq = length_sq(d);
  if (dist_sq >= r * r)
    return {};
  const f32 dist = std::sqrt(dist_sq);
  // Trùng tâm: không có hướng nào tự nhiên, chọn lên trên.
  const vec2 n = dist > 0.0f ? d / dist : vec2{0.0f, -1.0f};
  return {true, n, r - dist};
}

/// Va chạm hình tròn với hình chữ nhật.
/// @param c Hình tròn.
/// @param r Hình chữ nhật.
/// @return Thông tin va chạm; `normal` là hướng đẩy hình tròn ra khỏi `r`.
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
  // Tâm nằm trong hình chữ nhật: đẩy ra theo cạnh gần nhất.
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

/// Phản xạ vận tốc qua một pháp tuyến, như bóng nảy khỏi tường.
/// @param velocity Vận tốc tới.
/// @param normal Pháp tuyến của mặt, độ dài 1.
/// @return Vận tốc sau khi nảy.
constexpr vec2 reflect(vec2 velocity, vec2 normal) {
  return velocity - normal * (2.0f * dot(velocity, normal));
}
/// @}
} // namespace njin
