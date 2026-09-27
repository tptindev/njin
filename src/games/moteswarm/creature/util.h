#pragma once
// The dice, the turn and the clamp: the arithmetic the creature's parts are
// built out of, none of which knows what a mote is.
//
// Private to creature/. Inline because each is a few lines called from the
// middle of a step.
#include "types.h"

#include <cmath>

namespace moteswarm {
inline constexpr f32 tau = 6.2831853f;

inline f32 clamp01(f32 x) { return clamp(x, 0.0f, 1.0f); }

// Eases the heading towards `want` by a share of the remaining angle, so a turn
// closes on its mark and never quite touches it.
inline void turn_towards(vec2 &dir, vec2 want, f32 rate, f32 dt) {
  const f32 step = std::atan2(cross(dir, want), dot(dir, want)) * (1.0f - std::exp(-rate * dt));
  const f32 c = std::cos(step);
  const f32 s = std::sin(step);
  const vec2 turned{dir.x * c - dir.y * s, dir.x * s + dir.y * c};
  if (length(turned) > 1e-5f)
    dir = normalize(turned);
}

// xorshift32, in [0, 1). Seeded per creature so two motes never blink in step.
inline f32 rand01(u32 &state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return static_cast<f32>(state >> 8) * (1.0f / 16777216.0f);
}

inline f32 rand_range(u32 &state, f32 lo, f32 hi) { return lo + (hi - lo) * rand01(state); }
} // namespace moteswarm
