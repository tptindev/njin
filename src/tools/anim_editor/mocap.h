#pragma once
// Webcam motion capture: MediaPipe landmarks (from mocap/pose_stream.py) to
// bone rotations of the open rig.
#include "document.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace anim_editor {
// One frame from pose_stream.py, already in glTF axes (+Y up, +Z towards the
// camera, metres, origin between the hips): MediaPipe's (x, y, z) is (x, -y, -z).
struct MocapFrame {
  double time = 0;
  uint32_t number = 0;
  bool pose = false, hands[2]{}; // hands[0] the person's left
  std::array<Vector3, 33> points{};
  std::array<float, 33> visibility{};
  std::array<std::array<Vector3, 21>, 2> hand{};
};
bool parse_mocap_packet(const void *data, size_t size, MocapFrame &out);
// Swaps the person's left and right and the x axis: the character moves like a mirror image.
void mirror_frame(MocapFrame &f);

// The camera image from pose_stream.py --view (the skeleton drawn on it), as
// a JPEG pointing into the packet.
struct MocapView {
  uint32_t number = 0;
  int width = 0, height = 0;
  bool tracking = false;
  const unsigned char *jpeg = nullptr;
  size_t jpeg_size = 0;
};
constexpr size_t mocap_view_max = 60000; // VIEW_MAX in pose_stream.py
bool parse_view_packet(const void *data, size_t size, MocapView &out);
// RGB8 pixels of a JPEG (stb_image, JPEG only). False if it is not one.
bool decode_jpeg(const unsigned char *data, size_t size, std::vector<unsigned char> &rgb, int &width, int &height);

// One Euro filter (Casiez et al. 2012): smooth when still, quick when moving.
struct OneEuro {
  float min_cutoff = 1.5f, beta = 0.3f, d_cutoff = 1.0f;
  bool started = false;
  double last = 0;
  Vector3 value{}, speed{};
  Vector3 filter(Vector3 x, double time);
};
struct MocapSmoother {
  std::array<OneEuro, 33> pose;
  std::array<std::array<OneEuro, 21>, 2> hand;
  void set(float min_cutoff, float beta);
  void reset();
  MocapFrame filter(const MocapFrame &f);
};

// The rig's humanoid bones, found by njin::bone_humanoid_name().
enum Slot {
  hips, spine, chest, upper_chest, neck, head,
  l_upper_arm, l_lower_arm, l_hand, r_upper_arm, r_lower_arm, r_hand,
  l_upper_leg, l_lower_leg, l_foot, l_toes, r_upper_leg, r_lower_leg, r_foot, r_toes,
  l_finger, // 15 per hand: thumb, index, middle, ring, little; joints 1..3
  r_finger = l_finger + 15,
  slot_count = r_finger + 15,
};
struct MocapRig {
  std::array<int, slot_count> bone{};
  std::vector<int> order; // Slots in parent-before-child order.
  int found = 0;
  bool usable() const { return bone[hips] >= 0 && found >= 8; }
};
MocapRig find_mocap_rig(const Rig &rig);

enum MaskPreset { mask_whole, mask_upper, mask_left_arm, mask_right_arm, mask_head, mask_spine, mask_legs,
                  mask_hands, mask_selected, mask_preset_count };
extern const char *const mask_names[mask_preset_count];
// One flag per bone. `selected` (with everything under it) for mask_selected.
std::vector<char> mocap_mask(const Rig &rig, const MocapRig &m, MaskPreset preset, int selected);

// Sets the masked bones of `pose` (the clip's pose, one entry per bone) from
// the frame. Returns how many bones it moved.
int solve_mocap(const Rig &rig, const MocapRig &m, const MocapFrame &f, const std::vector<char> &mask,
                std::vector<BonePose> &pose);
// The frame a rig in `pose` would give: for tests.
MocapFrame frame_from_pose(const Rig &rig, const MocapRig &m, const std::vector<BonePose> &pose);

// Removes keys of `bones` in [from, to] that interpolation between their
// neighbours reproduces within `degrees` (and 1 mm). Returns how many it removed.
int reduce_keys(AnimationClip &clip, const std::vector<char> &bones, float from, float to, float degrees);

int mocap_unit_test();
int mocap_test(int argc, char **argv); // --mocap-test, see main.cpp
} // namespace anim_editor
