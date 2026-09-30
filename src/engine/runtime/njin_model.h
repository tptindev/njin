#pragma once
#include "njin_internal_only.h"

#include "_types.h"
#include "njin_3d.h"
#include <raylib.h>
#include <algorithm>
#include <vector>

namespace njin {
// Same rules as the other stores (njin_texture.h): handle id 0 is "invalid",
// id N maps to slots[N - 1], and slots are never reused, so a stale handle can
// never alias a newer model.

// One mesh of a level of detail, with its own vertices (only those its
// triangles use) and bone buffers when the mesh has a skin.
struct model_lod_mesh {
  Mesh mesh{};
  u32 bone_vbo = 0;
  u32 weight_vbo = 0;
};

struct model_slot {
  Model model{};
  bool alive = false;
  // What the game sees and sets (model_material_set), one per material of
  // `model`. Starts from the file's colours; its handles are the game's and
  // stay the game's to unload. Applied over a copy of the file's material at
  // each draw, so the file's own textures are never replaced for good.
  std::vector<model_material> materials;
  BoundingBox bounds{}; // of the file's meshes, in model space
  // Skeletal animation (glTF skin): the file's clips, and per mesh the bone
  // index and weight buffers, uploaded next to the mesh's own (raylib is built
  // without GPU skinning, so it leaves them on the CPU) at skin_bone_loc and
  // skin_weight_loc of the mesh's vertex array. 0 = the mesh has no skin.
  ModelAnimation *anims = nullptr;
  i32 anim_count = 0; // allocated, for UnloadModelAnimations
  i32 anim_kept = 0;  // usable: anims[0..anim_kept) match the skeleton
  std::vector<u32> bone_vbo;
  std::vector<u32> weight_vbo;
  bool skinned = false; // at least one mesh has bone buffers and the skeleton fits
  std::vector<Matrix> inv_bind; // per bone, the inverse of its rest pose (model space)
  // Levels of detail (model_lod_build): lods[k - 1][i] is mesh i at level k.
  // A mesh with vaoId 0 has nothing simpler at that level: the level above
  // is drawn. Level 1 below `lod_screen` of the screen's height, each next
  // at half the previous.
  std::vector<std::vector<model_lod_mesh>> lods;
  f32 lod_screen = 0.25f;
};

// Mesh `mesh` of `slot` at level `lod`, or null for the model's own mesh.
inline const model_lod_mesh *model_lod_of(const model_slot &slot, u32 lod, i32 mesh) {
  for (u32 k = std::min<u32>(lod, (u32)slot.lods.size()); k > 0; k--) {
    const model_lod_mesh &m = slot.lods[k - 1][(usize)mesh];
    if (m.mesh.vaoId != 0)
      return &m;
  }
  return nullptr;
}

// Frees the levels of detail of `slot`.
void model_lod_free(model_slot &slot);

// Vertex attribute locations of the skin buffers: past raylib's own (0..6)
// and below the instance attributes (12..15, render3d.cpp).
inline constexpr i32 skin_bone_loc = 10;
inline constexpr i32 skin_weight_loc = 11;
// Bones the skinning shader takes (its boneMatrices array).
inline constexpr i32 skin_max_bones = 128;

// Owns every loaded model with its meshes and material textures. The
// destructor frees them, so it must run while the GL context is still alive.
struct model_store {
  std::vector<model_slot> slots;

  model_store() = default;
  ~model_store();
  // Copying would free the same GPU buffers twice.
  model_store(const model_store &) = delete;
  model_store &operator=(const model_store &) = delete;
};

inline const model_slot *model_slot_of(const model_store &store, model_handle handle) {
  if (handle.id == 0 || handle.id > store.slots.size())
    return nullptr;
  const model_slot &slot = store.slots[handle.id - 1];
  return slot.alive ? &slot : nullptr;
}

inline model_slot *model_slot_of(model_store &store, model_handle handle) {
  return const_cast<model_slot *>(model_slot_of(static_cast<const model_store &>(store), handle));
}

model_handle model_store_load(model_store &store, const char *path);
model_handle model_store_create(model_store &store, const mesh3d_data &mesh);
void model_store_unload(model_store &store, model_handle handle);
} // namespace njin
