// IQM, VOX and M3D read by Njin's own importers (njin_model_formats.h) give what
// raylib's LoadModel / LoadModelAnimations gave the legacy renderer: every vertex
// (position, normal, UV, colour, bone ids and weights), every index, the material
// colours, the bones, the bind pose and every keyframe pose. Runs while raylib is still
// in the build (docs/renderer-legacy-removal.md, step 4); a hidden raylib window gives
// LoadModel the GL context it uploads to. Then, end to end: the same files through
// njin::model_load on the SDL3/bgfx renderer give raylib's keyframe poses from
// model_bone_pose() at each keyframe's time (k / 60 s, the rate legacy plays clips at).
//   njin_model_formats_test <file.iqm|.vox|.m3d>...
#include "njin_model_formats.h"
#include "njin.h"
#include "njin_3d.h"
#include <raylib.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <exception>

namespace {
int failures = 0, checks = 0;
std::string where;
void expect(bool ok, const std::string &what) {
  ++checks;
  if (!ok && failures++ < 30)
    std::printf("FAIL %s: %s\n", where.c_str(), what.c_str());
}
bool same(float a, float b) { return std::abs(a - b) <= 1e-5f * std::max(1.0f, std::abs(a)); }
bool same_rotation(const float *a, const Quaternion &b) {
  // q and -q are the same rotation.
  const bool direct = same(a[0], b.x) && same(a[1], b.y) && same(a[2], b.z) && same(a[3], b.w);
  const bool flipped = same(a[0], -b.x) && same(a[1], -b.y) && same(a[2], -b.z) && same(a[3], -b.w);
  return direct || flipped;
}
bool same_transform(const njin::imported_bone &a, const Transform &b) {
  return same(a.translation[0], b.translation.x) && same(a.translation[1], b.translation.y) &&
         same(a.translation[2], b.translation.z) && same_rotation(a.rotation.data(), b.rotation) &&
         same(a.scale[0], b.scale.x) && same(a.scale[1], b.scale.y) && same(a.scale[2], b.scale.z);
}

// What raylib gave, kept for the end-to-end check after its window closes.
struct record {
  std::string path;
  int bones = 0;
  std::vector<std::vector<std::vector<Transform>>> clips; // [clip][keyframe][bone]
};
std::vector<record> records;

void compare(const char *path) {
  where = path;
  std::string ext = std::strrchr(path, '.') ? std::strrchr(path, '.') : "";
  for (auto &c : ext)
    c = (char)std::tolower((unsigned char)c);
  njin::imported_model ours;
  std::string error;
  const bool ok = ext == ".iqm" ? njin::import_iqm(path, ours, error)
                  : ext == ".vox" ? njin::import_vox(path, ours, error)
                                  : njin::import_m3d(path, ours, error);
  expect(ok, "import: " + error);
  if (!ok)
    return;
  Model theirs = LoadModel(path);
  expect(theirs.meshCount == (int)ours.meshes.size(),
         "mesh count " + std::to_string(ours.meshes.size()) + " vs " + std::to_string(theirs.meshCount));
  for (int m = 0; m < theirs.meshCount && m < (int)ours.meshes.size(); ++m) {
    const Mesh &t = theirs.meshes[m];
    const njin::imported_mesh &o = ours.meshes[(size_t)m];
    const std::string at = "mesh " + std::to_string(m);
    expect((int)o.positions.size() == t.vertexCount * 3, at + " vertex count");
    if ((int)o.positions.size() != t.vertexCount * 3)
      continue;
    bool pos = true, nor = true, uv = true, col = true, bones = true, idx = true;
    for (int i = 0; i < t.vertexCount * 3; ++i)
      pos &= same(o.positions[(size_t)i], t.vertices[i]);
    if (t.normals) {
      nor = o.normals.size() == (size_t)t.vertexCount * 3;
      for (int i = 0; nor && i < t.vertexCount * 3; ++i)
        nor &= same(o.normals[(size_t)i], t.normals[i]);
    }
    if (t.texcoords) {
      uv = o.texcoords.size() == (size_t)t.vertexCount * 2;
      for (int i = 0; uv && i < t.vertexCount * 2; ++i)
        uv &= same(o.texcoords[(size_t)i], t.texcoords[i]);
    }
    if (t.colors) {
      col = o.colors.size() == (size_t)t.vertexCount * 4;
      for (int i = 0; col && i < t.vertexCount * 4; ++i)
        col &= o.colors[(size_t)i] == t.colors[i];
    }
    if (t.boneIndices && t.boneWeights) {
      bones = o.bone_ids.size() == (size_t)t.vertexCount * 4 && o.bone_weights.size() == (size_t)t.vertexCount * 4;
      for (int i = 0; bones && i < t.vertexCount * 4; ++i)
        bones &= o.bone_ids[(size_t)i] == t.boneIndices[i] && same(o.bone_weights[(size_t)i], t.boneWeights[i]);
    }
    if (t.indices) {
      idx = o.indices.size() == (size_t)t.triangleCount * 3;
      for (int i = 0; idx && i < t.triangleCount * 3; ++i)
        idx &= o.indices[(size_t)i] == t.indices[i];
    } else {
      idx = o.indices.empty();
    }
    expect(pos, at + " positions");
    expect(nor, at + " normals");
    expect(uv, at + " texcoords");
    expect(col, at + " colours");
    expect(bones, at + " bone ids/weights");
    expect(idx, at + " indices");
    expect(o.material == theirs.meshMaterial[m], at + " material index");
  }
  for (int i = 0; i < theirs.materialCount && i < (int)ours.materials.size(); ++i) {
    const Color c = theirs.materials[i].maps[MATERIAL_MAP_DIFFUSE].color;
    const auto &o = ours.materials[(size_t)i].color;
    expect(std::abs(o[0] * 255 - c.r) < 1 && std::abs(o[1] * 255 - c.g) < 1 && std::abs(o[2] * 255 - c.b) < 1 &&
               std::abs(o[3] * 255 - c.a) < 1,
           "material " + std::to_string(i) + " colour");
  }
  expect(theirs.skeleton.boneCount == (int)ours.bones.size(), "bone count");
  for (int b = 0; b < theirs.skeleton.boneCount && b < (int)ours.bones.size(); ++b) {
    expect(ours.bones[(size_t)b].name == theirs.skeleton.bones[b].name, "bone " + std::to_string(b) + " name");
    expect(ours.bones[(size_t)b].parent == theirs.skeleton.bones[b].parent, "bone " + std::to_string(b) + " parent");
    expect(same_transform(ours.bones[(size_t)b], theirs.skeleton.bindPose[b]), "bone " + std::to_string(b) + " bind pose");
  }
  int count = 0;
  // Animations even from a file without meshes (an IQM animation pack).
  ModelAnimation *anims = ext != ".vox" ? LoadModelAnimations(path, &count) : nullptr;
  expect(count == (int)ours.clips.size(), "clip count " + std::to_string(ours.clips.size()) + " vs " + std::to_string(count));
  for (int a = 0; a < count && a < (int)ours.clips.size(); ++a) {
    const auto &o = ours.clips[(size_t)a];
    const std::string at = "clip " + std::to_string(a);
    expect(o.name == anims[a].name, at + " name");
    expect((int)o.frames.size() == anims[a].keyframeCount, at + " keyframe count");
    bool poses = true;
    for (int f = 0; f < anims[a].keyframeCount && f < (int)o.frames.size(); ++f)
      for (int b = 0; b < anims[a].boneCount && b < (int)o.frames[(size_t)f].size(); ++b)
        poses &= same_transform(o.frames[(size_t)f][(size_t)b], anims[a].keyframePoses[f][b]);
    expect(poses, at + " keyframe poses");
  }
  record rec;
  rec.path = path;
  rec.bones = theirs.meshCount > 0 ? theirs.skeleton.boneCount : -1; // -1: nothing to model_load
  for (int a = 0; a < count; ++a) {
    std::vector<std::vector<Transform>> frames;
    for (int f = 0; f < anims[a].keyframeCount; ++f)
      frames.emplace_back(anims[a].keyframePoses[f], anims[a].keyframePoses[f] + anims[a].boneCount);
    rec.clips.push_back(std::move(frames));
  }
  records.push_back(std::move(rec));
  if (anims)
    UnloadModelAnimations(anims, count);
  UnloadModel(theirs);
  std::printf("%s: %zu meshes, %zu bones, %zu clips\n", path, ours.meshes.size(), ours.bones.size(), ours.clips.size());
}
} // namespace

int main(int argc, char **argv) {
  SetTraceLogLevel(LOG_WARNING);
  SetConfigFlags(FLAG_WINDOW_HIDDEN);
  InitWindow(64, 64, "njin model formats test");
  for (int i = 1; i < argc; ++i)
    compare(argv[i]);
  CloseWindow();

  njin::config cfg{.title = "njin model formats", .width = 320.0f, .height = 240.0f};
  cfg.audio = false;
  njin::context *ctx = njin::create(cfg);
  for (const record &rec : records) {
    if (rec.bones < 0)
      continue;
    where = rec.path + " (model_load)";
    njin::model_handle model{};
    try {
      model = njin::model_load(*ctx, rec.path.c_str());
    } catch (const std::exception &e) {
      expect(false, std::string("model_load threw: ") + e.what());
      continue;
    }
    expect(model.id != 0, "model_load");
    if (model.id == 0)
      continue;
    expect(njin::model_bone_count(*ctx, model) == rec.bones, "model_bone_count");
    expect(njin::model_anim_count(*ctx, model) == (int)rec.clips.size(), "model_anim_count");
    double worst = 0;
    for (size_t a = 0; a < rec.clips.size() && rec.bones > 0; ++a) {
      const auto &frames = rec.clips[a];
      // The last keyframe is replaced by the one before (legacy's end-of-clip fix).
      for (size_t k = 0; k + 1 < frames.size(); ++k)
        for (int b = 0; b < rec.bones; ++b) {
          njin::model_pose pose{};
          pose.anim = (int)a;
          pose.time = (float)k / 60.0f;
          pose.loop = false;
          const njin::bone_pose3d got = njin::model_bone_pose(*ctx, model, pose, b);
          const Transform &want = frames[k][(size_t)b];
          const float d[3] = {got.position.x - want.translation.x, got.position.y - want.translation.y,
                              got.position.z - want.translation.z};
          worst = std::max(worst, (double)std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
          // The bone's y axis: the rotation applied to (0, 1, 0).
          const Quaternion q = want.rotation;
          const float yx = 2 * (q.x * q.y - q.w * q.z), yy = 1 - 2 * (q.x * q.x + q.z * q.z), yz = 2 * (q.y * q.z + q.w * q.x);
          const float axis = std::abs(got.y_axis.x - yx) + std::abs(got.y_axis.y - yy) + std::abs(got.y_axis.z - yz);
          worst = std::max(worst, (double)axis);
        }
    }
    std::printf("%s: model_load %d bones, %zu clips, worst keyframe difference %.6f\n", rec.path.c_str(), rec.bones,
                rec.clips.size(), worst);
    expect(worst < 1e-3, "model_bone_pose at keyframes matches raylib's poses");
    njin::model_unload(*ctx, model);
  }
  njin::destroy(ctx);
  std::printf("model_formats: %d checks, %d failed\n", checks, failures);
  return failures == 0 && checks > 0 ? 0 : 1;
}
