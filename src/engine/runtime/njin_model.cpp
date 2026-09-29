#include "njin_model.h"
#include "njin_log.h"
#include "njin_path.h"
#include <external/cgltf.h> // compiled into raylib (rmodels.c)
#include <raymath.h>
#include <rlgl.h>
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
} // namespace

model_handle model_store_load(model_store &store, const char *path) {
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
  const Model model = LoadModel(resolved.c_str());
  if (!model_loaded(model)) {
    NJIN_WARN("model: failed to load: %s", path);
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
  load_skin(slot, resolved, path);
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
  UnloadModel(slot->model);
  *slot = model_slot{};
}

model_store::~model_store() {
  for (usize i = 0; i < slots.size(); i++)
    model_store_unload(*this, model_handle{.id = (u32)(i + 1)});
}
} // namespace njin
