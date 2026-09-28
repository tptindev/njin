#version 330

// The minimalist stick figure, drawn once per cell into the sheets. Instead of
// a colour it writes how much of each pixel belongs to the body's one colour
// slot, so one sheet serves every DNA: crowd.fs multiplies the weight by the
// person's own colour. Every shape is filled solid.
//
//   u_pass 0, body A: r the body (torso, legs, arms)
//   u_pass 1, body B: unused; the top-left texel of each cell holds the head
//                     centre in g, b (fraction of the cell), read by crowd.vs
//   u_pass 2, head:   r the head (a plain circle, same colour as the body)
//
// Body cells are one per (build, pose, base direction, frame); head cells one
// per (hair style, base direction) though the shape ignores hair style, placed
// on the body at the stored centre. Weights are premultiplied by coverage, so
// the sheets filter linearly. Alpha is written as 1: the default blend would
// square it otherwise.

in vec2 v_local;
flat in vec2 v_frame; // frame, frame count
flat in vec4 v_pose;  // base direction, pose, build, hair style
out vec4 finalColor;

uniform vec2 u_cell;        // cell size, pixels
uniform float u_scale;      // pixels per shape unit
uniform float u_bottom;     // shape-space y of the bottom edge of a body cell
uniform vec2 u_head_center; // head pass: head centre, pixels from the cell's top-left
uniform int u_pass;

#define DIR_S  0
#define DIR_SE 1
#define DIR_E  2
#define DIR_NE 3
#define DIR_N  4

// Same numbers as pose_id in game.h.
#define POSE_IDLE   0
#define POSE_WALK   1
#define POSE_RUN    2
#define POSE_JUMP   3
#define POSE_SIT    4
#define POSE_LIE    5
#define POSE_WAVE   6
#define POSE_SHAKE  7
#define POSE_HOLD_L 8
#define POSE_HOLD_R 9
#define POSE_HOLD_B 10
#define POSE_PUNCH  11
#define POSE_KICK   12
#define POSE_LEAP_PUNCH 13
#define POSE_LEAP_KICK  14

#define GROUND -0.080
#define HEAD_R 0.010
// Where the hands are, from the body centre. game.h spaces people by these
// (hold_spacing, shake_distance): keep them in step.
#define HOLD_REACH  0.026
#define SHAKE_REACH 0.022

#define TAU 6.2831853

// ---- the body's one colour slot --------------------------------------------
struct W {
  vec3 a; // cloth
  vec3 b; // shoe (unused, one colour for the whole person)
};
W w_mix(W x, W y, float t) { return W(mix(x.a, y.a, t), mix(x.b, y.b, t)); }
W w_cloth() { return W(vec3(1, 0, 0), vec3(0)); }

// ---- distance functions (from the reference sample) ----------------------
float smin(float a, float b, float k) {
  float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
  return mix(b, a, h) - k * h * (1.0 - h);
}

float sdCircle(vec2 p, float r) { return length(p) - r; }

float sdTaperedCapsule(vec2 p, vec2 a, vec2 b, float ra, float rb) {
  vec2 pa = p - a, ba = b - a;
  float l2 = dot(ba, ba);
  float rr = ra - rb;
  float h = clamp((dot(pa, ba) + rr * ra) / (l2 + rr * rr), 0.0, 1.0);
  return length(pa - ba * h) - mix(ra, rb, h);
}

// Quadratic bezier with a radius that tapers from rA to rB.
float sdBezier(vec2 pos, vec2 A, vec2 B, vec2 C, float rA, float rB) {
  vec2 a = B - A;
  vec2 b = A - 2.0 * B + C;
  vec2 c = a * 2.0;
  vec2 d = A - pos;

  float kk = 1.0 / max(dot(b, b), 0.00001);
  float kx = kk * dot(a, b);
  float ky = kk * (2.0 * dot(a, a) + dot(d, b)) / 3.0;
  float kz = kk * dot(d, a);

  float p = -kx * kx + ky;
  float p3 = p * p * p;
  float q = kx * (2.0 * kx * kx - 3.0 * ky) + kz;
  float h = q * q + 4.0 * p3;

  if (h >= 0.0) {
    h = sqrt(h);
    vec2 x = (vec2(h, -h) - q) / 2.0;
    vec2 uv = sign(x) * pow(abs(x), vec2(1.0 / 3.0));
    float t = clamp(uv.x + uv.y - kx, 0.0, 1.0);
    vec2 qpos = d + (c + b * t) * t;
    return length(qpos) - mix(rA, rB, t);
  }

  float z = sqrt(max(-p, 0.0));
  float v = acos(clamp(q / (p * z * 2.0), -1.0, 1.0)) / 3.0;
  float m = cos(v);
  float n = sin(v) * 1.732050808;
  vec3 t = clamp(vec3(m + m, -n - m, n - m) * z - kx, 0.0, 1.0);

  vec2 qpos = d + (c + b * t.x) * t.x;
  float dsq = dot(qpos, qpos);
  float r = mix(rA, rB, t.x);

  vec2 qpos2 = d + (c + b * t.y) * t.y;
  float dsq2 = dot(qpos2, qpos2);
  if (dsq2 < dsq) { dsq = dsq2; r = mix(rA, rB, t.y); }

  vec2 qpos3 = d + (c + b * t.z) * t.z;
  float dsq3 = dot(qpos3, qpos3);
  if (dsq3 < dsq) { dsq = dsq3; r = mix(rA, rB, t.z); }

  return sqrt(dsq) - r;
}

// Thigh to ankle in one bezier stroke, plus the foot.
float sdSeamlessLeg(vec2 p, vec2 hip, vec2 knee, vec2 ankle, vec2 heel, vec2 toe,
                    float rThigh, float rAnkle, float rToe, inout float isFoot) {
  vec2 ctrl = 2.0 * knee - 0.5 * (hip + ankle);
  float dLeg = sdBezier(p, hip, ctrl, ankle, rThigh, rAnkle);
  float dFoot = sdTaperedCapsule(p, heel, toe, rAnkle * 1.15, rToe);
  float dTotal = smin(dLeg, dFoot, 0.005);
  if (dFoot < dLeg) isFoot = 1.0;
  return dTotal;
}

// Shoulder to arm tip in one tapered bezier stroke, no hand.
float sdSeamlessArm(vec2 p, vec2 shoulder, vec2 elbow, vec2 armTip, float rShoulder, float rTip) {
  vec2 ctrl = 2.0 * elbow - 0.5 * (shoulder + armTip);
  return sdBezier(p, shoulder, ctrl, armTip, rShoulder, rTip);
}

// ---- skeleton --------------------------------------------------------------
struct Pose {
  vec2 hip, shoulder, head;
  vec2 shL, shR, hipL, hipR;
  vec2 kneeL, kneeR, ankleL, ankleR;
  vec2 heelL, heelR, toeL, toeR; // offsets from the ankle
  vec2 elbL, elbR, tipL, tipR;
  float rSh, rHip;
  float capUp, capDown; // torso ends pushed out so a narrower torso keeps its height
};

// Direction the feet step towards, as seen on screen (y up). Front and back
// views are foreshortened.
vec2 forwardOf(int dir) {
  if (dir == DIR_S)  return vec2(0.0, -0.35);
  if (dir == DIR_SE) return vec2(0.8, -0.25);
  if (dir == DIR_E)  return vec2(1.0, 0.0);
  if (dir == DIR_NE) return vec2(0.8, 0.25);
  return vec2(0.0, 0.35);
}

// The standing figure of the reference sample, hips moved by `hipOff` (the
// feet stay on the ground).
Pose basePose(int dir, float bT, vec2 hipOff) {
  Pose P;
  float rShoulder = 0.013 * bT; // both ends of the torso capsule use this radius
  P.hip = vec2(0.0, -0.020) + hipOff;
  P.shoulder = P.hip + vec2(0.0, 0.024);
  P.head = P.shoulder + vec2(0.0, 0.013 + 0.008 + HEAD_R);
  float g = GROUND + 0.004;

  // The torso is wide from the front, V-shaped, and narrow in profile
  // (chest depth), with the three-quarter views in between.
  if (dir == DIR_S || dir == DIR_N) {
    P.rSh = rShoulder * 1.32;
    P.rHip = P.rSh;
    P.shL = P.shoulder + vec2(-P.rSh * 0.72, 0.0);
    P.shR = P.shoulder + vec2( P.rSh * 0.72, 0.0);
    P.hipL = P.hip + vec2(-P.rHip * 0.55, 0.0);
    P.hipR = P.hip + vec2( P.rHip * 0.55, 0.0);
    P.kneeL = P.hipL + vec2(-0.002, -0.026);
    P.ankleL = vec2(P.hipL.x - 0.002, g);
    P.heelL = vec2(0.0, -0.001);
    P.toeL = vec2(-0.0045, -0.0035);
    P.kneeR = P.hipR + vec2(0.002, -0.026);
    P.ankleR = vec2(P.hipR.x + 0.002, g);
    P.heelR = vec2(0.0, -0.001);
    P.toeR = vec2(0.0045, -0.0035);
    P.elbL = P.shL + vec2(-0.005, -0.015);
    P.tipL = P.shL + vec2(-0.003, -0.028);
    P.elbR = P.shR + vec2(0.005, -0.015);
    P.tipR = P.shR + vec2(0.003, -0.028);
  } else if (dir == DIR_E) {
    P.rSh = rShoulder * 0.72;
    P.rHip = P.rSh;
    P.shL = P.shoulder + vec2(-0.002, 0.0);
    P.shR = P.shoulder + vec2( 0.002, 0.0);
    P.hipL = P.hip + vec2(-0.003, 0.0);
    P.hipR = P.hip + vec2( 0.003, 0.0);
    P.kneeL = P.hipL + vec2(0.001, -0.028);
    P.ankleL = vec2(-0.005, g);
    P.heelL = vec2(-0.0045, -0.001);
    P.toeL = vec2(0.0135, -0.0035);
    P.kneeR = P.hipR + vec2(0.0045, -0.027);
    P.ankleR = vec2(0.003, g);
    P.heelR = vec2(-0.0045, -0.001);
    P.toeR = vec2(0.0155, -0.0035);
    P.elbL = P.shL + vec2(-0.004, -0.015);
    P.tipL = P.shL + vec2(0.001, -0.028);
    P.elbR = P.shR + vec2(-0.002, -0.015);
    P.tipR = P.shR + vec2(0.004, -0.028);
  } else if (dir == DIR_SE) {
    P.rSh = rShoulder * 1.02;
    P.rHip = P.rSh;
    P.shL = P.shoulder + vec2(-P.rSh * 0.60, 0.0);
    P.shR = P.shoulder + vec2( P.rSh * 0.75, 0.0);
    P.hipL = P.hip + vec2(-P.rHip * 0.45, 0.0);
    P.hipR = P.hip + vec2( P.rHip * 0.65, 0.0);
    P.kneeL = P.hipL + vec2(0.001, -0.027);
    P.ankleL = vec2(P.hipL.x - 0.003, g);
    P.heelL = vec2(-0.003, -0.001);
    P.toeL = vec2(0.0095, -0.0035);
    P.kneeR = P.hipR + vec2(0.004, -0.027);
    P.ankleR = vec2(P.hipR.x + 0.002, g);
    P.heelR = vec2(-0.003, -0.001);
    P.toeR = vec2(0.0135, -0.0035);
    P.elbL = P.shL + vec2(-0.004, -0.015);
    P.tipL = P.shL + vec2(-0.002, -0.028);
    P.elbR = P.shR + vec2(0.005, -0.015);
    P.tipR = P.shR + vec2(0.004, -0.028);
    P.head.x += 0.002;
  } else {
    P.rSh = rShoulder * 1.02;
    P.rHip = P.rSh;
    P.shL = P.shoulder + vec2(-P.rSh * 0.75, 0.0);
    P.shR = P.shoulder + vec2( P.rSh * 0.60, 0.0);
    P.hipL = P.hip + vec2(-P.rHip * 0.65, 0.0);
    P.hipR = P.hip + vec2( P.rHip * 0.45, 0.0);
    P.kneeL = P.hipL + vec2(0.0025, -0.027);
    P.ankleL = vec2(P.hipL.x - 0.002, g);
    P.heelL = vec2(-0.0035, -0.001);
    P.toeL = vec2(0.0115, -0.0035);
    P.kneeR = P.hipR + vec2(0.0015, -0.027);
    P.ankleR = vec2(P.hipR.x + 0.002, g);
    P.heelR = vec2(-0.0035, -0.001);
    P.toeR = vec2(0.0090, -0.0035);
    P.elbL = P.shL + vec2(-0.005, -0.015);
    P.tipL = P.shL + vec2(-0.003, -0.028);
    P.elbR = P.shR + vec2(0.004, -0.015);
    P.tipR = P.shR + vec2(0.002, -0.028);
    P.head.x += 0.001;
  }
  // Both ends push out by the same amount (rSh == rHip here), so the torso's
  // height stays the same across every direction's radius.
  P.capUp = rShoulder * 1.32 - P.rSh;
  P.capDown = P.capUp;
  return P;
}

// Legs half a cycle apart: each foot moves along `fwd` and lifts on its way
// forward; the knee follows and bends.
void stepLegs(inout Pose P, vec2 fwd, float ph, float stride, float lift, float kneeBend) {
  float s = sin(ph);
  float lL = max(0.0, cos(ph)), lR = max(0.0, -cos(ph));
  vec2 dL = fwd * stride * s + vec2(0.0, lift * lL);
  vec2 dR = -fwd * stride * s + vec2(0.0, lift * lR);
  P.ankleL += dL;
  P.ankleR += dR;
  P.kneeL += dL * 0.5 + vec2(fwd.x * kneeBend * lL, 0.0);
  P.kneeR += dR * 0.5 + vec2(fwd.x * kneeBend * lR, 0.0);
}

// Each arm swings with the opposite leg.
void swingArmL(inout Pose P, vec2 fwd, float ph, float amp) {
  P.tipL -= fwd * amp * sin(ph);
  P.elbL -= fwd * amp * 0.5 * sin(ph);
}
void swingArmR(inout Pose P, vec2 fwd, float ph, float amp) {
  P.tipR += fwd * amp * sin(ph);
  P.elbR += fwd * amp * 0.5 * sin(ph);
}

// Upper body shifted sideways (leaning into a run).
void leanBody(inout Pose P, float dx) {
  vec2 d = vec2(dx, 0.0);
  P.shoulder += d; P.head += d; P.shL += d; P.shR += d;
  P.elbL += d; P.elbR += d; P.tipL += d; P.tipR += d;
}

// Fists up by the chin, elbows down. Seen from the front or back the fists are
// beside the face, where the torso does not hide them.
void guardArms(inout Pose P, vec2 fwd, bool fb) {
  if (fb) {
    P.elbL = P.shL + vec2(-0.007, -0.006);
    P.tipL = P.shL + vec2(-0.004,  0.016);
    P.elbR = P.shR + vec2( 0.007, -0.006);
    P.tipR = P.shR + vec2( 0.004,  0.016);
  } else {
    P.elbL = P.shL + fwd * 0.003 + vec2(0.0, -0.010);
    P.tipL = P.shL + fwd * 0.011 + vec2(0.0,  0.011);
    P.elbR = P.shR + fwd * 0.002 + vec2(0.0, -0.011);
    P.tipR = P.shR + fwd * 0.008 + vec2(0.0,  0.009);
  }
}

Pose makePose(int dir, int pose, int frame, int frames, float bT) {
  vec2 fwd = forwardOf(dir);
  bool fb = dir == DIR_S || dir == DIR_N; // front or back view
  bool holding = pose >= POSE_HOLD_L && pose <= POSE_HOLD_B;
  bool fighting = pose == POSE_PUNCH || pose == POSE_KICK;
  bool leaping = pose == POSE_LEAP_PUNCH || pose == POSE_LEAP_KICK;
  bool walking = pose == POSE_WALK || (holding && frame > 0);
  float ph = TAU * float(frame) / float(frames);
  if (holding) ph = TAU * float(frame - 1) / 6.0; // frame 0 stands, 1..6 walk

  vec2 hipOff = vec2(0.0);
  if (pose == POSE_IDLE) hipOff.y = 0.0007 * sin(ph); // breathing
  if (walking) hipOff.y = 0.0012 * (1.0 - abs(sin(ph)));
  if (pose == POSE_RUN) hipOff.y = 0.0028 * abs(cos(ph)) - 0.0022;
  if (pose == POSE_JUMP) hipOff.y = frame == 0 ? -0.007 : (frame == 1 ? 0.003 : (frame == 2 ? 0.002 : -0.006));
  if (pose == POSE_SIT) hipOff.y = (GROUND + 0.009) + 0.020 + 0.0005 * float(frame);
  if (fighting) hipOff.y = -0.0015; // knees soft
  if (leaping) hipOff.y = frame == 0 ? -0.007 : (frame == 3 ? -0.006 : 0.0);

  Pose P = basePose(dir, bT, hipOff);

  if (pose == POSE_IDLE) {
    float s = 0.0007 * sin(ph);
    P.tipL.x -= s;
    P.tipR.x += s;
  }

  if (walking) {
    stepLegs(P, fwd, ph, 0.009, 0.004, 0.004);
    if (pose != POSE_HOLD_R && pose != POSE_HOLD_B) swingArmL(P, fwd, ph, 0.006);
    if (pose != POSE_HOLD_L && pose != POSE_HOLD_B) swingArmR(P, fwd, ph, 0.006);
  }

  if (pose == POSE_RUN) {
    leanBody(P, fwd.x * 0.004);
    stepLegs(P, fwd, ph, 0.015, 0.009, 0.008);
    // Bent arms pumping against the legs.
    float s = sin(ph);
    vec2 outL = fb ? vec2(-0.004, 0.0) : vec2(0.0);
    vec2 inL = fb ? vec2(0.002, 0.0) : vec2(0.0);
    P.elbL = P.shL + vec2(0.0, -0.011) - fwd * 0.006 * s + outL;
    P.tipL = P.elbL + fwd * 0.007 + vec2(0.0, 0.005) - fwd * 0.003 * s + inL;
    P.elbR = P.shR + vec2(0.0, -0.011) + fwd * 0.006 * s - outL;
    P.tipR = P.elbR + fwd * 0.007 + vec2(0.0, 0.005) + fwd * 0.003 * s - inL;
  }

  if (pose == POSE_JUMP) {
    // Knees bend forward, or apart when seen from the front or back.
    vec2 kL = fb ? vec2(-0.004, 0.002) : vec2(fwd.x * 0.007, 0.002);
    vec2 kR = fb ? vec2( 0.004, 0.002) : vec2(fwd.x * 0.007, 0.002);
    if (frame == 0) { // crouch, arms back
      P.kneeL += kL; P.kneeR += kR;
      P.tipL += -fwd * 0.006 + (fb ? vec2(-0.003, 0.003) : vec2(0.0, 0.002));
      P.tipR += -fwd * 0.006 + (fb ? vec2( 0.003, 0.003) : vec2(0.0, 0.002));
    } else if (frame == 1) { // take off: stretched, arms up
      P.ankleL.y += 0.003; P.ankleR.y += 0.003;
      P.tipL = P.shL + (fb ? vec2(-0.005, 0.025) : fwd * 0.004 + vec2(-0.002, 0.025));
      P.tipR = P.shR + (fb ? vec2( 0.005, 0.025) : fwd * 0.004 + vec2( 0.002, 0.025));
      P.elbL = mix(P.shL, P.tipL, 0.5) + vec2(-0.003, 0.0);
      P.elbR = mix(P.shR, P.tipR, 0.5) + vec2( 0.003, 0.0);
    } else if (frame == 2) { // in the air: knees tucked, arms out
      vec2 up = vec2(0.0, 0.011) - fwd * 0.004;
      P.ankleL += up; P.ankleR += up;
      P.kneeL += vec2(0.0, 0.010) + kL * 1.2;
      P.kneeR += vec2(0.0, 0.010) + kR * 1.2;
      P.tipL = P.shL + (fb ? vec2(-0.014, -0.004) : fwd * 0.010 + vec2(0.0, -0.002));
      P.tipR = P.shR + (fb ? vec2( 0.014, -0.004) : fwd * 0.012 + vec2(0.0, -0.002));
      P.elbL = mix(P.shL, P.tipL, 0.5) + vec2(0.0, -0.002);
      P.elbR = mix(P.shR, P.tipR, 0.5) + vec2(0.0, -0.002);
    } else { // landing: crouch, arms out for balance
      P.kneeL += kL * 0.8; P.kneeR += kR * 0.8;
      P.tipL = P.shL + (fb ? vec2(-0.012, -0.014) : fwd * 0.011 + vec2(0.0, -0.012));
      P.tipR = P.shR + (fb ? vec2( 0.012, -0.014) : fwd * 0.012 + vec2(0.0, -0.012));
      P.elbL = mix(P.shL, P.tipL, 0.5) + vec2(0.0, -0.002);
      P.elbR = mix(P.shR, P.tipR, 0.5) + vec2(0.0, -0.002);
    }
  }

  if (pose == POSE_SIT) {
    // On the ground, knees up, hands on the knees.
    float g = GROUND + 0.004;
    if (dir == DIR_S) {
      // Knees apart and up, feet together in front.
      P.kneeL = P.hipL + vec2(-0.009, 0.006);
      P.kneeR = P.hipR + vec2( 0.009, 0.006);
      P.ankleL = vec2(P.hipL.x - 0.001, g - 0.005);
      P.ankleR = vec2(P.hipR.x + 0.001, g - 0.005);
    } else if (dir == DIR_N) {
      P.kneeL = P.hipL + vec2(-0.006, 0.004);
      P.kneeR = P.hipR + vec2( 0.006, 0.004);
      P.ankleL = P.hipL + vec2(-0.007, -0.002);
      P.ankleR = P.hipR + vec2( 0.007, -0.002);
    } else {
      P.kneeL = P.hipL + vec2(fwd.x * 0.012, 0.010 + fwd.y * 0.012);
      P.kneeR = P.hipR + vec2(fwd.x * 0.013, 0.011 + fwd.y * 0.012);
      P.ankleL = vec2(P.hipL.x + fwd.x * 0.020, g + fwd.y * 0.012);
      P.ankleR = vec2(P.hipR.x + fwd.x * 0.022, g + fwd.y * 0.012);
    }
    P.tipL = P.kneeL + vec2(0.0, -0.002);
    P.tipR = P.kneeR + vec2(0.0, -0.002);
    P.elbL = mix(P.shL, P.tipL, 0.5) + (fb ? vec2(-0.004, -0.002) : vec2(-fwd.x * 0.003, -0.003));
    P.elbR = mix(P.shR, P.tipR, 0.5) + (fb ? vec2( 0.004, -0.002) : vec2(-fwd.x * 0.003, -0.003));
  }

  if (pose == POSE_LIE) {
    // Baked standing, front view; crowd.vs lays it on the ground.
    float b = 0.0006 * float(frame);
    P.elbL = P.shL + vec2(-0.006, -0.012);
    P.tipL = P.shL + vec2(-0.011, -0.024 + b);
    P.elbR = P.shR + vec2( 0.006, -0.012);
    P.tipR = P.shR + vec2( 0.011, -0.024 + b);
    P.kneeL.x -= 0.002; P.ankleL.x -= 0.004;
    P.kneeR.x += 0.002; P.ankleR.x += 0.004;
  }

  if (pose == POSE_WAVE) {
    // Near (right) hand raised beside the head, waving.
    float s = sin(ph);
    P.elbR = P.shR + (fb ? vec2(0.008, 0.007) : vec2(0.006, 0.006));
    P.tipR = P.shR + vec2(0.011 + 0.004 * s, 0.021);
  }

  if (pose == POSE_SHAKE) {
    // Near hand forward at waist height, pumping up and down.
    P.elbR = P.shR + vec2(0.008, -0.013);
    P.tipR = vec2(SHAKE_REACH, P.shR.y - 0.012 + 0.0022 * sin(ph));
  }

  if (holding) {
    // Hands out to the neighbours, at hip height.
    if (pose != POSE_HOLD_R) {
      P.tipL = vec2(-HOLD_REACH, P.hip.y + 0.001);
      P.elbL = mix(P.shL, P.tipL, 0.5) + vec2(-0.001, -0.002);
    }
    if (pose != POSE_HOLD_L) {
      P.tipR = vec2(HOLD_REACH, P.hip.y + 0.001);
      P.elbR = mix(P.shR, P.tipR, 0.5) + vec2(0.001, -0.002);
    }
  }

  if (fighting) {
    // Stance: feet apart (the near one forward), knees bent, guard up.
    vec2 spread = fb ? vec2(0.002, 0.0) : fwd * 0.004;
    vec2 bend = vec2(fwd.x * 0.003, 0.0);
    P.ankleL -= spread;
    P.ankleR += spread;
    P.kneeL += bend - spread * 0.5;
    P.kneeR += bend + spread * 0.5;
    guardArms(P, fwd, fb);
  }

  if (pose == POSE_PUNCH && (frame == 1 || frame == 3)) {
    // Frame 1 a jab with the far fist, 3 a cross with the near one, at chin
    // height. Front and back views punch out to the side so the arm shows.
    bool cross = frame == 3;
    if (fb) {
      if (cross) { P.tipR = P.shR + vec2(0.018, 0.009); P.elbR = P.shR + vec2(0.009, 0.001); }
      else       { P.tipL = P.shL + vec2(-0.018, 0.009); P.elbL = P.shL + vec2(-0.009, 0.001); }
    } else {
      leanBody(P, fwd.x * (cross ? 0.003 : 0.0015));
      vec2 reach = fwd * 0.026 + vec2(0.0, 0.007);
      if (cross) { P.tipR = P.shR + reach; P.elbR = mix(P.shR, P.tipR, 0.5) + vec2(0.0, -0.001); }
      else       { P.tipL = P.shL + reach; P.elbL = mix(P.shL, P.tipL, 0.5) + vec2(0.0, -0.001); }
    }
  }

  if (pose == POSE_KICK && frame > 0) {
    // Frames 1 and 3 draw the near knee up, 2 kicks out at hip height while
    // the body leans away. Front and back views kick out to the side.
    if (frame == 2) {
      leanBody(P, fb ? -0.004 : -fwd.x * 0.005);
      P.ankleR = P.hipR + (fb ? vec2(0.034, 0.008) : fwd * 0.040 + vec2(0.0, 0.014));
      P.kneeR = mix(P.hipR, P.ankleR, 0.5) + vec2(0.0, 0.001);
      P.heelR = vec2(fb ? 0.0 : fwd.x * 0.001, -0.002);
      P.toeR = vec2(fb ? 0.004 : fwd.x * 0.006, 0.006); // sole first
    } else {
      P.kneeR = P.hipR + (fb ? vec2(0.010, 0.004) : fwd * 0.014 + vec2(0.0, 0.002));
      P.ankleR = P.hipR + (fb ? vec2(0.006, -0.018) : fwd * 0.004 + vec2(0.0, -0.017));
      P.heelR = vec2(fb ? 0.0 : -fwd.x * 0.003, 0.0);
      P.toeR = vec2(fb ? 0.004 : fwd.x * 0.009, -0.004);
    }
  }

  if (leaping) {
    // 0 crouch, 1 take off with the legs tucked, 2 strike in the air, 3 land.
    // The feet leave the ground only through the lift crowd.vs adds.
    bool kick = pose == POSE_LEAP_KICK;
    guardArms(P, fwd, fb);
    if (frame == 0 || frame == 3) {
      vec2 kL = fb ? vec2(-0.004, 0.002) : vec2(fwd.x * 0.007, 0.002);
      vec2 kR = fb ? vec2( 0.004, 0.002) : vec2(fwd.x * 0.007, 0.002);
      P.kneeL += kL;
      P.kneeR += kR;
    } else if (fb) {
      // Knees apart and up.
      P.kneeL = P.hipL + vec2(-0.010, -0.012);
      P.ankleL = P.hipL + vec2(-0.004, -0.028);
      P.kneeR = P.hipR + vec2( 0.010, -0.012);
      P.ankleR = P.hipR + vec2( 0.004, -0.028);
    } else {
      // Knees forward, feet under the hips.
      P.kneeL = P.hipL + fwd * 0.016 + vec2(0.0, -0.010);
      P.ankleL = P.hipL + fwd * 0.002 + vec2(0.0, -0.030);
      P.kneeR = P.hipR + fwd * 0.020 + vec2(0.0, -0.006);
      P.ankleR = P.hipR + fwd * 0.008 + vec2(0.0, -0.026);
    }

    if (frame == 1 && !fb) {
      if (kick) { // near knee drawn up high
        P.kneeR = P.hipR + fwd * 0.018 + vec2(0.0, 0.004);
        P.ankleR = P.hipR + fwd * 0.010 + vec2(0.0, -0.016);
      } else { // near fist cocked back
        P.tipR = P.shR - fwd * 0.004 + vec2(0.0, 0.010);
        P.elbR = P.shR - fwd * 0.008 + vec2(0.0, -0.004);
      }
    }

    if (frame == 2 && kick) {
      // Flying kick: near leg straight out, the body leaning away, the near
      // arm thrown back for balance.
      leanBody(P, fb ? -0.004 : -fwd.x * 0.005);
      P.ankleR = P.hipR + (fb ? vec2(0.034, 0.006) : fwd * 0.040 + vec2(0.0, 0.008));
      P.kneeR = mix(P.hipR, P.ankleR, 0.5) + vec2(0.0, 0.001);
      P.heelR = vec2(fb ? 0.0 : fwd.x * 0.001, -0.002);
      P.toeR = vec2(fb ? 0.004 : fwd.x * 0.006, 0.006);
      if (!fb) {
        P.tipR = P.shR - fwd * 0.012 + vec2(0.0, -0.010);
        P.elbR = P.shR - fwd * 0.006 + vec2(0.0, -0.006);
      }
    } else if (frame == 2) {
      // Punch from the air: near fist out, far arm and far leg trailing.
      if (fb) {
        P.tipR = P.shR + vec2(0.018, 0.009);
        P.elbR = P.shR + vec2(0.009, 0.001);
      } else {
        leanBody(P, fwd.x * 0.004);
        P.tipR = P.shR + fwd * 0.028 + vec2(0.0, 0.002);
        P.elbR = mix(P.shR, P.tipR, 0.5) + vec2(0.0, -0.001);
        P.tipL = P.shL - fwd * 0.010 + vec2(0.0, -0.012);
        P.elbL = P.shL - fwd * 0.006 + vec2(0.0, -0.006);
        P.ankleL = P.hipL - fwd * 0.016 + vec2(0.0, -0.044);
        P.kneeL = mix(P.hipL, P.ankleL, 0.5) + fwd * 0.002;
      }
    }
  }
  return P;
}

// ---- body: torso filled, legs and arms a single thin stroke -----------------
W drawBody(vec2 p, Pose P, int dir, float bL, float px) {
  // One flat colour for the whole person: legs and arms are a single stroke,
  // one radius end to end (no taper).
  float rLimb = 0.0018 * bL;
  float legK = 0.005, armK = 0.0035; // half the old blend radius: a tighter joint to the torso

  float isFootL = 0.0, isFootR = 0.0;
  float dLegL = sdSeamlessLeg(p, P.hipL, P.kneeL, P.ankleL, P.ankleL + P.heelL, P.ankleL + P.toeL,
                              rLimb, rLimb, rLimb, isFootL);
  float dLegR = sdSeamlessLeg(p, P.hipR, P.kneeR, P.ankleR, P.ankleR + P.heelR, P.ankleR + P.toeR,
                              rLimb, rLimb, rLimb, isFootR);
  float dArmL = sdSeamlessArm(p, P.shL, P.elbL, P.tipL, rLimb, rLimb);
  float dArmR = sdSeamlessArm(p, P.shR, P.elbR, P.tipR, rLimb, rLimb);

  vec2 axis = normalize(P.shoulder - P.hip);
  float dBody = sdTaperedCapsule(p, P.shoulder + axis * P.capUp, P.hip - axis * P.capDown, P.rSh, P.rHip);
  dBody = smin(dBody, dLegL, legK);
  dBody = smin(dBody, dLegR, legK);
  dBody = smin(dBody, dArmL, armK);
  dBody = smin(dBody, dArmR, armK);
  return w_mix(W(vec3(0), vec3(0)), w_cloth(), smoothstep(px, -px, dBody));
}

// ---- head: a plain circle, same one colour slot as the body ----------------
vec3 drawHead(vec2 hp, int dir, int style, float px) {
  return vec3(smoothstep(px, -px, sdCircle(hp, HEAD_R)), 0.0, 0.0);
}

void main() {
  int dir = int(v_pose.x + 0.5);
  int pose = int(v_pose.y + 0.5);
  int build = int(v_pose.z + 0.5);
  int style = int(v_pose.w + 0.5);
  float px = 0.75 / u_scale;
  vec2 q = v_local * u_cell; // pixels from the cell's top-left

  if (u_pass == 2) {
    vec2 hp = vec2(q.x - u_head_center.x, u_head_center.y - q.y) / u_scale;
    finalColor = vec4(drawHead(hp, dir, style, px), 1.0);
    return;
  }

  float bT = build == 0 ? 0.85 : (build == 2 ? 1.22 : 1.0); // torso
  float bL = build == 0 ? 0.90 : (build == 2 ? 1.15 : 1.0); // limbs
  Pose P = makePose(dir, pose, int(v_frame.x + 0.5), int(v_frame.y + 0.5), bT);

  if (u_pass == 1 && q.x < 1.0 && q.y < 1.0) {
    vec2 h = vec2(P.head.x * u_scale + 0.5 * u_cell.x, u_cell.y - (P.head.y - u_bottom) * u_scale);
    finalColor = vec4(0.0, h / u_cell, 1.0);
    return;
  }

  vec2 p = vec2(q.x - 0.5 * u_cell.x, u_cell.y - q.y) / u_scale + vec2(0.0, u_bottom);
  W w = drawBody(p, P, dir, bL, px);
  finalColor = vec4(u_pass == 0 ? w.a : w.b, 1.0);
}
