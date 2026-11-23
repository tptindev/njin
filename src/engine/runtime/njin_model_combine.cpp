#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_log.h"
#include "njin_model.h"
#include "njin_model_portable.h"
#include <stdexcept>
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <atomic>
#include <thread>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

// model_combine() (njin_3d.h): placed models baked into one static model,
// their meshes merged by material name, their levels of detail alike. A town
// of kit pieces drawn as a few such models costs a few draws a pass instead of
// one per piece type and material, and a few ray tracing instances instead of
// thousands.

namespace njin {
namespace {
constexpr u32 most_vertices = 65535; // raylib's 16-bit indices
constexpr i32 map_count = 12;        // raylib's MAX_MATERIAL_MAPS

Matrix placement(const transform3d &t) {
  const vec3 r = t.rotation * (PI / 180.0f);
  Matrix m = MatrixScale(t.scale.x, t.scale.y, t.scale.z);
  m = MatrixMultiply(m, MatrixRotateZ(r.z));
  m = MatrixMultiply(m, MatrixRotateX(r.x));
  m = MatrixMultiply(m, MatrixRotateY(r.y));
  return MatrixMultiply(m, MatrixTranslate(t.position.x, t.position.y, t.position.z));
}

// Mesh `i` of `slot` at `level` (0 is the model's own): the simplest it has up to there.
const Mesh &level_mesh(const model_slot &slot, i32 i, usize level) {
  for (usize l = std::min(level, slot.lods.size()); l > 0; l--) {
    const Mesh &m = slot.lods[l - 1][(usize)i].mesh;
    if (m.vaoId != 0 && m.vertices != nullptr)
      return m;
  }
  return slot.model.meshes[i];
}

// One source mesh: which place, which of its model's meshes.
struct piece {
  u32 place;
  i32 mesh;
};

// The meshes of one material: pieces split so each mesh stays under 65535
// vertices at full detail, and every level splits the same way.
struct group {
  std::string name;
  const model_slot *from = nullptr; // the first model with this material
  i32 material = 0;
  std::vector<std::vector<piece>> meshes;
  u32 last_vertices = 0;
};

// One combined mesh from its parts, each placed by its transform (normals and
// tangents by its normal matrix): counted first, then written straight into
// the arrays raylib keeps (it frees them with RL_FREE). Not uploaded: built on
// any thread, uploaded on the one with the GL context.
struct placed_part {
  const Mesh *mesh;
  const Matrix *t, *n;
};

Mesh build(const std::vector<placed_part> &parts, bool tangents) {
  u32 vertices = 0, indices = 0;
  for (const placed_part &p : parts) {
    vertices += (u32)p.mesh->vertexCount;
    indices += (u32)(p.mesh->indices != nullptr ? p.mesh->triangleCount * 3 : p.mesh->vertexCount);
  }
  Mesh m{};
  m.vertexCount = (i32)vertices;
  m.triangleCount = (i32)(indices / 3);
  m.vertices = (f32 *)MemAlloc(vertices * 12);
  m.normals = (f32 *)MemAlloc(vertices * 12);
  m.texcoords = (f32 *)MemAlloc(vertices * 8);
  m.colors = (u8 *)MemAlloc(vertices * 4);
  m.tangents = tangents ? (f32 *)MemAlloc(vertices * 16) : nullptr;
  m.indices = (u16 *)MemAlloc(indices * 2);
  f32 *pos = m.vertices, *nrm = m.normals, *uv = m.texcoords, *tan = m.tangents;
  u8 *col = m.colors;
  u16 *idx = m.indices;
  u32 base = 0;
  for (const placed_part &p : parts) {
    const Mesh &src = *p.mesh;
    const Matrix &t = *p.t, &n = *p.n;
    for (i32 v = 0; v < src.vertexCount; v++) {
      const Vector3 q = Vector3Transform({src.vertices[v * 3], src.vertices[v * 3 + 1], src.vertices[v * 3 + 2]}, t);
      *pos++ = q.x, *pos++ = q.y, *pos++ = q.z;
      Vector3 d{0.0f, 1.0f, 0.0f};
      if (src.normals != nullptr)
        d = Vector3Normalize(Vector3Transform({src.normals[v * 3], src.normals[v * 3 + 1], src.normals[v * 3 + 2]}, n));
      *nrm++ = d.x, *nrm++ = d.y, *nrm++ = d.z;
      *uv++ = src.texcoords != nullptr ? src.texcoords[v * 2] : 0.0f;
      *uv++ = src.texcoords != nullptr ? src.texcoords[v * 2 + 1] : 0.0f;
      if (src.colors != nullptr)
        std::memcpy(col, src.colors + v * 4, 4);
      else
        std::memset(col, 255, 4);
      col += 4;
      if (tan != nullptr) {
        Vector3 e{1.0f, 0.0f, 0.0f};
        f32 w = 1.0f;
        if (src.tangents != nullptr) {
          e = Vector3Normalize(Vector3Transform({src.tangents[v * 4], src.tangents[v * 4 + 1], src.tangents[v * 4 + 2]}, n));
          w = src.tangents[v * 4 + 3];
        }
        *tan++ = e.x, *tan++ = e.y, *tan++ = e.z, *tan++ = w;
      }
    }
    if (src.indices != nullptr)
      for (i32 k = 0; k < src.triangleCount * 3; k++)
        *idx++ = (u16)(base + src.indices[k]);
    else
      for (i32 k = 0; k < src.vertexCount; k++)
        *idx++ = (u16)(base + (u32)k);
    base += (u32)src.vertexCount;
  }
  return m;
}
} // namespace

model_handle model_combine(context &ctx, const model_place *places, u32 count, f32 screen, u32 from_level) {
  if(ctx_ref<model_store>(ctx)->portable_context) return portable_model_combine(ctx,places,count,screen,from_level);
  model_store &store = ctx_ref<model_store>(ctx);
  std::vector<group> groups;
  std::unordered_map<std::string, usize> by_name;
  std::vector<Matrix> transforms(count), normals(count);
  usize levels = 0;
  bool tangents = false;
  for (u32 p = 0; p < count && places != nullptr; p++) {
    const model_slot *slot = model_slot_of(store, places[p].model);
    if (slot == nullptr)
      continue;
    if (slot->skinned || !slot->morphs.empty()) {
      NJIN_WARN("model_combine: a model with bones or morphs is left out");
      continue;
    }
    transforms[p] = MatrixMultiply(slot->model.transform, placement(places[p].transform));
    normals[p] = MatrixTranspose(MatrixInvert(transforms[p]));
    normals[p].m12 = normals[p].m13 = normals[p].m14 = 0.0f;
    // The pieces' levels past `from_level` make the new model's simpler ones.
    levels = std::max(levels, slot->lods.size() > from_level ? slot->lods.size() - from_level : 0);
    for (i32 i = 0; i < slot->model.meshCount; i++) {
      const i32 material = slot->model.meshMaterial[i];
      const std::string &name = (usize)material < slot->material_names.size() ? slot->material_names[(usize)material] : "";
      // Unnamed materials only merge within their own model.
      const std::string key = name.empty() ? "#" + std::to_string(places[p].model.id) + ":" + std::to_string(material) : name;
      auto found = by_name.find(key);
      if (found == by_name.end()) {
        found = by_name.emplace(key, groups.size()).first;
        groups.push_back({.name = name, .from = slot, .material = material});
      }
      group &g = groups[found->second];
      const u32 vertices = (u32)slot->model.meshes[i].vertexCount;
      if (g.meshes.empty() || g.last_vertices + vertices > most_vertices) {
        g.meshes.emplace_back();
        g.last_vertices = 0;
      }
      g.meshes.back().push_back({p, i});
      g.last_vertices += vertices;
      tangents |= slot->model.meshes[i].tangents != nullptr;
    }
  }
  usize mesh_count = 0;
  for (const group &g : groups)
    mesh_count += g.meshes.size();
  if (mesh_count == 0) {
    NJIN_WARN("model_combine: no meshes to combine");
    return model_handle{};
  }
  model_slot slot;
  Model &model = slot.model;
  model.transform = MatrixIdentity();
  model.meshCount = (i32)mesh_count;
  model.meshes = (Mesh *)MemAlloc((u32)(mesh_count * sizeof(Mesh)));
  model.meshMaterial = (i32 *)MemAlloc((u32)(mesh_count * sizeof(i32)));
  model.materialCount = (i32)groups.size();
  model.materials = (Material *)MemAlloc((u32)(groups.size() * sizeof(Material)));
  slot.lods.assign(levels, std::vector<model_lod_mesh>(mesh_count));
  usize out = 0;
  struct job {
    std::vector<placed_part> parts;
    Mesh *into = nullptr;
  };
  std::vector<job> jobs;
  for (usize gi = 0; gi < groups.size(); gi++) {
    const group &g = groups[gi];
    // The material: a copy of the first model's, sharing its textures; the
    // shader is the default one, as for every file (njin binds its own).
    Material &material = model.materials[gi];
    material = g.from->model.materials[g.material];
    material.shader = Shader{rlGetShaderIdDefault(), rlGetShaderLocsDefault()};
    material.maps = (MaterialMap *)MemAlloc((u32)(map_count * sizeof(MaterialMap)));
    std::memcpy(material.maps, g.from->model.materials[g.material].maps, map_count * sizeof(MaterialMap));
    slot.materials.push_back(g.from->materials[(usize)g.material]);
    slot.material_names.push_back(g.name);
    for (const std::vector<piece> &pieces : g.meshes) {
      for (usize level = 0; level <= levels; level++) {
        job j;
        u32 vertices = 0;
        for (const piece &pc : pieces) {
          const Mesh &m = level_mesh(*model_slot_of(store, places[pc.place].model), pc.mesh, level + from_level);
          j.parts.push_back({&m, &transforms[pc.place], &normals[pc.place]});
          vertices += (u32)m.vertexCount;
        }
        // Past 65535 at a simpler level: none there, the level above is drawn.
        if (level == 0 || vertices <= most_vertices)
          j.into = level == 0 ? &model.meshes[out] : &slot.lods[level - 1][out].mesh, jobs.push_back(std::move(j));
      }
      model.meshMaterial[out++] = (i32)gi;
    }
  }
  // The meshes are built on a few threads (the sources are only read), then
  // uploaded here, where the GL context is.
  std::atomic<usize> next{0};
  const auto work = [&]() {
    for (usize i = next++; i < jobs.size(); i = next++)
      *jobs[i].into = build(jobs[i].parts, tangents);
  };
  const usize helpers = std::min<usize>(std::max(std::thread::hardware_concurrency(), 1u) - 1, jobs.size() / 4);
  std::vector<std::thread> threads;
  for (usize t = 0; t < helpers; t++)
    threads.emplace_back(work);
  work();
  for (std::thread &t : threads)
    t.join();
  for (const job &j : jobs)
    UploadMesh(j.into, false);
  slot.alive = true;
  slot.borrows_textures = true;
  slot.bounds = GetModelBoundingBox(model);
  slot.lod_screen = std::max(screen, 0.0f);
  // What a pass pays for the result: one draw per mesh, and raylib's 16-bit
  // indices split a material's pieces into meshes of up to 65535 vertices.
  NJIN_INFO("model_combine: %u places into %zu meshes of %zu materials, %zu levels", count, mesh_count, groups.size(),
            levels);
  store.slots.push_back(std::move(slot));
  return model_handle{.id = (u32)store.slots.size()};
}

} // namespace njin
