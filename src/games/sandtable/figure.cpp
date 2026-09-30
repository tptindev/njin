#include "figure.h"
#include "view.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace sandtable {

namespace {

// The figure is worked out in its own frame, one unit tall: x to its right,
// y up, z where it faces; then turned and scaled onto the table.

// Proportions, after the reference: a big round head on a thin neck, a slim
// body with sloping shoulders, long thin limbs, small hands and feet.
constexpr f32 head_r = 0.085f;
constexpr f32 neck_len = 0.06f, neck_r = 0.017f;
constexpr f32 body_len = 0.24f, hip_r = 0.07f, chest_r = 0.05f;
constexpr f32 pelvis_y = 0.49f; // hip height standing: the knees just soft
constexpr f32 shoulder_x = 0.078f, shoulder_drop = 0.035f, hip_x = 0.042f;
constexpr f32 upper_arm = 0.19f, forearm = 0.18f, hand_len = 0.05f;
constexpr f32 thigh = 0.245f, shin = 0.235f, foot_len = 0.07f;
constexpr f32 sole = 0.02f; // ankle height over the ground when the foot is flat

// How softly parts melt into what is already there (smooth min): generous
// where the limbs and the head join the body, slight along a limb, whose
// segments follow one smooth curve and need only the seams hidden.
constexpr f32 soft_body = 0.035f, soft_root = 0.045f, soft_along = 0.006f, soft_end = 0.01f;

// Steps: how far a foot travels either side of the hip in one stance.
constexpr f32 walk_reach = 0.25f, run_reach = 0.34f;

f32 smooth(f32 t) {
  t = clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
f32 ramp(f32 a, f32 b, f32 t) { return smooth((t - a) / (b - a)); }

f32 hash01(u32 seed, u32 salt) {
  u32 h = seed * 0x9E3779B1u + salt * 0x85EBCA77u;
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  return static_cast<f32>(h & 0xFFFFu) / 65535.0f;
}

// A slow wander, -1 to 1: where an idle man looks, how he shifts his weight.
f32 wander(f32 t, f32 phase) { return 0.6f * std::sin(t * 0.37f + phase) + 0.4f * std::sin(t * 0.83f + phase * 2.3f); }

vec3 rot_x(vec3 v, f32 a) {
  const f32 c = std::cos(a), s = std::sin(a);
  return {v.x, v.y * c - v.z * s, v.y * s + v.z * c};
}
vec3 rot_y(vec3 v, f32 a) {
  const f32 c = std::cos(a), s = std::sin(a);
  return {v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
}
vec3 rot_z(vec3 v, f32 a) {
  const f32 c = std::cos(a), s = std::sin(a);
  return {v.x * c - v.y * s, v.x * s + v.y * c, v.z};
}

// A limb's direction: `pitch` from straight down toward the front, `out`
// sideways, to the right for the right side (sign 1) and left for the left.
vec3 limb_dir(f32 pitch, f32 out, f32 sign) {
  return normalize(vec3{sign * std::sin(out), -std::cos(pitch) * std::cos(out), std::sin(pitch) * std::cos(out)});
}

// The pose as numbers, eased from frame to frame (figure_memory). Angles in
// radians; limbs 0 left, 1 right.
struct pose {
  f32 hip[2];      // thigh forward from down
  f32 knee[2];     // shin bent back from the thigh
  f32 foot[2];     // foot pitch from down (pi/2 flat)
  f32 shoulder[2]; // upper arm forward from down
  f32 out[2];      // arm out to the side
  f32 elbow[2];    // forearm bent forward
  f32 lean;        // body forward from the hips (negative back)
  f32 roll;        // body tipped to its right
  f32 twist;       // shoulders turned (negative brings the right one forward)
  f32 nod;         // head forward
  f32 turn;        // head turned to its left
  f32 bob;         // hips up and down
  f32 sway;        // hips to the right
  f32 fall;        // whole body tipped back (negative: forward), pi/2 lying
};
constexpr usize pose_floats = sizeof(pose) / sizeof(f32);
static_assert(pose_floats <= sizeof(figure_memory::joint) / sizeof(f32));
constexpr usize nod_at = offsetof(pose, nod) / sizeof(f32);
constexpr usize fall_at = offsetof(pose, fall) / sizeof(f32);

pose mix(const pose &a, const pose &b, f32 t) {
  f32 va[pose_floats], vb[pose_floats];
  std::memcpy(va, &a, sizeof(pose));
  std::memcpy(vb, &b, sizeof(pose));
  for (usize i = 0; i < pose_floats; ++i)
    va[i] = lerp(va[i], vb[i], t);
  pose o{};
  std::memcpy(&o, va, sizeof(pose));
  return o;
}

// A leg reaching for an ankle `forward` of the hip and `down` below it, in
// the leg's own plane: the thigh and shin angles two-bone IK gives, the knee
// bending forward.
void reach_leg(pose &p, i32 k, f32 forward, f32 down) {
  const f32 d = std::min(std::sqrt(forward * forward + down * down), thigh + shin - 0.002f);
  const f32 to = std::atan2(forward, down); // from straight down toward the front
  const f32 at_hip = std::acos(clamp((thigh * thigh + d * d - shin * shin) / (2.0f * thigh * d), -1.0f, 1.0f));
  const f32 inner = std::acos(clamp((thigh * thigh + shin * shin - d * d) / (2.0f * thigh * shin), -1.0f, 1.0f));
  p.hip[k] = to + at_hip;
  p.knee[k] = pi - inner;
}

// Both feet planted at `z0`, `z1` forward of the hips (left, right), the
// hips at their height plus `p.bob`.
void stand_on(pose &p, f32 z0, f32 z1) {
  const f32 z[2] = {z0, z1};
  for (i32 k = 0; k < 2; ++k) {
    reach_leg(p, k, z[k], pelvis_y + p.bob - sole);
    p.foot[k] = pi * 0.5f;
  }
}

// Standing: breathing, the weight drifting from foot to foot, arms a little
// out from the body, looking about now and then.
pose idle(const figure_state &st, f32 me) {
  pose p{};
  const f32 breath = std::sin(st.time * (1.6f + 0.4f * me) + me * 6.0f);
  const f32 drift = wander(st.time, me * 9.0f);
  for (i32 k = 0; k < 2; ++k) {
    p.shoulder[k] = 0.05f + 0.03f * breath;
    p.out[k] = 0.18f + 0.1f * me + 0.02f * breath;
    p.elbow[k] = 0.15f + 0.1f * me;
  }
  p.bob = 0.004f * breath - 0.006f * std::fabs(drift);
  p.sway = 0.014f * drift;
  p.roll = -0.05f * drift;
  p.lean = 0.02f + 0.03f * me;
  p.turn = 0.5f * wander(st.time * 0.7f, me * 17.0f);
  p.nod = 0.05f * breath;
  stand_on(p, 0.02f * me, -0.02f * me);
  return p;
}

// Squared up for a fight: knees bent, fists up, bouncing on the feet.
pose guard(const figure_state &st, f32 me) {
  pose p{};
  const f32 bounce = std::sin(st.time * 7.0f + me * 5.0f);
  for (i32 k = 0; k < 2; ++k) {
    p.shoulder[k] = 0.75f;
    p.out[k] = 0.3f;
    p.elbow[k] = 1.95f;
  }
  p.shoulder[0] += 0.12f; // the lead hand a little further out
  p.elbow[0] -= 0.2f;
  p.bob = -0.035f + 0.008f * bounce;
  p.lean = 0.12f;
  p.twist = 0.15f;
  p.nod = 0.12f;
  stand_on(p, 0.09f, -0.07f);
  return p;
}

// Walking or running (`run` 0 to 1): each foot planted and carried under the
// moving body, then lifted and swung through on an arc; the hips rise over
// the planted foot and sway toward it, the shoulders turn against the hips,
// the arms swing against the legs.
pose gait(f32 stride_h, f32 run, f32 me) {
  pose p{};
  const f32 reach = lerp(walk_reach, run_reach, run);
  const f32 cycle = 4.0f * reach; // a full left-right cycle, figure heights walked
  const f32 u = stride_h / cycle + me;
  const f32 lift_h = lerp(0.05f, 0.13f, run);
  f32 z[2], lift[2], foot[2];
  for (i32 k = 0; k < 2; ++k) {
    const f32 v = u + (k == 0 ? 0.0f : 0.5f);
    const f32 w = v - std::floor(v);
    if (w < 0.5f) { // planted: the ground slides back under the body
      z[k] = reach * (1.0f - 4.0f * w);
      lift[k] = 0.0f;
      foot[k] = pi * 0.5f;
    } else { // swung through
      const f32 s = (w - 0.5f) * 2.0f;
      z[k] = -reach + 2.0f * reach * smooth(s);
      lift[k] = lift_h * std::sin(pi * s);
      foot[k] = pi * 0.5f - 0.45f * std::sin(pi * s); // toe down leaving the ground
    }
  }
  const f32 a = 2.0f * pi * u;
  p.bob = lerp(0.012f, 0.025f, run) * std::cos(2.0f * a) - lerp(0.0f, 0.04f, run);
  p.sway = 0.016f * std::sin(a) * (1.0f - run * 0.5f);
  p.roll = -0.04f * std::sin(a);
  p.twist = -lerp(0.14f, 0.22f, run) * std::sin(a);
  p.lean = lerp(0.05f, 0.3f, run);
  p.nod = lerp(0.0f, 0.1f, run);
  for (i32 k = 0; k < 2; ++k) {
    const f32 swing = -lerp(0.35f, 0.95f, run) * std::sin(a + (k == 0 ? pi : 0.0f));
    p.shoulder[k] = swing;
    p.out[k] = lerp(0.18f, 0.14f, run);
    p.elbow[k] = lerp(0.2f, 1.45f, run) + 0.25f * std::max(0.0f, swing);
    reach_leg(p, k, z[k], pelvis_y + p.bob - sole - lift[k]);
    p.foot[k] = foot[k];
  }
  return p;
}

// A punch in three beats: drawn back with the body turned away, thrown with
// the body turning into it, then brought back to guard.
void punch(pose &p, i32 arm, f32 t) {
  const f32 wind = ramp(0.0f, 0.3f, t) * (1.0f - ramp(0.3f, 0.42f, t));
  const f32 hit = ramp(0.3f, 0.42f, t) * (1.0f - ramp(0.55f, 1.0f, t));
  const f32 turn = arm == 1 ? -1.0f : 1.0f;
  p.shoulder[arm] = lerp(0.8f, 1.5f, hit) - 0.35f * wind;
  p.elbow[arm] = lerp(1.95f, 0.05f, hit) + 0.2f * wind;
  p.out[arm] = lerp(0.3f, 0.06f, hit);
  p.twist += turn * (0.45f * hit - 0.3f * wind);
  p.lean += 0.14f * hit - 0.04f * wind;
  p.nod += 0.08f * hit;
}

// A front kick with the right leg: knee drawn up, the leg snapped out, then
// brought back; the body leans back and the arms go out for balance.
void kick(pose &p, f32 t) {
  const f32 chamber = ramp(0.0f, 0.3f, t) * (1.0f - ramp(0.6f, 0.95f, t));
  const f32 snap = ramp(0.3f, 0.45f, t) * (1.0f - ramp(0.55f, 0.85f, t));
  p.hip[1] = lerp(p.hip[1], 1.25f + 0.2f * snap, chamber);
  p.knee[1] = lerp(p.knee[1], 1.7f - 1.6f * snap, chamber);
  p.foot[1] = lerp(p.foot[1], p.hip[1] - p.knee[1] + 1.0f, chamber);
  p.lean += -0.1f * chamber - 0.25f * snap;
  for (i32 k = 0; k < 2; ++k) {
    p.out[k] = lerp(p.out[k], 0.8f, chamber);
    p.shoulder[k] = lerp(p.shoulder[k], 0.4f, chamber);
    p.elbow[k] = lerp(p.elbow[k], 0.6f, chamber);
  }
}

// Lying on the back (or on the face), limbs where they fell.
pose lying(f32 me, bool face_down, f32 limp) {
  pose p{};
  p.fall = face_down ? -pi * 0.5f : pi * 0.5f;
  p.hip[0] = 0.15f + 0.2f * me;
  p.hip[1] = -0.05f;
  p.knee[0] = 0.25f + 0.3f * limp;
  p.knee[1] = 0.1f + 0.4f * me;
  p.foot[0] = p.foot[1] = pi * 0.5f;
  p.shoulder[0] = 0.5f;
  p.shoulder[1] = -0.3f + 0.5f * me;
  p.out[0] = 1.0f + 0.3f * limp;
  p.out[1] = 0.8f + 0.4f * me;
  p.elbow[0] = 0.4f;
  p.elbow[1] = 0.7f;
  p.nod = -0.15f * limp;
  p.turn = (me - 0.5f) * limp;
  return p;
}

// Knocked off his feet: the knees give, he tips over faster and faster with
// the arms flung up, lands with a little bounce, and lies there.
pose knocked(const figure_state &st, f32 me) {
  const f32 t = down_time - st.down;
  pose p = guard(st, me);
  const f32 give = ramp(0.0f, 0.12f, t);
  for (i32 k = 0; k < 2; ++k)
    p.knee[k] += 0.5f * give;
  const f32 tip = clamp((t - 0.05f) / 0.38f, 0.0f, 1.0f);
  const f32 fly = std::sin(pi * clamp(t / 0.5f, 0.0f, 1.0f));
  for (i32 k = 0; k < 2; ++k) {
    p.shoulder[k] = lerp(p.shoulder[k], 1.8f, fly);
    p.out[k] = lerp(p.out[k], 0.9f, fly);
    p.elbow[k] = lerp(p.elbow[k], 0.3f, fly);
  }
  p.nod -= 0.4f * fly;
  p.fall = pi * 0.5f * tip * tip; // gathering speed
  if (t > 0.43f) { // landed: a small bounce, then lying loose
    p = mix(p, lying(me, false, 0.0f), ramp(0.43f, 0.7f, t));
    p.fall = pi * 0.5f - 0.08f * std::sin(pi * clamp((t - 0.43f) / 0.2f, 0.0f, 1.0f));
  }
  return p;
}

// Getting up: sits up with his hands behind him, draws his feet under him
// into a crouch, and stands.
pose getting_up(const figure_state &st, f32 me) {
  const f32 q = 1.0f - st.rise / rise_time;
  pose sit = lying(me, false, 0.0f);
  sit.fall = 0.95f;
  sit.hip[0] = sit.hip[1] = 1.35f;
  sit.knee[0] = sit.knee[1] = 1.8f;
  sit.shoulder[0] = sit.shoulder[1] = -0.7f;
  sit.out[0] = sit.out[1] = 0.35f;
  sit.elbow[0] = sit.elbow[1] = 0.2f;
  sit.lean = 0.5f;
  pose crouch = guard(st, me);
  crouch.lean = 0.55f;
  crouch.bob = -0.2f;
  stand_on(crouch, 0.05f, -0.03f);
  crouch.shoulder[0] = crouch.shoulder[1] = 0.6f;
  crouch.elbow[0] = crouch.elbow[1] = 0.6f;
  if (q < 0.35f)
    return mix(lying(me, false, 0.0f), sit, smooth(q / 0.35f));
  if (q < 0.7f)
    return mix(sit, crouch, smooth((q - 0.35f) / 0.35f));
  return mix(crouch, guard(st, me), smooth((q - 0.7f) / 0.3f));
}

// Dying: the knees buckle and he slumps, then goes over, back or face down,
// and lies still.
pose dying(const figure_state &st, f32 me) {
  const f32 t = st.dead;
  const bool face_down = me > 0.55f;
  pose p = idle(st, me);
  const f32 buckle = ramp(0.0f, 0.25f, t);
  p.bob -= 0.12f * buckle;
  stand_on(p, 0.03f, -0.02f);
  p.lean += (face_down ? 0.5f : 0.2f) * buckle;
  p.nod += 0.5f * buckle;
  for (i32 k = 0; k < 2; ++k) {
    p.out[k] *= 1.0f - buckle;
    p.shoulder[k] = lerp(p.shoulder[k], 0.1f, buckle);
    p.elbow[k] = lerp(p.elbow[k], 0.1f, buckle);
  }
  const f32 tip = clamp((t - 0.18f) / 0.4f, 0.0f, 1.0f);
  const pose down = lying(me, face_down, 1.0f);
  p = mix(p, down, smooth(tip));
  p.fall = down.fall * tip * tip;
  return p;
}

pose target_pose(const figure_state &st, f32 stride_h, f32 me) {
  if (st.dead >= 0.0f)
    return dying(st, me);
  if (st.down > 0.0f)
    return knocked(st, me);
  if (st.rise > 0.0f)
    return getting_up(st, me);
  // On the move, or standing: at ease, or squared up when a fight is on.
  const f32 walk = fighter.speed, run = fighter.speed * run_factor;
  pose p = st.fighting || st.act > 0.0f ? guard(st, me) : idle(st, me);
  if (st.pace > 1.0f)
    p = mix(p, gait(stride_h, clamp((st.pace - walk) / (run - walk), 0.0f, 1.0f), me),
            clamp(st.pace / (walk * 0.6f), 0.0f, 1.0f));
  if (st.act > 0.0f) {
    const f32 t = 1.0f - st.act / blow_time;
    if (st.act_kind == 2)
      kick(p, t);
    else
      punch(p, st.act_kind, t);
  }
  // Reeling from a blow: the head snaps back first, the body after.
  if (st.hurt > 0.0f) {
    const f32 h = st.hurt / flinch_time;
    p.nod -= 0.55f * h;
    p.lean -= 0.3f * h * h;
    p.twist += 0.2f * h * (me - 0.5f);
    for (i32 k = 0; k < 2; ++k) {
      p.out[k] += 0.35f * h;
      p.elbow[k] += 0.4f * h;
    }
  }
  return p;
}

// Eases the pose from the last one toward `target`: most joints over about a
// tenth of a second, the head quicker, a fall not at all (gravity does not
// wait, and falling has its own timing).
pose ease_pose(figure_memory &m, const pose &target, f32 dt) {
  f32 want[pose_floats];
  std::memcpy(want, &target, sizeof(pose));
  if (!m.set) {
    std::memcpy(m.joint, want, sizeof(pose));
    m.set = true;
  } else if (dt > 0.0f) {
    const f32 k = 1.0f - std::exp(-14.0f * dt), quick = 1.0f - std::exp(-24.0f * dt);
    for (usize i = 0; i < pose_floats; ++i)
      m.joint[i] += (want[i] - m.joint[i]) * (i == nod_at ? quick : k);
    m.joint[fall_at] = want[fall_at];
  }
  pose out{};
  std::memcpy(&out, m.joint, sizeof(pose));
  return out;
}

} // namespace

figure_pose pose_figure(const figure_state &st, vec3 feet, vec2 facing, f32 height, figure_memory *memory, f32 dt) {
  const f32 me = hash01(st.seed, 1u);
  height *= 0.95f + 0.1f * hash01(st.seed, 2u);
  const f32 stride_h = st.stride * unit3d / height;
  const pose target = target_pose(st, stride_h, me);
  const pose a = memory != nullptr ? ease_pose(*memory, target, dt) : target;

  // Legs from the hips down, in their own planes.
  const vec3 pelvis{a.sway, pelvis_y + a.bob, 0.0f};
  vec3 hip[2], knee[2], ankle[2], toe[2];
  for (i32 k = 0; k < 2; ++k) {
    const f32 sign = k == 1 ? 1.0f : -1.0f;
    hip[k] = {sign * hip_x, pelvis_y + a.bob, 0.0f};
    knee[k] = hip[k] + limb_dir(a.hip[k], 0.03f, sign) * thigh;
    ankle[k] = knee[k] + limb_dir(a.hip[k] - a.knee[k], 0.02f, sign) * shin;
    toe[k] = ankle[k] + limb_dir(a.foot[k], 0.0f, sign) * foot_len;
  }
  // Onto the ground: the lower foot stands on it.
  const f32 lift = sole - std::min(ankle[0].y, ankle[1].y);

  // The body from the pelvis: leaning, tipped to a side and turned; the
  // shoulders, neck and head ride on it.
  const auto body = [&](vec3 v) { return rot_y(rot_x(rot_z(v, -a.roll), a.lean), a.twist); };
  const vec3 up = body({0.0f, 1.0f, 0.0f});
  const vec3 chest = pelvis + up * body_len;
  vec3 shoulder[2];
  for (i32 k = 0; k < 2; ++k)
    shoulder[k] = chest + body({(k == 1 ? 1.0f : -1.0f) * shoulder_x, -shoulder_drop, 0.0f});
  const vec3 neck0 = chest + up * 0.015f;
  const vec3 neck1 = neck0 + up * neck_len;
  const auto head_frame = [&](vec3 v) { return body(rot_y(rot_x(v, a.nod), a.turn)); };
  const vec3 head = neck1 + head_frame({0.0f, head_r * 0.85f, 0.0f});
  vec3 elbow[2], wrist[2], finger[2];
  for (i32 k = 0; k < 2; ++k) {
    const f32 sign = k == 1 ? 1.0f : -1.0f;
    elbow[k] = shoulder[k] + body(limb_dir(a.shoulder[k], a.out[k], sign)) * upper_arm;
    wrist[k] = elbow[k] + body(limb_dir(a.shoulder[k] + a.elbow[k], a.out[k] * 0.6f, sign)) * forearm;
    finger[k] = wrist[k] + body(limb_dir(a.shoulder[k] + a.elbow[k] + 0.15f, a.out[k] * 0.5f, sign)) * hand_len;
  }

  // Tipped about the feet when falling or lying, resting on the back (or
  // face); then turned to face `facing` and set on the table.
  const f32 tipped = std::sin(std::fabs(a.fall));
  const vec2 f2 = length_sq(facing) > 1e-6f ? normalize(facing) : vec2{0.0f, -1.0f};
  const vec3 fwd{f2.x, 0.0f, f2.y};
  const vec3 right{-fwd.z, 0.0f, fwd.x};
  const auto place = [&](vec3 p) {
    p.y += lift * (1.0f - tipped);
    p = rot_x(p, -a.fall);
    p.y += chest_r * tipped;
    return feet + (right * p.x + vec3{0.0f, p.y, 0.0f} + fwd * p.z) * height;
  };

  figure_pose out{};
  const auto part = [&](vec3 from, vec3 to, f32 ra, f32 rb, f32 soft) {
    if (out.count < sdf_blend_max)
      out.parts[out.count++] = {
          .a = place(from), .b = place(to), .ra = ra * height, .rb = rb * height, .blend = soft * height};
  };
  // A limb as one smooth curve through its middle joint, in four segments,
  // tapering from `r0` to `r1`: the joint rounds instead of breaking.
  const auto limb = [&](vec3 from, vec3 mid, vec3 to, f32 r0, f32 r1, f32 root_soft) {
    const vec3 c = mid * 2.0f - (from + to) * 0.5f; // so the curve passes through `mid`
    vec3 prev = from;
    for (i32 i = 1; i <= 4; ++i) {
      const f32 t0 = static_cast<f32>(i - 1) / 4.0f, t = static_cast<f32>(i) / 4.0f;
      const vec3 at = from * ((1.0f - t) * (1.0f - t)) + c * (2.0f * (1.0f - t) * t) + to * (t * t);
      part(prev, at, lerp(r0, r1, t0), lerp(r0, r1, t), i == 1 ? root_soft : soft_along);
      prev = at;
    }
  };
  part(pelvis, chest, hip_r, chest_r, soft_body); // wider at the hips than the chest
  for (i32 k = 0; k < 2; ++k)                     // sloping shoulders
    part(chest, shoulder[k], chest_r * 0.75f, 0.022f, soft_root);
  part(neck0, neck1, neck_r, neck_r, soft_body);
  part(head, head, head_r, head_r, soft_body);
  for (i32 k = 0; k < 2; ++k) {
    limb(shoulder[k], elbow[k], wrist[k], 0.021f, 0.0105f, soft_body);
    part(wrist[k], finger[k], 0.0105f, 0.013f, soft_end);
    limb(hip[k], knee[k], ankle[k], 0.03f, 0.013f, soft_root);
    part(ankle[k], toe[k], 0.013f, 0.016f, soft_end);
  }
  out.blend = soft_body * height;
  for (i32 k = 0; k < 2; ++k)
    out.eyes[k] = place(head + head_frame({(k == 1 ? 1.0f : -1.0f) * 0.03f, 0.015f, head_r * 0.9f}));
  out.eye_radius = 0.008f * height;
  out.hand = place(finger[1]);
  return out;
}

} // namespace sandtable
