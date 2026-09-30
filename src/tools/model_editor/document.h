#pragma once
#include "njin_json.h"
#include "raylib.h"
#include <string>
#include <vector>

namespace model_editor {
struct Bone {
  std::string name = "Bone";
  int parent = -1;
  Vector3 position{}; // Local rest translation relative to parent.
  Vector3 rotation{}; // Local rest Euler angles in degrees.
  Vector3 pose{};     // Additional local rotation in degrees.
  float length = 0.6f;
  Vector3 offset{}; // Pose translation in parent space, added to the rest position.
};
struct Keyframe {
  int bone = 0;
  float time = 0;
  Vector3 translation{};
  Vector3 rotation{}; // Pose Euler degrees, interpolated using quaternion slerp.
};
struct AnimationClip {
  std::string name = "Animation";
  float duration = 1;
  int fps = 30;
  bool loop = true;
  std::vector<Keyframe> keys; // Sorted by bone, then time; unique (bone,time).
};
struct BonePose { Vector3 translation{}; Vector4 rotation{0,0,0,1}; };
struct Shape {
  std::string name = "Shape";
  int kind = 0; // Sphere, box, capsule, cylinder, torus.
  int operation = 1; // Union, smooth union, subtraction, intersection.
  int bone = -1;
  Vector3 position{};
  Vector3 rotation{};
  Vector3 size{0.5f, 0.5f, 0.5f}; // Half extents for boxes.
  float radius = 0.3f, height = 0.8f, thickness = 0.1f, blend = 0.12f;
  bool visible = true;
};
struct Document {
  std::vector<Bone> bones;
  std::vector<Shape> shapes;
  Vector3 color{0.35f, 0.72f, 0.82f};
  std::vector<AnimationClip> clips;
};
Matrix local_matrix(Vector3 position, Vector3 rotation);
Vector3 matrix_euler(Matrix matrix);
std::vector<Matrix> bone_matrices(const Document &doc, bool posed, const std::vector<BonePose> *pose = nullptr);
std::vector<BonePose> sample_animation(const Document &doc, const AnimationClip &clip, float time);
bool set_key(AnimationClip &clip, Keyframe key);
bool remove_key(AnimationClip &clip, int bone, float time);
float advance_animation(float time, float delta, const AnimationClip &clip, bool &playing);
void add_demo_animation(Document &doc);
Matrix shape_matrix(const Shape &shape, const std::vector<Matrix> &bones);
bool can_parent(const Document &doc, int bone, int parent);
void remove_bone(Document &doc, int index); // Removes subtree and detaches its shapes in world space.
void bind_shape(Document &doc, int shape, int bone);
Document humanoid();
njin::json_value serialize(const Document &doc);
bool deserialize(const njin::json_value &json, Document &out, std::string &error);

struct FieldShape { Shape shape; Matrix inverse; };
struct Field {
  std::vector<FieldShape> shapes;
  Vector3 low{}, high{};
  explicit Field(const Document &doc, bool posed, const std::vector<Matrix> *matrices = nullptr);
  float distance(Vector3 point) const;
  Vector3 normal(Vector3 point) const;
};
struct Surface { std::vector<Vector3> vertices, normals; };
Surface triangulate(const Field &field, int resolution);
bool export_obj(const std::string &path, const Surface &surface);
int self_test();
}
