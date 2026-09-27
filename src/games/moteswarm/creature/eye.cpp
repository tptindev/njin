// The eye: where it sits, what it looks at, how wide the pupil is and when it
// blinks.
//
// It is the only part of a mote that acts of its own accord. Everything else
// follows the body; the eye picks somewhere to look on its own dice, and with
// nothing steering, the body is what comes round to it.
#include "util.h"
#include "parts.h"

#include <algorithm>
#include <cmath>

namespace moteswarm {
namespace {
// While going somewhere the eye looks where it is headed; standing still it
// picks somewhere new to look every so often, and the pupil follows its mood.
void pick_gaze(creature &c, f32 dt) {
  eye_part &eye = c.eye;
  if (c.moving) {
    eye.gaze_target = c.dir;
    eye.gaze_timer = rand_range(c.rng, 0.4f, 1.1f);
    eye.dilate_target = c.dilate_moving;
    return;
  }
  eye.gaze_timer -= dt;
  if (eye.gaze_timer <= 0.0f) {
    const f32 a = rand_range(c.rng, 0.0f, tau);
    const f32 r = rand_range(c.rng, 0.15f, 0.85f);
    eye.gaze_target = {std::cos(a) * r, std::sin(a) * r};
    eye.gaze_timer = rand_range(c.rng, 0.7f, 2.4f);
    // A new thing to look at usually changes the pupil too.
    if (rand01(c.rng) < 0.6f)
      eye.dilate_target = rand_range(c.rng, c.dilate_lo, c.dilate_hi);
  }
  eye.dilate_timer -= dt;
  if (eye.dilate_timer <= 0.0f) {
    eye.dilate_timer = rand_range(c.rng, 1.5f, 4.0f);
    eye.dilate_target = rand_range(c.rng, c.dilate_lo, c.dilate_hi);
  }
}

// A blink is the lid closing and opening again over blink_time, and it squints
// a little as the body picks up speed.
void step_blink(creature &c, f32 nose, f32 dt) {
  eye_part &eye = c.eye;
  if (eye.blink_phase > 0.0f) {
    eye.blink_phase -= dt;
    eye.open = std::fabs(2.0f * (eye.blink_phase / c.blink_time) - 1.0f);
  } else {
    eye.open = 1.0f;
    eye.blink_timer -= dt;
    if (eye.blink_timer <= 0.0f) {
      eye.blink_phase = c.blink_time;
      eye.blink_timer = rand_range(c.rng, 1.8f, 5.5f);
    }
  }
  eye.open *= 1.0f - c.eye_squint * nose;
}
} // namespace

void step_eye(creature &c, vec2 center, vec2 vel, f32 breath_uniform, f32 breath_tall, f32 dt) {
  eye_part &eye = c.eye;
  const f32 R = c.radius;
  const f32 sclera = R * c.eye_radius * (1.0f + breath_uniform);

  pick_gaze(c, dt);
  eye.gaze += (eye.gaze_target - eye.gaze) * (1.0f - std::exp(-c.gaze_rate * dt));
  eye.dilate += (eye.dilate_target - eye.dilate) * (1.0f - std::exp(-c.dilate_rate * dt));
  const f32 pupil = sclera * c.pupil_radius * eye.dilate;
  eye.sclera_r = sclera;
  eye.pupil_r = pupil;

  // The whole eye leans towards where the body is going, chases its anchor more
  // slowly than the pupil chases the gaze, and rides up on an inhale. The lean
  // is read off the velocity along the heading, signed and floored at zero, so
  // a body shoved backwards wears its eye in the middle rather than in the tail.
  const f32 nose_speed = std::max(dot(vel, c.dir), 0.0f);
  const f32 nose = nose_speed / (nose_speed + R * c.eye_forward_speed_k);
  const f32 room = std::max(c.ahead - sclera - R * c.eye_margin_k, 0.0f);
  const f32 lean = room * c.eye_forward * nose;
  // eye.pos chases its anchor, so a moving body trails its own eye by a fixed
  // distance; pushing the anchor along by exactly that much cancels the trail
  // and the eye settles on the lean. It is laid along the velocity, not the
  // heading, because the trail runs whichever way the body is carried, and
  // read off the step the chase actually takes rather than eye_chase so it is
  // exact at any frame rate.
  const f32 eye_t = 1.0f - std::exp(-c.eye_chase * dt);
  const f32 trail = eye_t > 1e-6f ? dt * (1.0f - eye_t) / eye_t : 0.0f;
  const vec2 anchor = center + c.dir * lean + vel * trail + eye.gaze * (sclera * c.eye_gaze_pull) -
                      vec2{0.0f, breath_tall * R * c.eye_breath_lift};
  eye.pos += (anchor - eye.pos) * eye_t;

  // The pupil slides inside the sclera towards the gaze, stretched the same way
  // the lid is so it stays inside it.
  const f32 es = 1.0f + (body_aspect(c) - 1.0f) * c.eye_stretch_mix;
  const vec2 g = eye.gaze * (sclera - pupil);
  eye.pupil = eye.pos + g / es + c.dir * ((es - 1.0f / es) * dot(g, c.dir));

  step_blink(c, nose, dt);
}
} // namespace moteswarm
