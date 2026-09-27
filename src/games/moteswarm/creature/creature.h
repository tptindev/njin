#pragma once
// The mote itself: a handful of circles melted into one field, one to three
// tails that paddle behind it and one eye. Only the parts that make it read
// as alive are here: its shape, its idle (breathing, blinking, looking
// around) and its move (turning, stroking the tails, leaning the eye into
// the way it goes).
//
// The shape is rigid. The circles are placed from the centre every frame and
// nothing lags or wobbles, so a body at a run is the same silhouette as one
// standing still. What moves is the breath and the tails.
//
// Nothing here integrates position. The game moves the body and hands the
// creature its centre, the velocity it is really travelling at, and the drive:
// the velocity it is asking for under its own power.
//
// Where the rest of it lives:
//   types.h    the parts and how many of each
//   blobs.cpp  the circles: placing them, measuring the silhouette
//   tails.cpp  the pair that paddles behind the body
//   eye.cpp    where the eye sits, what it looks at, when it blinks
//   tick.cpp   spawn, and one frame in the order the parts depend on each other
//   draw.cpp   one frame flattened for the shader
#include "types.h"

namespace moteswarm {

// Tuning and state in one aggregate. Every length is a ratio of `radius`, so a
// body that changes size never carries a stale absolute. Rates, times and
// dimensionless ratios are not scaled.
struct creature {
  f32 radius = 30.0f;

  // The velocity the creature is asking for under its own power, world units
  // per second. The heading and the tails read this and never the velocity: a
  // mote that is knocked back is not swimming backwards. The game writes it
  // every frame; tick clears it once read, so a body nobody steers stops
  // driving instead of coasting on the last thing it was asked.
  vec2 drive{};

  // --- heading ------------------------------------------------------------
  vec2 dir{1.0f, 0.0f};
  // How fast the heading comes round to the drive: a steered turn lands in a
  // fifth of a second.
  f32 dir_chase = 10.0f;
  // How fast it comes round to what its eye has settled on with nothing
  // driving it. Well under dir_chase: a glance is not a decision to go.
  f32 idle_turn_chase = 2.2f;
  // Under this, in radii per second, the body counts as standing still and the
  // eye stops reading it as going anywhere.
  f32 move_min_speed_k = 0.4167f;

  // --- body ---------------------------------------------------------------
  blob blobs[blob_count]{};
  // The smooth-min that melts the circles into one mass, in radii. Too small
  // and the satellites read as lumps stuck to a ball; too large and the whole
  // thing rounds off into a disc.
  f32 blend_k = 0.40f;

  // --- tails --------------------------------------------------------------
  tail tails[max_tails]{};
  // How many of `tails` are actually grown, 1..max_tails. Spawn deals out
  // `side` for exactly this many and every other step only ever touches
  // `tails[0 .. tail_count)`; the rest sit at their zero-initialized rest and
  // are never read.
  i32 tail_count = max_tails;
  // Where the pair leaves the body, measured back along the heading, and how
  // far they reach from there. The root sits inside the body on purpose, so
  // the smooth-min has something to build a haunch out of.
  f32 tail_root = 0.09f;
  f32 tail_len = 1.05f;
  f32 tail_r = 0.30f;     // at the root, in radii
  f32 tail_taper = 0.42f; // tip radius as a share of the root's
  // How far each root sits off the centre line at the widest point of its
  // swing, in radii. At tail_r the two rims just meet at full spread, so the
  // smooth-min has a seam to build a haunch across.
  f32 tail_root_spread = 0.30f;
  // The seam the pair is minned into each other through, in radii. Set to
  // reach as far as tail_root_spread can open the gap.
  f32 tail_pair_blend_k = 0.30f;
  f32 tail_blend_k = 0.30f;
  // The stroke. closed is where the pair sits with nothing going on, open is
  // the far end of a full swing; both in radians off the heading's tail.
  f32 tail_closed = 0.13f;
  f32 tail_open = 0.85f;
  // The phase only turns while the body is pushing itself along, so an idle
  // mote has stopped stroking and its tails have closed. drive_k is the speed,
  // in radii per second, that counts as a full stroke; gain is how fast the
  // amplitude follows it and chase how far behind its target the tip runs.
  f32 tail_rate = 10.0f;
  f32 tail_drive_k = 2.2f;
  f32 tail_gain = 7.0f;
  f32 tail_chase = 20.0f;
  // What a turn costs the tails, as a share of the turn's rim speed added to
  // the drive. Turning in place is the tails sculling; at 0 the body pivots
  // with the pair held shut, which reads as a sprite being rotated.
  f32 tail_turn_k = 1.4f;
  f32 tail_flap = 0.0f;  // state: 0 closed, 1 stroking at full amplitude
  f32 tail_phase = 0.0f; // state

  // --- idle ---------------------------------------------------------------
  // One sine, gated off by effort so it only runs while the body is standing
  // still. The rate decides whether it reads as breathing at all: slower than
  // about two seconds a breath and the eye stops reading it as motion. amp is
  // the swell and aniso the rise and fall against breath_axis, which is the
  // half the eye actually reads.
  f32 breathe_rate = 2.4f;
  f32 breathe_amp = 0.070f;
  f32 breathe_aniso = 0.10f;
  vec2 breath_axis{0.0f, 1.0f}; // straight up for a mote standing on the ground
  f32 breathe_stop_k = 2.5f;
  f32 breath_clock = 0.0f;

  // --- shadow -------------------------------------------------------------
  // Thrown by the body's own field, offset and blurred, so it follows every
  // stroke of the tails without anything being authored. y runs down: the
  // light is up and behind the left shoulder. The offset has to beat the blur
  // or the body covers its own shadow and there is nothing to see.
  vec2 shadow_offset{0.16f, 0.38f};
  f32 shadow_blur = 0.26f;
  f32 shadow_alpha = 0.30f;

  // --- eye ----------------------------------------------------------------
  eye_part eye{};
  // The sclera, in radii. It has to stay under what the circles reach ahead
  // (0.88 radii) or the forward lean has no room and the rim gets clipped.
  f32 eye_radius = 0.42f;
  f32 pupil_radius = 0.46f;
  f32 eye_margin_k = 0.1f;
  f32 eye_forward = 0.55f;
  f32 eye_forward_speed_k = 3.0f;
  f32 eye_chase = 8.0f;
  f32 gaze_rate = 16.0f;
  f32 blink_time = 0.13f;
  f32 eye_stretch_mix = 0.5f;
  f32 eye_squint = 0.20f;
  f32 eye_gaze_pull = 0.35f;
  f32 eye_breath_lift = 0.6f;
  f32 dilate_rate = 6.0f;
  f32 dilate_lo = 0.72f;
  f32 dilate_hi = 1.30f;
  f32 dilate_moving = 0.9f;

  u32 rng = 0x9e3779b9u;

  // How far the circles reach from the centre along the heading and across it,
  // measured once per tick after they have moved. The eye's forward room and
  // its stretch both read these.
  f32 ahead = 0.0f;
  f32 behind = 0.0f;
  f32 side = 0.0f;
  bool moving = false; // state: whether the drive counted as going anywhere this tick
};

// One frame flattened for the shader. Everything is already in world units.
struct creature_draw {
  vec4 lump[blob_count]; // xy centre, z radius
  f32 blend;

  vec4 tail_pts[max_tails]; // xy root, zw tip
  vec2 tail_r[max_tails];   // radius at the root and at the tip
  i32 tail_count;           // how many of the two arrays above the shader should read
  f32 tail_blend;
  f32 tail_pair_blend;

  vec2 shadow_offset;
  f32 shadow_blur;
  f32 shadow_alpha;

  vec2 eye_pos;
  vec2 pupil_pos;
  f32 eye_radius;
  f32 pupil_radius;
  f32 eye_open;
  f32 eye_stretch;
  vec2 dir;

  // Everything above fits inside this box, blend and shadow included, so the
  // quad covers just this much screen.
  vec2 bounds_min;
  vec2 bounds_max;
};

// Seats the circles, tails and eye around `center`. Call once before the first
// tick, or again to teleport a mote without dragging its shape across the map.
void creature_spawn(creature &c, vec2 center);

// One frame. `center` and `vel` are the body's own, owned by whatever moves it.
void creature_tick(creature &c, vec2 center, vec2 vel, f32 dt);

// Pure: only reads the creature, so it may be called from a render pass.
void creature_build_draw(const creature &c, creature_draw &out);
} // namespace moteswarm
