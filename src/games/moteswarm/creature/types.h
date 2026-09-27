#pragma once
// The parts a mote is made of, and how many of each. Nothing here does
// anything: these are the pieces `creature` holds and the counts that size the
// arrays holding them.
#include <njin.h>

namespace moteswarm {
using namespace njin;

// One circle at the centre plus a ring of three around it: the fewest that
// still cover every side, so a body leaning one way is never left flat on the
// other.
inline constexpr i32 blob_count = 4;
// The most a mote can have; how many it actually grows (1..max_tails) is
// `creature::tail_count`, set once at spawn.
inline constexpr i32 max_tails = 3;

// off and radius are ratios of the body radius. pos and draw_radius are world
// and written whole every tick, so neither is state.
struct blob {
  vec2 off{};
  f32 radius = 0.0f;
  vec2 pos{};
  f32 draw_radius = 0.0f;
};

// A capsule from a root shared with the others out to a tip that chases where
// the stroke says it should be. The lag in that chase is the whip.
struct tail {
  // -1 and +1 for a swinging pair, 0 for one held dead centre; with three, the
  // outer two swing and the middle one sits still. Spawn deals these out from
  // creature::tail_count so this and every other field starts at rest.
  f32 side = 0.0f;
  vec2 root{};
  vec2 tip{};
  f32 root_r = 0.0f;
  f32 tip_r = 0.0f;
};

struct eye_part {
  vec2 pos{};
  vec2 pupil{};
  vec2 gaze{};
  vec2 gaze_target{};
  f32 open = 1.0f;
  f32 blink_phase = 0.0f;
  f32 blink_timer = 2.5f;
  f32 gaze_timer = 1.0f;
  f32 dilate = 1.0f;
  f32 dilate_target = 1.0f;
  f32 dilate_timer = 1.5f;
  f32 sclera_r = 0.0f;
  f32 pupil_r = 0.0f;
};
} // namespace moteswarm
