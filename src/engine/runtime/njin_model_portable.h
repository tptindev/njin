#pragma once
#include "njin_internal_only.h"
#include "njin_model.h"
namespace njin {
struct LightmapBinding;
model_handle portable_model_create(model_store &,const mesh3d_data &);
model_handle portable_model_create_skinned(model_store &,const skinned_mesh3d_data &);
model_handle portable_model_commit_split(model_store &,const vec3 *,const vec3 *,u32,std::vector<std::vector<u32>>,std::vector<std::vector<u16>>);
void portable_model_clear_lods(model_store &,model_slot &);
int portable_model_lod_build(context &,model_handle,const model_lod_desc &);
std::vector<RenderGeometryData> portable_lod_prepare(const RenderGeometryData &,const PortableAnimation::Mesh *,const model_lod_desc &);
int portable_model_lod_load(context &,model_handle,const char *const *,int,float);
RenderMeshHandle portable_model_lod_mesh(const model_slot &,unsigned,unsigned);
void portable_model_update_vertices(model_store &,model_handle,const vec3 *,const vec3 *,u32);
model_handle portable_model_combine(context &,const model_place *,u32,float,u32);
model_handle portable_model_load(model_store &,const model_load_desc &);
void portable_model_unload(model_store &,model_handle);
void portable_model_free_cpu(model_slot &) noexcept;
void portable_material_validate(const model_material &);
void portable_light_validate(const light3d_source &);
void portable_light_add(context &,const light3d_source &);
void portable_surface_validate(const material3d &);
void portable_fx_validate(const fx3d &);
camera3d render3d_shaken_camera(const context &,camera3d);
void portable_begin_3d(context &,const camera3d &);
void portable_end_3d(context &);
void portable_close_3d(context &);
void portable_draw_model(const context &,model_handle,const transform3d &,rgba,const model_pose *pose=nullptr,
                         std::span<const model_recolor> recolors={},int level_override=-1,std::span<const LightmapBinding> lightmaps={});
void portable_draw_primitive(const context &,int,const transform3d &,rgba,const material3d &);
void portable_draw_instances(const context &,model_handle,int,instance_buffer_handle,u32,u32,shader_handle,
                             const model_pose *pose=nullptr,const instanced3d_desc *options=nullptr);
void portable_world_submit(const context &,MeshDraw,const material3d &,rgba,bool,std::uint64_t=0);
void portable_draw_shape(const context &,const shape3d &,rgba,const material3d &);
void portable_draw_sdf_blend(const context &,const sdf_part *,u32,f32,rgba,const material3d &);
}
