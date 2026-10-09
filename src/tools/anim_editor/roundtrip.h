#pragma once
// Shared by the editor (raylib side) and roundtrip.cpp (njin side), so it
// names no type of either.
#include <string>
#include <vector>

namespace anim_editor {
struct RoundtripSample {
  std::string bone;
  float time = 0;
  float position[3]{};
  float axes[9]{}; // x, y, z axes of the bone in model space, unit length.
};
struct RoundtripResult {
  bool loaded = false;
  int bones = 0, clips = 0;
  double max_angle = 0;  // Degrees, worst axis of any sample.
  double max_offset = 0; // Model units, worst bone origin.
  std::string error;
};
// Loads `glb` with njin's model_load() and compares model_bone_pose() of clip
// `clip` with each sample.
RoundtripResult njin_roundtrip(const std::string &glb, const std::string &clip,
                               const std::vector<RoundtripSample> &samples);
} // namespace anim_editor
