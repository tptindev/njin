#include "document.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace model_editor {
Matrix local_matrix(Vector3 p, Vector3 r) {
  return MatrixMultiply(MatrixRotateXYZ(Vector3Scale(r, DEG2RAD)), MatrixTranslate(p.x, p.y, p.z));
}
std::vector<Matrix> bone_matrices(const Document &doc, bool posed, const std::vector<BonePose> *pose) {
  std::vector<Matrix> result(doc.bones.size());
  std::vector<bool> done(doc.bones.size());
  std::function<Matrix(int)> visit = [&](int i) {
    if (done[i]) return result[i];
    const auto &b = doc.bones[i];
    Matrix m = local_matrix(b.position, b.rotation);
    if (posed) {
      auto rotation = pose ? QuaternionToMatrix((*pose)[i].rotation) : MatrixRotateXYZ(Vector3Scale(b.pose, DEG2RAD));
      m = MatrixMultiply(rotation, m);
      auto offset = pose ? (*pose)[i].translation : b.offset;
      m.m12 += offset.x; m.m13 += offset.y; m.m14 += offset.z;
    }
    if (b.parent >= 0) m = MatrixMultiply(m, visit(b.parent));
    done[i] = true;
    return result[i] = m;
  };
  for (int i = 0; i < (int)doc.bones.size(); ++i) visit(i);
  return result;
}
Matrix shape_matrix(const Shape &s, const std::vector<Matrix> &bones) {
  Matrix m = local_matrix(s.position, s.rotation);
  return s.bone < 0 ? m : MatrixMultiply(m, bones[s.bone]);
}
bool can_parent(const Document &doc, int bone, int parent) {
  if (parent < -1 || parent >= (int)doc.bones.size()) return false;
  for (int count = 0; parent >= 0; ++count) {
    if (parent == bone || count >= (int)doc.bones.size()) return false;
    parent = doc.bones[parent].parent;
  }
  return true;
}
Vector3 matrix_euler(Matrix m) {
  // Inverse of raymath MatrixRotateXYZ (not QuaternionToEuler's rotation order).
  float y = std::asin(std::clamp(m.m8, -1.0f, 1.0f));
  float x = std::atan2(-m.m9, m.m10), z = std::atan2(-m.m4, m.m0);
  if (std::abs(std::cos(y)) < 0.00001f) { x = std::atan2(m.m6, m.m5); z = 0; }
  return Vector3Scale({x,y,z}, RAD2DEG);
}
static void set_transform(Shape &s, Matrix m) {
  s.position = {m.m12, m.m13, m.m14};
  s.rotation = matrix_euler(m);
}
void bind_shape(Document &doc, int index, int bone) {
  auto matrices = bone_matrices(doc, false);
  auto &s = doc.shapes[index];
  Matrix world = shape_matrix(s, matrices);
  if (bone >= 0) world = MatrixMultiply(world, MatrixInvert(matrices[bone]));
  set_transform(s, world);
  s.bone = bone;
}
void remove_bone(Document &doc, int index) {
  std::vector<bool> removed(doc.bones.size());
  std::vector<int> remap(doc.bones.size(), -1);
  auto matrices = bone_matrices(doc, false);
  for (int i = 0; i < (int)doc.bones.size(); ++i)
    removed[i] = !can_parent(doc, index, i);
  int next = 0;
  for (int i = 0; i < (int)removed.size(); ++i) if (!removed[i]) remap[i] = next++;
  for (auto &s : doc.shapes) if (s.bone >= 0) {
    if (removed[s.bone]) set_transform(s, shape_matrix(s, matrices));
    s.bone = remap[s.bone];
  }
  std::vector<Bone> keep;
  for (int i = 0; i < (int)doc.bones.size(); ++i) if (!removed[i]) {
    Bone b = doc.bones[i];
    if (b.parent >= 0) b.parent = remap[b.parent];
    keep.push_back(b);
  }
  for (auto &clip : doc.clips) {
    std::erase_if(clip.keys, [&](const Keyframe &key) { return removed[key.bone]; });
    for (auto &key : clip.keys) key.bone = remap[key.bone];
  }
  doc.bones = std::move(keep);
}
Document humanoid() {
  Document d;
  d.bones = {{"Hips", -1, {0,1.25f,0}, {}, {}, 0.45f},
             {"Chest", 0, {0,0.45f,0}, {}, {}, 0.4f},
             {"Head", 1, {0,0.55f,0}, {}, {}, 0.35f},
             {"Arm.L", 1, {-0.3f,0.25f,0}, {0,0,75}, {}, 0.55f},
             {"Forearm.L", 3, {0,0.55f,0}, {}, {}, 0.5f},
             {"Arm.R", 1, {0.3f,0.25f,0}, {0,0,-75}, {}, 0.55f},
             {"Forearm.R", 5, {0,0.55f,0}, {}, {}, 0.5f},
             {"Thigh.L", 0, {-0.18f,0,0}, {0,0,180}, {}, 0.55f},
             {"Shin.L", 7, {0,0.55f,0}, {}, {}, 0.55f},
             {"Thigh.R", 0, {0.18f,0,0}, {0,0,180}, {}, 0.55f},
             {"Shin.R", 9, {0,0.55f,0}, {}, {}, 0.55f}};
  for (int i = 0; i < (int)d.bones.size(); ++i) {
    Shape s; s.name = d.bones[i].name; s.bone = i; s.kind = 2;
    s.height = d.bones[i].length; s.position.y = s.height * 0.5f;
    s.radius = i < 2 ? 0.24f : (i == 2 ? 0.25f : 0.1f);
    s.blend = 0.1f;
    if (i == 2) s.kind = 0;
    d.shapes.push_back(s);
  }
  return d;
}
using J = njin::json_value;
static J vec(Vector3 v) { auto a = J::make_array(); return a.push(v.x).push(v.y).push(v.z); }
njin::json_value serialize(const Document &d) {
  auto root = J::make_object(), bones = J::make_array(), shapes = J::make_array();
  for (const auto &b : d.bones) {
    auto j = J::make_object();
    j.set("name", b.name).set("parent", b.parent).set("position", vec(b.position))
      .set("rotation", vec(b.rotation)).set("pose", vec(b.pose)).set("length", b.length).set("offset", vec(b.offset));
    bones.push(j);
  }
  for (const auto &s : d.shapes) {
    auto j = J::make_object();
    j.set("name", s.name).set("kind", s.kind).set("operation", s.operation).set("bone", s.bone)
      .set("position", vec(s.position)).set("rotation", vec(s.rotation)).set("size", vec(s.size))
      .set("radius", s.radius).set("height", s.height).set("thickness", s.thickness)
      .set("blend", s.blend).set("visible", s.visible);
    shapes.push(j);
  }
  auto clips = J::make_array();
  for (const auto &clip : d.clips) {
    auto c = J::make_object(), keys = J::make_array();
    for (const auto &key : clip.keys) {
      auto k = J::make_object();
      keys.push(k.set("bone",key.bone).set("time",key.time)
        .set("translation",vec(key.translation)).set("rotation",vec(key.rotation)));
    }
    clips.push(c.set("name",clip.name).set("duration",clip.duration).set("fps",clip.fps)
      .set("loop",clip.loop).set("keys",keys));
  }
  return root.set("format", "njin.sdf-model").set("version", 2).set("bones", bones)
    .set("shapes", shapes).set("color", vec(d.color)).set("clips",clips);
}
static float number(const J &j, float low, float high) {
  if (!j.is(J::number) || !std::isfinite(j.num) || j.num < low || j.num > high)
    throw std::runtime_error("Missing or out-of-range number");
  return (float)j.num;
}
static int integer(const J &j, int low, int high) {
  float v = number(j, (float)low, (float)high);
  if (std::floor(j.num) != j.num) throw std::runtime_error("Expected integer");
  return (int)v;
}
static Vector3 vector(const J &j, float low = -100, float high = 100) {
  if (!j.is(J::array) || j.size() != 3) throw std::runtime_error("Expected three-component vector");
  return {number(j[0],low,high),number(j[1],low,high),number(j[2],low,high)};
}
static std::string name(const J &j) {
  if (!j.is(J::string) || j.str.size() > 127) throw std::runtime_error("Invalid name (max 127 bytes)");
  return j.str;
}
bool deserialize(const J &j, Document &out, std::string &error) {
  try {
    if (j["format"].str != "njin.sdf-model")
      throw std::runtime_error("Unsupported model format/version");
    int version = integer(j["version"],1,2);
    if (!j["bones"].is(J::array) || !j["shapes"].is(J::array) || j["bones"].size() > 128 || j["shapes"].size() > 128)
      throw std::runtime_error("Expected bones/shapes arrays (max 128 each)");
    Document d; d.color = vector(j["color"],0,1);
    for (const auto &v : j["bones"].items) {
      Bone b; b.name = name(v["name"]); b.parent = integer(v["parent"], -1, (int)j["bones"].size()-1);
      b.position = vector(v["position"]); b.rotation = vector(v["rotation"],-360,360);
      b.pose = vector(v["pose"],-360,360); b.length = number(v["length"],0.01f,20);
      if (version >= 2) b.offset = vector(v["offset"]);
      d.bones.push_back(b);
    }
    for (int i=0;i<(int)d.bones.size();++i)
      if (!can_parent(d,i,d.bones[i].parent)) throw std::runtime_error("Skeleton contains a cycle");
    for (const auto &v : j["shapes"].items) {
      Shape s; s.name = name(v["name"]); s.kind = integer(v["kind"],0,4);
      s.operation = integer(v["operation"],0,3); s.bone = integer(v["bone"],-1,(int)d.bones.size()-1);
      s.position = vector(v["position"]); s.rotation = vector(v["rotation"],-360,360);
      s.size = vector(v["size"],0.01f,20); s.radius = number(v["radius"],0.01f,20);
      s.height = number(v["height"],0.01f,20); s.thickness = number(v["thickness"],0.01f,20);
      s.blend = number(v["blend"],0,5);
      if (!v["visible"].is(J::boolean)) throw std::runtime_error("Expected visibility boolean");
      s.visible = v["visible"].b; d.shapes.push_back(s);
    }
    if (version >= 2) {
      if (!j["clips"].is(J::array) || j["clips"].size() > 64) throw std::runtime_error("Expected clips array (max 64)");
      size_t total_keys = 0;
      for (const auto &v : j["clips"].items) {
        AnimationClip c; c.name = name(v["name"]); c.duration = number(v["duration"],0.05f,600);
        c.fps = integer(v["fps"],1,120);
        if (!v["loop"].is(J::boolean)) throw std::runtime_error("Expected loop boolean");
        c.loop = v["loop"].b;
        total_keys += v["keys"].size();
        if (!v["keys"].is(J::array) || v["keys"].size() > 10000 || total_keys > 640000)
          throw std::runtime_error("Too many animation keys");
        for (const auto &k : v["keys"].items) {
          Keyframe key;
          key.bone=integer(k["bone"],0,(int)d.bones.size()-1); key.time=number(k["time"],0,c.duration);
          key.translation=vector(k["translation"]); key.rotation=vector(k["rotation"],-360,360);
          c.keys.push_back(key);
        }
        std::sort(c.keys.begin(),c.keys.end(),[](const Keyframe &a,const Keyframe &b) {
          return a.bone==b.bone ? a.time<b.time : a.bone<b.bone;
        });
        for (size_t i=1;i<c.keys.size();++i)
          if (c.keys[i].bone==c.keys[i-1].bone && c.keys[i].time-c.keys[i-1].time<0.0001f)
            throw std::runtime_error("Duplicate animation key time");
        d.clips.push_back(std::move(c));
      }
    }
    out = std::move(d); error.clear(); return true;
  } catch (const std::exception &e) { error = e.what(); return false; }
}
}
