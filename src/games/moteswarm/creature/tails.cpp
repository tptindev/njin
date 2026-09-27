// The pair that paddles behind the body.
//
// The one part of a mote that is genuinely a simulation: a phase that only
// turns while the body is pushing, an amplitude that follows how hard, and
// tips that chase where the stroke says they should be and never quite get
// there. That last lag is the whip.
#include "util.h"
#include "parts.h"

#include <cmath>

namespace moteswarm {
// The pair opens and closes off the back of the body. The stroke is one phase
// that only turns while the mote is pushing itself along, and an amplitude that
// follows how hard it is pushing: let go and the phase stops, the amplitude
// falls away, and the two close.
void step_tails(creature &c, vec2 center, f32 effort, f32 dt) {
  const f32 R = c.radius;
  const f32 drive = clamp01(effort / (R * c.tail_drive_k));
  c.tail_flap += (drive - c.tail_flap) * (1.0f - std::exp(-c.tail_gain * dt));
  c.tail_phase += dt * c.tail_rate * drive;
  if (c.tail_phase > tau)
    c.tail_phase -= tau;

  // 0 at phase 0 and 1 at the far end of the swing, so a stroke always starts
  // from closed however long the body has been standing still.
  const f32 swing = 0.5f - 0.5f * std::cos(c.tail_phase);
  const f32 half = c.tail_closed + (c.tail_open - c.tail_closed) * c.tail_flap * swing;

  const f32 back = std::atan2(-c.dir.y, -c.dir.x);
  const vec2 spine = center - c.dir * (c.tail_root * R);
  // Across the heading, pointing the way side +1 swings. The root and the swing
  // have to agree on which side is which, or a leg would leave the body on one
  // side and reach round the other.
  const vec2 across{c.dir.y, -c.dir.x};
  const f32 reach = c.tail_len * R;
  const f32 t = 1.0f - std::exp(-c.tail_chase * dt);
  // The root steps out from the spine exactly as far as the swing has opened,
  // so the pair is sealed shut at rest and only parts as it fans out.
  const f32 range = c.tail_open - c.tail_closed;
  const f32 spread_t = clamp01((half - c.tail_closed) / range);

  for (i32 i = 0; i < c.tail_count; i++) {
    tail &tl = c.tails[i];
    const f32 a = back + tl.side * half;
    tl.root = spine + across * (tl.side * c.tail_root_spread * R * spread_t);
    const vec2 want = tl.root + vec2{std::cos(a), std::sin(a)} * reach;
    // The root is pinned to the body and the tip lags, which is what bends the
    // tail: a stroke throws the root round first and the tip follows through.
    tl.tip += (want - tl.tip) * t;
    tl.root_r = c.tail_r * R;
    tl.tip_r = tl.root_r * c.tail_taper;
  }
}
} // namespace moteswarm
