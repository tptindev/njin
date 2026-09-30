#include "njin_ctx.h"
#include "njin_ctx_impl.h"
#include "njin_log.h"
#include "njin_model.h"
#include <meshoptimizer.h>
#include <rlgl.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

// Levels of detail for model_lod_build() (njin_3d.h), simplified by
// meshoptimizer (MIT, github.com/zeux/meshoptimizer). The draw picks the
// level in render3d.cpp (place_model).

namespace njin {
namespace {
// A vertex stream of a mesh: its data and bytes per vertex.
struct stream {
  const void *data = nullptr;
  usize size = 0;
};

// The mesh's vertex streams, in a fixed order (see set_streams()).
std::vector<stream> streams_of(const Mesh &m) {
  return {{m.vertices, 12},   {m.normals, 12},   {m.texcoords, 8},   {m.texcoords2, 8},
          {m.tangents, 16},   {m.colors, 4},     {m.boneIndices, 4}, {m.boneWeights, 16}};
}

// Points the mesh's attribute arrays at `data` (the same order as streams_of).
void set_streams(Mesh &m, const std::vector<void *> &data) {
  m.vertices = (f32 *)data[0];
  m.normals = (f32 *)data[1];
  m.texcoords = (f32 *)data[2];
  m.texcoords2 = (f32 *)data[3];
  m.tangents = (f32 *)data[4];
  m.colors = (u8 *)data[5];
  m.boneIndices = (u8 *)data[6];
  m.boneWeights = (f32 *)data[7];
}

// A mesh with shared vertices: raylib meshes made without indices (and
// model_create() ones) repeat each vertex once per triangle, and the
// simplifier can only merge what is shared.
struct welded {
  std::vector<u32> indices;
  std::vector<std::vector<u8>> data; // per stream of streams_of(), empty when absent
  usize vertex_count = 0;
};

welded weld(const Mesh &m) {
  welded w;
  const usize n = (usize)m.vertexCount;
  std::vector<u32> indices((usize)m.triangleCount * 3);
  for (usize i = 0; i < indices.size(); i++)
    indices[i] = m.indices != nullptr ? m.indices[i] : (u32)i;
  const std::vector<stream> streams = streams_of(m);
  std::vector<meshopt_Stream> present;
  for (const stream &s : streams)
    if (s.data != nullptr)
      present.push_back({s.data, s.size, s.size});
  std::vector<u32> remap(n);
  w.vertex_count =
      meshopt_generateVertexRemapMulti(remap.data(), indices.data(), indices.size(), n, present.data(), present.size());
  w.indices.resize(indices.size());
  meshopt_remapIndexBuffer(w.indices.data(), indices.data(), indices.size(), remap.data());
  w.data.resize(streams.size());
  for (usize k = 0; k < streams.size(); k++) {
    if (streams[k].data == nullptr)
      continue;
    w.data[k].resize(w.vertex_count * streams[k].size);
    meshopt_remapVertexBuffer(w.data[k].data(), streams[k].data, n, streams[k].size, remap.data());
  }
  return w;
}

// A raylib mesh of the triangles `indices` of `w`, keeping only the vertices
// they use, uploaded. vaoId 0 when it does not fit (16-bit indices).
Mesh make_mesh(const welded &w, const Mesh &source, const std::vector<u32> &indices) {
  std::vector<u32> remap(w.vertex_count);
  const usize n = meshopt_optimizeVertexFetchRemap(remap.data(), indices.data(), indices.size(), w.vertex_count);
  if (n == 0 || n > 65535)
    return Mesh{};
  const std::vector<stream> streams = streams_of(source);
  Mesh m{};
  m.vertexCount = (i32)n;
  m.triangleCount = (i32)(indices.size() / 3);
  m.boneCount = source.boneCount;
  // raylib frees these with RL_FREE in UnloadMesh, so they come from MemAlloc.
  std::vector<void *> data(streams.size(), nullptr);
  for (usize k = 0; k < streams.size(); k++) {
    if (w.data[k].empty())
      continue;
    data[k] = MemAlloc((u32)(n * streams[k].size));
    meshopt_remapVertexBuffer(data[k], w.data[k].data(), w.vertex_count, streams[k].size, remap.data());
  }
  set_streams(m, data);
  std::vector<u32> out(indices.size());
  meshopt_remapIndexBuffer(out.data(), indices.data(), indices.size(), remap.data());
  m.indices = (unsigned short *)MemAlloc((u32)(out.size() * sizeof(unsigned short)));
  for (usize i = 0; i < out.size(); i++)
    m.indices[i] = (unsigned short)out[i];
  UploadMesh(&m, false);
  return m;
}

// The bone buffers of a skinned LOD mesh, as load_skin (njin_model.cpp) puts
// them on the model's own meshes.
void upload_skin(model_lod_mesh &lod) {
  const Mesh &mesh = lod.mesh;
  if (mesh.boneIndices == nullptr || mesh.boneWeights == nullptr || mesh.vaoId == 0)
    return;
  rlEnableVertexArray(mesh.vaoId);
  lod.bone_vbo = rlLoadVertexBuffer(mesh.boneIndices, mesh.vertexCount * 4 * (i32)sizeof(u8), false);
  rlSetVertexAttribute(skin_bone_loc, 4, RL_UNSIGNED_BYTE, false, 0, 0);
  rlEnableVertexAttribute(skin_bone_loc);
  lod.weight_vbo = rlLoadVertexBuffer(mesh.boneWeights, mesh.vertexCount * 4 * (i32)sizeof(f32), false);
  rlSetVertexAttribute(skin_weight_loc, 4, RL_FLOAT, false, 0, 0);
  rlEnableVertexAttribute(skin_weight_loc);
  rlDisableVertexArray();
}
} // namespace

void model_lod_free(model_slot &slot) {
  for (std::vector<model_lod_mesh> &level : slot.lods)
    for (model_lod_mesh &m : level) {
      if (m.bone_vbo != 0)
        rlUnloadVertexBuffer(m.bone_vbo);
      if (m.weight_vbo != 0)
        rlUnloadVertexBuffer(m.weight_vbo);
      if (m.mesh.vaoId != 0)
        UnloadMesh(m.mesh);
    }
  slot.lods.clear();
}

i32 model_lod_build(context &ctx, model_handle handle, const model_lod_desc &desc) {
  model_slot *slot = model_slot_of(ctx.model, handle);
  if (slot == nullptr)
    return 0;
  model_lod_free(*slot);
  const i32 levels = std::clamp(desc.levels, 1, 4);
  const f32 ratio = std::clamp(desc.ratio, 0.1f, 0.9f);
  const f32 error = std::max(desc.max_error, 0.0f);
  slot->lod_screen = std::max(desc.screen, 0.0f);
  const Model &model = slot->model;
  slot->lods.assign((usize)levels, std::vector<model_lod_mesh>((usize)model.meshCount));
  i32 built = 0;
  u32 full = 0, kept[4] = {};
  for (i32 i = 0; i < model.meshCount; i++) {
    const Mesh &source = model.meshes[i];
    full += (u32)source.triangleCount;
    if (source.vertices == nullptr || source.triangleCount < 8) {
      for (i32 k = 0; k < levels; k++)
        kept[k] += (u32)source.triangleCount;
      continue;
    }
    const welded w = weld(source);
    const bool skinned = (usize)i < slot->bone_vbo.size() && slot->bone_vbo[(usize)i] != 0;
    usize last = w.indices.size();
    std::vector<u32> out(w.indices.size());
    for (i32 k = 0; k < levels; k++) {
      const usize target = (usize)((f32)w.indices.size() * std::pow(ratio, (f32)(k + 1))) / 3 * 3;
      f32 got = 0.0f;
      const usize count = meshopt_simplify(out.data(), w.indices.data(), w.indices.size(), (const f32 *)w.data[0].data(),
                                           w.vertex_count, 12, std::max<usize>(target, 3), error, 0, &got);
      // Not worth a level: no fewer triangles than the one above, near enough.
      if (count < 3 || (f32)count > (f32)last * 0.9f) {
        kept[k] += (u32)(last / 3);
        continue;
      }
      model_lod_mesh &lod = slot->lods[(usize)k][(usize)i];
      lod.mesh = make_mesh(w, source, std::vector<u32>(out.begin(), out.begin() + (std::ptrdiff_t)count));
      if (lod.mesh.vaoId == 0) {
        kept[k] += (u32)(last / 3);
        continue;
      }
      if (skinned)
        upload_skin(lod);
      last = count;
      kept[k] += (u32)(count / 3);
      built = std::max(built, k + 1);
    }
  }
  // No mesh simplified past level `built`: the levels after it are empty.
  slot->lods.resize((usize)built);
  NJIN_INFO("model: %u triangles, levels of detail: %u %u %u %u", full, built > 0 ? kept[0] : 0,
            built > 1 ? kept[1] : 0, built > 2 ? kept[2] : 0, built > 3 ? kept[3] : 0);
  return built;
}
} // namespace njin
