#pragma once
#include "njin_internal_only.h"

#include "_types.h"
#include "njin_3d.h"
#include <raylib.h>
#include <algorithm>
#include <string>
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

// Morph targets (glTF blend shapes) of one of raylib's meshes: its file
// positions and normals, and per target of its glTF mesh the offsets in the
// same space (raylib bakes the node's transform into the vertices, so the
// offsets get its linear part). Blended on the CPU into the mesh's GPU
// buffers when a draw asks for other weights than they hold (render3d.cpp).
struct mesh_morph {
  std::vector<f32> base_pos, base_nrm;
  std::vector<std::vector<f32>> dpos, dnrm; // per target; a dnrm may be empty
  std::vector<i32> slot;                    // model morph of each target
  mutable std::vector<f32> held;            // weight of each target now in the GPU buffers
};

// One glTF mesh's weight curve in a clip: key times, and per key one value
// per target of that mesh.
struct morph_channel {
  std::vector<f32> times;
  std::vector<f32> values;
  std::vector<i32> slot; // model morph of each target
  bool step = false;
};

struct morph_clip {
  std::string name;
  f32 duration = 0.0f;
  std::vector<morph_channel> channels;
};

struct model_slot {
  Model model{};
  bool alive = false;
  // What the game sees and sets (model_material_set), one per material of
  // `model`. Starts from the file's colours; its handles are the game's and
  // stay the game's to unload. Applied over a copy of the file's material at
  // each draw, so the file's own textures are never replaced for good.
  std::vector<model_material> materials;
  // Each material's name in the glTF file ("" for raylib's default first
  // one, and for every material of a file that is not glTF).
  std::vector<std::string> material_names;
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
  // model_create_skinned: the model whose skeleton this one copies and whose
  // clips it plays (never copied). 0 = its own clips, if any.
  model_handle anim_from{};
  // Levels of detail (model_lod_build): lods[k - 1][i] is mesh i at level k.
  // A mesh with vaoId 0 has nothing simpler at that level: the level above
  // is drawn. Level 1 below `lod_screen` of the screen's height, each next
  // at half the previous.
  std::vector<std::vector<model_lod_mesh>> lods;
  f32 lod_screen = 0.25f;
  // Morph targets: per mesh (empty when the model has none), the model's
  // morphs (targets of the same name in several glTF meshes are one), their
  // default weights (glTF mesh.weights), and the clips' weight curves by clip
  // index: the skin's clips, or for a model without a skin its only clips.
  std::vector<mesh_morph> morphs;
  std::vector<std::string> morph_names;
  std::vector<f32> morph_defaults;
  std::vector<morph_clip> morph_clips;
};

// Clips the game sees (model_anim_count): the skin's, or the weight-only
// clips of a model without a skin.
inline i32 model_clip_count(const model_slot &m) {
  return m.anim_kept > 0 ? m.anim_kept : (i32)m.morph_clips.size();
}

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

// The slot whose clips `m` plays: the one it borrows them from
// (model_create_skinned), or `m` itself, also when that one is gone.
inline const model_slot &model_anim_owner(const model_store &store, const model_slot &m) {
  const model_slot *owner = m.anim_from.id != 0 ? model_slot_of(store, m.anim_from) : nullptr;
  return owner != nullptr && owner->model.skeleton.boneCount == m.model.skeleton.boneCount ? *owner : m;
}

model_handle model_store_load(model_store &store, const model_load_desc &desc);
model_handle model_store_create(model_store &store, const mesh3d_data &mesh);
// New positions and normals for the single mesh of a model made by
// model_store_create, `count` of them (its vertex count), uploaded in place;
// its bounds follow.
void model_store_update_vertices(model_store &store, model_handle handle, const vec3 *positions, const vec3 *normals,
                                 u32 count);
model_handle model_store_create_skinned(model_store &store, const skinned_mesh3d_data &mesh);
// The weight of every morph of `m` for `pose` (nullptr: a draw without one):
// the defaults, the clips' curves, and the game's weights added on top.
void model_morph_eval(const model_store &store, const model_slot &m, const model_pose *pose, std::vector<f32> &out);
// Makes mesh `mesh`'s GPU buffers hold its morphs at the model weights `w`
// (model_morph_eval's), unless they already do.
void model_morph_upload(const model_slot &m, i32 mesh, const f32 *w);
void model_store_unload(model_store &store, model_handle handle);
} // namespace njin
