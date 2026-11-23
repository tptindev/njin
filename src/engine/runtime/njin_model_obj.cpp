#include "njin_model_obj.h"
#define FAST_OBJ_IMPLEMENTATION
#include <fast_obj.h>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace njin {
// Follows the legacy loader (raylib's LoadOBJ on tinyobj_loader_c) so a model
// looks the same on both renderers: faces fan-triangulated, a new group where
// the material changes, V flipped, missing normals point up (+Y), faces without
// a known material use the first one, and `l` polylines are ignored.
ObjData obj_parse(const std::string &path) {
  std::unique_ptr<fastObjMesh, void (*)(fastObjMesh *)> mesh(fast_obj_read(path.c_str()), fast_obj_destroy);
  if (!mesh) throw std::runtime_error("njin OBJ: could not read " + path);
  ObjData out;
  // fast_obj appends a "fallback" material for a usemtl name the mtllib lacks;
  // the legacy loader has no such material and draws those faces with the first.
  std::vector<unsigned> remap(mesh->material_count, 0);
  for (unsigned i = 0; i < mesh->material_count; ++i) {
    const auto &src = mesh->materials[i];
    if (src.fallback) continue;
    remap[i] = unsigned(out.materials.size());
    ObjMaterial m;
    if (src.name) m.name = src.name;
    const auto texture = [&](unsigned index) {
      if (index == 0 || index >= mesh->texture_count) return std::string();
      const auto &t = mesh->textures[index];
      return std::string(t.path ? t.path : t.name ? t.name : "");
    };
    for (unsigned c = 0; c < 3; ++c) {
      if (!std::isfinite(src.Kd[c]) || !std::isfinite(src.Ke[c])) throw std::runtime_error("njin OBJ: non-finite material color in " + path);
      m.diffuse[c] = src.Kd[c];
      m.emission[c] = src.Ke[c];
    }
    m.diffuse_map = texture(src.map_Kd);
    m.normal_map = texture(src.map_bump);
    out.materials.push_back(std::move(m));
  }
  if (out.materials.empty()) out.materials.emplace_back();
  const auto attribute = [&](const float *data, unsigned count, unsigned index, unsigned width, float *dst) {
    if (index == 0 || index >= count) return false;
    for (unsigned c = 0; c < width; ++c) {
      dst[c] = data[size_t(index) * width + c];
      if (!std::isfinite(dst[c])) throw std::runtime_error("njin OBJ: non-finite vertex data in " + path);
    }
    return true;
  };
  size_t base = 0;
  for (unsigned f = 0; f < mesh->face_count; ++f) {
    const unsigned corners = mesh->face_vertices[f];
    const size_t first = base;
    base += corners;
    if (base > mesh->index_count) throw std::runtime_error("njin OBJ: face indices out of range in " + path);
    if ((mesh->face_lines && mesh->face_lines[f]) || corners < 3) continue;
    const unsigned source = mesh->face_materials ? mesh->face_materials[f] : 0;
    const unsigned material = source < remap.size() ? remap[source] : 0;
    if (out.groups.empty() || out.groups.back().material != material) out.groups.push_back({material, {}});
    auto &group = out.groups.back();
    const auto corner = [&](size_t k) {
      const auto &index = mesh->indices[first + k];
      ObjVertex v;
      if (!attribute(mesh->positions, mesh->position_count, index.p, 3, v.position.data()))
        throw std::runtime_error("njin OBJ: face without a valid position in " + path);
      if (attribute(mesh->texcoords, mesh->texcoord_count, index.t, 2, v.uv.data())) v.uv[1] = 1.0f - v.uv[1];
      attribute(mesh->normals, mesh->normal_count, index.n, 3, v.normal.data());
      return v;
    };
    for (unsigned k = 2; k < corners; ++k) {
      group.vertices.push_back(corner(0));
      group.vertices.push_back(corner(k - 1));
      group.vertices.push_back(corner(k));
    }
  }
  if (out.groups.empty()) throw std::runtime_error("njin OBJ: no triangle faces in " + path);
  return out;
}
} // namespace njin
