#pragma once
#include "njin_json.h"
#include "raylib.h"
#include <string>
#include <vector>

namespace anim_editor {
// One joint of the model's skin, in skin joint order (the order raylib and
// njin give bones). Rest values are the joint node's own local TRS.
struct Bone {
  std::string name;
  int parent = -1; // Parent joint, -1 when the node above is not a joint.
  int node = -1;   // Index into Rig::nodes.
  Vector3 rest_translation{};
  Quaternion rest_rotation{0, 0, 0, 1};
  Vector3 rest_scale{1, 1, 1};
};
// Every node of the glTF scene graph, so a joint's world transform includes
// non-joint ancestors (an armature node) exactly as the loader computes it.
struct RigNode {
  int parent = -1;
  int joint = -1; // Index into Rig::bones, or -1.
  Matrix local{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};
struct Rig {
  std::vector<Bone> bones;
  std::vector<RigNode> nodes;
  BoundingBox bounds{};
};
struct Keyframe {
  int bone = 0;
  float time = 0;
  Vector3 translation{}; // Added to the rest translation, in the parent node's space.
  Vector3 rotation{};    // Euler degrees (raymath XYZ) after the rest rotation; slerped as quaternions.
};
struct AnimationClip {
  std::string name = "Animation";
  float duration = 1;
  int fps = 30;
  bool loop = true;
  std::vector<Keyframe> keys; // Sorted by bone, then time; unique (bone,time).
};
struct BonePose {
  Vector3 translation{};
  Quaternion rotation{0, 0, 0, 1};
};
struct Document {
  std::string model; // Source glTF, relative to the project file when saved.
  std::vector<AnimationClip> clips;
};

constexpr int max_clips = 64;
constexpr int max_keys = 10000;

Vector3 matrix_euler(Matrix matrix);
Quaternion euler_quaternion(Vector3 degrees);
bool set_key(AnimationClip &clip, Keyframe key);
bool remove_key(AnimationClip &clip, int bone, float time);
std::vector<BonePose> sample_animation(int bones, const AnimationClip &clip, float time);
float advance_animation(float time, float delta, const AnimationClip &clip, bool &playing);
// World matrices of every bone; `pose` (one entry per bone) or the rest pose.
std::vector<Matrix> bone_world(const Rig &rig, const std::vector<BonePose> *pose);
// World matrix of the node above `bone` with `pose` applied.
Matrix parent_world(const Rig &rig, int bone, const std::vector<BonePose> *pose);
// The key that puts `bone` at `local` (the bone's posed local matrix).
Keyframe key_from_local(const Rig &rig, int bone, float time, Matrix local);

njin::json_value serialize(const Document &doc, const Rig &rig);
bool deserialize(const njin::json_value &json, const Rig &rig, Document &out, std::string &error);
// Reads only the model path of a project, to load the rig before the clips.
bool project_model(const njin::json_value &json, std::string &model, std::string &error);

// rig.cpp: glTF through cgltf.
bool load_rig(const std::string &path, Rig &rig, std::vector<AnimationClip> &clips, std::string &error);
bool export_glb(const std::string &source, const Rig &rig, const std::vector<AnimationClip> &clips,
                const std::string &out, std::string &error);

int self_test();
int roundtrip_test(const char *model); // Opens njin's own window: call with none open.
std::string temp_folder(const char *tag);
std::string write_test_model(const std::string &folder); // A small skinned .gltf with one clip.
AnimationClip wave_clip();
} // namespace anim_editor
