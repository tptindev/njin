#pragma once
#include "njin_internal_only.h"
// IQM, MagicaVoxel VOX and Model 3D (M3D) files, read into plain CPU data for the
// SDL3/bgfx model store (njin_model_portable.cpp). Translated from raylib 6.0's loaders
// (rmodels.c: LoadIQM, LoadModelAnimationsIQM, LoadVOX, LoadM3D, LoadModelAnimationsM3D;
// zlib licence, THIRD_PARTY_NOTICES.md) so a model reads the same as it did through
// raylib's LoadModel on the legacy renderer: same vertices in the same order, same
// materials, same skeleton and the same animation poses.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace njin {

// One mesh as raylib's Mesh held it after loading, unindexed meshes kept unindexed.
struct imported_mesh {
  std::vector<float> positions;             // 3 per vertex
  std::vector<float> normals;               // 3 per vertex, or empty
  std::vector<float> texcoords;             // 2 per vertex, or empty
  std::vector<std::uint8_t> colors;         // 4 per vertex (RGBA8), or empty
  std::vector<std::uint32_t> indices;       // triangles; empty = every 3 vertices a triangle
  std::vector<std::uint8_t> bone_ids;       // 4 per vertex, or empty
  std::vector<float> bone_weights;          // 4 per vertex, or empty
  int material = 0;                         // index into imported_model::materials
};

struct imported_material {
  std::array<float, 4> color{1, 1, 1, 1};   // raylib MATERIAL_MAP_DIFFUSE colour, 0..1
  std::string albedo_path;                  // diffuse texture file, resolved, or empty
  // A diffuse texture embedded in the file (M3D inlined images), RGBA8; empty if none.
  unsigned albedo_width = 0, albedo_height = 0;
  std::vector<std::uint8_t> albedo_rgba;
};

// A bone; `bind` is its pose as raylib's skeleton.bindPose held it (model space).
struct imported_bone {
  std::string name;
  int parent = -1;
  std::array<float, 3> translation{0, 0, 0};
  std::array<float, 4> rotation{0, 0, 0, 1};  // quaternion x, y, z, w
  std::array<float, 3> scale{1, 1, 1};
};

// An animation as raylib's ModelAnimation held it: one pose per keyframe, each pose
// one transform per bone, in model space (as keyframePoses), sampled at `fps`.
struct imported_clip {
  std::string name;
  float fps = 30;
  std::vector<std::vector<imported_bone>> frames;  // [keyframe][bone]; only the TRS fields are used
};

struct imported_model {
  std::vector<imported_mesh> meshes;
  std::vector<imported_material> materials;  // at least one (raylib's default material)
  std::vector<imported_bone> bones;          // empty for a static model
  std::vector<imported_clip> clips;
};

// Each returns false with `error` set when the file is missing or malformed. `path` is a
// resolved file path; texture paths found in the file are resolved against its folder.
bool import_iqm(const std::string &path, imported_model &out, std::string &error);
bool import_vox(const std::string &path, imported_model &out, std::string &error);
bool import_m3d(const std::string &path, imported_model &out, std::string &error);

} // namespace njin
