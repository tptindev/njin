#version 330

// One painted person per quad, in the plainest form the painting allows: a
// flat dab of colour for the body, an ink dot for the head, and four curved
// ink strokes for the limbs. The whole crowd is a few draw_instanced() calls;
// person.vs places each quad and hands over what draw.cpp wrote for that
// person:
//
//   v_codes  = (pose, cloth colour index)
//   v_params = (phase, extra, extra2) each 0..1, and a seed (unused here)
//
// Where the person stands, how high they are off the ground, which way they
// face and how far they are tipped over (cartwheel, handstand, lying down)
// are the quad's own position, rotation and flip, so this shader only draws a
// person standing upright facing right, in "figure units": feet on the ground
// at the origin, up is -y, one unit is one world pixel at size 1.

in vec2 v_local;
flat in vec2 v_codes;
flat in vec4 v_params;
uniform vec4 cloth[16];
out vec4 finalColor;

const float quad_units = 28.0;         // must match draw.cpp
const vec2 pivot = vec2(0.0, -10.0);   // the quad's centre, in figure units
const float hip_y = -6.5;
const float shoulder_y = -12.4;
const vec2 head_at = vec2(0.0, -16.9);
const float pi = 3.14159265;
const float tau = 6.2831853;

const vec3 ink = vec3(0.12, 0.11, 0.13);

// Pose ids, in step with draw.cpp.
const int pose_stand = 0;
const int pose_walk = 1;
const int pose_run = 2;
const int pose_wave = 3;
const int pose_jump = 4;
const int pose_jacks = 5;
const int pose_dance = 6;
const int pose_cartwheel = 7;
const int pose_handstand = 8;
const int pose_lie = 9;
const int pose_sit = 10;
const int pose_ring = 11;

float sd_circle(vec2 p, float r) { return length(p) - r; }

float sd_segment(vec2 p, vec2 a, vec2 b, float r) {
  vec2 pa = p - a, ba = b - a;
  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
  return length(pa - ba * h) - r;
}

// Two-bone limb from `root` toward `target`: bones `l1` then `l2` long. A
// target out of reach straightens the limb toward it; a nearer one folds the
// middle joint out to the side `bend` points at (knees forward, elbows out).
// Returns the joint; `target` is moved to where the limb really ends.
vec2 solve_limb(vec2 root, inout vec2 target, float l1, float l2, vec2 bend) {
  vec2 d = target - root;
  float len = max(length(d), 1e-3);
  vec2 dir = d / len;
  len = clamp(len, abs(l1 - l2) + 0.05, l1 + l2 - 0.02);
  target = root + dir * len;
  float along = (l1 * l1 - l2 * l2 + len * len) / (2.0 * len);
  float h = sqrt(max(l1 * l1 - along * along, 0.0));
  vec2 side = vec2(-dir.y, dir.x);
  if (dot(side, bend) < 0.0)
    side = -side;
  return root + dir * along + side * h;
}

// Distance to the quadratic Bezier a-b-c (b is the control point), and the
// curve parameter of the nearest point. Exact: solves the cubic for the
// closest point (after Inigo Quilez's sdBezier). A curve with b on the line
// a-c is straight and the cubic degenerates, so that case is a segment.
vec2 sd_bezier(vec2 p, vec2 a, vec2 b, vec2 c) {
  vec2 A = b - a;
  vec2 B = a - 2.0 * b + c;
  if (dot(B, B) < 1e-5) {
    vec2 ba = c - a, pa = p - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return vec2(length(pa - ba * h), h);
  }
  vec2 C = A * 2.0;
  vec2 D = a - p;
  float kk = 1.0 / dot(B, B);
  float kx = kk * dot(A, B);
  float ky = kk * (2.0 * dot(A, A) + dot(D, B)) / 3.0;
  float kz = kk * dot(D, A);
  float pp = ky - kx * kx;
  float q = kx * (2.0 * kx * kx - 3.0 * ky) + kz;
  float h = q * q + 4.0 * pp * pp * pp;
  if (h >= 0.0) {
    h = sqrt(h);
    vec2 x = (vec2(h, -h) - q) / 2.0;
    vec2 uv = sign(x) * pow(abs(x), vec2(1.0 / 3.0));
    float t = clamp(uv.x + uv.y - kx, 0.0, 1.0);
    return vec2(length(D + (C + B * t) * t), t);
  }
  float z = sqrt(-pp);
  float v = acos(q / (pp * z * 2.0)) / 3.0;
  float m = cos(v);
  float n = sin(v) * 1.732050808;
  vec2 t = clamp(vec2(m + m, -n - m) * z - kx, 0.0, 1.0);
  float d0 = length(D + (C + B * t.x) * t.x);
  float d1 = length(D + (C + B * t.y) * t.y);
  return d0 < d1 ? vec2(d0, t.x) : vec2(d1, t.y);
}

// A limb as one brush stroke from `root` (hip, shoulder) to `end` (foot,
// hand), bowed toward the knee or elbow that solve_limb found: straight when
// the limb is straight, curving more the harder it bends, with no visible
// joint. The control point overshoots the joint a little so the curve's
// apex lands most of the way to it (a quadratic only reaches halfway to its
// control point). The stroke tapers from `r_root` to `r_end`, like a brush
// lifting off the paper.
float limb(vec2 p, vec2 root, vec2 joint, vec2 end, float r_root, float r_end) {
  vec2 mid = (root + end) * 0.5;
  vec2 ctrl = mid + (joint - mid) * 1.5;
  vec2 d = sd_bezier(p, root, ctrl, end);
  return d.x - mix(r_root, r_end, d.y);
}

float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

float noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),
             mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

// Painter's algorithm inside one fragment: each shape goes over the last.
vec4 color = vec4(0.0);
float aa = 0.5;

void paint(float d, vec3 rgb) {
  float a = 1.0 - smoothstep(-aa, aa, d);
  color.rgb = rgb * a + color.rgb * (1.0 - a);
  color.a = a + color.a * (1.0 - a);
}

void main() {
  vec2 p = (v_local - 0.5) * quad_units + pivot;
  aa = max(fwidth(p.x), fwidth(p.y)) * 0.75;

  int pose = int(v_codes.x + 0.5);
  int cloth_i = int(v_codes.y + 0.5) & 15;
  float phase = v_params.x;
  float extra = v_params.y;
  float extra2 = v_params.z;

  // ---- The pose: where hands and feet go, and how the upper body moves.
  vec2 hand_l = vec2(-4.4, -5.2);
  vec2 hand_r = vec2(4.4, -5.2);
  vec2 foot_l = vec2(-1.3, 0.0);
  vec2 foot_r = vec2(1.3, 0.0);
  float sink = 0.0;  // upper body lowered: sitting, landing
  float lean = 0.0;  // upper body shifted sideways

  float s = sin(phase * tau);
  if (pose == pose_stand) {
    lean = s * 0.3;
  } else if (pose == pose_walk || pose == pose_run) {
    // extra is the heading; steps toward or away from the viewer look shorter
    // from above.
    vec2 md = vec2(cos(extra * tau), sin(extra * tau) * 0.5);
    bool run = pose == pose_run;
    float amp = run ? 3.4 : 2.1;
    foot_l = vec2(-1.3, 0.0) + md * (s * amp);
    foot_r = vec2(1.3, 0.0) - md * (s * amp);
    // The foot swinging forward is off the ground; the knee folds under it.
    float c = cos(phase * tau);
    float step_h = run ? 1.9 : 1.0;
    foot_l.y -= max(0.0, c) * step_h;
    foot_r.y -= max(0.0, -c) * step_h;
    if (run) {
      // Elbows bent hard, fists pumping near the chest.
      hand_l = vec2(-3.0, -9.6) - md * (s * 2.6);
      hand_r = vec2(3.0, -9.6) + md * (s * 2.6);
      lean = md.x * 1.2;
    } else {
      hand_l = vec2(-4.2, -5.4) - md * (s * 2.2);
      hand_r = vec2(4.2, -5.4) + md * (s * 2.2);
    }
  } else if (pose == pose_wave) {
    // Forearm up, swinging from the elbow.
    hand_r = vec2(5.4 + s * 1.8, -18.0);
  } else if (pose == pose_jump) {
    float h = sin(pi * phase);
    if (h > 0.25) {
      hand_l = vec2(-4.6, -20.5);
      hand_r = vec2(4.6, -20.5);
      foot_l = vec2(-1.9, -1.6 * h);
      foot_r = vec2(1.9, -1.6 * h);
    } else {
      // Crouched for take-off and landing: knees fold, arms swing back.
      sink = 1.8 * (1.0 - h * 4.0);
      hand_l = vec2(-4.4, -6.0);
      hand_r = vec2(4.4, -6.0);
    }
  } else if (pose == pose_jacks) {
    float open = 0.5 - 0.5 * cos(phase * tau);
    hand_l = mix(vec2(-4.4, -5.2), vec2(-5.2, -19.5), open);
    hand_r = mix(vec2(4.4, -5.2), vec2(5.2, -19.5), open);
    foot_l = vec2(-1.3 - 2.8 * open, 0.0);
    foot_r = vec2(1.3 + 2.8 * open, 0.0);
  } else if (pose == pose_dance) {
    hand_l = vec2(-4.6, -18.0 + s * 3.0);
    hand_r = vec2(4.6, -18.0 - s * 3.0);
    foot_l.y = -max(0.0, s) * 2.0;
    foot_r.y = -max(0.0, -s) * 2.0;
  } else if (pose == pose_cartwheel) {
    hand_l = vec2(-5.6, -19.0);
    hand_r = vec2(5.6, -19.0);
    foot_l = vec2(-4.6, -0.6);
    foot_r = vec2(4.6, -0.6);
  } else if (pose == pose_handstand) {
    hand_l = vec2(-2.8, -20.0);
    hand_r = vec2(2.8, -20.0);
    foot_l = vec2(-1.6 - s * 1.2, 0.0);
    foot_r = vec2(1.6 + s * 1.2, 0.0);
  } else if (pose == pose_lie) {
    // Flat on the ground, slowly making a snow angel: nearly straight arms
    // sweep from along the body to over the head, the legs open and close.
    float k = 0.5 + 0.5 * s;
    vec2 arm = normalize(mix(vec2(-0.35, 1.0), vec2(-0.55, -1.0), k)) * 7.6;
    hand_l = vec2(-2.0, shoulder_y + 0.4) + arm;
    hand_r = vec2(2.0, shoulder_y + 0.4) + vec2(-arm.x, arm.y);
    vec2 leg = normalize(mix(vec2(-0.05, 1.0), vec2(-0.55, 1.0), k)) * 6.7;
    foot_l = vec2(-1.2, hip_y) + leg;
    foot_r = vec2(1.2, hip_y) + vec2(-leg.x, leg.y);
  } else if (pose == pose_sit) {
    sink = 5.0;
    foot_l = vec2(5.6, -2.2);
    foot_r = vec2(5.2, 0.4);
    hand_l = vec2(-3.4, -1.5);
    hand_r = vec2(-1.6, -1.2);
  } else if (pose == pose_ring) {
    // Reaching for the neighbours: extra and extra2 are the directions of the
    // left and right hand from their shoulders, which draw.cpp worked out
    // from where the neighbours stand so the two hands meet halfway.
    hand_l = vec2(-2.1, shoulder_y) + vec2(cos(extra * tau), sin(extra * tau)) * 6.4;
    hand_r = vec2(2.1, shoulder_y) + vec2(cos(extra2 * tau), sin(extra2 * tau)) * 6.4;
  }
  vec2 up = vec2(lean, sink); // upper-body points ride the sink and the lean

  // ---- Limbs: solve_limb still works out a knee or elbow from two bone
  // lengths, but only to decide how much each stroke curves and which way;
  // limb() draws the arm or leg as one line bowed toward it. The bones are a
  // touch longer than hip-to-ground and shoulder-to-hip, so standing still
  // leaves a slight curve rather than a ruler-straight stick.
  vec2 hip_l = vec2(-1.2, hip_y) + up, hip_r = vec2(1.2, hip_y) + up;
  vec2 sh_l = vec2(-2.0, shoulder_y + 0.4) + up, sh_r = vec2(2.0, shoulder_y + 0.4) + up;
  hand_l += up;
  hand_r += up;
  vec2 knee_l = solve_limb(hip_l, foot_l, 3.45, 3.45, vec2(1.0, -0.3));
  vec2 knee_r = solve_limb(hip_r, foot_r, 3.45, 3.45, vec2(1.0, -0.3));
  vec2 elbow_l = solve_limb(sh_l, hand_l, 4.1, 3.9, vec2(-1.0, 0.4));
  vec2 elbow_r = solve_limb(sh_r, hand_r, 4.1, 3.9, vec2(1.0, 0.4));

  // Each limb is a single curved line; the two legs (and the two arms) join
  // with a plain min, so a stride that crosses them never webs them together.
  float legs = min(limb(p, hip_l, knee_l, foot_l, 0.72, 0.6),
                   limb(p, hip_r, knee_r, foot_r, 0.72, 0.6));
  float arms = min(limb(p, sh_l, elbow_l, hand_l, 0.58, 0.46),
                   limb(p, sh_r, elbow_r, hand_r, 0.58, 0.46));
  // ---- Paper: every piece of the figure is cut from coloured paper. Two
  // octaves of noise, seeded per person so no two sheets match: `coarse`
  // nudges the cut edges so they are not vector-smooth, `fibre` is the
  // mottling of the sheet's surface.
  float seed = v_params.w * 91.0;
  float coarse = noise(p * 1.4 + seed);
  float fibre = noise(p * 4.6 - seed);
  float grain = 0.9 + 0.16 * fibre;
  vec3 ink_paper = ink * (0.85 + 0.35 * fibre);
  paint(legs, ink_paper);
  paint(arms, ink_paper);

  // ---- Body and head: a cut-out of coloured paper, and a dot of dark paper.
  // A slightly larger pale cut goes first: the white core of the sheet that
  // shows along a cut edge.
  vec3 sheet = cloth[cloth_i].rgb;
  float body = sd_segment(p - up, vec2(0.0, shoulder_y + 1.2), vec2(0.0, hip_y - 0.9), 2.5)
             + (coarse - 0.5) * 0.45;
  paint(body - 0.3, mix(sheet, vec3(0.97, 0.96, 0.93), 0.6));
  paint(body, sheet * grain);
  float head = sd_circle(p - up - head_at, 2.4) + (coarse - 0.5) * 0.3;
  paint(head, ink_paper);

  if (color.a < 0.004)
    discard;
  finalColor = vec4(color.rgb / color.a, color.a);
}
