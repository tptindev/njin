#include "document.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace anim_editor {
Vector3 matrix_euler(Matrix m) {
  // Inverse of raymath MatrixRotateXYZ (not QuaternionToEuler's rotation order).
  float y = std::asin(std::clamp(m.m8, -1.0f, 1.0f));
  float x = std::atan2(-m.m9, m.m10), z = std::atan2(-m.m4, m.m0);
  if (std::abs(std::cos(y)) < 0.00001f) {
    x = std::atan2(m.m6, m.m5);
    z = 0;
  }
  return Vector3Scale({x, y, z}, RAD2DEG);
}
static Matrix trs(Vector3 t, Quaternion r, Vector3 s) {
  return MatrixMultiply(MatrixMultiply(MatrixScale(s.x, s.y, s.z), QuaternionToMatrix(r)), MatrixTranslate(t.x, t.y, t.z));
}
static Matrix posed_local(const Bone &b, const BonePose *pose) {
  if (pose == nullptr)
    return trs(b.rest_translation, b.rest_rotation, b.rest_scale);
  return trs(Vector3Add(b.rest_translation, pose->translation), QuaternionMultiply(b.rest_rotation, pose->rotation),
             b.rest_scale);
}
static std::vector<Matrix> node_world(const Rig &rig, const std::vector<BonePose> *pose) {
  std::vector<Matrix> world(rig.nodes.size());
  std::vector<char> done(rig.nodes.size());
  std::function<Matrix(int)> visit = [&](int n) -> Matrix {
    if (done[n])
      return world[n];
    const RigNode &node = rig.nodes[n];
    Matrix local = node.local;
    if (node.joint >= 0)
      local = posed_local(rig.bones[node.joint], pose ? &(*pose)[node.joint] : nullptr);
    done[n] = 1; // glTF forbids cycles; this only stops runaway recursion on a broken file.
    Matrix m = node.parent >= 0 ? MatrixMultiply(local, visit(node.parent)) : local;
    return world[n] = m;
  };
  for (int n = 0; n < (int)rig.nodes.size(); ++n)
    visit(n);
  return world;
}
std::vector<Matrix> bone_world(const Rig &rig, const std::vector<BonePose> *pose) {
  auto world = node_world(rig, pose);
  std::vector<Matrix> result;
  result.reserve(rig.bones.size());
  for (const Bone &b : rig.bones)
    result.push_back(world[b.node]);
  return result;
}
Matrix parent_world(const Rig &rig, int bone, const std::vector<BonePose> *pose) {
  int parent = rig.nodes[rig.bones[bone].node].parent;
  return parent < 0 ? MatrixIdentity() : node_world(rig, pose)[parent];
}
Keyframe key_from_local(const Rig &rig, int bone, float time, Matrix local) {
  const Bone &b = rig.bones[bone];
  Vector3 t{}, s{};
  Quaternion q{};
  MatrixDecompose(local, &t, &q, &s);
  Quaternion pose = QuaternionNormalize(QuaternionMultiply(QuaternionInvert(b.rest_rotation), q));
  return {bone, time, Vector3Subtract(t, b.rest_translation), matrix_euler(QuaternionToMatrix(pose))};
}

using J = njin::json_value;
static J vec(Vector3 v) {
  auto a = J::make_array();
  return a.push(v.x).push(v.y).push(v.z);
}
njin::json_value serialize(const Document &d, const Rig &rig) {
  auto clips = J::make_array();
  for (const auto &clip : d.clips) {
    auto c = J::make_object(), keys = J::make_array();
    for (const auto &key : clip.keys) {
      auto k = J::make_object();
      keys.push(k.set("bone", rig.bones[key.bone].name)
                    .set("time", key.time)
                    .set("translation", vec(key.translation))
                    .set("rotation", vec(key.rotation)));
    }
    clips.push(c.set("name", clip.name)
                   .set("duration", clip.duration)
                   .set("fps", clip.fps)
                   .set("loop", clip.loop)
                   .set("keys", keys));
  }
  auto root = J::make_object();
  return root.set("format", "njin.anim-project").set("version", 1).set("model", d.model).set("clips", clips);
}
static float number(const J &j, float low, float high) {
  if (!j.is(J::number) || !std::isfinite(j.num) || j.num < low || j.num > high)
    throw std::runtime_error("Missing or out-of-range number");
  return (float)j.num;
}
static int integer(const J &j, int low, int high) {
  float v = number(j, (float)low, (float)high);
  if (std::floor(j.num) != j.num)
    throw std::runtime_error("Expected integer");
  return (int)v;
}
static Vector3 vector(const J &j, float low, float high) {
  if (!j.is(J::array) || j.size() != 3)
    throw std::runtime_error("Expected three-component vector");
  return {number(j[0], low, high), number(j[1], low, high), number(j[2], low, high)};
}
static std::string name(const J &j) {
  if (!j.is(J::string) || j.str.size() > 127)
    throw std::runtime_error("Invalid name (max 127 bytes)");
  return j.str;
}
static void check_format(const J &j) {
  if (j["format"].str == "njin.sdf-model")
    throw std::runtime_error("This is an old SDF model project (.model.json). It has no glTF rig, so the animation "
                             "editor cannot open it: open a skinned .glb/.gltf instead");
  if (j["format"].str != "njin.anim-project")
    throw std::runtime_error("Not an njin animation project");
  integer(j["version"], 1, 1);
  if (!j["model"].is(J::string) || j["model"].str.empty())
    throw std::runtime_error("The project names no model file");
}
bool project_model(const J &j, std::string &model, std::string &error) {
  try {
    check_format(j);
    model = j["model"].str;
    error.clear();
    return true;
  } catch (const std::exception &e) {
    error = e.what();
    return false;
  }
}
bool deserialize(const J &j, const Rig &rig, Document &out, std::string &error) {
  try {
    check_format(j);
    Document d;
    d.model = j["model"].str;
    if (!j["clips"].is(J::array) || j["clips"].size() > max_clips)
      throw std::runtime_error("Expected clips array (max 64)");
    size_t total_keys = 0;
    for (const auto &v : j["clips"].items) {
      AnimationClip c;
      c.name = name(v["name"]);
      c.duration = number(v["duration"], 0.05f, 600);
      c.fps = integer(v["fps"], 1, 120);
      if (!v["loop"].is(J::boolean))
        throw std::runtime_error("Expected loop boolean");
      c.loop = v["loop"].b;
      total_keys += v["keys"].size();
      if (!v["keys"].is(J::array) || v["keys"].size() > max_keys || total_keys > 640000)
        throw std::runtime_error("Too many animation keys");
      for (const auto &k : v["keys"].items) {
        const std::string bone = name(k["bone"]);
        auto it = std::find_if(rig.bones.begin(), rig.bones.end(), [&](const Bone &b) { return b.name == bone; });
        if (it == rig.bones.end())
          throw std::runtime_error("Clip '" + c.name + "' animates bone '" + bone + "', which the model does not have");
        Keyframe key;
        key.bone = (int)(it - rig.bones.begin());
        key.time = number(k["time"], 0, c.duration);
        key.translation = vector(k["translation"], -1e5f, 1e5f);
        key.rotation = vector(k["rotation"], -360, 360);
        c.keys.push_back(key);
      }
      std::sort(c.keys.begin(), c.keys.end(),
                [](const Keyframe &a, const Keyframe &b) { return a.bone == b.bone ? a.time < b.time : a.bone < b.bone; });
      for (size_t i = 1; i < c.keys.size(); ++i)
        if (c.keys[i].bone == c.keys[i - 1].bone && c.keys[i].time - c.keys[i - 1].time < 0.0001f)
          throw std::runtime_error("Duplicate animation key time");
      d.clips.push_back(std::move(c));
    }
    out = std::move(d);
    error.clear();
    return true;
  } catch (const std::exception &e) {
    error = e.what();
    return false;
  }
}
} // namespace anim_editor
