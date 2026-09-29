#pragma once
#include "_types.h"
#include <cmath>

namespace njin {
/// @addtogroup grp_math
/// @{

/// Số pi.
inline constexpr f32 pi = 3.14159265358979f;

/// Hình chữ nhật thẳng trục: `pos` là góc trên trái, `size` là rộng và cao.
struct rect {
  vec2 pos{};  ///< Góc trên trái.
  vec2 size{}; ///< Chiều rộng (`x`) và chiều cao (`y`).
};

/// Hình tròn.
struct circle {
  vec2 center{};     ///< Tâm.
  f32 radius = 0.0f; ///< Bán kính.
};

// Phép toán vec2: + - * / theo từng thành phần, * / với một số, đổi dấu,
// += -= *= /=, == và !=. Mô tả trong trang Toán học của wiki.
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

/// Tích vô hướng.
/// @param a Giá trị thứ nhất.
/// @param b Giá trị thứ hai.
/// @return `a.x * b.x + a.y * b.y`.
constexpr f32 dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }

/// Tích có hướng 2D (thành phần z của tích có hướng 3D).
/// @param a Giá trị thứ nhất.
/// @param b Giá trị thứ hai.
/// @return Dương nếu `b` nằm theo chiều kim đồng hồ so với `a` trên màn hình.
constexpr f32 cross(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }

/// Bình phương độ dài. Nhanh hơn length() khi chỉ cần so sánh.
/// @param v Vector.
/// @return Bình phương độ dài của `v`.
constexpr f32 length_sq(vec2 v) { return dot(v, v); }

/// Độ dài.
/// @param v Vector.
/// @return Độ dài của `v`.
inline f32 length(vec2 v) { return std::sqrt(length_sq(v)); }

/// Khoảng cách giữa hai điểm.
/// @param a Giá trị thứ nhất.
/// @param b Giá trị thứ hai.
/// @return Khoảng cách từ `a` đến `b`.
inline f32 distance(vec2 a, vec2 b) { return length(b - a); }

/// Vector cùng hướng, độ dài 1.
/// @param v Vector.
/// @return `v` đã chuẩn hóa, hoặc `{0, 0}` nếu `v` có độ dài 0.
inline vec2 normalize(vec2 v) {
  const f32 len = length(v);
  return len > 0.0f ? v / len : vec2{0.0f, 0.0f};
}

/// Xoay vector theo chiều kim đồng hồ trên màn hình (trục y hướng xuống).
/// @param v Vector cần xoay.
/// @param degrees Góc xoay, tính bằng độ.
/// @return Vector đã xoay.
inline vec2 rotate(vec2 v, f32 degrees) {
  const f32 r = degrees * (pi / 180.0f);
  const f32 c = std::cos(r);
  const f32 s = std::sin(r);
  return {v.x * c - v.y * s, v.x * s + v.y * c};
}

/// Vector độ dài 1 theo một góc, 0 độ là hướng sang phải, tăng theo chiều kim
/// đồng hồ trên màn hình.
/// @param degrees Góc, tính bằng độ.
/// @return Vector đơn vị.
inline vec2 from_angle(f32 degrees) { return rotate({1.0f, 0.0f}, degrees); }

/// Góc của vector, cùng quy ước với from_angle().
/// @param v Vector.
/// @return Góc tính bằng độ, trong khoảng -180 đến 180.
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

/// Tích vô hướng.
/// @param a Giá trị thứ nhất.
/// @param b Giá trị thứ hai.
/// @return `a.x * b.x + a.y * b.y + a.z * b.z`.
constexpr f32 dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/// Tích có hướng 3D.
/// @param a Giá trị thứ nhất.
/// @param b Giá trị thứ hai.
/// @return Vector vuông góc với cả `a` và `b` (quy tắc bàn tay phải).
constexpr vec3 cross(vec3 a, vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/// Bình phương độ dài. Nhanh hơn length() khi chỉ cần so sánh.
/// @param v Vector.
/// @return Bình phương độ dài của `v`.
constexpr f32 length_sq(vec3 v) { return dot(v, v); }

/// Độ dài.
/// @param v Vector.
/// @return Độ dài của `v`.
inline f32 length(vec3 v) { return std::sqrt(length_sq(v)); }

/// Khoảng cách giữa hai điểm.
/// @param a Điểm thứ nhất.
/// @param b Điểm thứ hai.
/// @return Khoảng cách từ `a` đến `b`.
inline f32 distance(vec3 a, vec3 b) { return length(b - a); }

/// Vector cùng hướng, độ dài 1.
/// @param v Vector.
/// @return `v` chia cho độ dài của nó, hoặc `{0, 0, 0}` nếu `v` bằng 0.
inline vec3 normalize(vec3 v) {
  const f32 len = length(v);
  return len > 0.0f ? v / len : vec3{0.0f, 0.0f, 0.0f};
}

/// Nội suy tuyến tính giữa hai vector.
/// @param a Giá trị khi `t = 0`.
/// @param b Giá trị khi `t = 1`.
/// @param t Tỉ lệ, thường trong 0..1.
/// @return `a + (b - a) * t`.
constexpr vec3 lerp(vec3 a, vec3 b, f32 t) { return a + (b - a) * t; }

/// Giới hạn `v` trong khoảng `[lo, hi]`.
/// @param v Vector.
/// @param lo Cận dưới.
/// @param hi Cận trên.
/// @return `lo` nếu `v < lo`, `hi` nếu `v > hi`, còn lại là `v`.
constexpr f32 clamp(f32 v, f32 lo, f32 hi) { return v < lo ? lo : (v > hi ? hi : v); }

/// Giới hạn từng thành phần của `v` trong khoảng `[lo, hi]`.
/// @param v Vector.
/// @param lo Cận dưới.
/// @param hi Cận trên.
/// @return `v` đã giới hạn.
constexpr vec2 clamp(vec2 v, vec2 lo, vec2 hi) {
  return {clamp(v.x, lo.x, hi.x), clamp(v.y, lo.y, hi.y)};
}

/// Nội suy tuyến tính: `t = 0` cho `a`, `t = 1` cho `b`.
/// @param a Giá trị thứ nhất.
/// @param b Giá trị thứ hai.
/// @param t Tiến độ, từ 0 đến 1.
/// @return `a + (b - a) * t`.
constexpr f32 lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }
/// Nội suy tuyến tính giữa hai vector.
/// @param a Giá trị thứ nhất.
/// @param b Giá trị thứ hai.
/// @param t Tiến độ, từ 0 đến 1.
/// @return `a + (b - a) * t`.
constexpr vec2 lerp(vec2 a, vec2 b, f32 t) { return a + (b - a) * t; }
/// Nội suy tuyến tính giữa hai màu, theo từng kênh.
/// @param a Giá trị thứ nhất.
/// @param b Giá trị thứ hai.
/// @param t Tiến độ, từ 0 đến 1.
/// @return Màu nằm giữa `a` và `b`.
constexpr rgba lerp(rgba a, rgba b, f32 t) {
  return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t),
          lerp(a.a, b.a, t)};
}

/// Di chuyển `from` về phía `to` một đoạn tối đa `max_step`, không vượt quá.
/// @param from Giá trị hiện tại.
/// @param to Giá trị đích.
/// @param max_step Bước tối đa, không âm.
/// @return Giá trị mới.
constexpr f32 move_toward(f32 from, f32 to, f32 max_step) {
  return to > from ? (from + max_step > to ? to : from + max_step)
                   : (from - max_step < to ? to : from - max_step);
}

/// Di chuyển điểm `from` về phía `to` một đoạn tối đa `max_step`, không vượt quá.
/// @param from Giá trị hiện tại.
/// @param to Giá trị đích.
/// @param max_step Bước tối đa, không âm.
/// @return Điểm mới.
inline vec2 move_toward(vec2 from, vec2 to, f32 max_step) {
  const vec2 d = to - from;
  const f32 len = length(d);
  return len <= max_step || len == 0.0f ? to : from + d / len * max_step;
}

/// Tâm của hình chữ nhật.
/// @param r Hình chữ nhật.
/// @return Điểm giữa của `r`.
constexpr vec2 rect_center(rect r) { return r.pos + r.size * 0.5f; }

/// Tạo hình chữ nhật từ tâm và kích thước.
/// @param center Tâm.
/// @param size Kích thước.
/// @return Hình chữ nhật có tâm `center`.
constexpr rect rect_from_center(vec2 center, vec2 size) {
  return {center - size * 0.5f, size};
}

/// Các màu hay dùng.
namespace colors {
inline constexpr rgba white{1.0f, 1.0f, 1.0f, 1.0f};  ///< Trắng.
inline constexpr rgba black{0.0f, 0.0f, 0.0f, 1.0f};  ///< Đen.
inline constexpr rgba red{0.9f, 0.16f, 0.22f, 1.0f};  ///< Đỏ.
inline constexpr rgba green{0.0f, 0.89f, 0.19f, 1.0f}; ///< Xanh lá.
inline constexpr rgba blue{0.0f, 0.47f, 0.95f, 1.0f}; ///< Xanh dương.
inline constexpr rgba yellow{0.99f, 0.98f, 0.0f, 1.0f}; ///< Vàng.
inline constexpr rgba gray{0.51f, 0.51f, 0.51f, 1.0f}; ///< Xám.
inline constexpr rgba transparent{0.0f, 0.0f, 0.0f, 0.0f}; ///< Trong suốt.
} // namespace colors
/// @}
} // namespace njin
