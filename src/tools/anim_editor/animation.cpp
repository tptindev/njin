#include "document.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

namespace anim_editor {
static bool key_less(const Keyframe &a, const Keyframe &b) {
  return a.bone == b.bone ? a.time < b.time : a.bone < b.bone;
}
bool set_key(AnimationClip &clip, Keyframe key) {
  key.time = std::clamp(key.time, 0.0f, clip.duration);
  auto at = std::lower_bound(clip.keys.begin(), clip.keys.end(), Keyframe{key.bone, key.time - 0.0001f}, key_less);
  if (at != clip.keys.end() && at->bone == key.bone && std::abs(at->time - key.time) < 0.0001f) {
    *at = key;
    return true;
  }
  if (clip.keys.size() >= max_keys)
    return false;
  clip.keys.insert(std::lower_bound(clip.keys.begin(), clip.keys.end(), key, key_less), key);
  return true;
}
bool remove_key(AnimationClip &clip, int bone, float time) {
  return std::erase_if(clip.keys, [&](const Keyframe &k) { return k.bone == bone && std::abs(k.time - time) < 0.0001f; }) > 0;
}
Quaternion euler_quaternion(Vector3 degrees) {
  return QuaternionFromMatrix(MatrixRotateXYZ(Vector3Scale(degrees, DEG2RAD)));
}
std::vector<BonePose> sample_animation(int bones, const AnimationClip &clip, float time) {
  // Sampling clamps, so the last key remains editable even for looping clips.
  time = std::clamp(time, 0.0f, clip.duration);
  std::vector<BonePose> result((size_t)std::max(bones, 0));
  for (int i = 0; i < bones; ++i) {
    auto begin = std::lower_bound(clip.keys.begin(), clip.keys.end(), Keyframe{i, 0}, key_less);
    auto end = std::lower_bound(begin, clip.keys.end(), Keyframe{i + 1, 0}, key_less);
    if (begin == end)
      continue;
    auto next = std::upper_bound(begin, end, time, [](float t, const Keyframe &k) { return t < k.time; });
    const auto &a = next == begin ? *begin : *(next - 1);
    const auto &b = next == end ? *(end - 1) : *next;
    float alpha = b.time > a.time ? std::clamp((time - a.time) / (b.time - a.time), 0.0f, 1.0f) : 0;
    result[i].translation = Vector3Lerp(a.translation, b.translation, alpha);
    result[i].rotation = QuaternionSlerp(euler_quaternion(a.rotation), euler_quaternion(b.rotation), alpha);
  }
  return result;
}
float advance_animation(float time, float delta, const AnimationClip &clip, bool &playing) {
  if (!playing)
    return std::clamp(time, 0.0f, clip.duration);
  time += std::max(0.0f, delta);
  if (clip.loop)
    return std::fmod(time, clip.duration);
  if (time >= clip.duration) {
    time = clip.duration;
    playing = false;
  }
  return time;
}
} // namespace anim_editor
