#pragma once
#include "_math.h"

namespace njin {
/// @addtogroup grp_random
/// @{

/// Bộ sinh số ngẫu nhiên PCG32: nhanh, chất lượng tốt, và **lặp lại được**:
/// cùng hạt giống thì cùng dãy số.
///
/// Engine có sẵn một bộ dùng chung, lấy bằng njin::random(). Tự tạo một `rng`
/// riêng khi cần một dãy độc lập, ví dụ sinh bản đồ từ một mã số.
struct rng {
  u64 state = 0x853c49e6748fea9bULL; ///< Trạng thái hiện tại.
  u64 inc = 0xda3e39cb94b95bdbULL;   ///< Luồng (phải là số lẻ).

  rng() = default;
  /// Tạo bộ sinh với hạt giống `seed`.
  /// @param seed Hạt giống.
  explicit rng(u64 seed) { reseed(seed); }

  /// Đặt lại hạt giống. Cùng hạt giống thì cùng dãy số.
  /// @param seed Hạt giống.
  void reseed(u64 seed) {
    state = 0;
    inc = (seed << 1u) | 1u;
    next_u32();
    state += seed;
    next_u32();
  }

  /// Số nguyên 32 bit tiếp theo.
  /// @return Số trong khoảng 0 đến 2^32 - 1.
  u32 next_u32() {
    const u64 old = state;
    state = old * 6364136223846793005ULL + inc;
    const u32 xorshifted = (u32)(((old >> 18u) ^ old) >> 27u);
    const u32 rot = (u32)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
  }

  /// Số thực trong `[0, 1)`.
  /// @return Số thực, không bao giờ bằng 1.
  f32 unit() { return (f32)(next_u32() >> 8) * (1.0f / 16777216.0f); }

  /// Số thực trong `[lo, hi)`.
  /// @param lo Cận dưới.
  /// @param hi Cận trên.
  /// @return Số thực ngẫu nhiên.
  f32 range(f32 lo, f32 hi) { return lo + (hi - lo) * unit(); }

  /// Số nguyên trong `[lo, hi]`, **gồm cả `hi`**.
  /// @param lo Cận dưới.
  /// @param hi Cận trên.
  /// @return Số nguyên ngẫu nhiên. Trả về `lo` nếu `hi < lo`.
  i32 range(i32 lo, i32 hi) {
    if (hi <= lo)
      return lo;
    const u32 span = (u32)((i64)hi - (i64)lo + 1);
    // Loại bỏ phần dư để mọi số có xác suất như nhau.
    const u32 limit = (u32)(-span) % span;
    u32 r = next_u32();
    while (r < limit)
      r = next_u32();
    return (i32)((i64)lo + (i64)(r % span));
  }

  /// Đúng với xác suất `p`.
  /// @param p Xác suất, từ 0 đến 1.
  /// @return `true` với xác suất `p`.
  bool chance(f32 p) { return unit() < p; }

  /// Vector độ dài 1 theo một hướng ngẫu nhiên.
  /// @return Vector đơn vị.
  vec2 direction() { return from_angle(range(0.0f, 360.0f)); }

  /// Điểm ngẫu nhiên trong hình chữ nhật.
  /// @param r Hình chữ nhật.
  /// @return Điểm nằm trong `r`.
  vec2 point_in(rect r) {
    return {r.pos.x + range(0.0f, r.size.x), r.pos.y + range(0.0f, r.size.y)};
  }
};
/// @}
} // namespace njin
