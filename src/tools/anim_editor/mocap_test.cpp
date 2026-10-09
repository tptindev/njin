// --mocap-test <model.glb> [clip] [dump.bin fps camera_yaw_degrees]
//   Synthetic: landmarks made from the rig's own clip, sent through the packet
//   format and solved back; the limbs must point where the clip points them.
//   Dump: packets pose_stream.py wrote (--dump) for frames of that clip
//   rendered from the front; reports how far MediaPipe and the solver are from
//   the clip's true limb directions.
#include "mocap.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace anim_editor {
namespace {
std::vector<unsigned char> encode(const MocapFrame &f) {
  std::vector<unsigned char> b;
  auto put = [&](const void *p, size_t n) { b.insert(b.end(), (const unsigned char *)p, (const unsigned char *)p + n); };
  put("NJMC", 4);
  const uint16_t version = 1, flags = (f.pose ? 1 : 0) | (f.hands[0] ? 2 : 0) | (f.hands[1] ? 4 : 0);
  put(&version, 2);
  put(&flags, 2);
  put(&f.time, 8);
  put(&f.number, 4);
  for (int i = 0; i < 33; ++i) {
    const float v[4] = {f.points[i].x, -f.points[i].y, -f.points[i].z, f.visibility[i]};
    put(v, 16);
  }
  for (int h = 0; h < 2; ++h)
    if (f.hands[h])
      for (const Vector3 &p : f.hand[h]) {
        const float v[3] = {p.x, -p.y, -p.z};
        put(v, 12);
      }
  return b;
}
Vector3 pos(const Matrix &m) { return {m.m12, m.m13, m.m14}; }
float angle(Vector3 a, Vector3 b) {
  const float la = Vector3Length(a), lb = Vector3Length(b);
  if (la < 1e-9f || lb < 1e-9f)
    return 0;
  return std::acos(std::clamp(Vector3DotProduct(a, b) / (la * lb), -1.0f, 1.0f)) * RAD2DEG;
}
struct Seg {
  const char *name;
  Slot from, to;
  int a, b; // MediaPipe points
};
constexpr Seg segs[] = {{"L upper arm", l_upper_arm, l_lower_arm, 11, 13}, {"L forearm", l_lower_arm, l_hand, 13, 15},
                        {"R upper arm", r_upper_arm, r_lower_arm, 12, 14}, {"R forearm", r_lower_arm, r_hand, 14, 16},
                        {"L thigh", l_upper_leg, l_lower_leg, 23, 25},     {"L shin", l_lower_leg, l_foot, 25, 27},
                        {"R thigh", r_upper_leg, r_lower_leg, 24, 26},     {"R shin", r_lower_leg, r_foot, 26, 28}};
Vector3 seg_dir(const std::vector<Matrix> &w, const MocapRig &m, const Seg &s) {
  return Vector3Subtract(pos(w[m.bone[s.to]]), pos(w[m.bone[s.from]]));
}
float percentile(std::vector<float> v, float p) {
  if (v.empty())
    return 0;
  std::sort(v.begin(), v.end());
  return v[std::min(v.size() - 1, (size_t)(p * (v.size() - 1) + 0.5f))];
}
} // namespace

int mocap_test(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: --mocap-test model.glb [clip] [dump.bin fps camera_yaw_degrees]\n");
    return 2;
  }
  Rig rig;
  std::vector<AnimationClip> clips;
  std::string error;
  if (!load_rig(argv[2], rig, clips, error)) {
    std::fprintf(stderr, "FAIL: %s\n", error.c_str());
    return 1;
  }
  const MocapRig m = find_mocap_rig(rig);
  std::printf("rig: %d bones, %d humanoid slots found%s\n", (int)rig.bones.size(), m.found,
              m.usable() ? "" : " (NOT usable)");
  for (const Seg &s : segs)
    if (m.bone[s.from] < 0 || m.bone[s.to] < 0) {
      std::fprintf(stderr, "FAIL: rig has no %s\n", s.name);
      return 1;
    }
  const AnimationClip *clip = clips.empty() ? nullptr : &clips[0];
  if (argc > 3)
    for (const auto &c : clips)
      if (c.name == argv[3])
        clip = &c;
  if (!clip) {
    std::fprintf(stderr, "FAIL: no clip\n");
    return 1;
  }
  int failures = 0;

  // 1. Synthetic: clip -> landmarks -> packet -> solve from rest.
  float worst = 0, worst_head = 0;
  const std::vector<char> all = mocap_mask(rig, m, mask_whole, -1);
  for (int i = 0; i < 40; ++i) {
    const float t = clip->duration * i / 40.0f;
    const auto truth = sample_animation((int)rig.bones.size(), *clip, t);
    MocapFrame f = frame_from_pose(rig, m, truth);
    f.time = t;
    f.number = (uint32_t)i;
    const auto bytes = encode(f);
    MocapFrame g;
    if (!parse_mocap_packet(bytes.data(), bytes.size(), g)) {
      std::fprintf(stderr, "FAIL: synthetic packet did not parse\n");
      return 1;
    }
    std::vector<BonePose> solved(rig.bones.size());
    solve_mocap(rig, m, g, all, solved);
    const auto tw = bone_world(rig, &truth), sw = bone_world(rig, &solved);
    for (const Seg &s : segs)
      worst = std::max(worst, angle(seg_dir(tw, m, s), seg_dir(sw, m, s)));
    if (m.bone[head] >= 0) {
      Vector3 tt{}, ts{}, st{}, ss{};
      Quaternion tq{}, sq{};
      MatrixDecompose(tw[m.bone[head]], &tt, &tq, &ts);
      MatrixDecompose(sw[m.bone[head]], &st, &sq, &ss);
      const float dot = std::fabs(tq.x * sq.x + tq.y * sq.y + tq.z * sq.z + tq.w * sq.w);
      worst_head = std::max(worst_head, 2 * std::acos(std::min(1.0f, dot)) * RAD2DEG);
    }
  }
  const bool synth_ok = worst < 0.05f && worst_head < 0.1f;
  failures += !synth_ok;
  std::printf("%s: synthetic '%s' (40 poses through the packet format): worst limb %.4f deg, head %.4f deg\n",
              synth_ok ? "PASS" : "FAIL", clip->name.c_str(), worst, worst_head);

  // 2. Mask: the left arm only moves left-arm bones.
  {
    const auto base = sample_animation((int)rig.bones.size(), *clip, clip->duration * 0.3f);
    const auto other = sample_animation((int)rig.bones.size(), *clip, clip->duration * 0.7f);
    const MocapFrame f = frame_from_pose(rig, m, other);
    const auto mask = mocap_mask(rig, m, mask_left_arm, -1);
    auto out = base;
    const int moved = solve_mocap(rig, m, f, mask, out);
    bool only = moved > 0;
    for (size_t b = 0; b < rig.bones.size(); ++b)
      if (!mask[b] && std::memcmp(&out[b], &base[b], sizeof(BonePose)) != 0)
        only = false;
    failures += !only;
    std::printf("%s: left-arm mask moved %d bones, none outside the mask\n", only ? "PASS" : "FAIL", moved);
  }

  // 3. A dump of pose_stream.py on frames of this clip.
  if (argc > 6) {
    const float fps = (float)std::atof(argv[5]), yaw = (float)std::atof(argv[6]) * DEG2RAD;
    const Quaternion to_camera = QuaternionFromAxisAngle({0, 1, 0}, -yaw);
    std::ifstream in(argv[4], std::ios::binary);
    std::vector<float> raw, solved_err;
    std::vector<std::vector<float>> per(8);
    int frames = 0, found = 0;
    for (;;) {
      uint32_t n = 0;
      if (!in.read(reinterpret_cast<char *>(&n), 4))
        break;
      std::vector<unsigned char> b(n);
      if (!in.read(reinterpret_cast<char *>(b.data()), n))
        break;
      MocapFrame f;
      ++frames;
      if (!parse_mocap_packet(b.data(), b.size(), f) || !f.pose)
        continue;
      ++found;
      const float t = (float)f.number / fps;
      const auto truth = sample_animation((int)rig.bones.size(), *clip, std::fmod(t, clip->duration));
      std::vector<BonePose> solved(rig.bones.size());
      solve_mocap(rig, m, f, all, solved);
      const auto tw = bone_world(rig, &truth), sw = bone_world(rig, &solved);
      for (int i = 0; i < 8; ++i) {
        const Seg &s = segs[i];
        const Vector3 want = Vector3RotateByQuaternion(seg_dir(tw, m, s), to_camera);
        const Vector3 got = Vector3RotateByQuaternion(seg_dir(sw, m, s), to_camera);
        const float e_raw = angle(want, Vector3Subtract(f.points[s.b], f.points[s.a]));
        raw.push_back(e_raw);
        solved_err.push_back(angle(want, got));
        per[i].push_back(e_raw);
      }
    }
    std::printf("video: %d frames, body found in %d\n", frames, found);
    std::printf("  MediaPipe limb direction vs clip:  median %.1f deg, p90 %.1f deg\n", percentile(raw, 0.5f),
                percentile(raw, 0.9f));
    std::printf("  solved bones vs clip:              median %.1f deg, p90 %.1f deg\n", percentile(solved_err, 0.5f),
                percentile(solved_err, 0.9f));
    for (int i = 0; i < 8; ++i)
      std::printf("    %-12s median %5.1f  p90 %5.1f\n", segs[i].name, percentile(per[i], 0.5f), percentile(per[i], 0.9f));
    if (found < frames / 2) {
      std::printf("FAIL: MediaPipe found the body in too few frames\n");
      ++failures;
    }
  }
  return failures == 0 ? 0 : 1;
}
} // namespace anim_editor
