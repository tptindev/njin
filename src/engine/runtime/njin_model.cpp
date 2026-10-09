#include "njin_model.h"
#include "njin_log.h"
#include "njin_path.h"
#include <external/cgltf.h> // compiled into raylib (rmodels.c)
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
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

// A glTF's metallic-roughness and occlusion maps are never drawn (render3d.cpp
// gives those slots to the under layer), yet raylib's loader splits a
// metallic-roughness map pixel by pixel through GetImageColor: about two
// seconds for a 4096 x 4096 atlas. LoadModel() reads the file through this
// instead, which renames those keys in the JSON to ones glTF does not know,
// of the same length, so nothing in the file moves and cgltf skips them.
struct hidden_key {
  const char *key, *as;
};
constexpr hidden_key hidden_maps[] = {{"\"metallicRoughnessTexture\"", "\"metallicRoughnessUnused_\""},
                                      {"\"occlusionTexture\"", "\"occlusionUnused_\""}};
static_assert(sizeof("\"metallicRoughnessTexture\"") == sizeof("\"metallicRoughnessUnused_\""));
static_assert(sizeof("\"occlusionTexture\"") == sizeof("\"occlusionUnused_\""));

unsigned char *load_gltf_without_unused_maps(const char *file_name, int *data_size) {
  *data_size = 0;
  FILE *f = std::fopen(file_name, "rb");
  if (f == nullptr)
    return nullptr;
  std::fseek(f, 0, SEEK_END);
  const long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (n <= 0) {
    std::fclose(f);
    return nullptr;
  }
  auto *data = static_cast<unsigned char *>(MemAlloc(static_cast<u32>(n)));
  const size_t got = std::fread(data, 1, static_cast<size_t>(n), f);
  std::fclose(f);
  *data_size = static_cast<int>(got);
  // cgltf reads a .gltf's buffers and images through here too: left as they are.
  const char *ext = GetFileExtension(file_name);
  if (ext == nullptr || (!TextIsEqual(TextToLower(ext), ".glb") && !TextIsEqual(TextToLower(ext), ".gltf")))
    return data;
  // The JSON: all of a .gltf, a .glb's first chunk.
  size_t from = 0, to = got;
  if (got >= 20 && std::memcmp(data, "glTF", 4) == 0) {
    u32 json_len = 0;
    std::memcpy(&json_len, data + 12, 4);
    from = 20;
    to = std::min(got, from + json_len);
  }
  char *json = reinterpret_cast<char *>(data);
  for (const hidden_key &h : hidden_maps) {
    const size_t len = std::strlen(h.key);
    for (size_t i = from; i + len <= to; i++) {
      if (std::memcmp(json + i, h.key, len) != 0)
        continue;
      size_t j = i + len;
      while (j < to && (json[j] == ' ' || json[j] == '\t' || json[j] == '\n' || json[j] == '\r'))
        j++;
      if (j < to && json[j] == ':')
        std::memcpy(json + i, h.as, len);
    }
  }
  return data;
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
// buffers of every skinned mesh on the GPU for the skinning shader. `source`
// gets the file's index of each clip kept.
void load_skin(model_slot &slot, const std::string &path, const char *name, std::vector<i32> &source) {
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
      if (IsModelAnimationValid(model, anims[i]) && anims[i].keyframeCount > 0) {
        source.push_back(i);
        std::swap(anims[kept++], anims[i]);
      } else
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
// 16-bit indices allow. Bone data is dropped: the result is static. `from`
// gets, per merged mesh, each source mesh in it and its first vertex there.
void merge_meshes(const Model &model, const std::vector<i32> &keep, std::vector<Mesh> &out_meshes,
                  std::vector<i32> &out_materials, std::vector<std::vector<std::pair<i32, u32>>> &from) {
  std::vector<i32> materials;
  for (const i32 k : keep)
    if (std::find(materials.begin(), materials.end(), model.meshMaterial[k]) == materials.end())
      materials.push_back(model.meshMaterial[k]);
  for (const i32 material : materials) {
    std::vector<f32> pos, nrm, uv, tan;
    std::vector<u8> col;
    std::vector<unsigned short> idx;
    std::vector<std::pair<i32, u32>> parts;
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
      from.push_back(std::move(parts));
      parts.clear();
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
      parts.push_back({k, base});
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
// by material when it asks. False when nothing is left. `kept` gets the old
// index of each mesh left when they are filtered without merging, `merged`
// what merge_meshes says each merged mesh is made of.
bool filter_model(Model &model, const cgltf_data *data, const model_load_desc &desc, const char *path,
                  std::vector<i32> &kept, std::vector<std::vector<std::pair<i32, u32>>> &merged) {
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
    merge_meshes(model, keep, meshes, materials, merged);
  } else {
    kept = keep;
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

// A material whose base colour reads its second UV set (texCoord 1, as a
// texture baked onto a fresh unwrap while the source UVs are kept): raylib
// samples every map with the first set only. The mesh's two sets are swapped,
// on the CPU and in its buffers, so the shader's set is the one meant.
void apply_texcoord_sets(Model &model, const cgltf_data *data) {
  if ((usize)model.materialCount != data->materials_count + 1)
    return;
  for (i32 k = 0; k < model.meshCount; k++) {
    Mesh &m = model.meshes[k];
    const i32 mat = model.meshMaterial[k] - 1; // raylib's default material is first
    if (mat < 0 || (cgltf_size)mat >= data->materials_count || m.texcoords == nullptr || m.texcoords2 == nullptr)
      continue;
    const cgltf_material &gm = data->materials[mat];
    if (!gm.has_pbr_metallic_roughness || gm.pbr_metallic_roughness.base_color_texture.texture == nullptr ||
        gm.pbr_metallic_roughness.base_color_texture.texcoord != 1)
      continue;
    std::swap(m.texcoords, m.texcoords2);
    const i32 bytes = m.vertexCount * 2 * (i32)sizeof(f32);
    for (const i32 at : {RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD, RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD2})
      if (m.vboId != nullptr && m.vboId[at] != 0)
        UpdateMeshBuffer(m, at, at == RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD ? m.texcoords : m.texcoords2, bytes, 0);
  }
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

std::vector<f32> unpack(const cgltf_accessor *a) {
  std::vector<f32> out;
  if (a == nullptr)
    return out;
  out.resize(cgltf_accessor_unpack_floats(a, nullptr, 0));
  cgltf_accessor_unpack_floats(a, out.data(), out.size());
  return out;
}

cgltf_size target_count(const cgltf_mesh &gm) {
  cgltf_size n = 0;
  for (cgltf_size p = 0; p < gm.primitives_count; p++)
    n = std::max(n, gm.primitives[p].targets_count);
  return n;
}

// The model morph of each target of glTF mesh `gm`, by name, so a target of
// the same name in several meshes (a blink on the face and on the lashes) is
// one morph. An unnamed target is "<mesh>.<n>".
std::vector<i32> morph_slots(model_slot &slot, const cgltf_data *data, const cgltf_mesh &gm) {
  std::vector<i32> out;
  const cgltf_size n = target_count(gm);
  const std::string mesh = gm.name != nullptr ? gm.name : "mesh" + std::to_string(&gm - data->meshes);
  for (cgltf_size t = 0; t < n; t++) {
    const std::string name = t < gm.target_names_count && gm.target_names[t] != nullptr
                                 ? std::string(gm.target_names[t])
                                 : mesh + "." + std::to_string(t);
    const auto at = std::find(slot.morph_names.begin(), slot.morph_names.end(), name);
    if (at != slot.morph_names.end()) {
      out.push_back((i32)(at - slot.morph_names.begin()));
      continue;
    }
    out.push_back((i32)slot.morph_names.size());
    slot.morph_names.push_back(name);
    slot.morph_defaults.push_back(t < gm.weights_count ? gm.weights[t] : 0.0f);
  }
  return out;
}

// The offsets of `src`, through the linear part of `m` (raylib's matrix layout).
void transform_offsets(std::vector<f32> &v, const Matrix &m) {
  for (usize i = 0; i + 2 < v.size(); i += 3) {
    const f32 x = v[i], y = v[i + 1], z = v[i + 2];
    v[i] = m.m0 * x + m.m4 * y + m.m8 * z;
    v[i + 1] = m.m1 * x + m.m5 * y + m.m9 * z;
    v[i + 2] = m.m2 * x + m.m6 * y + m.m10 * z;
  }
}

// The morph targets of every raylib mesh (one per triangle primitive, nodes
// in file order, as LoadGLTF makes them), and per glTF mesh its targets'
// model morphs. Needs the file's buffers loaded.
void load_morphs(model_slot &slot, const cgltf_data *data, const char *path,
                 std::vector<std::pair<const cgltf_mesh *, std::vector<i32>>> &mesh_slots) {
  const Model &model = slot.model;
  std::vector<mesh_morph> morphs((usize)model.meshCount);
  bool any = false;
  i32 k = 0;
  for (cgltf_size i = 0; i < data->nodes_count; i++) {
    const cgltf_node &node = data->nodes[i];
    if (node.mesh == nullptr)
      continue;
    const cgltf_mesh &gm = *node.mesh;
    std::vector<i32> slots;
    bool known = false;
    for (const auto &[mesh, s] : mesh_slots)
      if (mesh == &gm) {
        slots = s;
        known = true;
      }
    if (!known && target_count(gm) > 0) {
      slots = morph_slots(slot, data, gm);
      mesh_slots.push_back({&gm, slots});
    }
    f32 w[16];
    cgltf_node_transform_world(&node, w);
    const Matrix world = {w[0], w[4], w[8], w[12], w[1], w[5], w[9], w[13],
                          w[2], w[6], w[10], w[14], w[3], w[7], w[11], w[15]};
    const Matrix normals = MatrixTranspose(MatrixInvert(world));
    for (cgltf_size p = 0; p < gm.primitives_count; p++) {
      const cgltf_primitive &prim = gm.primitives[p];
      if (prim.type != cgltf_primitive_type_triangles)
        continue;
      if (k >= model.meshCount)
        return; // the meshes do not follow the nodes: no morphs
      const Mesh &mesh = model.meshes[k];
      mesh_morph &mm = morphs[(usize)k++];
      if (prim.targets_count == 0 || mesh.vertices == nullptr)
        continue;
      const usize floats = (usize)mesh.vertexCount * 3;
      mm.base_pos.assign(mesh.vertices, mesh.vertices + floats);
      if (mesh.normals != nullptr)
        mm.base_nrm.assign(mesh.normals, mesh.normals + floats);
      if (mesh.tangents != nullptr)
        mm.base_tan.assign(mesh.tangents, mesh.tangents + (usize)mesh.vertexCount * 4);
      bool ok = true;
      for (cgltf_size t = 0; t < prim.targets_count && t < slots.size(); t++) {
        std::vector<f32> dp, dn, dt;
        for (cgltf_size a = 0; a < prim.targets[t].attributes_count; a++) {
          const cgltf_attribute &attr = prim.targets[t].attributes[a];
          if (attr.type == cgltf_attribute_type_position)
            dp = unpack(attr.data);
          else if (attr.type == cgltf_attribute_type_normal && !mm.base_nrm.empty())
            dn = unpack(attr.data);
          else if (attr.type == cgltf_attribute_type_tangent && !mm.base_tan.empty())
            dt = unpack(attr.data);
        }
        if (dp.empty())
          dp.assign(floats, 0.0f);
        if (dp.size() != floats || (!dn.empty() && dn.size() != floats) || (!dt.empty() && dt.size() != floats)) {
          ok = false;
          break;
        }
        transform_offsets(dp, world);
        transform_offsets(dn, normals);
        transform_offsets(dt, world); // tangents turn like directions in the surface
        mm.dpos.push_back(std::move(dp));
        mm.dnrm.push_back(std::move(dn));
        mm.dtan.push_back(std::move(dt));
        mm.slot.push_back(slots[t]);
      }
      if (!ok) {
        NJIN_WARN("model: %s: a morph target of mesh '%s' does not match its vertices: its morphs are skipped", path,
                  gm.name != nullptr ? gm.name : "");
        mm = mesh_morph{};
        continue;
      }
      mm.held.assign(mm.slot.size(), 0.0f); // the buffers hold the file's shape
      any = true;
    }
  }
  if (any && k == model.meshCount)
    slot.morphs = std::move(morphs);
}

// Each glTF animation's weight curves, in file order.
std::vector<morph_clip> load_morph_clips(const cgltf_data *data,
                                         const std::vector<std::pair<const cgltf_mesh *, std::vector<i32>>> &mesh_slots) {
  std::vector<morph_clip> clips;
  for (cgltf_size a = 0; a < data->animations_count; a++) {
    const cgltf_animation &anim = data->animations[a];
    morph_clip clip;
    clip.name = anim.name != nullptr ? anim.name : "";
    for (cgltf_size c = 0; c < anim.channels_count; c++) {
      const cgltf_animation_channel &ch = anim.channels[c];
      if (ch.sampler == nullptr || ch.sampler->input == nullptr)
        continue;
      const cgltf_accessor &input = *ch.sampler->input;
      if (input.has_max)
        clip.duration = std::max(clip.duration, input.max[0]);
      if (ch.target_path != cgltf_animation_path_type_weights || ch.target_node == nullptr ||
          ch.target_node->mesh == nullptr)
        continue;
      const std::vector<i32> *slots = nullptr;
      for (const auto &[mesh, s] : mesh_slots)
        if (mesh == ch.target_node->mesh)
          slots = &s;
      if (slots == nullptr || slots->empty())
        continue;
      morph_channel mc;
      mc.times = unpack(&input);
      const std::vector<f32> out = unpack(ch.sampler->output);
      const usize n = slots->size(), keys = mc.times.size();
      const bool cubic = ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline;
      if (keys == 0 || out.size() != keys * n * (cubic ? 3 : 1))
        continue;
      if (!input.has_max)
        clip.duration = std::max(clip.duration, mc.times.back());
      mc.values.resize(keys * n);
      for (usize key = 0; key < keys; key++)
        for (usize t = 0; t < n; t++)
          mc.values[key * n + t] = cubic ? out[(key * 3 + 1) * n + t] : out[key * n + t];
      mc.step = ch.sampler->interpolation == cgltf_interpolation_type_step;
      mc.slot = *slots;
      clip.channels.push_back(std::move(mc));
    }
    clips.push_back(std::move(clip));
  }
  return clips;
}

// The morphs of merged mesh `out`, made of the source meshes `parts` of
// `src` (each from its first vertex): every model morph any of them has,
// with zero offsets on the vertices of the others.
mesh_morph merge_morph(const std::vector<mesh_morph> &src, const std::vector<std::pair<i32, u32>> &parts,
                       const Mesh &out) {
  mesh_morph mm;
  const usize n = (usize)out.vertexCount;
  for (const auto &[k, base] : parts)
    if ((usize)k < src.size())
      for (const i32 s : src[(usize)k].slot)
        if (std::find(mm.slot.begin(), mm.slot.end(), s) == mm.slot.end())
          mm.slot.push_back(s);
  if (mm.slot.empty())
    return mm;
  mm.base_pos.assign(out.vertices, out.vertices + n * 3);
  if (out.normals != nullptr)
    mm.base_nrm.assign(out.normals, out.normals + n * 3);
  if (out.tangents != nullptr)
    mm.base_tan.assign(out.tangents, out.tangents + n * 4);
  for (const i32 s : mm.slot) {
    std::vector<f32> dp(n * 3, 0.0f), dn, dt;
    for (const auto &[k, base] : parts) {
      if ((usize)k >= src.size())
        continue;
      const mesh_morph &from = src[(usize)k];
      const auto at = std::find(from.slot.begin(), from.slot.end(), s);
      if (at == from.slot.end())
        continue;
      const usize t = (usize)(at - from.slot.begin());
      std::copy(from.dpos[t].begin(), from.dpos[t].end(), dp.begin() + (std::ptrdiff_t)(base * 3));
      if (!mm.base_nrm.empty() && !from.dnrm[t].empty()) {
        dn.resize(n * 3, 0.0f);
        std::copy(from.dnrm[t].begin(), from.dnrm[t].end(), dn.begin() + (std::ptrdiff_t)(base * 3));
      }
      if (!mm.base_tan.empty() && t < from.dtan.size() && !from.dtan[t].empty()) {
        dt.resize(n * 3, 0.0f);
        std::copy(from.dtan[t].begin(), from.dtan[t].end(), dt.begin() + (std::ptrdiff_t)(base * 3));
      }
    }
    mm.dpos.push_back(std::move(dp));
    mm.dnrm.push_back(std::move(dn));
    mm.dtan.push_back(std::move(dt));
  }
  mm.held.assign(mm.slot.size(), 0.0f); // the merged buffers hold the file's shape
  return mm;
}

// Morphs the slot keeps through filter_model: those of the merged meshes'
// sources when merged, the kept ones when filtered.
void filter_morphs(model_slot &slot, const model_load_desc &desc, const std::vector<i32> &kept,
                   const std::vector<std::vector<std::pair<i32, u32>>> &merged, const Model &model) {
  if (slot.morphs.empty())
    return;
  if (desc.merge) {
    std::vector<mesh_morph> out((usize)model.meshCount);
    bool any = false;
    for (usize i = 0; i < merged.size() && i < out.size(); i++) {
      out[i] = merge_morph(slot.morphs, merged[i], model.meshes[i]);
      any |= !out[i].slot.empty();
    }
    if (any)
      slot.morphs = std::move(out);
    else
      slot.morphs.clear();
    return;
  }
  if (kept.empty())
    return;
  std::vector<mesh_morph> left;
  for (const i32 k : kept)
    left.push_back(std::move(slot.morphs[(usize)k]));
  slot.morphs = std::move(left);
}

// Value of `ch`'s curve for target `t` at `time` seconds.
f32 sample_channel(const morph_channel &ch, f32 time, usize t) {
  const usize n = ch.slot.size(), keys = ch.times.size();
  if (time <= ch.times[0])
    return ch.values[t];
  if (time >= ch.times[keys - 1])
    return ch.values[(keys - 1) * n + t];
  const usize i1 = (usize)(std::upper_bound(ch.times.begin(), ch.times.end(), time) - ch.times.begin());
  const usize i0 = i1 - 1;
  const f32 a = ch.values[i0 * n + t], b = ch.values[i1 * n + t];
  if (ch.step)
    return a;
  const f32 span = ch.times[i1] - ch.times[i0];
  return span > 0.0f ? a + (b - a) * (time - ch.times[i0]) / span : a;
}

// Clip `clip`'s curves at `time` over `out` (the weights it does not move stay).
void sample_morph_clip(const model_slot &m, i32 clip, f32 time, bool loop, std::vector<f32> &out) {
  if (clip < 0 || clip >= (i32)m.morph_clips.size())
    return;
  const morph_clip &c = m.morph_clips[(usize)clip];
  if (c.duration > 0.0f) {
    if (loop) {
      time = std::fmod(time, c.duration);
      if (time < 0.0f)
        time += c.duration;
    } else {
      time = std::clamp(time, 0.0f, c.duration);
    }
  }
  for (const morph_channel &ch : c.channels)
    for (usize t = 0; t < ch.slot.size(); t++)
      if (ch.slot[t] >= 0 && (usize)ch.slot[t] < out.size())
        out[(usize)ch.slot[t]] = sample_channel(ch, time, t);
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
  SetLoadFileDataCallback(load_gltf_without_unused_maps);
  Model model = LoadModel(resolved.c_str());
  SetLoadFileDataCallback(nullptr);
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
  model_slot slot;
  // Morph targets need the file's buffers; read before the node filter moves meshes.
  std::vector<std::pair<const cgltf_mesh *, std::vector<i32>>> mesh_slots;
  std::vector<morph_clip> morph_clips;
  if (gltf != nullptr) {
    bool targets = false;
    for (cgltf_size i = 0; i < gltf->meshes_count; i++)
      targets |= target_count(gltf->meshes[i]) > 0;
    cgltf_options options{};
    if (targets && cgltf_load_buffers(&options, gltf, resolved.c_str()) == cgltf_result_success) {
      slot.model = model;
      load_morphs(slot, gltf, path, mesh_slots);
      morph_clips = load_morph_clips(gltf, mesh_slots);
    }
  }
  if (gltf != nullptr)
    apply_texcoord_sets(model, gltf);
  std::vector<i32> kept;
  std::vector<std::vector<std::pair<i32, u32>>> merged;
  if (gltf != nullptr && !filter_model(model, gltf, desc, path, kept, merged)) {
    NJIN_WARN("model: %s: the node filter leaves no mesh", path);
    cgltf_free(gltf);
    UnloadModel(model);
    return model_handle{};
  }
  filter_morphs(slot, desc, kept, merged, model);
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
  std::vector<i32> source;
  if (!desc.merge)
    load_skin(slot, resolved, path, source);
  if (slot.morphs.empty()) {
    slot.morph_names.clear();
    slot.morph_defaults.clear();
  } else if (slot.anim_kept > 0) {
    // The skin's clips, each with its own weight curves.
    for (const i32 i : source)
      slot.morph_clips.push_back((usize)i < morph_clips.size() ? morph_clips[(usize)i] : morph_clip{});
    for (i32 k = 0; k < slot.anim_kept; k++)
      slot.morph_clips[(usize)k].duration = (f32)std::max(slot.anims[k].keyframeCount - 1, 0) / 60.0f;
  } else {
    // No skin: the clips that move weights are the model's clips.
    for (morph_clip &c : morph_clips)
      if (!c.channels.empty())
        slot.morph_clips.push_back(std::move(c));
  }
  store.slots.push_back(std::move(slot));
  return model_handle{.id = (u32)store.slots.size()};
}

void model_morph_eval(const model_store &store, const model_slot &m, const model_pose *pose, std::vector<f32> &out) {
  out.assign(m.morph_defaults.begin(), m.morph_defaults.end());
  if (out.empty() || pose == nullptr)
    return;
  if (&model_anim_owner(store, m) == &m) {
    sample_morph_clip(m, pose->anim, pose->time, pose->loop, out);
    const f32 k = std::clamp(pose->blend, 0.0f, 1.0f);
    if (pose->blend_anim >= 0 && k > 0.0f) {
      std::vector<f32> b(m.morph_defaults.begin(), m.morph_defaults.end());
      sample_morph_clip(m, pose->blend_anim, pose->blend_time, pose->blend_loop, b);
      for (usize i = 0; i < out.size(); i++)
        out[i] += (b[i] - out[i]) * k;
    }
  }
  if (pose->morph_weights != nullptr)
    for (i32 i = 0; i < pose->morph_count && (usize)i < out.size(); i++)
      if (std::isfinite(pose->morph_weights[i]))
        out[(usize)i] += pose->morph_weights[i];
}

void morph_positions(const mesh_morph &mm, const f32 *w, std::vector<f32> &out) {
  out = mm.base_pos;
  if (w == nullptr)
    return;
  for (usize t = 0; t < mm.slot.size(); t++) {
    const f32 k = w[mm.slot[t]];
    if (k == 0.0f)
      continue;
    const std::vector<f32> &d = mm.dpos[t];
    for (usize i = 0; i < out.size(); i++)
      out[i] += k * d[i];
  }
}

void morph_upload(const mesh_morph &mm, const Mesh &me, const f32 *w) {
  const usize n = mm.slot.size();
  if (n == 0 || me.vaoId == 0)
    return;
  thread_local std::vector<f32> weights, pos, nrm, tan;
  weights.resize(n);
  bool same = mm.held.size() == n;
  for (usize t = 0; t < n; t++) {
    weights[t] = w != nullptr ? w[mm.slot[t]] : 0.0f;
    same = same && weights[t] == mm.held[t];
  }
  if (same)
    return;
  pos = mm.base_pos;
  bool bends = false, turns = false; // some target moves the normals, the tangents
  for (usize t = 0; t < n; t++) {
    bends |= !mm.dnrm[t].empty();
    turns |= t < mm.dtan.size() && !mm.dtan[t].empty();
    if (weights[t] == 0.0f)
      continue;
    const f32 k = weights[t];
    const std::vector<f32> &d = mm.dpos[t];
    for (usize i = 0; i < pos.size(); i++)
      pos[i] += k * d[i];
  }
  const i32 bytes = (i32)(pos.size() * sizeof(f32));
  UpdateMeshBuffer(me, RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION, pos.data(), bytes, 0);
  if (bends && !mm.base_nrm.empty() && me.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL] != 0) {
    nrm = mm.base_nrm;
    for (usize t = 0; t < n; t++)
      if (weights[t] != 0.0f && !mm.dnrm[t].empty())
        for (usize i = 0; i < nrm.size(); i++)
          nrm[i] += weights[t] * mm.dnrm[t][i];
    for (usize i = 0; i + 2 < nrm.size(); i += 3) {
      const f32 l = std::sqrt(nrm[i] * nrm[i] + nrm[i + 1] * nrm[i + 1] + nrm[i + 2] * nrm[i + 2]);
      if (l > 1e-12f)
        nrm[i] /= l, nrm[i + 1] /= l, nrm[i + 2] /= l;
    }
    UpdateMeshBuffer(me, RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL, nrm.data(), bytes, 0);
  }
  if (turns && !mm.base_tan.empty() && me.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_TANGENT] != 0) {
    tan = mm.base_tan;
    for (usize t = 0; t < n; t++)
      if (weights[t] != 0.0f && t < mm.dtan.size() && !mm.dtan[t].empty())
        for (usize v = 0; v * 3 + 2 < mm.dtan[t].size(); v++)
          for (usize a = 0; a < 3; a++)
            tan[v * 4 + a] += weights[t] * mm.dtan[t][v * 3 + a];
    for (usize i = 0; i + 3 < tan.size(); i += 4) {
      const f32 l = std::sqrt(tan[i] * tan[i] + tan[i + 1] * tan[i + 1] + tan[i + 2] * tan[i + 2]);
      if (l > 1e-12f)
        tan[i] /= l, tan[i + 1] /= l, tan[i + 2] /= l;
    }
    UpdateMeshBuffer(me, RL_DEFAULT_SHADER_ATTRIB_LOCATION_TANGENT, tan.data(), (i32)(tan.size() * sizeof(f32)), 0);
  }
  mm.held = weights;
}

void model_morph_upload(const model_slot &m, i32 mesh, const f32 *w) {
  if (mesh < 0 || (usize)mesh >= m.morphs.size())
    return;
  morph_upload(m.morphs[(usize)mesh], m.model.meshes[mesh], w);
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
    if (mesh.texcoords != nullptr) {
      m.texcoords[i * 2] = mesh.texcoords[i].x;
      m.texcoords[i * 2 + 1] = mesh.texcoords[i].y;
    }
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

model_handle model_store_create_split(model_store &store, const vec3 *positions, const vec3 *normals, u32 count,
                                      const u32 *indices, u32 index_count) {
  if (positions == nullptr || normals == nullptr || count == 0 || indices == nullptr || index_count < 3) {
    NJIN_WARN("model_create_split: no triangles");
    return model_handle{};
  }
  constexpr u32 max_vertices = 65535;
  std::vector<std::vector<u32>> source;   // per mesh: source vertex of each local one
  std::vector<std::vector<u16>> local_ix; // per mesh: its triangles
  std::vector<i32> local(count, -1);      // source -> local of the mesh being filled
  for (u32 t = 0; t + 2 < index_count; t += 3) {
    u32 fresh = 0;
    for (u32 k = 0; k < 3; k++) {
      if (indices[t + k] >= count) {
        NJIN_WARN("model_create_split: index %u is %u, past the %u vertices", t + k, indices[t + k], count);
        return model_handle{};
      }
      if (source.empty() || local[indices[t + k]] < 0)
        fresh++;
    }
    if (source.empty() || source.back().size() + fresh > max_vertices) {
      if (!source.empty())
        for (u32 s : source.back())
          local[s] = -1;
      source.emplace_back();
      local_ix.emplace_back();
    }
    for (u32 k = 0; k < 3; k++) {
      const u32 s = indices[t + k];
      if (local[s] < 0) {
        local[s] = (i32)source.back().size();
        source.back().push_back(s);
      }
      local_ix.back().push_back((u16)local[s]);
    }
  }
  const i32 mesh_count = (i32)source.size();
  Model model{};
  model.transform = MatrixIdentity();
  model.meshCount = mesh_count;
  model.meshes = (Mesh *)MemAlloc((u32)(mesh_count * sizeof(Mesh)));
  model.materialCount = 1;
  model.materials = (Material *)MemAlloc((u32)sizeof(Material));
  model.materials[0] = LoadMaterialDefault();
  model.meshMaterial = (i32 *)MemAlloc((u32)(mesh_count * sizeof(i32)));
  for (i32 k = 0; k < mesh_count; k++) {
    const std::vector<u32> &src = source[(usize)k];
    const usize n = src.size();
    Mesh &m = model.meshes[k];
    m = Mesh{};
    m.vertexCount = (i32)n;
    m.triangleCount = (i32)(local_ix[(usize)k].size() / 3);
    m.vertices = (f32 *)MemAlloc((u32)(n * 3 * sizeof(f32)));
    m.normals = (f32 *)MemAlloc((u32)(n * 3 * sizeof(f32)));
    m.texcoords = (f32 *)MemAlloc((u32)(n * 2 * sizeof(f32)));
    m.colors = (u8 *)MemAlloc((u32)(n * 4));
    for (usize i = 0; i < n; i++) {
      const vec3 p = positions[src[i]], q = normals[src[i]];
      m.vertices[i * 3] = p.x;
      m.vertices[i * 3 + 1] = p.y;
      m.vertices[i * 3 + 2] = p.z;
      m.normals[i * 3] = q.x;
      m.normals[i * 3 + 1] = q.y;
      m.normals[i * 3 + 2] = q.z;
      std::memset(m.colors + i * 4, 255, 4);
    }
    m.indices = (unsigned short *)MemAlloc((u32)(local_ix[(usize)k].size() * sizeof(unsigned short)));
    std::memcpy(m.indices, local_ix[(usize)k].data(), local_ix[(usize)k].size() * sizeof(unsigned short));
    UploadMesh(&m, true);
  }
  if (!model_loaded(model)) {
    NJIN_WARN("model_create_split: the meshes could not be uploaded");
    UnloadModel(model);
    return model_handle{};
  }
  model_slot slot;
  slot.model = model;
  slot.alive = true;
  slot.bounds = GetModelBoundingBox(model);
  slot.materials.push_back(model_material{});
  slot.split_source = std::move(source);
  slot.split_count = count;
  store.slots.push_back(std::move(slot));
  return model_handle{.id = (u32)store.slots.size()};
}

void model_store_update_vertices(model_store &store, model_handle handle, const vec3 *positions, const vec3 *normals,
                                 u32 count) {
  model_slot *slot = model_slot_of(store, handle);
  if (slot == nullptr)
    return;
  if (!slot->split_source.empty()) {
    if (count != slot->split_count)
      return;
    Vector3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (i32 k = 0; k < slot->model.meshCount; k++) {
      Mesh &m = slot->model.meshes[k];
      const std::vector<u32> &src = slot->split_source[(usize)k];
      for (usize i = 0; i < src.size(); i++) {
        const vec3 p = positions[src[i]], q = normals[src[i]];
        m.vertices[i * 3] = p.x;
        m.vertices[i * 3 + 1] = p.y;
        m.vertices[i * 3 + 2] = p.z;
        m.normals[i * 3] = q.x;
        m.normals[i * 3 + 1] = q.y;
        m.normals[i * 3 + 2] = q.z;
        lo = Vector3Min(lo, {p.x, p.y, p.z});
        hi = Vector3Max(hi, {p.x, p.y, p.z});
      }
      const i32 bytes = (i32)(src.size() * 3 * sizeof(f32));
      UpdateMeshBuffer(m, RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION, m.vertices, bytes, 0);
      UpdateMeshBuffer(m, RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL, m.normals, bytes, 0);
    }
    slot->bounds = BoundingBox{lo, hi};
    return;
  }
  if (slot->model.meshCount != 1)
    return;
  Mesh &m = slot->model.meshes[0];
  if ((u32)m.vertexCount != count || m.vertices == nullptr || m.normals == nullptr)
    return;
  Vector3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
  for (u32 i = 0; i < count; i++) {
    const vec3 p = positions[i];
    m.vertices[i * 3] = p.x;
    m.vertices[i * 3 + 1] = p.y;
    m.vertices[i * 3 + 2] = p.z;
    m.normals[i * 3] = normals[i].x;
    m.normals[i * 3 + 1] = normals[i].y;
    m.normals[i * 3 + 2] = normals[i].z;
    lo = Vector3Min(lo, {p.x, p.y, p.z});
    hi = Vector3Max(hi, {p.x, p.y, p.z});
  }
  const i32 bytes = (i32)(count * 3 * sizeof(f32));
  UpdateMeshBuffer(m, RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION, m.vertices, bytes, 0);
  UpdateMeshBuffer(m, RL_DEFAULT_SHADER_ATTRIB_LOCATION_NORMAL, m.normals, bytes, 0);
  slot->bounds = BoundingBox{lo, hi};
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
    m.texcoords = (f32 *)MemAlloc((u32)(room * 2 * sizeof(f32))); // zeros unless given: a white texture's one texel
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
      if (mesh.texcoords != nullptr) {
        m.texcoords[j * 2] = mesh.texcoords[v].x;
        m.texcoords[j * 2 + 1] = mesh.texcoords[v].y;
      }
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
