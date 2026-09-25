#pragma once
#include "_math.h"

namespace njin {
/// @addtogroup grp_tween
/// @{

/// Bộ đếm thời gian: báo khi hết giờ.
///
/// Gọi tick() mỗi frame với delta(). Dùng cho hồi chiêu, sinh quái theo nhịp,
/// nhấp nháy.
struct timer {
  f32 duration = 1.0f; ///< Thời lượng, tính bằng giây.
  bool repeat = false; ///< Hết giờ thì tự bắt đầu lại.
  f32 time = 0.0f;     ///< Thời gian đã trôi qua.
  bool finished = false; ///< Đã hết giờ (chỉ dùng khi không lặp).

  /// Cho thời gian trôi.
  /// @param dt Thời gian của frame, thường là delta().
  /// @return `true` ở lần gọi mà thời gian vừa chạm `duration`. Với timer lặp,
  /// trả về `true` mỗi lần hết một vòng.
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

  /// Tiến độ từ 0 đến 1.
  /// @return `time / duration`, giới hạn trong 0..1.
  f32 progress() const {
    return duration > 0.0f ? clamp(time / duration, 0.0f, 1.0f) : 1.0f;
  }

  /// Bắt đầu lại từ 0.
  void reset() {
    time = 0.0f;
    finished = false;
  }
};

/// Đường cong chuyển động của tween.
///
/// `in` là chậm lúc đầu, `out` là chậm lúc cuối, `in_out` là chậm cả hai đầu.
enum class ease {
  linear,       ///< Đều.
  in_quad,      ///< Tăng tốc nhẹ.
  out_quad,     ///< Giảm tốc nhẹ.
  in_out_quad,  ///< Tăng rồi giảm tốc nhẹ.
  in_cubic,     ///< Tăng tốc mạnh.
  out_cubic,    ///< Giảm tốc mạnh.
  in_out_cubic, ///< Tăng rồi giảm tốc mạnh.
  in_sine,      ///< Tăng tốc theo hình sin.
  out_sine,     ///< Giảm tốc theo hình sin.
  in_out_sine,  ///< Tăng rồi giảm tốc theo hình sin.
  in_back,      ///< Lùi lại một chút trước khi đi.
  out_back,     ///< Vượt quá đích một chút rồi quay về.
  out_bounce,   ///< Nảy ở cuối như quả bóng rơi.
  out_elastic,  ///< Rung như lò xo ở cuối.
};

/// Áp đường cong lên tiến độ `t`.
/// @param curve Đường cong.
/// @param t Tiến độ từ 0 đến 1.
/// @return Tiến độ sau khi áp đường cong. `back` và `elastic` có thể ra ngoài 0..1.
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

/// Chuyển mượt một giá trị từ `from` đến `to` trong `duration` giây.
///
/// `T` là njin::f32, njin::vec2 hoặc njin::rgba (bất kỳ kiểu nào có lerp()).
/// Gọi tick() mỗi frame và dùng giá trị trả về.
template <class T> struct tween {
  T from{};                  ///< Giá trị lúc đầu.
  T to{};                    ///< Giá trị lúc cuối.
  f32 duration = 1.0f;       ///< Thời lượng, tính bằng giây.
  ease curve = ease::linear; ///< Đường cong chuyển động.
  f32 time = 0.0f;           ///< Thời gian đã trôi qua.

  /// Giá trị hiện tại.
  /// @return Giá trị giữa `from` và `to` theo tiến độ và đường cong.
  T value() const {
    const f32 t = duration > 0.0f ? time / duration : 1.0f;
    return lerp(from, to, ease_apply(curve, t));
  }

  /// Cho thời gian trôi rồi trả về giá trị mới.
  /// @param dt Thời gian của frame, thường là delta().
  /// @return Giá trị hiện tại.
  T tick(f32 dt) {
    time = time + dt > duration ? duration : time + dt;
    return value();
  }

  /// Đã chạy hết chưa.
  /// @return `true` nếu thời gian đã chạm `duration`.
  bool done() const { return time >= duration; }

  /// Bắt đầu lại từ `from`.
  void reset() { time = 0.0f; }
};
/// @}
} // namespace njin
