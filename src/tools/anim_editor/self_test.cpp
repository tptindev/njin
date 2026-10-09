#include "document.h"
#include "roundtrip.h"
#include "cgltf.h"
#include "raymath.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace anim_editor {
namespace {
using J = njin::json_value;
std::string base64(const std::vector<uint8_t> &in) {
  static const char *abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  for (size_t i = 0; i < in.size(); i += 3) {
    uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < in.size() ? (uint32_t)in[i + 1] << 8 : 0) |
                 (i + 2 < in.size() ? (uint32_t)in[i + 2] : 0);
    out += abc[v >> 18 & 63];
    out += abc[v >> 12 & 63];
    out += i + 1 < in.size() ? abc[v >> 6 & 63] : '=';
    out += i + 2 < in.size() ? abc[v & 63] : '=';
  }
  return out;
}
float dot4(Quaternion a, Quaternion b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
J numbers(std::initializer_list<double> values) {
  auto a = J::make_array();
  for (double v : values)
    a.push(v);
  return a;
}
} // namespace

// A column of four rings on three chained joints (Hip > Spine > Head) under a
// non-joint "Armature" node, with a "Bend" clip: Spine turns 45 degrees about
// z, Head scales up (a channel the editor does not edit, kept on export).
std::string write_test_model(const std::string &folder) {
  std::vector<uint8_t> bin;
  auto put = [&](const void *p, size_t n) {
    while (bin.size() % 4)
      bin.push_back(0);
    size_t at = bin.size();
    bin.insert(bin.end(), (const uint8_t *)p, (const uint8_t *)p + n);
    return at;
  };
  std::vector<float> pos, weights;
  std::vector<uint8_t> joints;
  for (int ring = 0; ring < 4; ++ring)
    for (int c = 0; c < 4; ++c) {
      pos.insert(pos.end(), {0.5f + ((c == 1 || c == 2) ? 0.2f : -0.2f), (float)ring, (c >= 2) ? 0.2f : -0.2f});
      joints.insert(joints.end(), {(uint8_t)std::min(ring, 2), 0, 0, 0});
      weights.insert(weights.end(), {1, 0, 0, 0});
    }
  std::vector<uint16_t> idx;
  for (int ring = 0; ring < 3; ++ring)
    for (int c = 0; c < 4; ++c) {
      uint16_t a = (uint16_t)(ring * 4 + c), b = (uint16_t)(ring * 4 + (c + 1) % 4);
      idx.insert(idx.end(), {a, b, (uint16_t)(b + 4), a, (uint16_t)(b + 4), (uint16_t)(a + 4)});
    }
  std::vector<float> ibm;
  for (int j = 0; j < 3; ++j)
    ibm.insert(ibm.end(), {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -0.5f, -(float)j, 0, 1});
  const float s = std::sin(22.5f * DEG2RAD), co = std::cos(22.5f * DEG2RAD);
  std::vector<float> times{0, 1}, rot{0, 0, 0, 1, 0, 0, s, co}, scale{1, 1, 1, 1.5f, 1.5f, 1.5f};
  struct View {
    size_t offset, size;
  };
  std::vector<View> v{{put(pos.data(), pos.size() * 4), pos.size() * 4},
                      {put(joints.data(), joints.size()), joints.size()},
                      {put(weights.data(), weights.size() * 4), weights.size() * 4},
                      {put(idx.data(), idx.size() * 2), idx.size() * 2},
                      {put(ibm.data(), ibm.size() * 4), ibm.size() * 4},
                      {put(times.data(), 8), 8},
                      {put(rot.data(), 32), 32},
                      {put(scale.data(), 24), 24}};
  while (bin.size() % 4)
    bin.push_back(0);
  auto views = J::make_array(), accessors = J::make_array();
  for (const auto &w : v) {
    auto o = J::make_object();
    views.push(o.set("buffer", 0).set("byteOffset", (double)w.offset).set("byteLength", (double)w.size));
  }
  auto accessor = [&](int view, int component, const char *type, int count) {
    auto o = J::make_object();
    o.set("bufferView", view).set("componentType", component).set("type", type).set("count", count);
    return o;
  };
  accessors.push(accessor(0, 5126, "VEC3", 16).set("min", numbers({0.3, 0, -0.2})).set("max", numbers({0.7, 3, 0.2})))
      .push(accessor(1, 5121, "VEC4", 16))
      .push(accessor(2, 5126, "VEC4", 16))
      .push(accessor(3, 5123, "SCALAR", (int)idx.size()))
      .push(accessor(4, 5126, "MAT4", 3))
      .push(accessor(5, 5126, "SCALAR", 2).set("min", numbers({0})).set("max", numbers({1})))
      .push(accessor(6, 5126, "VEC4", 2))
      .push(accessor(7, 5126, "VEC3", 2));
  auto node = [](const char *name) {
    auto o = J::make_object();
    o.set("name", name);
    return o;
  };
  auto nodes = J::make_array();
  nodes.push(node("Armature").set("translation", numbers({0.5, 0, 0})).set("children", numbers({1})))
      .push(node("Hip").set("children", numbers({2})))
      .push(node("Spine").set("translation", numbers({0, 1, 0})).set("children", numbers({3})))
      .push(node("Head").set("translation", numbers({0, 1, 0})))
      .push(node("Body").set("mesh", 0).set("skin", 0));
  auto attributes = J::make_object();
  attributes.set("POSITION", 0).set("JOINTS_0", 1).set("WEIGHTS_0", 2);
  auto prim = J::make_object(), mesh = J::make_object(), skin = J::make_object(), material = J::make_object(),
       pbr = J::make_object();
  prim.set("attributes", attributes).set("indices", 3).set("material", 0);
  mesh.set("name", "Body").set("primitives", J::make_array().push(prim));
  skin.set("joints", numbers({1, 2, 3})).set("inverseBindMatrices", 4).set("skeleton", 1);
  pbr.set("baseColorFactor", numbers({0.8, 0.5, 0.3, 1}));
  material.set("pbrMetallicRoughness", pbr);
  auto sampler = [](int in, int out) {
    auto o = J::make_object();
    o.set("input", in).set("output", out).set("interpolation", "LINEAR");
    return o;
  };
  auto channel = [](int sampler, int node, const char *path) {
    auto o = J::make_object(), target = J::make_object();
    target.set("node", node).set("path", path);
    return o.set("sampler", sampler).set("target", target);
  };
  auto anim = J::make_object();
  anim.set("name", "Bend")
      .set("samplers", J::make_array().push(sampler(5, 6)).push(sampler(5, 7)))
      .set("channels", J::make_array().push(channel(0, 2, "rotation")).push(channel(1, 3, "scale")));
  auto buffer = J::make_object(), asset = J::make_object(), scene = J::make_object(), root = J::make_object();
  buffer.set("byteLength", (double)bin.size()).set("uri", "data:application/octet-stream;base64," + base64(bin));
  asset.set("version", "2.0");
  scene.set("nodes", numbers({0, 4}));
  root.set("asset", asset)
      .set("scene", 0)
      .set("scenes", J::make_array().push(scene))
      .set("nodes", nodes)
      .set("meshes", J::make_array().push(mesh))
      .set("skins", J::make_array().push(skin))
      .set("materials", J::make_array().push(material))
      .set("accessors", accessors)
      .set("bufferViews", views)
      .set("buffers", J::make_array().push(buffer))
      .set("animations", J::make_array().push(anim));
  const std::string path = (std::filesystem::path(folder) / "column.gltf").string();
  if (!njin::json_save(path.c_str(), root))
    throw std::runtime_error("cannot write the test model");
  return path;
}

// A "Wave" clip on the test model: the hip rises, the head turns and back.
AnimationClip wave_clip() {
  AnimationClip c;
  c.name = "Wave";
  c.duration = 1;
  set_key(c, {0, 0, {}, {}});
  set_key(c, {0, 1, {0, 0.5f, 0}, {}});
  set_key(c, {2, 0, {}, {}});
  set_key(c, {2, 0.5f, {}, {30, 0, 80}});
  set_key(c, {2, 1, {}, {0, 0, 0}});
  return c;
}

std::string temp_folder(const char *tag) {
  auto folder = std::filesystem::temp_directory_path() /
                (std::string("njin-anim-editor-") + tag + "-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(folder);
  return folder.string();
}

int self_test() {
  int checks = 0;
  auto check = [&](bool condition, const char *message) {
    ++checks;
    if (!condition)
      throw std::runtime_error(message);
  };
  auto near = [](Vector3 a, Vector3 b, float e = 1e-4f) { return Vector3Distance(a, b) < e; };
  const std::string folder = temp_folder("self");
  try {
    const std::string model = write_test_model(folder);
    Rig rig;
    std::vector<AnimationClip> clips;
    std::string error;
    check(load_rig(model, rig, clips, error), "load the skinned test model");
    check(rig.bones.size() == 3 && rig.bones[0].name == "Hip" && rig.bones[2].name == "Head", "joints in skin order");
    check(rig.bones[0].parent == -1 && rig.bones[1].parent == 0 && rig.bones[2].parent == 1, "joint parents");
    auto rest = bone_world(rig, nullptr);
    check(near({rest[2].m12, rest[2].m13, rest[2].m14}, {0.5f, 2, 0}), "rest world includes the Armature node");
    check(clips.size() == 1 && clips[0].name == "Bend", "import the file's clip");
    check(clips[0].keys.size() == 2 && clips[0].keys[1].bone == 1, "linear channel keeps its two keys");
    check(near(clips[0].keys[1].rotation, {0, 0, 45}, 1e-3f), "imported rotation as pose degrees");
    auto half = sample_animation(3, clips[0], 0.5f);
    Vector3 axis{};
    float angle = 0;
    QuaternionToAxisAngle(half[1].rotation, &axis, &angle);
    check(std::abs(angle - 22.5f * DEG2RAD) < 1e-4f, "slerp between keys");
    auto posed = bone_world(rig, &half);
    check(near({posed[2].m12, posed[2].m13, posed[2].m14},
               {0.5f - std::sin(22.5f * DEG2RAD), 1 + std::cos(22.5f * DEG2RAD), 0}),
          "child follows posed parent");
    // A gizmo edit becomes the key that reproduces it.
    Matrix local = MatrixMultiply(QuaternionToMatrix(euler_quaternion({10, 20, 30})), MatrixTranslate(0.1f, 1.2f, 0));
    Keyframe k = key_from_local(rig, 1, 0.25f, local);
    AnimationClip edit;
    edit.duration = 1;
    check(set_key(edit, k), "set key");
    auto back = sample_animation(3, edit, 0.25f);
    check(near(back[1].translation, {0.1f, 0.2f, 0}) &&
              std::abs(dot4(back[1].rotation, euler_quaternion({10, 20, 30}))) > 0.99999f,
          "key from a local matrix round trips");
    check(remove_key(edit, 1, 0.25f) && edit.keys.empty(), "remove key");

    Document doc{model, {clips[0], wave_clip()}};
    std::string text = njin::json_dump(serialize(doc, rig), false);
    njin::json_value json;
    Document loaded;
    check(njin::json_parse(text, json) && deserialize(json, rig, loaded, error), "project JSON loads");
    check(text == njin::json_dump(serialize(loaded, rig), false), "project round trip");
    auto wrong = serialize(doc, rig);
    wrong.find("clips")->items[1].find("keys")->items[0].set("bone", "Tail");
    check(!deserialize(wrong, rig, loaded, error) && error.find("Tail") != std::string::npos, "unknown bone refused");
    auto old = J::make_object();
    old.set("format", "njin.sdf-model");
    check(!deserialize(old, rig, loaded, error) && error.find(".model.json") != std::string::npos,
          "old SDF project refused with a reason");

    const std::string out = (std::filesystem::path(folder) / "column.glb").string();
    check(export_glb(model, rig, doc.clips, out, error), "export .glb");
    Rig again;
    std::vector<AnimationClip> again_clips;
    check(load_rig(out, again, again_clips, error), "exported file loads");
    check(again.bones.size() == 3 && again_clips.size() == 2 && again_clips[1].name == "Wave", "clips exported");
    float worst = 0;
    for (float t : {0.0f, 0.2f, 0.5f, 0.8f, 1.0f})
      for (int c = 0; c < 2; ++c) {
        auto a = sample_animation(3, doc.clips[c], t), b = sample_animation(3, again_clips[c], t);
        auto wa = bone_world(rig, &a), wb = bone_world(again, &b);
        for (int i = 0; i < 3; ++i)
          for (int e = 0; e < 16; ++e)
            worst = std::max(worst, std::abs(MatrixToFloatV(wa[i]).v[e] - MatrixToFloatV(wb[i]).v[e]));
      }
    check(worst < 1e-4f, "re-imported clips pose the same");
    cgltf_options options{};
    cgltf_data *data = nullptr;
    check(cgltf_parse_file(&options, out.c_str(), &data) == cgltf_result_success, "parse exported .glb");
    bool scale_kept = false;
    for (cgltf_size a = 0; a < data->animations_count; ++a)
      for (cgltf_size c = 0; c < data->animations[a].channels_count; ++c)
        scale_kept |= std::strcmp(data->animations[a].name, "Bend") == 0 &&
                      data->animations[a].channels[c].target_path == cgltf_animation_path_type_scale;
    const bool kept = data->file_type == cgltf_file_type_glb && data->meshes_count == 1 && data->skins_count == 1 &&
                      data->skins[0].joints_count == 3 && data->materials_count == 1 && data->buffers_count == 1;
    cgltf_free(data);
    check(kept, "mesh, skin and material kept in one GLB buffer");
    check(scale_kept, "channels the editor does not edit are kept");
    std::printf("PASS: %d checks (rig load, sampling, keys, project JSON, glTF export and re-import; worst %.2g)\n",
                checks, worst);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "FAIL after %d checks: %s\n", checks, e.what());
    return 1;
  }
  std::error_code ec;
  std::filesystem::remove_all(folder, ec);
  return 0;
}

int roundtrip_test(const char *source) {
  // The generated column with a new "Wave" clip, or a given model exported
  // with its own clips unchanged and checked on its first clip.
  const std::string folder = temp_folder("roundtrip");
  const std::string model = source ? std::string(source) : write_test_model(folder);
  const std::string out = (std::filesystem::path(folder) / "export.glb").string();
  Rig rig;
  std::vector<AnimationClip> clips;
  std::string error;
  bool ok = load_rig(model, rig, clips, error);
  if (ok && !source)
    clips.push_back(wave_clip());
  if (ok && clips.empty()) {
    error = "the model has no clip to compare";
    ok = false;
  }
  if (!ok || !export_glb(model, rig, clips, out, error)) {
    std::fprintf(stderr, "FAIL: %s\n", error.c_str());
    return 1;
  }
  const AnimationClip &checked = source ? clips.front() : clips.back();
  std::vector<RoundtripSample> samples;
  // Inside the clip: at its very end njin plays the frame 1/60 s before.
  for (float k : {0.0f, 0.25f, 0.4f, 0.5f, 0.75f, 0.95f}) {
    const float t = k * checked.duration;
    auto pose = sample_animation((int)rig.bones.size(), checked, t);
    auto world = bone_world(rig, &pose);
    for (size_t b = 0; b < rig.bones.size(); ++b) {
      const Matrix &m = world[b];
      RoundtripSample s;
      s.bone = rig.bones[b].name;
      s.time = t;
      const Vector3 axes[3]{Vector3Normalize({m.m0, m.m1, m.m2}), Vector3Normalize({m.m4, m.m5, m.m6}),
                            Vector3Normalize({m.m8, m.m9, m.m10})};
      for (int a = 0; a < 3; ++a)
        s.axes[a * 3] = axes[a].x, s.axes[a * 3 + 1] = axes[a].y, s.axes[a * 3 + 2] = axes[a].z;
      s.position[0] = m.m12, s.position[1] = m.m13, s.position[2] = m.m14;
      samples.push_back(s);
    }
  }
  const RoundtripResult r = njin_roundtrip(out, checked.name, samples);
  ok = r.loaded && r.bones == (int)rig.bones.size() && r.clips == (int)clips.size() && r.max_angle < 0.5 &&
       r.max_offset < 1e-3;
  std::printf("%s: njin model_load() of the export: %d bones, %d clips, %zu samples, worst %.4f deg, %.6f units%s%s\n",
              ok ? "PASS" : "FAIL", r.bones, r.clips, samples.size(), r.max_angle, r.max_offset,
              r.error.empty() ? "" : ", ", r.error.c_str());
  std::error_code ec;
  std::filesystem::remove_all(folder, ec);
  return ok ? 0 : 1;
}
} // namespace anim_editor
