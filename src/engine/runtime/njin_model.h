#pragma once
#include "njin_internal_only.h"

#include "_types.h"
#include "njin_3d.h"
#include "njin_renderer.h"
#include "njin_lightmap_internal.h"
#include "njin_post3d.h"
#include "njin_animation_portable.h"
#include "njin_temporal_portable.h"
#include "njin_light_clusters.h"
#include "njin_atmosphere_portable.h"
#include "modules/debug3d.h"
#include <raylib.h>
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace njin {
// Same rules as the other stores (njin_texture.h): handle id 0 is "invalid",
// id N maps to slots[N - 1], and slots are never reused, so a stale handle can
// never alias a newer model.

// Morph targets (glTF blend shapes) of one of raylib's meshes: its file
// positions, normals and tangents, and per target of its glTF mesh the offsets
// in the same space (raylib bakes the node's transform into the vertices, so
// the offsets get its linear part). Blended on the CPU into the mesh's GPU
// buffers when a draw asks for other weights than they hold (render3d.cpp).
struct mesh_morph {
  std::vector<f32> base_pos, base_nrm;
  std::vector<f32> base_tan;                      // 4 per vertex (xyz, handedness w), empty without tangents
  std::vector<std::vector<f32>> dpos, dnrm, dtan; // per target, 3 per vertex; a dnrm or dtan may be empty
  std::vector<i32> slot;                          // model morph of each target
  mutable std::vector<f32> held;                  // weight of each target now in the GPU buffers
};

// One mesh of a level of detail, with its own vertices (only those its
// triangles use), bone buffers when the mesh has a skin, and its morph
// targets (the source mesh's, through the same vertex remap) when it has any.
struct model_lod_mesh {
  Mesh mesh{};
  u32 bone_vbo = 0;
  u32 weight_vbo = 0;
  mesh_morph morph;
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

struct PortableAnimation;
struct model_slot {
  bool streaming_lods=false;
  std::shared_ptr<PortableAnimation> portable_animation;
  bool portable=false;
  std::vector<RenderMeshHandle> portable_meshes;
  std::vector<std::vector<RenderMeshHandle>> portable_lods;
  struct portable_material_data { RenderTextureMap albedo; RenderMaterialMaps maps; bool blend=false,mask=false; };
  std::vector<portable_material_data> portable_materials;
  std::vector<RenderTextureHandle> portable_owned_textures;
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
  bool merged = false;  // loaded with model_load_desc::merge: model_lod_load merges its levels alike
  bool borrows_textures = false; // model_combine: its materials use its sources' textures
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
  // model_store_create_split: per mesh, the source vertex of each of its
  // vertices, and how many source vertices there are. Empty otherwise.
  std::vector<std::vector<u32>> split_source;
  u32 split_count = 0;
  // The PBR maps of the file's materials (glTF metallic-roughness, occlusion),
  // per material [0] and [1]: where the image is, read on the first draw of a
  // PBR material (model_file_pbr_map). raylib never loads them
  // (load_gltf_without_unused_maps), so a game that does not use PBR does
  // not pay for them. Mutable: draws see a const slot.
  struct file_map {
    std::string path;          // an image file, or
    std::vector<u8> bytes;     // an image inside the file (.glb, data URI)
    std::string ext;           // of `bytes` (".png", ".jpg")
    Texture2D texture{};
    bool tried = false;
    bool second_uv = false; // read through the mesh's second texture coordinates
  };
  mutable std::vector<std::array<file_map, 2>> pbr_maps;
};

// The file's metallic-roughness (`occlusion` false) or occlusion map of
// material `index`, loaded on first use; an empty texture when it has none.
Texture2D model_file_pbr_map(const model_slot &slot, i32 index, bool occlusion);

// Whether that map reads the mesh's second texture coordinates.
bool model_file_pbr_map_uv2(const model_slot &slot, i32 index, bool occlusion);

// Clips the game sees (model_anim_count): the skin's, or the weight-only
// clips of a model without a skin.
inline i32 model_clip_count(const model_slot &m) {
  if(m.portable_animation) return int(m.portable_animation->clips_view().size());
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

// Legacy models own raylib GPU resources. Portable models retain CPU geometry
// and renderer-owned handles; explicit unload releases them outside a frame.
// Window/device ownership outlives this store on both paths.
struct model_store {
  std::vector<model_slot> slots;
  std::unordered_map<std::uint64_t,LightmapResource> lightmap_assets;

  context *portable_context=nullptr;
  bool portable_pass=false;
  bool portable_auto_end_target=false;
  camera3d portable_camera;
  render3d_options portable_options{};
  light3d portable_light;
  light3d portable_frame_light;
  std::vector<MeshDraw> portable_queue;
  debug3d_frame portable_debug;
  PortableTemporalState portable_temporal;
  std::vector<light3d_source> portable_lights;
  LightClusters portable_cluster_grid;
  RenderTextureHandle portable_cluster_texture;
  std::shared_ptr<const RenderClusterData> portable_clusters;
  std::shared_ptr<const RenderShadowData> portable_shadows;
  RenderTargetHandle portable_target;
  RenderTargetHandle portable_destination;
  RenderTargetHandle portable_hdr;
  post3d portable_post{},portable_frame_post{};
  std::vector<RenderTargetHandle> portable_bloom;
  RenderTargetHandle portable_aa;
  std::array<RenderTargetHandle,2> portable_smaa{};
  std::array<RenderTextureHandle,2> portable_smaa_lookup{};
  std::vector<RenderTargetHandle> portable_luminance;
  std::array<RenderTargetHandle,2> portable_exposure{};
  unsigned portable_exposure_index=0;
  bool portable_exposure_valid=false;
  double portable_exposure_at=0;
  std::array<RenderTargetHandle,2> portable_ao{};
  RenderTargetHandle portable_ao_scene;
  RenderTargetHandle portable_ssr_mask,portable_ssr_scene;
  std::array<RenderTargetHandle,2> portable_effect_scene{},portable_shafts{};
  RenderTargetHandle portable_flare_visibility;
  std::array<RenderTargetHandle,2> portable_oit{};
  bool portable_ao_half=true;
  unsigned portable_post_width=0,portable_post_height=0;
  RenderTargetHandle portable_shadow;
  std::shared_ptr<RenderEnvironmentData> portable_environment;
  std::shared_ptr<AtmosphereState> portable_atmosphere;
  std::vector<RenderTargetHandle> portable_hdr_retired;
  bool portable_atmosphere_drawn=false;
  std::vector<std::shared_ptr<RenderEnvironmentData>> portable_environment_retired;
  u32 portable_environment_source=0,portable_environment_version=0;
  unsigned portable_shadow_width=0,portable_shadow_height=0;
  // Ray traced shadows (njin_raytrace_portable.cpp): the camera's packed depth.
  RenderTargetHandle portable_rt_depth;
  RenderTargetHandle portable_occlusion_depth;
  unsigned portable_occlusion_width=0,portable_occlusion_height=0;
  struct OcclusionTarget {RenderTargetHandle target;unsigned width,height;std::uint64_t frame;};
  std::vector<OcclusionTarget> portable_occlusion_targets;
  unsigned portable_rt_width=0,portable_rt_height=0;
  std::uint64_t portable_rt_frame=0;
  // light3d::shadow_lod: queue index -> the coarser level its rays meet instead.
  std::vector<std::pair<size_t,RenderMeshHandle>> portable_rt_lod;
  // The last BVH and the triangles it was built from: an unchanged scene reuses it.
  std::vector<std::array<float,4>> portable_rt_corners;
  std::shared_ptr<const std::vector<std::array<float,4>>> portable_rt_nodes,portable_rt_triangles;
  std::uint64_t portable_rt_grid_generation=0; // irradiance3d_set starts the traced grid over
  irradiance3d_desc portable_grid{}; // irradiance3d_set on the bgfx renderer
  bool portable_grid_set=false;
  unsigned portable_hdr_width=0,portable_hdr_height=0;
  bool portable_hdr_active=false;
  Matrix portable_view_projection{},portable_projection{},portable_view{};
  material3d portable_material;
  fx3d portable_fx;
  mutable RenderTextureHandle portable_white;
  mutable std::array<RenderMeshHandle,6> portable_primitives{}; // mesh3d_kind order: box, sphere, plane, cylinder, low sphere, low cylinder
  std::vector<std::pair<std::vector<float>,std::shared_ptr<const RenderGeometryData>>> portable_sdf_meshes;
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
// One material, as many meshes as it takes to keep each under raylib's 16-bit
// indices: triangles go in order into a mesh until it would pass 65535
// vertices, vertices shared across a cut are copied. For a surface too big
// for model_store_create (a large soft body).
model_handle model_store_create_split(model_store &store, const vec3 *positions, const vec3 *normals, u32 count,
                                      const u32 *indices, u32 index_count);
// New positions and normals for a model made by model_store_create (its single
// mesh) or model_store_create_split, `count` of them (the vertex count it was
// made with), uploaded in place; its bounds follow.
void model_store_update_vertices(model_store &store, model_handle handle, const vec3 *positions, const vec3 *normals,
                                 u32 count);
model_handle model_store_create_skinned(model_store &store, const skinned_mesh3d_data &mesh);
// The weight of every morph of `m` for `pose` (nullptr: a draw without one):
// the defaults, the clips' curves, and the game's weights added on top.
void model_morph_eval(const model_store &store, const model_slot &m, const model_pose *pose, std::vector<f32> &out);
// Makes mesh `mesh`'s GPU buffers hold its morphs at the model weights `w`
// (model_morph_eval's), unless they already do.
void model_morph_upload(const model_slot &m, i32 mesh, const f32 *w);
// The same for any mesh and its morphs (a level of detail's).
void morph_upload(const mesh_morph &mm, const Mesh &me, const f32 *w);
// The positions of `mm` at the model weights `w` (3 per vertex), on the CPU.
void morph_positions(const mesh_morph &mm, const f32 *w, std::vector<f32> &out);
void model_store_unload(model_store &store, model_handle handle);
} // namespace njin
