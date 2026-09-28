#pragma once
#include "_math.h"

namespace njin {
/// @addtogroup grp_random
/// @{

/// PCG32 random number generator: fast, good quality, and **repeatable**:
/// the same seed gives the same sequence.
///
/// The engine has a shared generator, obtained with njin::random(). Create your
/// own `rng` when you need an independent sequence, for example to generate a map from a code number.
struct rng {
  u64 state = 0x853c49e6748fea9bULL; ///< Current state.
  u64 inc = 0xda3e39cb94b95bdbULL;   ///< Stream (must be odd).

  rng() = default;
  /// Creates a generator with the seed `seed`.
  /// @param seed Seed.
  explicit rng(u64 seed) { reseed(seed); }

  /// Resets the seed. The same seed gives the same sequence.
  /// @param seed Seed.
  void reseed(u64 seed) {
    state = 0;
    inc = (seed << 1u) | 1u;
    next_u32();
    state += seed;
    next_u32();
  }

  /// Next 32-bit integer.
  /// @return A number from 0 to 2^32 - 1.
  u32 next_u32() {
    const u64 old = state;
    state = old * 6364136223846793005ULL + inc;
    const u32 xorshifted = (u32)(((old >> 18u) ^ old) >> 27u);
    const u32 rot = (u32)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
  }

  /// Float in `[0, 1)`.
  /// @return A float, never equal to 1.
  f32 unit() { return (f32)(next_u32() >> 8) * (1.0f / 16777216.0f); }

  /// Float in `[lo, hi)`.
  /// @param lo Lower bound.
  /// @param hi Upper bound.
  /// @return A random float.
  f32 range(f32 lo, f32 hi) { return lo + (hi - lo) * unit(); }

  /// Integer in `[lo, hi]`, **including `hi`**.
  /// @param lo Lower bound.
  /// @param hi Upper bound.
  /// @return A random integer. Returns `lo` if `hi < lo`.
  i32 range(i32 lo, i32 hi) {
    if (hi <= lo)
      return lo;
    const u32 span = (u32)((i64)hi - (i64)lo + 1);
    // Discard the remainder so every number has the same probability.
    const u32 limit = (u32)(-span) % span;
    u32 r = next_u32();
    while (r < limit)
      r = next_u32();
    return (i32)((i64)lo + (i64)(r % span));
  }

  /// True with probability `p`.
  /// @param p Probability, from 0 to 1.
  /// @return `true` with probability `p`.
  bool chance(f32 p) { return unit() < p; }

  /// Length-1 vector in a random direction.
  /// @return Unit vector.
  vec2 direction() { return from_angle(range(0.0f, 360.0f)); }

  /// Random point inside a rectangle.
  /// @param r Rectangle.
  /// @return A point inside `r`.
  vec2 point_in(rect r) {
    return {r.pos.x + range(0.0f, r.size.x), r.pos.y + range(0.0f, r.size.y)};
  }
};
/// @}
} // namespace njin
