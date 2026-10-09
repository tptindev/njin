#include "mocap.h"
#include "njin_anim3d.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace anim_editor {
namespace {
// Quaternions as (x, y, z, w); qmul(a, b) turns by b, then by a.
Quaternion qmul(Quaternion a, Quaternion b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
Quaternion qinv(Quaternion q) { return {-q.x, -q.y, -q.z, q.w}; }
Vector3 qrot(Quaternion q, Vector3 v) {
  const Quaternion r = qmul(qmul(q, {v.x, v.y, v.z, 0}), qinv(q));
  return {r.x, r.y, r.z};
}
Vector3 unit(Vector3 v) {
  const float l = Vector3Length(v);
  return l > 1e-9f ? Vector3Scale(v, 1 / l) : Vector3{0, 1, 0};
}
// The shortest turn taking direction a to direction b.
Quaternion arc(Vector3 a, Vector3 b) {
  a = unit(a);
  b = unit(b);
  const float d = Vector3DotProduct(a, b);
  if (d > 0.999999f)
    return {0, 0, 0, 1};
  if (d < -0.999999f) {
    Vector3 axis = Vector3CrossProduct({1, 0, 0}, a);
    if (Vector3Length(axis) < 1e-4f)
      axis = Vector3CrossProduct({0, 0, 1}, a);
    axis = unit(axis);
    return {axis.x, axis.y, axis.z, 0};
  }
  const Vector3 c = Vector3CrossProduct(a, b);
  return QuaternionNormalize({c.x, c.y, c.z, 1 + d});
}
// The turn taking +Y to `up` and +X to `side` (made square to up).
Quaternion frame(Vector3 up, Vector3 side) {
  up = unit(up);
  const Quaternion q1 = arc({0, 1, 0}, up);
  const Vector3 s1 = qrot(q1, {1, 0, 0});
  Vector3 s = Vector3Subtract(side, Vector3Scale(up, Vector3DotProduct(side, up)));
  if (Vector3Length(s) < 1e-6f)
    return q1;
  return QuaternionNormalize(qmul(arc(s1, s), q1));
}
Quaternion slerp(Quaternion a, Quaternion b, float t) { return QuaternionSlerp(a, b, t); }
Vector3 pos(const Matrix &m) { return {m.m12, m.m13, m.m14}; }
Quaternion rot(const Matrix &m) {
  Vector3 t{}, s{};
  Quaternion q{};
  MatrixDecompose(m, &t, &q, &s);
  return QuaternionNormalize(q);
}
Vector3 mid(Vector3 a, Vector3 b) { return Vector3Scale(Vector3Add(a, b), 0.5f); }

const char *const finger_names[5] = {"thumb", "index", "middle", "ring", "little"};
// MediaPipe hand landmarks: the first point of each finger (thumb 1..4, index 5..8 ...).
constexpr int finger_first[5] = {1, 5, 9, 13, 17};

constexpr int mp_pairs[][2] = {{1, 4},   {2, 5},   {3, 6},   {7, 8},   {9, 10},  {11, 12}, {13, 14}, {15, 16},
                               {17, 18}, {19, 20}, {21, 22}, {23, 24}, {25, 26}, {27, 28}, {29, 30}, {31, 32}};

struct Limb {
  Slot slot, child;
  int from, to; // MediaPipe pose points
};
// Left points are the person's left (MediaPipe 11, 13, 15... are left).
constexpr Limb limbs[] = {
    {l_upper_arm, l_lower_arm, 11, 13}, {l_lower_arm, l_hand, 13, 15},   {r_upper_arm, r_lower_arm, 12, 14},
    {r_lower_arm, r_hand, 14, 16},      {l_upper_leg, l_lower_leg, 23, 25}, {l_lower_leg, l_foot, 25, 27},
    {r_upper_leg, r_lower_leg, 24, 26}, {r_lower_leg, r_foot, 26, 28},   {l_foot, l_toes, 27, 31},
    {r_foot, r_toes, 28, 32}};

int child_of(Slot s) {
  for (const Limb &l : limbs)
    if (l.slot == s)
      return l.child;
  if (s == l_hand)
    return l_finger + 2 * 3; // middle_1
  if (s == r_hand)
    return r_finger + 2 * 3;
  if (s >= l_finger && s < slot_count) {
    const int base = s >= r_finger ? r_finger : l_finger, k = s - base;
    return k % 3 < 2 ? s + 1 : -1;
  }
  return -1;
}

// World pose of every bone, recomputed only after a bone changed.
struct Posed {
  const Rig &rig;
  std::vector<BonePose> &pose;
  std::vector<Matrix> world;
  bool dirty = true;
  const std::vector<Matrix> &get() {
    if (dirty) {
      world = bone_world(rig, &pose);
      dirty = false;
    }
    return world;
  }
  // Gives `bone` the world rotation `q`, keeping its position and scale.
  void set_world_rotation(int bone, Quaternion q) {
    const Matrix &w = get()[bone];
    Vector3 t{}, s{};
    Quaternion old{};
    MatrixDecompose(w, &t, &old, &s);
    const Matrix wanted = MatrixMultiply(MatrixMultiply(MatrixScale(s.x, s.y, s.z), QuaternionToMatrix(q)),
                                         MatrixTranslate(t.x, t.y, t.z));
    const Matrix local = MatrixMultiply(wanted, MatrixInvert(parent_world(rig, bone, &pose)));
    Vector3 lt{}, ls{};
    Quaternion lq{};
    MatrixDecompose(local, &lt, &lq, &ls);
    pose[bone].rotation = QuaternionNormalize(QuaternionMultiply(QuaternionInvert(rig.bones[bone].rest_rotation), lq));
    dirty = true;
  }
  // Where the bone points: towards its child slot, or along the line from its
  // parent when it has none (a finger tip, a hand without fingers).
  Vector3 aim(const MocapRig &m, int slot) {
    const int b = m.bone[slot];
    const int c = child_of((Slot)slot);
    if (c >= 0 && m.bone[c] >= 0)
      return Vector3Subtract(pos(get()[m.bone[c]]), pos(get()[b]));
    const Bone &bone = rig.bones[b];
    return qrot(rot(get()[b]), qrot(qinv(bone.rest_rotation), unit(bone.rest_translation)));
  }
};

// Rest-pose references, in the rig's world.
struct RestRefs {
  Vector3 up, hip_side, shoulder_side, head_up;
  std::vector<Matrix> world;
};
RestRefs rest_refs(const Rig &rig, const MocapRig &m) {
  RestRefs r;
  r.world = bone_world(rig, nullptr);
  auto p = [&](int s) { return pos(r.world[m.bone[s]]); };
  const Vector3 legs = mid(p(l_upper_leg), p(r_upper_leg)), arms = mid(p(l_upper_arm), p(r_upper_arm));
  r.up = Vector3Subtract(arms, legs);
  r.hip_side = Vector3Subtract(p(l_upper_leg), p(r_upper_leg));
  r.shoulder_side = Vector3Subtract(p(l_upper_arm), p(r_upper_arm));
  r.head_up = m.bone[head] >= 0 ? Vector3Subtract(p(head), arms) : r.up;
  return r;
}
bool has_body(const MocapRig &m) {
  for (Slot s : {hips, l_upper_arm, r_upper_arm, l_upper_leg, r_upper_leg})
    if (m.bone[s] < 0)
      return false;
  return true;
}
} // namespace

bool parse_mocap_packet(const void *data, size_t size, MocapFrame &out) {
  const auto *p = static_cast<const unsigned char *>(data);
  constexpr size_t head = 4 + 2 + 2 + 8 + 4, pose_bytes = 33 * 16, hand_bytes = 21 * 12;
  if (size < head + pose_bytes || std::memcmp(p, "NJMC", 4) != 0)
    return false;
  uint16_t version = 0, flags = 0;
  std::memcpy(&version, p + 4, 2);
  std::memcpy(&flags, p + 6, 2);
  if (version != 1)
    return false;
  const size_t want = head + pose_bytes + ((flags & 2) ? hand_bytes : 0) + ((flags & 4) ? hand_bytes : 0);
  if (size != want)
    return false;
  MocapFrame f;
  std::memcpy(&f.time, p + 8, 8);
  std::memcpy(&f.number, p + 16, 4);
  f.pose = flags & 1;
  const unsigned char *at = p + head;
  auto glTF = [](const float *v) { return Vector3{v[0], -v[1], -v[2]}; }; // MediaPipe y down, z away
  for (int i = 0; i < 33; ++i, at += 16) {
    float v[4];
    std::memcpy(v, at, 16);
    f.points[i] = glTF(v);
    f.visibility[i] = v[3];
  }
  for (int h = 0; h < 2; ++h) {
    f.hands[h] = flags & (h == 0 ? 2 : 4);
    if (!f.hands[h])
      continue;
    for (int i = 0; i < 21; ++i, at += 12) {
      float v[3];
      std::memcpy(v, at, 12);
      f.hand[h][i] = glTF(v);
    }
  }
  out = f;
  return true;
}

bool parse_view_packet(const void *data, size_t size, MocapView &out) {
  const auto *p = static_cast<const unsigned char *>(data);
  constexpr size_t head = 4 + 2 + 2 + 4 + 2 + 2 + 4;
  if (size <= head || size > mocap_view_max || std::memcmp(p, "NJMV", 4) != 0)
    return false;
  uint16_t version = 0, flags = 0, w = 0, h = 0;
  uint32_t number = 0, bytes = 0;
  std::memcpy(&version, p + 4, 2);
  std::memcpy(&flags, p + 6, 2);
  std::memcpy(&number, p + 8, 4);
  std::memcpy(&w, p + 12, 2);
  std::memcpy(&h, p + 14, 2);
  std::memcpy(&bytes, p + 16, 4);
  if (version != 1 || w == 0 || h == 0 || w > 4096 || h > 4096 || bytes != size - head)
    return false;
  out = {number, w, h, (flags & 1) != 0, p + head, bytes};
  return true;
}

void mirror_frame(MocapFrame &f) {
  for (auto &p : f.points)
    p.x = -p.x;
  for (const auto &pair : mp_pairs) {
    std::swap(f.points[pair[0]], f.points[pair[1]]);
    std::swap(f.visibility[pair[0]], f.visibility[pair[1]]);
  }
  for (auto &hand : f.hand)
    for (auto &p : hand)
      p.x = -p.x;
  std::swap(f.hand[0], f.hand[1]);
  std::swap(f.hands[0], f.hands[1]);
}

Vector3 OneEuro::filter(Vector3 x, double time) {
  if (!started) {
    started = true;
    last = time;
    value = x;
    speed = {};
    return x;
  }
  const float dt = (float)(time - last);
  if (dt <= 1e-6f)
    return value;
  last = time;
  auto alpha = [dt](float cutoff) {
    const float tau = 1.0f / (2 * PI * cutoff);
    return 1.0f / (1.0f + tau / dt);
  };
  const Vector3 dx = Vector3Scale(Vector3Subtract(x, value), 1 / dt);
  speed = Vector3Lerp(speed, dx, alpha(d_cutoff));
  const float cutoff = min_cutoff + beta * Vector3Length(speed);
  value = Vector3Lerp(value, x, alpha(cutoff));
  return value;
}
void MocapSmoother::set(float min_cutoff, float beta) {
  for (auto &f : pose)
    f.min_cutoff = min_cutoff, f.beta = beta;
  for (auto &h : hand)
    for (auto &f : h)
      f.min_cutoff = min_cutoff, f.beta = beta;
}
void MocapSmoother::reset() {
  for (auto &f : pose)
    f.started = false;
  for (auto &h : hand)
    for (auto &f : h)
      f.started = false;
}
MocapFrame MocapSmoother::filter(const MocapFrame &in) {
  MocapFrame f = in;
  if (f.pose)
    for (int i = 0; i < 33; ++i)
      f.points[i] = pose[i].filter(in.points[i], in.time);
  for (int h = 0; h < 2; ++h) {
    if (!f.hands[h]) {
      for (auto &e : hand[h])
        e.started = false;
      continue;
    }
    for (int i = 0; i < 21; ++i)
      f.hand[h][i] = hand[h][i].filter(in.hand[h][i], in.time);
  }
  return f;
}

MocapRig find_mocap_rig(const Rig &rig) {
  MocapRig m;
  m.bone.fill(-1);
  static const char *const trunk[] = {"hips", "spine", "chest", "upper_chest", "neck", "head"};
  static const char *const side[] = {"upper_arm", "lower_arm", "hand"};
  static const char *const leg[] = {"upper_leg", "lower_leg", "foot", "toes"};
  for (int b = 0; b < (int)rig.bones.size(); ++b) {
    const std::string h = njin::bone_humanoid_name(rig.bones[b].name.c_str());
    if (h.empty())
      continue;
    int slot = -1;
    for (int i = 0; i < 6; ++i)
      if (h == trunk[i])
        slot = i;
    for (int s = 0; s < 2 && slot < 0; ++s) {
      const std::string pre = s == 0 ? "left_" : "right_";
      if (h.rfind(pre, 0) != 0)
        continue;
      const std::string rest = h.substr(pre.size());
      for (int i = 0; i < 3; ++i)
        if (rest == side[i])
          slot = (s == 0 ? l_upper_arm : r_upper_arm) + i;
      for (int i = 0; i < 4; ++i)
        if (rest == leg[i])
          slot = (s == 0 ? l_upper_leg : r_upper_leg) + i;
      for (int f = 0; f < 5; ++f)
        for (int j = 1; j <= 3; ++j)
          if (rest == std::string(finger_names[f]) + "_" + std::to_string(j))
            slot = (s == 0 ? l_finger : r_finger) + f * 3 + j - 1;
    }
    if (slot >= 0 && m.bone[slot] < 0) {
      m.bone[slot] = b;
      ++m.found;
    }
  }
  auto depth = [&](int b) {
    int d = 0;
    for (; b >= 0; b = rig.bones[b].parent)
      ++d;
    return d;
  };
  for (int s = 0; s < slot_count; ++s)
    if (m.bone[s] >= 0)
      m.order.push_back(s);
  std::stable_sort(m.order.begin(), m.order.end(), [&](int a, int b) { return depth(m.bone[a]) < depth(m.bone[b]); });
  return m;
}

const char *const mask_names[mask_preset_count] = {"Whole body", "Upper body", "Left arm", "Right arm", "Head and neck",
                                                   "Spine",      "Legs",       "Hands",    "Selected bone and below"};

std::vector<char> mocap_mask(const Rig &rig, const MocapRig &m, MaskPreset preset, int selected) {
  std::vector<char> mask(rig.bones.size(), 0);
  auto add = [&](int slot) {
    if (m.bone[slot] >= 0)
      mask[m.bone[slot]] = 1;
  };
  auto fingers = [&](int base) {
    for (int k = 0; k < 15; ++k)
      add(base + k);
  };
  switch (preset) {
  case mask_whole:
    for (int s = 0; s < slot_count; ++s)
      add(s);
    break;
  case mask_upper:
    for (int s : {spine, chest, upper_chest, neck, head, l_upper_arm, l_lower_arm, l_hand, r_upper_arm, r_lower_arm,
                  r_hand})
      add(s);
    fingers(l_finger);
    fingers(r_finger);
    break;
  case mask_left_arm:
    for (int s : {l_upper_arm, l_lower_arm, l_hand})
      add(s);
    fingers(l_finger);
    break;
  case mask_right_arm:
    for (int s : {r_upper_arm, r_lower_arm, r_hand})
      add(s);
    fingers(r_finger);
    break;
  case mask_head:
    add(neck);
    add(head);
    break;
  case mask_spine:
    for (int s : {hips, spine, chest, upper_chest})
      add(s);
    break;
  case mask_legs:
    for (int s : {hips, l_upper_leg, l_lower_leg, l_foot, l_toes, r_upper_leg, r_lower_leg, r_foot, r_toes})
      add(s);
    break;
  case mask_hands:
    add(l_hand);
    add(r_hand);
    fingers(l_finger);
    fingers(r_finger);
    break;
  case mask_selected:
    for (int b = 0; b < (int)rig.bones.size(); ++b)
      for (int a = b; a >= 0 && selected >= 0; a = rig.bones[a].parent)
        if (a == selected) {
          mask[b] = 1;
          break;
        }
    break;
  default:
    break;
  }
  return mask;
}

int solve_mocap(const Rig &rig, const MocapRig &m, const MocapFrame &f, const std::vector<char> &mask,
                std::vector<BonePose> &pose) {
  if (!f.pose || !has_body(m) || pose.size() != rig.bones.size())
    return 0;
  const RestRefs rest = rest_refs(rig, m);
  Posed posed{rig, pose, {}, true};
  const auto &P = f.points;
  auto seen = [&](std::initializer_list<int> ids) {
    for (int i : ids)
      if (f.visibility[i] < 0.5f)
        return false;
    return true;
  };
  auto use = [&](int slot) { return m.bone[slot] >= 0 && mask[m.bone[slot]]; };
  int moved = 0;
  auto set_delta = [&](int slot, Quaternion delta) {
    const int b = m.bone[slot];
    posed.set_world_rotation(b, qmul(delta, rot(rest.world[b])));
    ++moved;
  };

  // Trunk: whole frames (an up direction and a left-right line).
  if (!seen({11, 12, 23, 24}))
    return 0;
  const Vector3 hip_mid = mid(P[23], P[24]), shoulder_mid = mid(P[11], P[12]);
  const Vector3 up = Vector3Subtract(shoulder_mid, hip_mid);
  const Quaternion d_hips = qmul(frame(up, Vector3Subtract(P[23], P[24])), qinv(frame(rest.up, rest.hip_side)));
  const Quaternion d_chest =
      qmul(frame(up, Vector3Subtract(P[11], P[12])), qinv(frame(rest.up, rest.shoulder_side)));
  Quaternion d_head = d_chest;
  const bool head_seen = seen({7, 8});
  if (head_seen)
    d_head = qmul(frame(Vector3Subtract(mid(P[7], P[8]), shoulder_mid), Vector3Subtract(P[7], P[8])),
                  qinv(frame(rest.head_up, rest.shoulder_side)));
  if (use(hips))
    set_delta(hips, d_hips);
  std::vector<int> chain;
  for (int s : {spine, chest, upper_chest})
    if (m.bone[s] >= 0)
      chain.push_back(s);
  for (size_t i = 0; i < chain.size(); ++i)
    if (use(chain[i]))
      set_delta(chain[i], slerp(d_hips, d_chest, (float)(i + 1) / (float)chain.size()));
  if (use(neck) && head_seen)
    set_delta(neck, slerp(d_chest, d_head, 0.5f));
  if (use(head) && head_seen)
    set_delta(head, d_head);

  // Limbs and fingers: the shortest turn that points each bone along its
  // segment, parents first so children start from where their parent went.
  auto swing = [&](int slot, Vector3 target) {
    const int b = m.bone[slot];
    posed.set_world_rotation(b, qmul(arc(posed.aim(m, slot), target), rot(posed.get()[b])));
    ++moved;
  };
  for (int slot : m.order) {
    if (!use(slot))
      continue;
    for (const Limb &l : limbs)
      if (l.slot == slot && seen({l.from, l.to}))
        swing(slot, Vector3Subtract(P[l.to], P[l.from]));
    if (slot == l_hand || slot == r_hand) {
      const int h = slot == l_hand ? 0 : 1, base = h == 0 ? l_finger : r_finger;
      const int index1 = m.bone[base + 3], middle1 = m.bone[base + 6], little1 = m.bone[base + 12];
      const auto &H = f.hand[h];
      if (f.hands[h] && index1 >= 0 && middle1 >= 0 && little1 >= 0) {
        const Vector3 rest_aim = Vector3Subtract(pos(rest.world[middle1]), pos(rest.world[m.bone[slot]]));
        const Vector3 rest_side = Vector3Subtract(pos(rest.world[index1]), pos(rest.world[little1]));
        set_delta(slot, qmul(frame(Vector3Subtract(H[9], H[0]), Vector3Subtract(H[5], H[17])),
                             qinv(frame(rest_aim, rest_side))));
      } else {
        const int wrist = h == 0 ? 15 : 16, index = h == 0 ? 19 : 20, pinky = h == 0 ? 17 : 18;
        if (seen({wrist, index, pinky}))
          swing(slot, Vector3Subtract(mid(P[index], P[pinky]), P[wrist]));
      }
    }
    if (slot >= l_finger && slot < slot_count) {
      const int h = slot >= r_finger ? 1 : 0, k = slot - (h == 0 ? l_finger : r_finger);
      if (f.hands[h]) {
        const int first = finger_first[k / 3] + k % 3;
        swing(slot, Vector3Subtract(f.hand[h][first + 1], f.hand[h][first]));
      }
    }
  }
  return moved;
}

MocapFrame frame_from_pose(const Rig &rig, const MocapRig &m, const std::vector<BonePose> &in) {
  std::vector<BonePose> pose = in;
  Posed posed{rig, pose, {}, true};
  const RestRefs rest = rest_refs(rig, m);
  const auto &W = posed.get();
  MocapFrame f;
  f.pose = true;
  f.visibility.fill(1);
  auto p = [&](int slot) { return pos(W[m.bone[slot]]); };
  auto tip = [&](int slot, float length) {
    return Vector3Add(p(slot), Vector3Scale(unit(posed.aim(m, slot)), length));
  };
  f.points[11] = p(l_upper_arm), f.points[12] = p(r_upper_arm);
  f.points[13] = p(l_lower_arm), f.points[14] = p(r_lower_arm);
  f.points[15] = p(l_hand), f.points[16] = p(r_hand);
  f.points[23] = p(l_upper_leg), f.points[24] = p(r_upper_leg);
  f.points[25] = p(l_lower_leg), f.points[26] = p(r_lower_leg);
  f.points[27] = p(l_foot), f.points[28] = p(r_foot);
  f.points[31] = m.bone[l_toes] >= 0 ? p(l_toes) : tip(l_foot, 0.12f);
  f.points[32] = m.bone[r_toes] >= 0 ? p(r_toes) : tip(r_foot, 0.12f);
  for (int h = 0; h < 2; ++h) {
    const int hand_slot = h == 0 ? l_hand : r_hand, base = h == 0 ? l_finger : r_finger;
    const Vector3 a = tip(hand_slot, 0.08f);
    f.points[h == 0 ? 19 : 20] = m.bone[base + 3] >= 0 ? p(base + 3) : a;
    f.points[h == 0 ? 17 : 18] = m.bone[base + 12] >= 0 ? p(base + 12) : a;
    f.points[h == 0 ? 21 : 22] = m.bone[base] >= 0 ? p(base) : a;
    bool full = true;
    for (int k = 0; k < 15; ++k)
      full = full && m.bone[base + k] >= 0;
    f.hands[h] = full;
    if (!full)
      continue;
    f.hand[h][0] = p(hand_slot);
    for (int fi = 0; fi < 5; ++fi) {
      for (int j = 0; j < 3; ++j)
        f.hand[h][finger_first[fi] + j] = p(base + fi * 3 + j);
      const float len = Vector3Distance(p(base + fi * 3 + 1), p(base + fi * 3 + 2));
      f.hand[h][finger_first[fi] + 3] = tip(base + fi * 3 + 2, std::max(len, 0.01f));
    }
  }
  // The head: its turn from rest applied to the rest references.
  const Quaternion d_head =
      m.bone[head] >= 0 ? qmul(rot(W[m.bone[head]]), qinv(rot(rest.world[m.bone[head]]))) : Quaternion{0, 0, 0, 1};
  const Vector3 shoulder_mid = mid(f.points[11], f.points[12]);
  const Vector3 ear_mid = Vector3Add(shoulder_mid, qrot(d_head, rest.head_up));
  const Vector3 side = Vector3Scale(unit(qrot(d_head, rest.shoulder_side)), 0.075f);
  f.points[7] = Vector3Add(ear_mid, side), f.points[8] = Vector3Subtract(ear_mid, side);
  f.points[0] = Vector3Add(ear_mid, qrot(d_head, {0, 0, 0.1f}));
  for (int i : {1, 2, 3, 4, 5, 6, 9, 10})
    f.points[i] = f.points[0];
  f.points[29] = f.points[27], f.points[30] = f.points[28];
  const Vector3 origin = mid(f.points[23], f.points[24]);
  for (auto &q : f.points)
    q = Vector3Subtract(q, origin);
  return f;
}

int reduce_keys(AnimationClip &clip, const std::vector<char> &bones, float from, float to, float degrees) {
  int removed = 0;
  for (int b = 0; b < (int)bones.size(); ++b) {
    if (!bones[b])
      continue;
    std::vector<Keyframe> keys;
    for (const auto &k : clip.keys)
      if (k.bone == b && k.time >= from - 1e-5f && k.time <= to + 1e-5f)
        keys.push_back(k);
    if (keys.size() < 3)
      continue;
    auto fits = [&](size_t a, size_t c) {
      const Quaternion qa = euler_quaternion(keys[a].rotation), qc = euler_quaternion(keys[c].rotation);
      for (size_t j = a + 1; j < c; ++j) {
        const float t = (keys[j].time - keys[a].time) / (keys[c].time - keys[a].time);
        const Quaternion q = QuaternionSlerp(qa, qc, t), real = euler_quaternion(keys[j].rotation);
        const float dot = std::fabs(q.x * real.x + q.y * real.y + q.z * real.z + q.w * real.w);
        if (2 * std::acos(std::min(1.0f, dot)) * RAD2DEG > degrees)
          return false;
        if (Vector3Distance(Vector3Lerp(keys[a].translation, keys[c].translation, t), keys[j].translation) > 0.001f)
          return false;
      }
      return true;
    };
    size_t last = 0;
    std::vector<char> drop(keys.size(), 0);
    for (size_t i = 1; i + 1 < keys.size(); ++i) {
      if (fits(last, i + 1))
        drop[i] = 1;
      else
        last = i;
    }
    for (size_t i = 0; i < keys.size(); ++i)
      if (drop[i] && remove_key(clip, b, keys[i].time))
        ++removed;
  }
  return removed;
}

int mocap_unit_test() {
  int failures = 0;
  auto check = [&](bool ok, const char *what) {
    if (!ok) {
      std::fprintf(stderr, "mocap unit test failed: %s\n", what);
      ++failures;
    }
  };
  // Packet: one known point, a right hand, glTF axes.
  {
    std::vector<unsigned char> b(4 + 2 + 2 + 8 + 4 + 33 * 16 + 21 * 12);
    std::memcpy(b.data(), "NJMC", 4);
    const uint16_t version = 1, flags = 1 | 4;
    const double t = 2.5;
    const uint32_t n = 7;
    std::memcpy(&b[4], &version, 2);
    std::memcpy(&b[6], &flags, 2);
    std::memcpy(&b[8], &t, 8);
    std::memcpy(&b[16], &n, 4);
    const float p13[4] = {0.1f, 0.2f, 0.3f, 0.9f}, h5[3] = {1, 2, 3};
    std::memcpy(&b[20 + 13 * 16], p13, 16);
    std::memcpy(&b[20 + 33 * 16 + 5 * 12], h5, 12);
    MocapFrame f;
    check(parse_mocap_packet(b.data(), b.size(), f), "packet parses");
    check(f.pose && !f.hands[0] && f.hands[1] && f.number == 7 && f.time == 2.5, "packet flags and header");
    check(f.points[13].x == 0.1f && f.points[13].y == -0.2f && f.points[13].z == -0.3f && f.visibility[13] == 0.9f,
          "packet point in glTF axes");
    check(f.hand[1][5].x == 1 && f.hand[1][5].y == -2 && f.hand[1][5].z == -3, "packet hand point");
    check(!parse_mocap_packet(b.data(), b.size() - 1, f), "short packet refused");
    b[0] = 'X';
    check(!parse_mocap_packet(b.data(), b.size(), f), "bad magic refused");

    // View packet: header then the JPEG bytes (not decoded here).
    auto view = [](uint16_t w, uint16_t h, size_t jpeg, uint32_t claim) {
      std::vector<unsigned char> v(20 + jpeg, 0xAB);
      const uint16_t ver = 1, fl = 1;
      const uint32_t num = 42;
      std::memcpy(&v[0], "NJMV", 4);
      std::memcpy(&v[4], &ver, 2);
      std::memcpy(&v[6], &fl, 2);
      std::memcpy(&v[8], &num, 4);
      std::memcpy(&v[12], &w, 2);
      std::memcpy(&v[14], &h, 2);
      std::memcpy(&v[16], &claim, 4);
      return v;
    };
    MocapView mv;
    auto good = view(320, 180, 1000, 1000);
    check(parse_view_packet(good.data(), good.size(), mv) && mv.number == 42 && mv.width == 320 && mv.height == 180 &&
              mv.tracking && mv.jpeg_size == 1000 && mv.jpeg == good.data() + 20,
          "view packet parses");
    check(!parse_view_packet(good.data(), good.size() - 1, mv), "truncated view packet refused");
    check(!parse_view_packet(good.data(), 19, mv), "view header alone refused");
    auto big = view(320, 180, mocap_view_max, (uint32_t)mocap_view_max);
    check(!parse_view_packet(big.data(), big.size(), mv), "oversized view packet refused");
    auto liar = view(320, 180, 1000, 999);
    check(!parse_view_packet(liar.data(), liar.size(), mv), "view packet with a wrong size refused");
    auto zero = view(0, 180, 1000, 1000);
    check(!parse_view_packet(zero.data(), zero.size(), mv), "view packet without width refused");
    std::vector<unsigned char> rgb;
    int dw = 0, dh = 0;
    check(!decode_jpeg(good.data() + 20, 1000, rgb, dw, dh), "non-JPEG bytes are not decoded");
    MocapFrame g;
    g.points[11] = {1, 2, 3};
    g.points[12] = {-4, 5, 6};
    mirror_frame(g);
    check(g.points[12].x == -1 && g.points[11].x == 4 && g.points[11].y == 5, "mirror swaps sides and x");
  }
  // One Euro: a constant stays put; noise around a constant is reduced.
  {
    OneEuro e;
    Vector3 out{};
    for (int i = 0; i < 60; ++i)
      out = e.filter({1, 2, 3}, i / 30.0);
    check(Vector3Distance(out, {1, 2, 3}) < 1e-6f, "one euro keeps a constant");
    OneEuro n;
    float raw = 0, smooth = 0;
    for (int i = 0; i < 300; ++i) {
      const float noise = ((i * 7919) % 13 - 6) * 0.002f;
      const Vector3 v = n.filter({noise, 0, 0}, i / 30.0);
      if (i > 30)
        raw += std::fabs(noise), smooth += std::fabs(v.x);
    }
    check(smooth < raw * 0.6f, "one euro smooths noise");
  }
  // Key reduction: keys on a straight slerp go, a corner stays.
  {
    AnimationClip c;
    c.duration = 1;
    for (int i = 0; i <= 10; ++i)
      set_key(c, {0, i * 0.1f, {}, {0, 0, i <= 5 ? i * 6.0f : 30 - (i - 5) * 6.0f}});
    const int removed = reduce_keys(c, {1}, 0, 1, 0.5f);
    check(removed == 8 && c.keys.size() == 3, "key reduction keeps ends and the corner");
    bool corner = false;
    for (const auto &k : c.keys)
      corner = corner || std::fabs(k.time - 0.5f) < 1e-4f;
    check(corner, "key reduction keeps the corner key");
  }
  if (failures == 0)
    std::printf("PASS: mocap packet, mirror, One Euro filter, key reduction\n");
  return failures == 0 ? 0 : 1;
}
} // namespace anim_editor
