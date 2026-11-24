#include "njin_lightmap_internal.h"
#include "njin_model.h"
#include "njin_ctx_get.h"
#include "njin_ctx.h"
#include "njin_frame_impl.h"
#include "njin_texture.h"
#include "njin_rlmath.h"
#include <cmath>
#include <atomic>
#include <stdexcept>

namespace njin {
LightmapScene lightmap_scene_snapshot(context &ctx,std::string identity) {
  LightmapScene scene;scene.identity=std::move(identity);
  const auto &light=ctx_ref<model_store>(ctx)->portable_light;
  scene.direction={light.direction.x,light.direction.y,light.direction.z};
  const auto linear=[](float v){return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);};
  scene.illuminance={linear(light.color.r)*light.color.a,linear(light.color.g)*light.color.a,linear(light.color.b)*light.color.a};
  for(auto [entity,transform,model,binding]:world(ctx).view<const transform3d,const model3d,const lightmap3d>().each()) if(model.visible)
    lightmap_add_model(ctx,scene,model.model,transform,binding.object,true);
  for(auto [entity,transform,model,occluder]:world(ctx).view<const transform3d,const model3d,const lightmap_occluder3d>().each()) if(model.visible) {
    if(world(ctx).all_of<lightmap3d>(entity)) throw std::invalid_argument("njin lightmap snapshot: object cannot be receiver and occluder-only");
    lightmap_add_model(ctx,scene,model.model,transform,occluder.object,false);
  }
  return scene;
}
std::span<const LightmapBinding> lightmap_object_bindings(const context &ctx,lightmap_handle asset,uint64_t object) {
  const auto &assets=ctx_ref<model_store>(ctx)->lightmap_assets;
  const auto found=assets.find(asset.id);if(found==assets.end()) throw std::invalid_argument("njin lightmap: invalid/foreign asset handle");
  const auto bindings=found->second.objects.find(object);if(bindings==found->second.objects.end()) throw std::invalid_argument("njin lightmap: object missing from asset");
  return bindings->second;
}
lightmap_handle lightmap_load(context &ctx,const char *path,uint64_t expected) {
  auto asset=lightmap_load(path,expected);static std::atomic<uint64_t> next{0};const auto id=++next;
  auto &assets=ctx_ref<model_store>(ctx)->lightmap_assets;
  auto &resource=assets.try_emplace(id).first->second;resource.asset=std::move(asset);
  try {
    for(const auto &page:resource.asset.pages) if(!resource.objects.contains(page.source_instance))
      resource.objects.emplace(page.source_instance,lightmap_upload_instance(ctx,resource.asset,page.source_instance));
  } catch(...) {for(auto &[object,bindings]:resource.objects) lightmap_unload(ctx,bindings);assets.erase(id);throw;}
  return {id};
}
void lightmap_unload(context &ctx,lightmap_handle handle) {
  auto &assets=ctx_ref<model_store>(ctx)->lightmap_assets;auto found=assets.find(handle.id);if(found==assets.end()) return;
  auto &window=ctx_ref<window_guard>(ctx);
  for(const auto &[object,bindings]:found->second.objects) for(const auto &binding:bindings)
    if(window.renderer_resources.pending_3d_textures.contains(binding.map.texture.id)||window.graphics->texture_in_use(binding.map.texture))
      throw std::logic_error("njin lightmap: asset still referenced by queued/active draws");
  for(auto &[object,bindings]:found->second.objects) lightmap_unload(ctx,bindings);
  assets.erase(found);
}
void lightmap_add_model(context &ctx,LightmapScene &scene,model_handle handle,const transform3d &transform,uint64_t object,bool receiver) {
  const auto *model=model_slot_of(ctx_ref<model_store>(ctx),handle);
  if(!object||!model||!model->portable||model->skinned||!model->morph_names.empty()||model_clip_count(*model)>0)
    throw std::invalid_argument("njin lightmap snapshot: valid static portable model/object identity required");
  const auto a=transform.rotation*(PI/180.f);
  Matrix world=MatrixScale(transform.scale.x,transform.scale.y,transform.scale.z);
  world=MatrixMultiply(world,MatrixRotateZ(a.z));world=MatrixMultiply(world,MatrixRotateX(a.x));world=MatrixMultiply(world,MatrixRotateY(a.y));
  world=MatrixMultiply(world,MatrixTranslate(transform.position.x,transform.position.y,transform.position.z));
  const auto values=MatrixToFloatV(world);
  const auto linear=[](float v){return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);};
  const auto texture=[&](const RenderTextureMap &map) {
    LightmapTexture result;if(!map.texture) return result;
    const auto &resource=ctx_ref<window_guard>(ctx).renderer_resources.textures.at(map.texture.id);
    if(resource.target) throw std::invalid_argument("njin lightmap snapshot: render-target material must be exported to a CPU asset first");
    if(resource.virtual_params[0]>0) throw std::invalid_argument("njin lightmap snapshot: use the source image instead of a virtual page cache");
    result.image=std::make_shared<RenderImageAsset>(resource.cpu);result.uv=map.uv;result.wrap_s=map.wrap_s;result.wrap_t=map.wrap_t;result.linear=map.linear;return result;
  };
  std::vector<LightmapInstance> snapshots;
  for(unsigned mesh=0;mesh<model->portable_meshes.size();++mesh) {
    if(!model->portable_meshes[mesh]) throw std::invalid_argument("njin lightmap snapshot: refine streamed model to LOD 0 and pin it before baking");
    const auto &resource=ctx_ref<window_guard>(ctx).renderer_resources.meshes.at(model->portable_meshes[mesh].id);
    const auto material_index=model->model.meshMaterial[mesh];const auto &material=model->materials.at(material_index);
    if(material.color.a<1||material.surface.transmission>0||material.shader.id||(material.surface.pbr&&material.surface.metallic>0))
      throw std::invalid_argument("njin lightmap snapshot: requires opaque diffuse material");
    if(size_t(material_index)<model->portable_materials.size()) {
      const auto &data=model->portable_materials[material_index];
      if(data.blend||data.mask) throw std::invalid_argument("njin lightmap snapshot: blend/masked occluders are unsupported");
    }
    auto geometry=std::make_shared<RenderGeometryData>();geometry->vertices=resource.cpu;geometry->indices=resource.indices;
    LightmapInstance instance;instance.source_identity=object;instance.mesh_index=mesh;instance.receiver=receiver;
    instance.identity=object^(0x517cc1b727220a95ull*(mesh+1));if(!instance.identity) instance.identity=~object;
    instance.geometry=geometry;std::copy(std::begin(values.v),std::end(values.v),instance.placement.transform.begin());
    instance.albedo={linear(material.color.r),linear(material.color.g),linear(material.color.b)};
    instance.emission={linear(material.surface.emission.r)*material.surface.emission.a,linear(material.surface.emission.g)*material.surface.emission.a,linear(material.surface.emission.b)*material.surface.emission.a};
    model_slot::portable_material_data data;
    if(size_t(material_index)<model->portable_materials.size()) data=model->portable_materials[material_index];
    const auto override_map=[&](texture_handle handle,RenderTextureMap &map) {
      if(!handle.id) return;
      const auto *slot=texture_slot_of(ctx_ref<texture_store>(ctx),handle);
      if(!slot||!slot->portable||slot->packed) throw std::invalid_argument("njin lightmap snapshot: invalid/packed texture");
      map.texture=slot->portable;
    };
    override_map(material.albedo,data.albedo);override_map(material.emission,data.maps.emission);
    instance.albedo_texture=texture(data.albedo);
    auto factor=data.maps.emission_factor;
    if(material.emission.id) {factor={1,1,1,1};data.maps.constant_emission=false;}
    const float tint[]{linear(material.emission_color.r),linear(material.emission_color.g),linear(material.emission_color.b),material.emission_color.a};
    for(unsigned c=0;c<4;++c) factor[c]*=tint[c];
    if(data.maps.constant_emission) for(unsigned c=0;c<3;++c) instance.emission[c]+=factor[c]*factor[3];
    else if(data.maps.emission.texture) {instance.emission_texture=texture(data.maps.emission);for(unsigned c=0;c<3;++c) instance.emission_factor[c]=factor[c]*factor[3];}
    snapshots.push_back(std::move(instance));
  }
  scene.instances.insert(scene.instances.end(),snapshots.begin(),snapshots.end());
}
}
