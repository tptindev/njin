#include "njin_model.h"
#include "njin_log.h"
#include "njin_path.h"
#include <external/cgltf.h> // compiled into raylib (rmodels.c)
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_set>
#include <utility>

namespace njin {
namespace {
// raylib allocates this many maps per material (MAX_MATERIAL_MAPS in its
// config.h, not exported by raylib.h).
constexpr i32 material_map_count = 12;

// What raylib's IsModelValid checks, minus the bone buffers: raylib is built
// without GPU skinning, so a mesh has 7 vertex buffers, and IsModelValid
// reads vboId[7] and [8] past them for any skinned mesh (raylib 6.0).
bool model_loaded(const Model &model) {
  if (model.meshes == nullptr || model.materials == nullptr || model.meshMaterial == nullptr ||
      model.meshCount <= 0 || model.materialCount <= 0)
    return false;
  for (i32 i = 0; i < model.meshCount; i++) {
    const Mesh &m = model.meshes[i];
    if (m.vboId == nullptr || (m.vertices != nullptr && m.vboId[0] == 0) || (m.indices != nullptr && m.vboId[6] == 0))
      return false;
  }
  return true;
}

// raylib's glTF animation loader reads the parent of the skeleton's first
// bone without checking it exists: Blender always exports one (the armature
// object), a file whose root bone sits at the top of the scene crashes it.
// Such a skin is refused before raylib sees it.
bool gltf_skin_loadable(const std::string &path, const char *name) {
  const std::string ext = GetFileExtension(path.c_str()) != nullptr ? GetFileExtension(path.c_str()) : "";
  if (!TextIsEqual(TextToLower(ext.c_str()), ".glb") && !TextIsEqual(TextToLower(ext.c_str()), ".gltf"))
    return true;
  cgltf_options options{};
  cgltf_data *data = nullptr;
  if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success)
    return false;
  const bool ok = data->skins_count == 0 || data->skins[0].joints_count == 0 ||
                  data->skins[0].joints[0]->parent != nullptr;
  cgltf_free(data);
  if (!ok)
    NJIN_WARN("model: %s: the root bone has no parent node (export the armature object with it): "
              "animations not loaded",
              name);
  return ok;
}

// The bone buffers of every skinned mesh of `slot` on the GPU, for the
// skinning shader.
void upload_skin(model_slot &slot) {
  const Model &model = slot.model;
  slot.bone_vbo.assign((usize)model.meshCount, 0);
  slot.weight_vbo.assign((usize)model.meshCount, 0);
  for (i32 i = 0; i < model.meshCount; i++) {
    const Mesh &mesh = model.meshes[i];
    if (mesh.boneIndices == nullptr || mesh.boneWeights == nullptr || mesh.vaoId == 0)
      continue;
    rlEnableVertexArray(mesh.vaoId);
    slot.bone_vbo[(usize)i] = rlLoadVertexBuffer(mesh.boneIndices, mesh.vertexCount * 4 * (i32)sizeof(u8), false);
    rlSetVertexAttribute(skin_bone_loc, 4, RL_UNSIGNED_BYTE, false, 0, 0);
    rlEnableVertexAttribute(skin_bone_loc);
    slot.weight_vbo[(usize)i] = rlLoadVertexBuffer(mesh.boneWeights, mesh.vertexCount * 4 * (i32)sizeof(f32), false);
    rlSetVertexAttribute(skin_weight_loc, 4, RL_FLOAT, false, 0, 0);
    rlEnableVertexAttribute(skin_weight_loc);
    rlDisableVertexArray();
    slot.skinned = true;
  }
}

// The file's animation clips that match the model's skeleton, and the bone
// buffers of every skinned mesh on the GPU for the skinning shader.
void load_skin(model_slot &slot, const std::string &path, const char *name) {
  const Model &model = slot.model;
  if (model.skeleton.boneCount <= 0)
    return;
  if (model.skeleton.boneCount > skin_max_bones) {
    NJIN_WARN("model: %s has %d bones, more than %d: drawn in its rest pose", name, model.skeleton.boneCount,
              skin_max_bones);
    return;
  }
  i32 count = 0;
  ModelAnimation *anims = gltf_skin_loadable(path, name) ? LoadModelAnimations(path.c_str(), &count) : nullptr;
  if (anims != nullptr) {
    // Keep only the clips made for this skeleton, in file order.
    i32 kept = 0;
    for (i32 i = 0; i < count; i++) {
      if (IsModelAnimationValid(model, anims[i]) && anims[i].keyframeCount > 0)
        std::swap(anims[kept++], anims[i]);
      else
        NJIN_WARN("model: %s: animation '%s' does not match the skeleton, skipped", name, anims[i].name);
    }
    // raylib samples a glTF clip at 60 frames a second, but at the clip's
    // very end (its last key's time) it finds no key interval and returns
    // the first pose (GetPoseAtTimeGLTF): a held motion (a fall, a kick)
    // would end standing. The frame 1/60 s before the end stands in for it.
    for (i32 i = 0; i < kept; i++) {
      ModelAnimation &a = anims[i];
      if (a.keyframeCount >= 2)
        std::copy(a.keyframePoses[a.keyframeCount - 2], a.keyframePoses[a.keyframeCount - 2] + a.boneCount,
                  a.keyframePoses[a.keyframeCount - 1]);
    }
    slot.anims = anims;
    slot.anim_count = count;
    // The skipped clips stay at the end of the array until unload frees them.
    slot.anim_kept = kept;
  }
  for (i32 b = 0; b < model.skeleton.boneCount; b++) {
    const Transform &t = model.skeleton.bindPose[b];
    const Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(t.scale.x, t.scale.y, t.scale.z),
                                                   QuaternionToMatrix(t.rotation)),
                                    MatrixTranslate(t.translation.x, t.translation.y, t.translation.z));
    slot.inv_bind.push_back(MatrixInvert(m));
  }
  upload_skin(slot);
}
} // namespace

namespace {
bool name_has(const char *name, const char *const *list, u32 count) {
  if (name == nullptr || list == nullptr)
    return false;
  for (u32 i = 0; i < count; i++)
    if (list[i] != nullptr && std::strstr(name, list[i]) != nullptr)
      return true;
  return false;
}

// The node name of each of raylib's meshes: LoadGLTF makes one mesh per
// triangle primitive, visiting the nodes in file order (rmodels.c). Empty
// when the file does not parse or the count does not match.
std::vector<std::string> mesh_node_names(const cgltf_data *data, i32 mesh_count) {
  std::vector<std::string> names;
  for (cgltf_size i = 0; i < data->nodes_count; i++) {
    const cgltf_node &node = data->nodes[i];
    if (node.mesh == nullptr)
      continue;
    for (cgltf_size p = 0; p < node.mesh->primitives_count; p++)
      if (node.mesh->primitives[p].type == cgltf_primitive_type_triangles)
        names.push_back(node.name != nullptr ? node.name : "");
  }
  if ((i32)names.size() != mesh_count)
    names.clear();
  return names;
}

// Copies `count` items of `n` floats (or bytes) each, or `fill` when the mesh has none.
template <typename T> void append(std::vector<T> &out, const T *src, i32 count, i32 n, T fill) {
  for (i32 i = 0; i < count * n; i++)
    out.push_back(src != nullptr ? src[i] : fill);
}

template <typename T> T *to_raylib_array(const std::vector<T> &v) {
  T *p = (T *)MemAlloc((u32)(v.size() * sizeof(T)));
  std::copy(v.begin(), v.end(), p);
  return p;
}

// The meshes in `keep`, sharing material `material`, as few meshes as their
// 16-bit indices allow. Bone data is dropped: the result is static.
void merge_meshes(const Model &model, const std::vector<i32> &keep, std::vector<Mesh> &out_meshes,
                  std::vector<i32> &out_materials) {
  std::vector<i32> materials;
  for (const i32 k : keep)
    if (std::find(materials.begin(), materials.end(), model.meshMaterial[k]) == materials.end())
      materials.push_back(model.meshMaterial[k]);
  for (const i32 material : materials) {
    std::vector<f32> pos, nrm, uv, tan;
    std::vector<u8> col;
    std::vector<unsigned short> idx;
    bool has_nrm = false, has_uv = false, has_tan = false, has_col = false;
    for (const i32 k : keep)
      if (model.meshMaterial[k] == material) {
        const Mesh &m = model.meshes[k];
        has_nrm |= m.normals != nullptr;
        has_uv |= m.texcoords != nullptr;
        has_tan |= m.tangents != nullptr;
        has_col |= m.colors != nullptr;
      }
    const auto flush = [&]() {
      if (pos.empty())
        return;
      Mesh m{};
      m.vertexCount = (i32)(pos.size() / 3);
      m.triangleCount = (i32)(idx.size() / 3);
      m.vertices = to_raylib_array(pos);
      m.normals = has_nrm ? to_raylib_array(nrm) : nullptr;
      m.texcoords = to_raylib_array(uv.empty() ? std::vector<f32>(pos.size() / 3 * 2, 0.0f) : uv);
      m.tangents = has_tan ? to_raylib_array(tan) : nullptr;
      m.colors = has_col ? to_raylib_array(col) : nullptr;
      m.indices = to_raylib_array(idx);
      UploadMesh(&m, false);
      out_meshes.push_back(m);
      out_materials.push_back(material);
      pos.clear(), nrm.clear(), uv.clear(), tan.clear(), col.clear(), idx.clear();
    };
    for (const i32 k : keep) {
      if (model.meshMaterial[k] != material)
        continue;
      const Mesh &m = model.meshes[k];
      if (m.vertices == nullptr || m.vertexCount <= 0)
        continue;
      if (pos.size() / 3 + (usize)m.vertexCount > 65535)
        flush();
      const u32 base = (u32)(pos.size() / 3);
      append(pos, m.vertices, m.vertexCount, 3, 0.0f);
      if (has_nrm)
        append(nrm, m.normals, m.vertexCount, 3, 0.0f);
      if (has_uv)
        append(uv, m.texcoords, m.vertexCount, 2, 0.0f);
      if (has_tan)
        append(tan, m.tangents, m.vertexCount, 4, 0.0f);
      if (has_col)
        append(col, m.colors, m.vertexCount, 4, (u8)255);
      if (m.indices != nullptr)
        for (i32 i = 0; i < m.triangleCount * 3; i++)
          idx.push_back((unsigned short)(base + m.indices[i]));
      else
        for (i32 i = 0; i < m.vertexCount; i++)
          idx.push_back((unsigned short)(base + (u32)i));
    }
    flush();
  }
}

// Leaves out the meshes of the nodes `desc` filters out, and merges the rest
// by material when it asks. False when nothing is left.
bool filter_model(Model &model, const cgltf_data *data, const model_load_desc &desc, const char *path) {
  const bool filtering = desc.skip_count > 0 || desc.only_count > 0;
  if (!filtering && !desc.merge)
    return true;
  const std::vector<std::string> names = mesh_node_names(data, model.meshCount);
  if (filtering && names.empty()) {
    NJIN_WARN("model: %s: its meshes do not follow its nodes, the node filter is not applied", path);
    return true;
  }
  std::vector<i32> keep;
  for (i32 i = 0; i < model.meshCount; i++) {
    const char *name = names.empty() ? "" : names[(usize)i].c_str();
    if (desc.only_count > 0 && !name_has(name, desc.only_nodes, desc.only_count))
      continue;
    if (name_has(name, desc.skip_nodes, desc.skip_count))
      continue;
    keep.push_back(i);
  }
  if (keep.empty())
    return false;
  std::vector<Mesh> meshes;
  std::vector<i32> materials;
  if (desc.merge) {
    merge_meshes(model, keep, meshes, materials);
  } else {
    for (const i32 k : keep) {
      meshes.push_back(model.meshes[k]);
      materials.push_back(model.meshMaterial[k]);
      model.meshes[k] = Mesh{}; // moved: not unloaded below
    }
  }
  for (i32 i = 0; i < model.meshCount; i++)
    if (model.meshes[i].vaoId != 0 || model.meshes[i].vertices != nullptr)
      UnloadMesh(model.meshes[i]);
  RL_FREE(model.meshes);
  RL_FREE(model.meshMaterial);
  model.meshCount = (i32)meshes.size();
  model.meshes = (Mesh *)RL_CALLOC(meshes.size(), sizeof(Mesh));
  model.meshMaterial = (i32 *)RL_CALLOC(meshes.size(), sizeof(i32));
  std::copy(meshes.begin(), meshes.end(), model.meshes);
  std::copy(materials.begin(), materials.end(), model.meshMaterial);
  return model.meshCount > 0;
}

// KHR_materials_transmission: raylib reads no such thing, so a pane of clear
// glass would be an opaque sheet of its base colour. Drawn see-through
// instead (render3d's translucent pass), lit like glass, casting no shadow.
void apply_transmission(model_slot &slot, const cgltf_data *data) {
  if ((usize)slot.model.materialCount != data->materials_count + 1)
    return;
  for (cgltf_size i = 0; i < data->materials_count; i++) {
    const cgltf_material &m = data->materials[i];
    if (!m.has_transmission || m.transmission.transmission_factor <= 0.0f)
      continue;
    model_material &mm = slot.materials[i + 1]; // raylib's default material is first
    mm.color.a *= std::clamp(1.0f - 0.78f * m.transmission.transmission_factor, 0.08f, 1.0f);
    mm.surface.cast_shadows = false;
    mm.surface.specular = 0.9f;
    mm.surface.shininess = 90.0f;
  }
}
} // namespace

model_handle model_store_load(model_store &store, const model_load_desc &desc) {
  const char *path = desc.path;
  if (path == nullptr) {
    NJIN_WARN("model: path is null");
    return model_handle{};
  }
  // raylib only logs a missing file; fail loudly with the path instead.
  const std::string resolved = asset_path(path);
  if (!FileExists(resolved.c_str())) {
    NJIN_WARN("model: file not found: %s", path);
    return model_handle{};
  }
  Model model = LoadModel(resolved.c_str());
  if (!model_loaded(model)) {
    NJIN_WARN("model: failed to load: %s", path);
    return model_handle{};
  }
  cgltf_data *gltf = nullptr;
  const char *ext = GetFileExtension(resolved.c_str());
  const bool is_gltf = ext != nullptr && (TextIsEqual(TextToLower(ext), ".glb") || TextIsEqual(TextToLower(ext), ".gltf"));
  if (is_gltf) {
    cgltf_options options{};
    if (cgltf_parse_file(&options, resolved.c_str(), &gltf) != cgltf_result_success)
      gltf = nullptr;
  }
  if (gltf != nullptr && !filter_model(model, gltf, desc, path)) {
    NJIN_WARN("model: %s: the node filter leaves no mesh", path);
    cgltf_free(gltf);
    UnloadModel(model);
    return model_handle{};
  }
  model_slot slot;
  slot.model = model;
  slot.alive = true;
  slot.bounds = GetModelBoundingBox(model);
  for (i32 i = 0; i < model.materialCount; i++) {
    const Color c = model.materials[i].maps[MATERIAL_MAP_DIFFUSE].color;
    const Color e = model.materials[i].maps[MATERIAL_MAP_EMISSION].color;
    slot.materials.push_back(model_material{
        .color = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f},
        .emission_color = {e.r / 255.0f, e.g / 255.0f, e.b / 255.0f, 1.0f}});
  }
  slot.material_names.assign(slot.materials.size(), std::string());
  if (gltf != nullptr) {
    apply_transmission(slot, gltf);
    // raylib's default material is first, then the file's in order. raylib
    // reads neither the names nor doubleSided.
    if ((usize)slot.model.materialCount == gltf->materials_count + 1)
      for (cgltf_size i = 0; i < gltf->materials_count; i++) {
        if (gltf->materials[i].name != nullptr)
          slot.material_names[i + 1] = gltf->materials[i].name;
        slot.materials[i + 1].double_sided = gltf->materials[i].double_sided != 0;
      }
    cgltf_free(gltf);
  }
  if (!desc.merge)
    load_skin(slot, resolved, path);
  store.slots.push_back(std::move(slot));
  return model_handle{.id = (u32)store.slots.size()};
}

namespace {
// Per-vertex normals from the triangles sharing each vertex, weighted by
// their area (the cross product's length), so small slivers count less.
void smooth_normals(const mesh3d_data &mesh, f32 *out) {
  const u32 tris = mesh.indices != nullptr ? mesh.index_count / 3 : mesh.vertex_count / 3;
  std::fill(out, out + (usize)mesh.vertex_count * 3, 0.0f);
  for (u32 t = 0; t < tris; t++) {
    const u32 i0 = mesh.indices != nullptr ? mesh.indices[t * 3] : t * 3;
    const u32 i1 = mesh.indices != nullptr ? mesh.indices[t * 3 + 1] : t * 3 + 1;
    const u32 i2 = mesh.indices != nullptr ? mesh.indices[t * 3 + 2] : t * 3 + 2;
    const vec3 a = mesh.positions[i0], b = mesh.positions[i1], c = mesh.positions[i2];
    const vec3 n = cross(b - a, c - a);
    for (const u32 i : {i0, i1, i2}) {
      out[i * 3] += n.x;
      out[i * 3 + 1] += n.y;
      out[i * 3 + 2] += n.z;
    }
  }
  for (u32 i = 0; i < mesh.vertex_count; i++) {
    const vec3 n{out[i * 3], out[i * 3 + 1], out[i * 3 + 2]};
    const vec3 u = length_sq(n) > 1e-12f ? normalize(n) : vec3{0.0f, 1.0f, 0.0f};
    out[i * 3] = u.x;
    out[i * 3 + 1] = u.y;
    out[i * 3 + 2] = u.z;
  }
}

u8 color_byte(f32 v) { return (u8)(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }
} // namespace

model_handle model_store_create(model_store &store, const mesh3d_data &mesh) {
  if (mesh.positions == nullptr || mesh.vertex_count == 0) {
    NJIN_WARN("model_create: no vertices");
    return model_handle{};
  }
  if (mesh.indices != nullptr) {
    if (mesh.index_count == 0 || mesh.index_count % 3 != 0) {
      NJIN_WARN("model_create: index_count %u is not a positive multiple of 3", mesh.index_count);
      return model_handle{};
    }
    if (mesh.vertex_count > 65535) {
      NJIN_WARN("model_create: %u vertices with indices, at most 65535 (split the mesh)", mesh.vertex_count);
      return model_handle{};
    }
    for (u32 i = 0; i < mesh.index_count; i++)
      if (mesh.indices[i] >= mesh.vertex_count) {
        NJIN_WARN("model_create: index %u is %u, past the %u vertices", i, mesh.indices[i], mesh.vertex_count);
        return model_handle{};
      }
  } else if (mesh.vertex_count % 3 != 0) {
    NJIN_WARN("model_create: %u vertices without indices is not a multiple of 3", mesh.vertex_count);
    return model_handle{};
  }

  // raylib frees these with RL_FREE in UnloadModel, so they come from MemAlloc.
  const usize n = mesh.vertex_count;
  Mesh m{};
  m.vertexCount = (i32)n;
  m.triangleCount = (i32)(mesh.indices != nullptr ? mesh.index_count / 3 : mesh.vertex_count / 3);
  m.vertices = (f32 *)MemAlloc((u32)(n * 3 * sizeof(f32)));
  m.normals = (f32 *)MemAlloc((u32)(n * 3 * sizeof(f32)));
  m.texcoords = (f32 *)MemAlloc((u32)(n * 2 * sizeof(f32))); // zeros: the shader samples a white texture
  m.colors = (u8 *)MemAlloc((u32)(n * 4));
  for (usize i = 0; i < n; i++) {
    m.vertices[i * 3] = mesh.positions[i].x;
    m.vertices[i * 3 + 1] = mesh.positions[i].y;
    m.vertices[i * 3 + 2] = mesh.positions[i].z;
    const rgba c = mesh.colors != nullptr ? mesh.colors[i] : rgba{1.0f, 1.0f, 1.0f, 1.0f};
    m.colors[i * 4] = color_byte(c.r);
    m.colors[i * 4 + 1] = color_byte(c.g);
    m.colors[i * 4 + 2] = color_byte(c.b);
    m.colors[i * 4 + 3] = color_byte(c.a);
  }
  if (mesh.normals != nullptr) {
    for (usize i = 0; i < n; i++) {
      m.normals[i * 3] = mesh.normals[i].x;
      m.normals[i * 3 + 1] = mesh.normals[i].y;
      m.normals[i * 3 + 2] = mesh.normals[i].z;
    }
  } else {
    smooth_normals(mesh, m.normals);
  }
  if (mesh.indices != nullptr) {
    m.indices = (unsigned short *)MemAlloc((u32)(mesh.index_count * sizeof(unsigned short)));
    for (u32 i = 0; i < mesh.index_count; i++)
      m.indices[i] = (unsigned short)mesh.indices[i];
  }
  UploadMesh(&m, false);
  const Model model = LoadModelFromMesh(m);
  if (!model_loaded(model)) {
    NJIN_WARN("model_create: the mesh could not be uploaded");
    UnloadModel(model);
    return model_handle{};
  }
  model_slot slot;
  slot.model = model;
  slot.alive = true;
  slot.bounds = GetModelBoundingBox(model);
  slot.materials.push_back(model_material{});
  store.slots.push_back(std::move(slot));
  return model_handle{.id = (u32)store.slots.size()};
}

model_handle model_store_create_skinned(model_store &store, const skinned_mesh3d_data &mesh) {
  const model_slot *bones = model_slot_of(store, mesh.skeleton);
  if (bones == nullptr || !bones->skinned || bones->model.skeleton.boneCount <= 0) {
    NJIN_WARN("model_create_skinned: the skeleton model is not a loaded model with a skin");
    return model_handle{};
  }
  const i32 bone_count = bones->model.skeleton.boneCount;
  if (mesh.positions == nullptr || mesh.joints == nullptr || mesh.weights == nullptr || mesh.vertex_count == 0 ||
      mesh.indices == nullptr || mesh.index_count == 0 || mesh.index_count % 3 != 0) {
    NJIN_WARN("model_create_skinned: positions, joints, weights and indices (a multiple of 3) are needed");
    return model_handle{};
  }
  for (u32 i = 0; i < mesh.index_count; i++)
    if (mesh.indices[i] >= mesh.vertex_count) {
      NJIN_WARN("model_create_skinned: index %u is %u, past the %u vertices", i, mesh.indices[i], mesh.vertex_count);
      return model_handle{};
    }
  for (u32 i = 0; i < mesh.vertex_count * 4; i++)
    if (mesh.joints[i] >= bone_count) {
      NJIN_WARN("model_create_skinned: vertex %u names bone %u, the skeleton has %d", i / 4, (u32)mesh.joints[i],
                bone_count);
      return model_handle{};
    }
  // The parts: index ranges, each its own mesh and material.
  std::vector<u32> ends;
  if (mesh.part_ends != nullptr && mesh.part_count > 0)
    ends.assign(mesh.part_ends, mesh.part_ends + mesh.part_count);
  else
    ends.push_back(mesh.index_count);
  for (usize k = 0; k < ends.size(); k++)
    if (ends[k] % 3 != 0 || ends[k] > mesh.index_count || (k > 0 && ends[k] < ends[k - 1]) ||
        (k + 1 == ends.size() && ends[k] != mesh.index_count)) {
      NJIN_WARN("model_create_skinned: part %u ends at index %u: the ends must rise by whole triangles to %u",
                (u32)k, ends[k], mesh.index_count);
      return model_handle{};
    }
  std::vector<f32> normals((usize)mesh.vertex_count * 3);
  if (mesh.normals != nullptr) {
    for (u32 i = 0; i < mesh.vertex_count; i++) {
      normals[(usize)i * 3] = mesh.normals[i].x;
      normals[(usize)i * 3 + 1] = mesh.normals[i].y;
      normals[(usize)i * 3 + 2] = mesh.normals[i].z;
    }
  } else {
    smooth_normals({.positions = mesh.positions, .vertex_count = mesh.vertex_count, .indices = mesh.indices,
                    .index_count = mesh.index_count},
                   normals.data());
  }

  // raylib frees all of these with RL_FREE in UnloadModel, so they come from MemAlloc.
  const i32 parts = (i32)ends.size();
  Model model{};
  model.transform = MatrixIdentity();
  model.meshCount = parts;
  model.materialCount = parts;
  model.meshes = (Mesh *)MemAlloc((u32)(sizeof(Mesh) * (usize)parts));
  model.materials = (Material *)MemAlloc((u32)(sizeof(Material) * (usize)parts));
  model.meshMaterial = (i32 *)MemAlloc((u32)(sizeof(i32) * (usize)parts));
  std::vector<i32> local(mesh.vertex_count, -1);
  bool ok = true;
  for (i32 k = 0; k < parts; k++) {
    const u32 from = k == 0 ? 0 : ends[(usize)k - 1], to = ends[(usize)k];
    // Only the vertices this part's triangles use, in first-use order.
    std::fill(local.begin(), local.end(), -1);
    std::vector<u32> used;
    for (u32 i = from; i < to; i++)
      if (local[mesh.indices[i]] < 0) {
        local[mesh.indices[i]] = (i32)used.size();
        used.push_back(mesh.indices[i]);
      }
    if (used.size() > 65535) {
      NJIN_WARN("model_create_skinned: part %d uses %u vertices, at most 65535 (split it)", k, (u32)used.size());
      ok = false;
    }
    Mesh m{};
    const usize n = used.size(), room = std::max<usize>(n, 1);
    m.vertexCount = (i32)n;
    m.triangleCount = (i32)((to - from) / 3);
    m.vertices = (f32 *)MemAlloc((u32)(room * 3 * sizeof(f32)));
    m.normals = (f32 *)MemAlloc((u32)(room * 3 * sizeof(f32)));
    m.texcoords = (f32 *)MemAlloc((u32)(room * 2 * sizeof(f32))); // zeros: the shader samples a white texture
    m.colors = (u8 *)MemAlloc((u32)(room * 4));
    m.boneIndices = (u8 *)MemAlloc((u32)(room * 4));
    m.boneWeights = (f32 *)MemAlloc((u32)(room * 4 * sizeof(f32)));
    m.indices = (unsigned short *)MemAlloc((u32)(std::max<u32>(to - from, 3) * sizeof(unsigned short)));
    for (usize j = 0; j < n; j++) {
      const usize v = used[j];
      m.vertices[j * 3] = mesh.positions[v].x;
      m.vertices[j * 3 + 1] = mesh.positions[v].y;
      m.vertices[j * 3 + 2] = mesh.positions[v].z;
      for (usize c = 0; c < 3; c++)
        m.normals[j * 3 + c] = normals[v * 3 + c];
      for (usize c = 0; c < 4; c++) {
        m.colors[j * 4 + c] = 255;
        m.boneIndices[j * 4 + c] = mesh.joints[v * 4 + c];
        m.boneWeights[j * 4 + c] = mesh.weights[v * 4 + c];
      }
    }
    for (u32 i = from; i < to; i++)
      m.indices[i - from] = (unsigned short)std::max(local[mesh.indices[i]], 0);
    if (ok && n > 0)
      UploadMesh(&m, false);
    model.meshes[k] = m;
    model.materials[k] = LoadMaterialDefault();
    model.meshMaterial[k] = k;
  }
  // The skeleton's bones and rest pose, copied: UnloadModel frees the model's own.
  const ModelSkeleton &from = bones->model.skeleton;
  model.skeleton.boneCount = from.boneCount;
  model.skeleton.bones = (BoneInfo *)MemAlloc((u32)(sizeof(BoneInfo) * (usize)from.boneCount));
  model.skeleton.bindPose = (Transform *)MemAlloc((u32)(sizeof(Transform) * (usize)from.boneCount));
  std::memcpy(model.skeleton.bones, from.bones, sizeof(BoneInfo) * (usize)from.boneCount);
  std::memcpy(model.skeleton.bindPose, from.bindPose, sizeof(Transform) * (usize)from.boneCount);
  if (!ok || !model_loaded(model)) {
    if (ok)
      NJIN_WARN("model_create_skinned: the mesh could not be uploaded");
    UnloadModel(model);
    return model_handle{};
  }
  model_slot slot;
  slot.model = model;
  slot.alive = true;
  slot.bounds = GetModelBoundingBox(model);
  slot.materials.assign((usize)parts, model_material{});
  slot.material_names.assign((usize)parts, std::string());
  slot.inv_bind = bones->inv_bind;
  slot.anim_from = mesh.skeleton;
  upload_skin(slot);
  store.slots.push_back(std::move(slot));
  return model_handle{.id = (u32)store.slots.size()};
}

void model_store_unload(model_store &store, model_handle handle) {
  model_slot *slot = model_slot_of(store, handle);
  if (slot == nullptr)
    return;
  // UnloadModel frees the meshes and the map arrays but leaves the material
  // textures to the caller. They are not shared outside the model; within it,
  // two maps may name the same texture, so free each id once.
  std::unordered_set<u32> freed;
  for (i32 i = 0; i < slot->model.materialCount; i++) {
    const Material &material = slot->model.materials[i];
    for (i32 m = 0; m < material_map_count; m++) {
      const Texture2D &texture = material.maps[m].texture;
      if (texture.id != 0 && texture.id != rlGetTextureIdDefault() &&
          freed.insert(texture.id).second)
        UnloadTexture(texture);
    }
  }
  for (u32 vbo : slot->bone_vbo)
    rlUnloadVertexBuffer(vbo);
  for (u32 vbo : slot->weight_vbo)
    rlUnloadVertexBuffer(vbo);
  if (slot->anims != nullptr)
    UnloadModelAnimations(slot->anims, slot->anim_count);
  model_lod_free(*slot);
  UnloadModel(slot->model);
  *slot = model_slot{};
}

model_store::~model_store() {
  for (usize i = 0; i < slots.size(); i++)
    model_store_unload(*this, model_handle{.id = (u32)(i + 1)});
}
} // namespace njin
