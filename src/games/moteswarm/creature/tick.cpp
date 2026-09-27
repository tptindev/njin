// Bringing a mote into existence, and one frame of it: what the body is doing
// under its own power, and then every part in the order the parts depend on
// each other.
//
// This is the only file that knows that order. Each step lives in a file of its
// own and asks nothing about the others; what a step needs to know about the
// frame, the effort, the breath, the turn just made, is worked out here and
// handed down.
#include "util.h"
#include "parts.h"

#include <cmath>

namespace moteswarm {
namespace {
// The satellites are dealt evenly around the circle with a random turn on the
// whole set. The turn is the only variation left in the silhouette, and worth
// keeping: dealt without it every mote is the same shape at the same angle.
void deal_blobs(creature &c) {
  blob &core = c.blobs[0];
  core.off = {0.0f, 0.0f};
  core.radius = 0.80f;

  constexpr i32 ring = blob_count - 1;
  const f32 turn = rand_range(c.rng, 0.0f, tau);
  for (i32 i = 0; i < ring; i++) {
    blob &b = c.blobs[1 + i];
    const f32 a = turn + static_cast<f32>(i) * tau / static_cast<f32>(ring);
    b.off = {std::cos(a) * 0.26f, std::sin(a) * 0.26f};
    b.radius = 0.62f;
  }
}

// The heading follows the drive. With no drive there is nothing to face, and a
// creature standing perfectly still facing one way is a prop, so it follows its
// own eye instead: it looks somewhere, then comes round to it, slower than it
// turns when it is actually going there, because a glance is not a decision to
// go.
void turn_heading(creature &c, f32 effort, f32 dt) {
  if (effort > 1.0f)
    turn_towards(c.dir, c.drive / effort, c.dir_chase, dt);
  else if (length(c.eye.gaze) > 0.05f)
    turn_towards(c.dir, normalize(c.eye.gaze), c.idle_turn_chase, dt);
}
} // namespace

void creature_spawn(creature &c, vec2 center) {
  deal_blobs(c);
  for (blob &b : c.blobs) {
    b.pos = center + b.off * c.radius;
    b.draw_radius = b.radius * c.radius;
  }

  // The same places step_tails puts them at rest, so the first tick has nothing
  // to slide out to where they belong.
  const vec2 spine = center - c.dir * (c.tail_root * c.radius);
  c.tail_count = c.tail_count < 1 ? 1 : (c.tail_count > max_tails ? max_tails : c.tail_count);
  const i32 n = c.tail_count;
  for (i32 i = 0; i < n; i++) {
    tail &tl = c.tails[i];
    // Spread evenly across [-1, 1]: one tail sits dead centre, two split into
    // a swinging pair, three keep that pair and add the centre one back.
    tl.side = n > 1 ? -1.0f + 2.0f * static_cast<f32>(i) / static_cast<f32>(n - 1) : 0.0f;
    tl.root = spine;
    tl.tip = tl.root - c.dir * (c.tail_len * c.radius);
    tl.root_r = c.tail_r * c.radius;
    tl.tip_r = tl.root_r * c.tail_taper;
  }
  c.tail_flap = 0.0f;
  c.tail_phase = 0.0f;

  c.eye.pos = center;
  c.eye.pupil = center;
  c.eye.sclera_r = c.radius * c.eye_radius;
  c.eye.pupil_r = c.eye.sclera_r * c.pupil_radius;

  c.ahead = c.behind = c.side = c.radius;
}

void creature_tick(creature &c, vec2 center, vec2 vel, f32 dt) {
  const f32 R = c.radius;
  // What the body is doing under its own power, which is the drive and not the
  // velocity: a mote knocked about keeps facing the way it was swimming.
  const f32 effort = length(c.drive);
  c.moving = effort > R * c.move_min_speed_k;

  const vec2 was = c.dir;
  turn_heading(c, effort, dt);

  // What a turn costs: a body pivoting on the spot does it with the same two
  // tails it swims with, so a turn strokes them as far as the heading actually
  // moved, in the same unit the drive is in.
  const f32 turned = std::fabs(std::atan2(cross(was, c.dir), dot(was, c.dir)));
  const f32 scull = dt > 1e-6f ? turned / dt * R * c.tail_turn_k : 0.0f;

  // The idle breath, faded out by effort so it only runs while standing still.
  c.breath_clock += dt * c.breathe_rate;
  f32 rest = clamp01(1.0f - effort / (R * c.breathe_stop_k));
  rest = rest * rest * (3.0f - 2.0f * rest);
  const f32 breath_phase = std::sin(c.breath_clock);
  const f32 breath_uniform = c.breathe_amp * rest * breath_phase;
  const f32 breath_tall = c.breathe_aniso * rest * breath_phase;

  place_blobs(c, center, breath_uniform, breath_tall);
  measure_extents(c, center);
  step_tails(c, center, effort + scull, dt);
  step_eye(c, center, vel, breath_uniform, breath_tall, dt);

  // Consumed: a body whose steerer stops writing it drives at nothing next
  // frame instead of holding the last thing it was asked forever.
  c.drive = {};
}
} // namespace moteswarm
