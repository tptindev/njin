#pragma once
// Wavefront OBJ/MTL parsing for the portable (bgfx) model loader, on fast_obj
// (MIT, shipped with the pinned meshoptimizer checkout). CPU only: no GPU or
// raylib calls, so the result is turned into renderer meshes by the caller.
#include "njin_internal_only.h"
#include <array>
#include <string>
#include <vector>

namespace njin {
struct ObjMaterial {
  std::string name;
  std::array<float, 3> diffuse{1, 1, 1}, emission{0, 0, 0}; // Kd, Ke
  std::string diffuse_map, normal_map;                      // map_Kd, map_bump (resolved paths)
};
struct ObjVertex {
  std::array<float, 3> position{}, normal{0, 1, 0};
  std::array<float, 2> uv{};
};
// A run of triangles (fan-triangulated faces) sharing one material.
struct ObjGroup {
  unsigned material = 0;
  std::vector<ObjVertex> vertices; // three per triangle
};
struct ObjData {
  // The mtllib's materials in file order; one default material when it has none.
  std::vector<ObjMaterial> materials;
  std::vector<ObjGroup> groups;
};
// Throws std::runtime_error when the file cannot be read or is malformed.
ObjData obj_parse(const std::string &path);
} // namespace njin
