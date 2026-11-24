#include "njin_file.h"
#include "njin_fs.h"
#include "njin_image_io.h"
#include "njin_model_portable.h"
#include "njin_stream_internal.h"
#include "njin_ctx_impl.h"
#include "njin_image_mips.h"
#include "njin_lightmap.h"
#include "njin_model_obj.h"
#include "njin_model_formats.h"
#include "njin_image_decode.h"
#include "njin_world3d_impl.h"
#include "njin_raytrace.h"
#include "njin_sdf_mesh.h"
#include "modules/gizmo.h"
#include "modules/particles3d.h"
#include "modules/post_fx.h"
#include "modules/render_stats.h"
#include "modules/debug.h"
#include "njin_render_texture_portable.h"
#include "njin_shadow_portable.h"
#include "njin_occlusion_portable.h"
#include "njin_environment_portable.h"
#include "njin_post_portable.h"
#include "njin_material_uniforms.h"
#include "njin_instance.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_path.h"
#include "njin_log.h"
#include "njin_log_impl.h"
#include "njin_texture.h"
#include "njin_shader.h"
#include "njin_view.h"
#include "njin_quality_impl.h"
#include "njin_window.h"
#include "njin_audio_impl.h"
#include <external/cgltf.h>
#include <meshoptimizer.h>
#include "njin_rlmath.h"
#include <algorithm>
#include <bit>
#include <cfloat>
#include <climits>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>

namespace njin {
namespace {
constexpr size_t max_elements=10000000;
[[noreturn]] void unsupported(const char *what) {
  throw std::runtime_error(std::string("njin bgfx 3D: ")+what+" is not ported");
}
float linear_color(float v) { return v<=0.04045f?v/12.92f:std::pow((v+0.055f)/1.055f,2.4f); }
float display_color(float v) { return v<=0.0031308f?v*12.92f:1.055f*std::pow(v,1.0f/2.4f)-0.055f; }
bool finite(vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
void color_check(rgba c) {
  for(float x:{c.r,c.g,c.b,c.a}) if(!std::isfinite(x)||x<0||x>1)
    throw std::invalid_argument("njin bgfx 3D: color must be finite and in [0,1]");
}
Matrix transform_matrix(const transform3d &t) {
  if(!finite(t.position)||!finite(t.rotation)||!finite(t.scale))
    throw std::invalid_argument("njin bgfx 3D: non-finite transform");
  const auto a=t.rotation*(PI/180.0f);
  Matrix m=MatrixScale(t.scale.x,t.scale.y,t.scale.z);
  m=MatrixMultiply(m,MatrixRotateZ(a.z));
  m=MatrixMultiply(m,MatrixRotateX(a.x));
  m=MatrixMultiply(m,MatrixRotateY(a.y));
  return MatrixMultiply(m,MatrixTranslate(t.position.x,t.position.y,t.position.z));
}
struct CpuMesh {
  std::vector<RenderVertex> vertices;
  std::vector<vec3> normals;
  std::vector<uint16_t> indices;
  int material=0;
};
struct PreparedModel:StreamPayload {
  model_slot slot;
  std::vector<CpuMesh> meshes;
  std::vector<RenderImageAsset> images;
  std::vector<rhi::ImageMipChain> chains;
  std::vector<RenderTextureHandle> textures;
  std::vector<RenderMeshHandle> gpu_meshes;
  size_t cursor=0;
  ~PreparedModel() override {portable_model_free_cpu(slot);}
  usize cpu_bytes() const override {
    usize n=0;for(const auto &im:images) n+=im.pixels.size();for(const auto &chain:chains) n+=chain.pixels.size();
    for(const auto &m:meshes) n+=m.vertices.size()*sizeof(RenderVertex)+m.indices.size()*2+m.normals.size()*sizeof(vec3);
    for(int i=0;i<slot.model.meshCount;++i) {const auto &m=slot.model.meshes[i];n+=size_t(m.vertexCount)*(12+12+8+4)+size_t(m.triangleCount)*6;}
    return n;
  }
  usize image_bytes(size_t i) const {
    const auto &im=images[i];usize bytes=0;auto w=im.width,h=im.height;
    for(;;) {bytes+=usize(w)*h*render_image_pixel_bytes(im.format);if(im.mips==RenderMipMode::None||(w==1&&h==1)) break;w=std::max(1u,w/2);h=std::max(1u,h/2);}
    return bytes;
  }
  usize gpu_bytes() const override {
    usize n=0;for(size_t i=0;i<images.size();++i) n+=image_bytes(i);
    for(const auto &m:meshes) n+=m.vertices.size()*sizeof(RenderVertex)+m.indices.size()*2;return n;
  }
  usize next_bytes() const override {
    if(cursor<images.size()) return image_bytes(cursor);
    const auto &m=meshes.at(cursor-images.size());return m.vertices.size()*sizeof(RenderVertex)+m.indices.size()*2;
  }
  bool done() const override {return cursor==images.size()+meshes.size();}
  void upload(context &ctx) override {
    if(cursor<images.size()) {
      const auto &im=images[cursor];auto t=renderer_create_texture(ctx,im.view());textures.push_back(t);
      const auto &chain=chains[cursor];ctx.window.graphics->ensure_texture_prepared(t,im.view(),chain.pixels,chain.levels);
    } else {
      const auto &m=meshes[cursor-images.size()];auto h=renderer_create_mesh(ctx,m.vertices,m.indices);gpu_meshes.push_back(h);
      ctx.window.graphics->ensure_mesh(h,m.vertices,m.indices);
    }
    ++cursor;
  }
  void discard(context &ctx) override {
    for(auto t:textures) renderer_destroy_texture(ctx,t);textures.clear();
    for(auto m:gpu_meshes) renderer_destroy_mesh(ctx,m);gpu_meshes.clear();
  }
  void publish(context &ctx,u32 handle) override {
    model_store &store=ctx_ref<model_store>(ctx);
    auto *target=model_slot_of(store,{handle});if(!target) throw std::runtime_error("stream model unloaded before publish");
    const auto remap=[&](RenderTextureMap &map){if(map.texture) map.texture=textures.at(map.texture.id-1);};
    for(auto &material:slot.portable_materials) {
      remap(material.albedo);
      auto &m=material.maps;
      for(auto *map:{&m.normal,&m.metal_rough,&m.occlusion,&m.emission,&m.transmission,&m.thickness,&m.clearcoat,&m.clearcoat_roughness,&m.clearcoat_normal}) remap(*map);
    }
    portable_model_unload(store,{handle});
    slot.portable_owned_textures=std::move(textures);slot.portable_meshes=std::move(gpu_meshes);
    store.slots.at(handle-1)=std::move(slot);slot={};
  }
};
template<class T> T *allocate(size_t count) {
  auto *p=static_cast<T*>(MemAlloc(static_cast<unsigned>(count*sizeof(T))));
  if(!p) throw std::bad_alloc();
  return p;
}
void release_gpu(model_store &store,model_slot &slot) {
  auto &ctx=*store.portable_context;
  portable_model_clear_lods(store,slot);
  for(auto mesh:slot.portable_meshes) if(mesh) renderer_destroy_mesh(ctx,mesh);
  for(auto texture:slot.portable_owned_textures) renderer_destroy_texture(ctx,texture);
}
model_handle commit(model_store &store,std::vector<CpuMesh> &meshes,
                    std::vector<model_material> materials,std::vector<std::string> names,
                    std::vector<model_slot::portable_material_data> albedos={},
                    std::vector<RenderTextureHandle> owned={},std::shared_ptr<PortableAnimation> animation={},PreparedModel *prepared=nullptr) {
  model_slot slot;
  slot.portable=true;slot.portable_animation=std::move(animation);
  slot.materials=std::move(materials); slot.material_names=std::move(names);
  slot.portable_materials=std::move(albedos); slot.portable_owned_textures=std::move(owned);
  slot.model.transform=MatrixIdentity(); slot.model.materialCount=(int)slot.materials.size();
  slot.bounds={{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};
  try {
    slot.portable_meshes.reserve(meshes.size());
    slot.model.meshes=allocate<Mesh>(meshes.size());
    std::fill_n(slot.model.meshes,meshes.size(),Mesh{});
    slot.model.meshCount=(int)meshes.size();
    slot.model.meshMaterial=allocate<int>(meshes.size());
    for(size_t k=0;k<meshes.size();++k) {
      auto &source=meshes[k]; auto &mesh=slot.model.meshes[k];
      if(source.normals.empty()) {
        source.normals.resize(source.vertices.size());
        const size_t count=source.indices.empty()?source.vertices.size():source.indices.size();
        for(size_t j=0;j<count;j+=3) {
          const size_t a=source.indices.empty()?j:source.indices[j], b=source.indices.empty()?j+1:source.indices[j+1], c=source.indices.empty()?j+2:source.indices[j+2];
          const auto point=[&](size_t i){const auto &v=source.vertices[i];return vec3{v.x,v.y,v.z};};
          const auto n=cross(point(b)-point(a),point(c)-point(a));
          source.normals[a]+=n; source.normals[b]+=n; source.normals[c]+=n;
        }
      }
      for(size_t j=0;j<source.vertices.size();++j) {
        auto &n=source.normals[j]; if(length(n)>0) n=normalize(n);
        source.vertices[j].normal={n.x,n.y,n.z};
      }
      if(slot.portable_animation) {
        auto &animated=slot.portable_animation->meshes.at(k);animated.base.vertices=source.vertices;animated.base.indices=source.indices;
        if(!animated.bone.empty()) {
          mesh.boneIndices=allocate<unsigned char>(source.vertices.size()*4);mesh.boneWeights=allocate<float>(source.vertices.size()*4);
          for(size_t v=0;v<source.vertices.size();++v) for(unsigned c=0;c<4;++c) {
            const int node=animated.joints[animated.bone[v][c]];
            const auto &joints=slot.portable_animation->joints;
            mesh.boneIndices[v*4+c]=static_cast<unsigned char>(std::find(joints.begin(),joints.end(),node)-joints.begin());
            mesh.boneWeights[v*4+c]=animated.weights[v][c];
          }
        }
      }
      mesh.vertexCount=(int)source.vertices.size();
      mesh.triangleCount=(int)(source.indices.empty()?source.vertices.size():source.indices.size())/3;
      mesh.vertices=allocate<float>(source.vertices.size()*3);
      mesh.normals=allocate<float>(source.vertices.size()*3);
      mesh.texcoords=allocate<float>(source.vertices.size()*2);
      mesh.colors=allocate<unsigned char>(source.vertices.size()*4);
      if(!source.indices.empty()) {
        mesh.indices=allocate<unsigned short>(source.indices.size());
        std::copy(source.indices.begin(),source.indices.end(),mesh.indices);
      }
      for(size_t i=0;i<source.vertices.size();++i) {
        const auto &v=source.vertices[i];
        mesh.vertices[i*3]=v.x; mesh.vertices[i*3+1]=v.y; mesh.vertices[i*3+2]=v.z;
        const auto n=source.normals.empty()?vec3{}:source.normals[i];
        mesh.normals[i*3]=n.x; mesh.normals[i*3+1]=n.y; mesh.normals[i*3+2]=n.z;
        RenderUvTransform uv;
        if(size_t(source.material)<slot.portable_materials.size()) uv=slot.portable_materials[source.material].albedo.uv;
        const auto base=uv.set?v.uv1:std::array<float,2>{v.u,v.v}; const auto &a=uv.values;
        mesh.texcoords[i*2]=a[0]*base[0]+a[1]*base[1]+a[4];
        mesh.texcoords[i*2+1]=a[2]*base[0]+a[3]*base[1]+a[5];
        for(int c=0;c<4;++c) mesh.colors[i*4+c]=(unsigned char)(v.color[c]*255);
        slot.bounds.min=Vector3Min(slot.bounds.min,{v.x,v.y,v.z});
        slot.bounds.max=Vector3Max(slot.bounds.max,{v.x,v.y,v.z});
      }
      slot.model.meshMaterial[k]=source.material;
      if(!prepared) slot.portable_meshes.push_back(renderer_create_mesh(*store.portable_context,source.vertices,source.indices));
    }
    portable_animation_attach(slot);
    slot.alive=true;
    if(prepared) {prepared->slot=std::move(slot);prepared->meshes=std::move(meshes);return {1};}
    store.slots.push_back(std::move(slot));
    return {(u32)store.slots.size()};
  } catch(...) {
    if(!prepared) release_gpu(store,slot); portable_model_free_cpu(slot); throw;
  }
}
RenderTextureHandle white(const model_store &store) {
  if(!store.portable_white) {
    const unsigned char pixel[]{255,255,255,255};
    store.portable_white=renderer_create_texture(*store.portable_context,{1,1,pixel});
  }
  return store.portable_white;
}
void require_pass(const model_store &store) {
  if(!store.portable_pass) throw std::logic_error("njin bgfx 3D: draw requires begin_3d/end_3d inside phase_render");
}
void submit(const context &ctx,RenderMeshHandle mesh,RenderTextureHandle texture,bool linear,
            const Matrix &world,rgba tint,const material3d &surface,RenderCull cull,bool shadow_only=false,const RenderMaterialMaps &maps={},const RenderTextureMap &color_map={},bool alpha_blend=false,bool alpha_mask=false,shader_handle shader={},bool force_opaque=false,bool builtin=false,std::shared_ptr<const RenderTerrainImprint> imprint={},std::shared_ptr<const RenderTerrainRelief> relief={}) {
  model_store &store=ctx_ref<model_store>(const_cast<context&>(ctx)); require_pass(store);
  if(ctx_ref<window_guard>(ctx).renderer_resources.active_target.id!=store.portable_target.id)
    throw std::logic_error("njin bgfx 3D: render target changed inside an open 3D pass");
  color_check(tint); portable_surface_validate(surface);
  if(surface.clay>0) unsupported("clay on ordinary meshes");
  const auto &textures=ctx_ref<window_guard>(ctx).renderer_resources.textures;
  const bool texture_alpha=texture&&textures.at(texture.id).alpha;
  alpha_mask=alpha_mask||surface.alpha_cutoff>0;
  const bool transparent=!force_opaque&&(surface.transmission>0||alpha_blend||(!alpha_mask&&(tint.a<1||texture_alpha)));
  // Without post3d.oit transparent draws are alpha blended back to front after the opaque
  // ones (as on OpenGL); transmission needs OIT's scene copy.
  if(transparent&&!store.portable_frame_post.oit&&surface.transmission>0) unsupported("transmission without post3d.oit");
  if(!shader.id&&!builtin) shader=ctx_ref<shader_store>(ctx)->active;
  auto custom=portable_shader_bindings(ctx,shader);
  if(custom&&custom->custom_vertex) unsupported("3D draws with a shader's own vertex stage (it would need shadow/normal/velocity variants)");
  if(custom&&transparent) unsupported("custom transparent material shaders require an OIT variant");
  MeshDraw draw{};draw.shader=std::move(custom); draw.mesh=mesh; draw.texture=texture?texture:white(store);
  const auto &f=store.portable_fx;portable_fx_validate(f);
  // Like the legacy path, game fragment shaders own their color/coverage behavior.
  if(!draw.shader) {
    auto &out=draw.world_effects;
    out.flash={f.flash.r,f.flash.g,f.flash.b,f.flash.a};
    out.edge={f.edge_color.r,f.edge_color.g,f.edge_color.b,f.edge_color.a};
    out.dissolve={f.dissolve,f.grain,f.seed,f.edge_width};
    out.from={f.dissolve_from.x,f.dissolve_from.y,f.dissolve_from.z,f.dissolve_sweep};
    out.to={f.dissolve_to.x,f.dissolve_to.y,f.dissolve_to.z,0};
    out.mask={f.mask_center.x,f.mask_center.y,f.mask_center.z,f.mask_radius};
    const auto p=store.portable_camera.position;out.camera={p.x,p.y,p.z,0};
  }
  const auto &sun=store.portable_frame_light;
  draw.surface_effects={{{sun.fog_color.r,sun.fog_color.g,sun.fog_color.b,sun.fog_density},
    {float(sun.toon_bands),sun.toon_softness,sun.toon_threshold,0},
    {surface.rim.r,surface.rim.g,surface.rim.b,surface.rim.a},
    {surface.clearcoat,surface.clearcoat_roughness,surface.anisotropy,surface.anisotropy_rotation*PI/180.f}}};
  draw.world_style[4]={surface.snow_color.r,surface.snow_color.g,surface.snow_color.b,surface.snow};
  draw.world_style[5]={surface.glitter,surface.glitter_lamps?1.f:0.f,surface.glitter_metal?1.f:0.f,0};
  draw.tint={tint.r,tint.g,tint.b,tint.a}; draw.linear_sampling=linear;
  draw.maps=maps; draw.color_uv=color_map.uv; draw.wrap_s=color_map.wrap_s; draw.wrap_t=color_map.wrap_t; draw.color_min_filter=color_map.min_filter;
  const auto advanced_map=[&](texture_handle id,RenderTextureMap &out) {
    if(!id.id) return;
    const auto *t=texture_slot_of(ctx_ref<texture_store>(ctx),id);
    if(!t||!t->portable||t->packed) throw std::invalid_argument("njin bgfx material: invalid or packed advanced map");
    out.texture=t->portable;out.linear=t->filter!=filter_nearest;
  };
  advanced_map(surface.height,draw.maps.height);advanced_map(surface.lightmap,draw.maps.lightmap);
  advanced_map(surface.under,draw.maps.under);advanced_map(surface.under_normal,draw.maps.under_normal);
  draw.maps.advanced={surface.world_uv,surface.under_amount,surface.height_scale,surface.lightmap_intensity};
  draw.transmission={{{surface.transmission,surface.ior,surface.thickness,surface.attenuation_distance},
    {linear_color(surface.attenuation_color.r),linear_color(surface.attenuation_color.g),linear_color(surface.attenuation_color.b),0}}};
  if(draw.maps.lightmap.texture) draw.maps.lightmap_linear=textures.at(draw.maps.lightmap.texture.id).cpu.format==RenderImageFormat::RGBA16F;
  draw.lighting.roughness=surface.roughness;
  if(!surface.unlit) {
    if(std::abs(MatrixDeterminant(world))<1e-12f) throw std::invalid_argument("njin bgfx 3D: singular lit transform");
    const auto &light=store.portable_frame_light;
    draw.lighting.enabled=true;
    draw.lighting.direction={light.direction.x,light.direction.y,light.direction.z};
    draw.lighting.color={light.color.r,light.color.g,light.color.b}; draw.lighting.intensity=light.color.a;
    draw.lighting.ambient={light.ambient.r,light.ambient.g,light.ambient.b};
    draw.lighting.camera={store.portable_camera.position.x,store.portable_camera.position.y,store.portable_camera.position.z};
    draw.lighting.pbr=surface.pbr; draw.lighting.metallic=surface.metallic; draw.lighting.roughness=surface.roughness;
    draw.lighting.specular=surface.specular; draw.lighting.shininess=surface.shininess;
  }
  const auto model=MatrixToFloatV(world),normal=MatrixToFloatV(MatrixTranspose(MatrixInvert(world)));
  std::copy_n(model.v,16,draw.world.begin()); std::copy_n(normal.v,16,draw.normal_transform.begin());
  draw.cast_shadow=surface.cast_shadows; draw.receive_shadow=surface.receive_shadows; draw.shadow_only=shadow_only;
  draw.linear_output=store.portable_hdr_active;draw.reflectivity=surface.reflect;
  draw.emission={surface.emission.r,surface.emission.g,surface.emission.b}; draw.emission_strength=surface.emission.a;
  if(!surface.unlit) {
    draw.tonemap=static_cast<RenderToneMap>(store.portable_frame_light.tonemap);
    draw.exposure=store.portable_frame_light.exposure;
  }
  const auto mvp=MatrixToFloatV(MatrixMultiply(world,store.portable_view_projection));
  std::copy_n(mvp.v,16,draw.transform.begin());
  draw.alpha_cutoff=surface.alpha_cutoff; draw.opaque_alpha=!transparent; draw.transparent=transparent; draw.cull=cull;
  auto &resources=ctx_ref<window_guard>(const_cast<context&>(ctx)).renderer_resources;
  bool retained_mesh=false; std::array<uint64_t,18> retained{}; unsigned retained_count=0;
  try {
    retained_mesh=draw.mesh&&resources.pending_3d_meshes.insert(draw.mesh.id).second;
    if(resources.pending_3d_textures.insert(draw.texture.id).second) retained[retained_count++]=draw.texture.id;
    for(auto map:rhi::all_material_maps(draw)) if(map->texture && resources.pending_3d_textures.insert(map->texture.id).second) retained[retained_count++]=map->texture.id;
    if(imprint&&resources.pending_3d_textures.insert(imprint->texture.id).second) retained[retained_count++]=imprint->texture.id;
    if(relief) for(auto h:relief->textures) if(resources.pending_3d_textures.insert(h.id).second) retained[retained_count++]=h.id;
    draw.terrain_relief=std::move(relief);
    draw.terrain_imprint=std::move(imprint);
    store.portable_queue.push_back(draw);
  } catch(...) {
    if(retained_mesh) resources.pending_3d_meshes.erase(draw.mesh.id);
    for(unsigned i=0;i<retained_count;++i) resources.pending_3d_textures.erase(retained[i]);
    throw;
  }
}
std::vector<float> unpack(const cgltf_accessor *accessor,size_t components,size_t count) {
  if(!accessor) return {};
  if(accessor->count!=count || cgltf_num_components(accessor->type)!=components)
    throw std::runtime_error("njin bgfx 3D: invalid glTF attribute size");
  std::vector<float> values(count*components);
  if(cgltf_accessor_unpack_floats(accessor,values.data(),values.size())!=values.size())
    throw std::runtime_error("njin bgfx 3D: could not decode glTF attribute");
  for(float x:values) if(!std::isfinite(x)) throw std::runtime_error("njin bgfx 3D: non-finite glTF attribute");
  return values;
}
bool name_matches(const char *name,const char *const *list,u32 count) {
  for(u32 i=0;i<count;++i) if(list[i]&&name&&std::strstr(name,list[i])) return true;
  return false;
}
RenderTextureHandle upload_image(context *ctx,RenderImageAsset image,const std::string &image_error,bool mask,const char *role,bool mipmapped,PreparedModel *prepared=nullptr);
// EXT/KHR_meshopt_compression: decodes each compressed buffer view into its
// cgltf override. Translated from meshoptimizer v1.3
// gltf/parsegltf.cpp decompressMeshopt (MIT), without the colour filter this
// cgltf version does not parse.
void decompress_meshopt(cgltf_data &data) {
  for(size_t i=0;i<data.buffer_views_count;++i) {
    auto &view=data.buffer_views[i];
    if(!view.has_meshopt_compression) continue;
    const auto &mc=view.meshopt_compression;
    if(!mc.buffer||!mc.buffer->data||mc.offset+mc.size>mc.buffer->size||!mc.count||!mc.stride||mc.count>UINT_MAX/mc.stride)
      throw std::runtime_error("njin bgfx 3D: invalid meshopt compressed buffer view");
    const auto *source=static_cast<const unsigned char*>(mc.buffer->data)+mc.offset;
    // cgltf_free releases it through raylib's allocator (Njin's tracked heap).
    void *result=MemAlloc(unsigned(mc.count*mc.stride));
    if(!result) throw std::bad_alloc();
    view.data=result;
    int rc=-1;
    switch(mc.mode) {
      case cgltf_meshopt_compression_mode_attributes:rc=meshopt_decodeVertexBuffer(result,mc.count,mc.stride,source,mc.size);break;
      case cgltf_meshopt_compression_mode_triangles:rc=meshopt_decodeIndexBuffer(result,mc.count,mc.stride,source,mc.size);break;
      case cgltf_meshopt_compression_mode_indices:rc=meshopt_decodeIndexSequence(result,mc.count,mc.stride,source,mc.size);break;
      default:break;
    }
    if(rc!=0) throw std::runtime_error("njin bgfx 3D: meshopt buffer view could not be decoded");
    switch(mc.filter) {
      case cgltf_meshopt_compression_filter_octahedral:meshopt_decodeFilterOct(result,mc.count,mc.stride);break;
      case cgltf_meshopt_compression_filter_quaternion:meshopt_decodeFilterQuat(result,mc.count,mc.stride);break;
      case cgltf_meshopt_compression_filter_exponential:meshopt_decodeFilterExp(result,mc.count,mc.stride);break;
      default:break;
    }
  }
}
RenderTextureHandle image_texture(context *ctx,const cgltf_image &source,const std::string &path,bool mask,const char *role="albedo",bool mipmapped=false,PreparedModel *prepared=nullptr) {
  const auto image_error=std::string("njin bgfx 3D: glTF ")+role+" image";
  RenderImageAsset image;
  // Decoded with bimg (njin_image_io.h), not raylib; an image that is not there or not
  // an image is the same error as before.
  const auto decode=[&](std::span<const std::uint8_t> bytes) {
    try {image=image_io_decode(bytes,RenderImageFormat::RGBA8,image_error);}
    catch(const std::runtime_error &) {image={};}
  };
  if(source.buffer_view) {
    const auto *bytes=static_cast<const std::uint8_t*>(cgltf_buffer_view_data(source.buffer_view));
    if(bytes && source.buffer_view->size<=INT_MAX) decode({bytes,source.buffer_view->size});
  } else if(source.uri && std::strncmp(source.uri,"data:",5)==0) {
    const char *comma=std::strchr(source.uri,',');
    if(!comma || std::string(source.uri,size_t(comma-source.uri)).find(";base64")==std::string::npos)
      unsupported("non-base64 glTF data URI");
    const auto decoded=base64_decode(comma+1);
    if(!decoded.empty()) decode(decoded);
  } else if(source.uri) {
    std::string uri=source.uri; cgltf_decode_uri(uri.data());
    std::vector<std::uint8_t> bytes;
    if(image_io_read_file((std::filesystem::path(path).parent_path()/uri).string().c_str(),bytes)) decode(bytes);
  }
  return upload_image(ctx,std::move(image),image_error,mask,role,mipmapped,prepared);
}
// An RGBA8 image decoded by njin_image_io (empty when it could not be).
RenderTextureHandle upload_image(context *ctx,RenderImageAsset image,const std::string &image_error,bool mask,const char *role,bool mipmapped,PreparedModel *prepared) {
  if(image.pixels.empty()) throw std::runtime_error(image_error+" could not be decoded");
  if(image.format!=RenderImageFormat::RGBA8||image.pixels.size()!=size_t(image.width)*image.height*4)
    throw std::runtime_error(image_error+" conversion to RGBA8 failed");
  if(image.width<=0 || image.height<=0 || image.width>16384 || image.height>16384)
    throw std::runtime_error(image_error+" dimensions must be in [1,16384]");
  auto *pixels=image.pixels.data();
  if(!mask) for(size_t i=0;i<size_t(image.width)*image.height;++i) pixels[i*4+3]=255;
  const auto mips=!mipmapped?RenderMipMode::None:std::strcmp(role,"normal")==0?RenderMipMode::Normal:(std::strcmp(role,"albedo")==0||std::strcmp(role,"emissive")==0)?RenderMipMode::SRGB:RenderMipMode::Linear;
  const RenderImage view{(unsigned)image.width,(unsigned)image.height,{pixels,size_t(image.width)*image.height*4},RenderImageFormat::RGBA8,mips};
  if(prepared) {
    prepared->images.push_back({view.width,view.height,{view.rgba.begin(),view.rgba.end()},view.format,view.mips});
    prepared->chains.emplace_back(view);
    return {prepared->images.size()};
  }
  return renderer_create_texture(*ctx,view);
}
}

void portable_model_free_cpu(model_slot &slot) noexcept {
  for(int i=0;i<slot.model.meshCount;++i) {
    auto &m=slot.model.meshes[i];
    MemFree(m.vertices); MemFree(m.normals); MemFree(m.texcoords); MemFree(m.colors); MemFree(m.indices); MemFree(m.boneIndices); MemFree(m.boneWeights);
  }
  MemFree(slot.model.meshes); MemFree(slot.model.meshMaterial); slot.model={};
}
void portable_model_unload(model_store &store,model_handle handle) {
  auto *slot=model_slot_of(store,handle); if(!slot) return;
  if(ctx_ref<window_guard>(*store.portable_context).graphics->frame_open())
    throw std::logic_error("njin bgfx 3D: unload models outside render frames");
  release_gpu(store,*slot);
  for(auto &borrower:store.slots) if(borrower.alive&&borrower.anim_from.id==handle.id&&borrower.portable_animation)
    borrower.portable_animation->animation_library.reset();
  portable_model_free_cpu(*slot); *slot={};
}
void portable_light_validate(const light3d_source &l) {
  if(l.shadows && l.kind==light3d_spot && l.cone>=179.0f) throw std::invalid_argument("njin shadows: shadowed spot cone must be below 179 degrees");
  if(l.kind!=light3d_point&&l.kind!=light3d_spot) throw std::invalid_argument("njin bgfx 3D: invalid local light kind");
  if(!finite(l.position)||!finite(l.direction)||!std::isfinite(l.radius)||l.radius<=0||!std::isfinite(l.intensity)||l.intensity<0)
    throw std::invalid_argument("njin bgfx 3D: invalid local light position/radius/intensity");
  for(float c:{l.color.r,l.color.g,l.color.b}) if(!std::isfinite(c)||c<0) throw std::invalid_argument("njin bgfx 3D: invalid local light color");
  if(l.kind==light3d_spot && (length(l.direction)<0.00001f||!std::isfinite(l.cone)||l.cone<=0||l.cone>180||!std::isfinite(l.softness)||l.softness<0||l.softness>1))
    throw std::invalid_argument("njin bgfx 3D: invalid spot cone/direction");
}
void portable_light_add(context &ctx,const light3d_source &l) {
  model_store &store=ctx_ref<model_store>(ctx); if(!store.portable_pass) return;
  portable_light_validate(l);
  if(store.portable_lights.size()>=size_t(store.portable_frame_light.lights_max)) {
    NJIN_WARN_LIMITED("bgfx 3D: local light limit reached (%d)",store.portable_frame_light.lights_max); return;
  }
  store.portable_lights.push_back(l);
}
void portable_surface_validate(const material3d &m) {
  color_check(m.rim);
  if(!std::isfinite(m.clearcoat)||m.clearcoat<0||m.clearcoat>1||
     !std::isfinite(m.clearcoat_roughness)||m.clearcoat_roughness<0||m.clearcoat_roughness>1||
     !std::isfinite(m.anisotropy)||m.anisotropy < -1||m.anisotropy>1||!std::isfinite(m.anisotropy_rotation))
    throw std::invalid_argument("njin material: invalid clearcoat or anisotropy");
  if(!std::isfinite(m.reflect)||m.reflect<0||m.reflect>1) throw std::invalid_argument("njin bgfx 3D: reflect must be in [0,1]");
  if(!std::isfinite(m.specular)||m.specular<0||!std::isfinite(m.shininess)||m.shininess<=0)
    throw std::invalid_argument("njin bgfx 3D: invalid specular/shininess");
  if(!std::isfinite(m.metallic)||m.metallic<0||m.metallic>1||!std::isfinite(m.roughness)||m.roughness<0||m.roughness>1)
    throw std::invalid_argument("njin bgfx 3D: metallic/roughness must be in [0,1]");
  for(float v:{m.world_uv,m.under_amount,m.height_scale,m.lightmap_intensity})
    if(!std::isfinite(v)||v<0) throw std::invalid_argument("njin material: invalid advanced map factor");
  if(m.under_amount>1) throw std::invalid_argument("njin material: under_amount outside [0,1]");
  if(m.under_normal.id&&!m.under.id) throw std::invalid_argument("njin material: under normal requires an under color map");
  if((m.under.id||m.under_amount!=0)&&m.world_uv<=0) throw std::invalid_argument("njin material: under layer requires world_uv");
  if(!std::isfinite(m.snow)||m.snow<0||m.snow>1||!std::isfinite(m.glitter)||m.glitter<0||m.glitter>4)
    throw std::invalid_argument("njin material: snow must be in [0,1] and glitter in [0,4]");
  if(m.snow>0) color_check(m.snow_color);
  if(!std::isfinite(m.clay)||m.clay<0||m.clay>1||(m.clay>0&&(!std::isfinite(m.clay_detail)||m.clay_detail<=0)))
    throw std::invalid_argument("njin material: invalid clay amount or frequency");
  if(!std::isfinite(m.transmission)||m.transmission<0||m.transmission>1||!std::isfinite(m.ior)||m.ior<1||
     !std::isfinite(m.thickness)||m.thickness<0||!std::isfinite(m.attenuation_distance)||m.attenuation_distance<0)
    throw std::invalid_argument("njin material: invalid transmission/volume factor");
  color_check(m.attenuation_color);
  if(m.transmission>0&&!m.pbr) throw std::invalid_argument("njin material: transmission requires PBR");
  if(!std::isfinite(m.emission.r)||!std::isfinite(m.emission.g)||!std::isfinite(m.emission.b)||!std::isfinite(m.emission.a)||
     m.emission.r<0||m.emission.g<0||m.emission.b<0||m.emission.a<0)
    throw std::invalid_argument("njin bgfx 3D: emission must be finite and nonnegative");
  if(!std::isfinite(m.occlusion_strength)||m.occlusion_strength<0||m.occlusion_strength>1) throw std::invalid_argument("njin bgfx 3D: invalid occlusion strength");
  if(!std::isfinite(m.alpha_cutoff)||m.alpha_cutoff<0||m.alpha_cutoff>1)
    throw std::invalid_argument("njin bgfx 3D: invalid alpha cutoff");
}
void portable_fx_validate(const fx3d &f) {
  color_check(f.flash);color_check(f.edge_color);
  if(!std::isfinite(f.dissolve)||f.dissolve<0||f.dissolve>1||
     !std::isfinite(f.edge_width)||f.edge_width<0||f.edge_width>1||
     !std::isfinite(f.grain)||f.grain<=0||!std::isfinite(f.seed)||
     !finite(f.dissolve_from)||!finite(f.dissolve_to)||!finite(f.mask_center)||
     !std::isfinite(f.dissolve_sweep)||f.dissolve_sweep<0||f.dissolve_sweep>1||
     !std::isfinite(f.mask_radius)||f.mask_radius<0)
    throw std::invalid_argument("njin bgfx 3D FX: invalid finite/range parameter");
}
void portable_material_validate(const model_material &m) {
  portable_surface_validate(m.surface); color_check(m.color);
  for(float c:{m.emission_color.r,m.emission_color.g,m.emission_color.b,m.emission_color.a}) if(!std::isfinite(c)||c<0) throw std::invalid_argument("njin bgfx 3D: invalid emission map tint");
  if(m.sway!=0||m.flutter!=0 )
    unsupported("advanced model material maps/shaders");
}
model_handle portable_model_create(model_store &store,const mesh3d_data &source) {
  if(!source.positions || !source.vertex_count || source.vertex_count>max_elements ||
     (source.indices ? (!source.index_count||source.index_count%3||source.vertex_count>65535||source.index_count>max_elements)
                     : source.vertex_count%3!=0)) {
    NJIN_WARN("model_create: invalid triangle mesh"); return {};
  }

  CpuMesh mesh;
  for(u32 i=0;i<source.vertex_count;++i) {
    const auto p=source.positions[i]; const auto uv=source.texcoords?source.texcoords[i]:vec2{};
    const auto color=source.colors?source.colors[i]:rgba{1,1,1,1}; color_check(color);
    if(!finite(p)||!std::isfinite(uv.x)||!std::isfinite(uv.y)) throw std::invalid_argument("njin bgfx 3D: non-finite vertex");
    mesh.vertices.push_back({p.x,p.y,p.z,uv.x,uv.y,1,{color.r,color.g,color.b,color.a}});
    if(source.texcoords2) mesh.vertices.back().uv1={source.texcoords2[i].x,source.texcoords2[i].y};
    if(source.normals) { if(!finite(source.normals[i])) throw std::invalid_argument("njin bgfx 3D: non-finite normal"); mesh.normals.push_back(source.normals[i]); }
  }
  if(source.indices) for(u32 i=0;i<source.index_count;++i) {
    if(source.indices[i]>=source.vertex_count) { NJIN_WARN("model_create: index out of range"); return {}; }
    mesh.indices.push_back((uint16_t)source.indices[i]);
  }
  if(!source.normals) {
    mesh.normals.resize(mesh.vertices.size());
    const size_t count=mesh.indices.empty()?mesh.vertices.size():mesh.indices.size();
    for(size_t i=0;i<count;i+=3) {
      const size_t a=mesh.indices.empty()?i:mesh.indices[i],b=mesh.indices.empty()?i+1:mesh.indices[i+1],c=mesh.indices.empty()?i+2:mesh.indices[i+2];
      const auto point=[&](size_t k) { const auto &v=mesh.vertices[k]; return vec3{v.x,v.y,v.z}; };
      const auto n=cross(point(b)-point(a),point(c)-point(a));
      mesh.normals[a]+=n; mesh.normals[b]+=n; mesh.normals[c]+=n;
    }
    for(auto &n:mesh.normals) if(length(n)>0) n=normalize(n);
  }
  std::vector<CpuMesh> meshes; meshes.push_back(std::move(mesh));
  return commit(store,meshes,{model_material{}},{source.material?source.material:""});
}

model_handle portable_model_combine(context &ctx,const model_place *places,u32 count,float screen,u32 from_level) {
  auto &store=ctx_ref<model_store>(ctx);auto &window=ctx_ref<window_guard>(ctx);
  if(window.graphics->frame_open()) throw std::logic_error("njin model_combine: combine outside render frames");
  if(!std::isfinite(screen)||screen<0) throw std::invalid_argument("njin model_combine: invalid screen threshold");
  if(!places||!count) return {};
  from_level=std::min(from_level,4u);
  if(count>1000000) throw std::invalid_argument("njin model_combine: too many placements");
  struct Piece {model_handle model;unsigned mesh;Matrix world,normal;bool mirrored;};
  struct Group {std::string name;model_material material;model_slot::portable_material_data maps;std::vector<std::vector<Piece>> pieces;size_t vertices=0;};
  std::vector<Group> groups;std::unordered_map<std::string,size_t> names;unsigned levels=0;
  for(u32 i=0;i<count;++i) {
    const auto *slot=model_slot_of(store,places[i].model);if(!slot) continue;
    if(slot->portable_animation) {NJIN_WARN("model_combine: animated/morph models are left out");continue;}
    const auto transform=transform_matrix(places[i].transform);const float determinant=MatrixDeterminant(transform);
    if(std::abs(determinant)<1e-12f) throw std::invalid_argument("njin model_combine: singular placement");
    auto normal=MatrixTranspose(MatrixInvert(transform));normal.m12=normal.m13=normal.m14=0;
    levels=std::max(levels,unsigned(slot->portable_lods.size()>from_level?slot->portable_lods.size()-from_level:0));
    for(unsigned mesh=0;mesh<slot->portable_meshes.size();++mesh) {
      const auto material=slot->model.meshMaterial[mesh];const auto &name=slot->material_names.at(material);
      const auto key=name.empty()?"#"+std::to_string(places[i].model.id)+":"+std::to_string(material):name;
      auto found=names.find(key);
      if(found==names.end()) {
        Group group;group.name=name;group.material=slot->materials.at(material);
        if(size_t(material)<slot->portable_materials.size()) group.maps=slot->portable_materials[material];
        found=names.emplace(key,groups.size()).first;groups.push_back(std::move(group));
      }
      size_t vertices=0;
      for(unsigned level=from_level;level<=std::max(from_level,unsigned(slot->portable_lods.size()));++level)
        vertices=std::max(vertices,window.renderer_resources.meshes.at(portable_model_lod_mesh(*slot,mesh,level).id).cpu.size());
      if(vertices>65535) throw std::invalid_argument("njin model_combine: split source meshes into at most 65535 vertices");
      auto &group=groups[found->second];
      if(group.pieces.empty()||group.vertices+vertices>65535) {group.pieces.emplace_back();group.vertices=0;}
      group.pieces.back().push_back({places[i].model,mesh,transform,normal,determinant<0});group.vertices+=vertices;
    }
  }
  if(groups.empty()) return {};
  std::vector<model_material> materials;std::vector<std::string> material_names;std::vector<model_slot::portable_material_data> maps;
  for(const auto &group:groups) {materials.push_back(group.material);material_names.push_back(group.name);maps.push_back(group.maps);}
  std::vector<std::vector<CpuMesh>> output(levels+1);size_t vertex_budget=0;
  for(unsigned level=0;level<=levels;++level) for(size_t material=0;material<groups.size();++material) for(const auto &pieces:groups[material].pieces) {
    CpuMesh out;out.material=int(material);
    for(const auto &piece:pieces) {
      const auto *slot=model_slot_of(store,piece.model);const auto &source=window.renderer_resources.meshes.at(portable_model_lod_mesh(*slot,piece.mesh,level+from_level).id);
      const auto base=out.vertices.size();vertex_budget+=source.cpu.size();
      if(vertex_budget>max_elements) throw std::runtime_error("njin model_combine: combined vertex budget exceeded");
      for(auto v:source.cpu) {
        const auto p=Vector3Transform({v.x,v.y,v.z},piece.world);v.x=p.x;v.y=p.y;v.z=p.z;
        const auto n=Vector3Normalize(Vector3Transform({v.normal[0],v.normal[1],v.normal[2]},piece.normal));v.normal={n.x,n.y,n.z};out.normals.push_back({n.x,n.y,n.z});
        auto linear=piece.world;linear.m12=linear.m13=linear.m14=0;
        auto tangent=Vector3Transform({v.tangent[0],v.tangent[1],v.tangent[2]},linear);
        tangent=Vector3Normalize(Vector3Subtract(tangent,Vector3Scale(n,Vector3DotProduct(n,tangent))));
        v.tangent={tangent.x,tangent.y,tangent.z,v.tangent[3]*(piece.mirrored?-1.f:1.f)};out.vertices.push_back(v);
      }
      const auto indices=source.indices.empty()?source.cpu.size():source.indices.size();
      for(size_t triangle=0;triangle<indices;triangle+=3) for(unsigned corner=0;corner<3;++corner) {
        const auto k=triangle+(piece.mirrored&&corner?3-corner:corner);const auto index=source.indices.empty()?k:source.indices[k];
        out.indices.push_back(u16(base+index));
      }
    }
    output[level].push_back(std::move(out));
  }
  std::vector<std::vector<RenderMeshHandle>> staged(levels);
  try {
    for(unsigned level=0;level<levels;++level) for(const auto &mesh:output[level+1]) staged[level].push_back(renderer_create_mesh(ctx,mesh.vertices,mesh.indices));
    auto result=commit(store,output[0],std::move(materials),std::move(material_names),std::move(maps));
    auto &slot=store->slots.at(result.id-1);slot.portable_lods=std::move(staged);slot.lod_screen=screen;slot.borrows_textures=true;return result;
  } catch(...) {for(const auto &level:staged) for(auto mesh:level) renderer_destroy_mesh(ctx,mesh);throw;}
}

model_handle portable_model_create_skinned(model_store &store,const skinned_mesh3d_data &source) {
  const auto *skeleton=model_slot_of(store,source.skeleton);
  if(!skeleton||!skeleton->skinned||!skeleton->portable_animation||!source.positions||!source.joints||!source.weights||
     !source.indices||!source.vertex_count||source.vertex_count>max_elements||!source.index_count||source.index_count>max_elements||source.index_count%3) {
    NJIN_WARN("model_create_skinned: valid skeleton, positions, joints, weights and triangles are required");return {};
  }
  const auto library=skeleton->portable_animation;
  for(u32 i=0;i<source.index_count;++i) if(source.indices[i]>=source.vertex_count) {NJIN_WARN("model_create_skinned: invalid index");return {};}
  std::vector<vec3> normals(source.vertex_count);
  for(u32 i=0;i<source.vertex_count;++i) {
    if(!finite(source.positions[i])||(source.normals&&!finite(source.normals[i]))) throw std::invalid_argument("njin procedural skin: non-finite vertex/normal");
    if(source.texcoords&&(!std::isfinite(source.texcoords[i].x)||!std::isfinite(source.texcoords[i].y))) throw std::invalid_argument("njin procedural skin: non-finite UV");
    float total=0;
    for(unsigned k=0;k<4;++k) {
      const auto weight=source.weights[size_t(i)*4+k];
      if(source.joints[size_t(i)*4+k]>=library->joints.size()||!std::isfinite(weight)||weight<0) throw std::invalid_argument("njin procedural skin: invalid skin influence");
      total+=weight;
    }
    if(!std::isfinite(total)||total<1e-8f) throw std::invalid_argument("njin procedural skin: skin weights sum to zero");
    if(source.normals) normals[i]=source.normals[i];
  }
  if(!source.normals) for(u32 j=0;j<source.index_count;j+=3) {
    const auto a=source.indices[j],b=source.indices[j+1],c=source.indices[j+2];const auto n=cross(source.positions[b]-source.positions[a],source.positions[c]-source.positions[a]);
    normals[a]+=n;normals[b]+=n;normals[c]+=n;
  }
  std::vector<u32> ends;
  if(source.part_count) {if(!source.part_ends||source.part_count>source.index_count/3) throw std::invalid_argument("njin procedural skin: invalid parts");ends.assign(source.part_ends,source.part_ends+source.part_count);}
  else ends.push_back(source.index_count);
  u32 start=0;for(auto end:ends) {if(end<=start||end>source.index_count||end%3) throw std::invalid_argument("njin procedural skin: parts must contain whole triangles");start=end;}
  if(ends.back()!=source.index_count) throw std::invalid_argument("njin procedural skin: parts must cover every triangle");
  auto animation=std::make_shared<PortableAnimation>();
  animation->nodes=library->nodes;animation->joints=library->joints;animation->bones=library->bones;animation->bind_pose=library->bind_pose;
  animation->morph_names=library->morph_names;animation->morph_defaults=library->morph_defaults;
  animation->animation_library=library;animation->borrowed_clips=true;
  const auto bind=portable_animation_bones(*skeleton,{});std::vector<Matrix> inverse;for(const auto &matrix:bind) {
    if(std::abs(MatrixDeterminant(matrix))<1e-12f) throw std::invalid_argument("njin procedural skin: singular bind transform");
    inverse.push_back(MatrixInvert(matrix));
  }
  const int node=int(animation->nodes.size());PortableAnimation::Node root;root.matrix=true;root.fixed=MatrixIdentity();animation->nodes.push_back(root);
  std::vector<CpuMesh> meshes;meshes.reserve(ends.size());std::vector<int> remap(source.vertex_count,-1);start=0;
  for(auto end:ends) {
    CpuMesh mesh;mesh.material=int(meshes.size());PortableAnimation::Mesh skin;skin.node=node;skin.inverse_node=MatrixIdentity();skin.joints=library->joints;skin.inverse_bind=inverse;
    std::vector<u32> used;
    for(u32 j=start;j<end;++j) {
      const auto i=source.indices[j];
      if(remap[i]<0) {
        if(used.size()>=65535) throw std::invalid_argument("njin procedural skin: split parts into at most 65535 vertices");
        remap[i]=int(used.size());used.push_back(i);const auto p=source.positions[i];const auto uv=source.texcoords?source.texcoords[i]:vec2{};
        mesh.vertices.push_back({p.x,p.y,p.z,uv.x,uv.y});mesh.normals.push_back(normals[i]);
        std::array<unsigned char,4> bones;std::array<float,4> weights;float total=0;
        for(unsigned k=0;k<4;++k) {bones[k]=source.joints[size_t(i)*4+k];weights[k]=source.weights[size_t(i)*4+k];total+=weights[k];}
        for(auto &weight:weights) weight/=total;
        skin.bone.push_back(bones);skin.weights.push_back(weights);
      }
      mesh.indices.push_back(static_cast<u16>(remap[i]));
    }
    for(auto i:used) remap[i]=-1;
    start=end;meshes.push_back(std::move(mesh));animation->meshes.push_back(std::move(skin));
  }
  auto handle=commit(store,meshes,std::vector<model_material>(meshes.size()),std::vector<std::string>(meshes.size()),{}, {},animation);
  auto &slot=store.slots.at(handle.id-1);slot.anim_from=source.skeleton;slot.morph_names.clear();slot.morph_defaults.clear();return handle;
}

model_handle portable_model_commit_split(model_store &store,const vec3 *positions,const vec3 *normals,u32 count,
    std::vector<std::vector<u32>> source,std::vector<std::vector<u16>> indices) {
  std::vector<CpuMesh> meshes(source.size());
  for(size_t k=0;k<source.size();++k) {
    auto &mesh=meshes[k];mesh.indices=std::move(indices[k]);
    for(auto i:source[k]) {
      const auto p=positions[i],n=normals[i];
      if(!finite(p)||!finite(n)) throw std::invalid_argument("njin split mesh: non-finite vertex/normal");
      mesh.vertices.push_back({p.x,p.y,p.z,0,0});mesh.normals.push_back(n);
    }
  }
  auto handle=commit(store,meshes,{model_material{}},{""});auto &slot=store.slots.at(handle.id-1);
  slot.split_source=std::move(source);slot.split_count=count;return handle;
}
void portable_model_update_vertices(model_store &store,model_handle handle,const vec3 *positions,const vec3 *normals,u32 count) {
  auto *slot=model_slot_of(store,handle);if(!slot) return;
  if(!positions||!normals) throw std::invalid_argument("njin dynamic mesh: positions and normals are required");
  if(slot->portable_animation) throw std::invalid_argument("njin dynamic mesh: skinned/morph geometry must be changed through its pose");
  if(slot->split_source.empty()?(slot->model.meshCount!=1||unsigned(slot->model.meshes[0].vertexCount)!=count):slot->split_count!=count) return;
  auto &ctx=*store.portable_context;auto &window=ctx_ref<window_guard>(ctx);auto &r=window.renderer_resources;
  for(auto mesh:slot->portable_meshes) if(r.pending_3d_meshes.contains(mesh.id)||window.graphics->mesh_in_use(mesh))
    throw std::logic_error("njin dynamic mesh: update before submitting draws for this model");
  std::vector<std::vector<RenderVertex>> vertices;vertices.reserve(slot->portable_meshes.size());
  BoundingBox bounds{{INFINITY,INFINITY,INFINITY},{-INFINITY,-INFINITY,-INFINITY}};
  for(size_t k=0;k<slot->portable_meshes.size();++k) {
    vertices.push_back(r.meshes.at(slot->portable_meshes[k].id).cpu);
    for(size_t j=0;j<vertices.back().size();++j) {
      const auto i=slot->split_source.empty()?j:slot->split_source[k][j];const auto p=positions[i],n=normals[i];
      if(!finite(p)||!finite(n)) throw std::invalid_argument("njin dynamic mesh: non-finite vertex/normal");
      auto &v=vertices.back()[j];v.x=p.x;v.y=p.y;v.z=p.z;v.normal={n.x,n.y,n.z};
      bounds.min=Vector3Min(bounds.min,{p.x,p.y,p.z});bounds.max=Vector3Max(bounds.max,{p.x,p.y,p.z});
    }
  }
  std::vector<RenderMeshHandle> replacement;replacement.reserve(vertices.size());
  try {for(size_t k=0;k<vertices.size();++k) replacement.push_back(renderer_create_mesh(ctx,vertices[k],r.meshes.at(slot->portable_meshes[k].id).indices));}
  catch(...) {for(auto mesh:replacement) renderer_destroy_mesh(ctx,mesh);throw;}
  try {portable_model_clear_lods(store,*slot);} catch(...) {for(auto mesh:replacement) renderer_destroy_mesh(ctx,mesh);throw;}
  for(auto mesh:slot->portable_meshes) renderer_destroy_mesh(ctx,mesh);
  slot->portable_meshes=std::move(replacement);slot->bounds=bounds;
  for(size_t k=0;k<vertices.size();++k) for(size_t j=0;j<vertices[k].size();++j) {
    auto &mesh=slot->model.meshes[k];const auto &v=vertices[k][j];
    mesh.vertices[j*3]=v.x;mesh.vertices[j*3+1]=v.y;mesh.vertices[j*3+2]=v.z;
    std::copy_n(v.normal.data(),3,mesh.normals+j*3);
  }
}

// OBJ/MTL with the legacy loader's material layout: the mtllib's materials from
// index 0 (no default material first), Kd as the colour unless map_Kd is given,
// Ke kept as emission_color (it only tints an emission map, as before), and
// map_bump as the normal map. Node filters and merge do not apply to OBJ.
static model_handle portable_obj_load(model_store &store,const std::string &path,const char *name,PreparedModel *prepared=nullptr) {
  ObjData obj;
  try {obj=obj_parse(path);} catch(const std::runtime_error &e) {NJIN_WARN("model: invalid OBJ: %s (%s)",name,e.what());return {};}
  // raylib stored these colours as 8-bit values; keep the same quantization.
  const auto byte=[](float v){return float((unsigned char)(std::clamp(v,0.0f,1.0f)*255.0f))/255.0f;};
  std::vector<model_material> materials;std::vector<std::string> names;
  std::vector<model_slot::portable_material_data> data(obj.materials.size());
  std::vector<RenderTextureHandle> owned;
  try {
    const auto file_texture=[&](const std::string &file,const char *role) {
      RenderTextureMap map;
      if(file.empty()) return map;
      RenderImageAsset image;
      try {image=image_io_load(file.c_str());} catch(const std::runtime_error &) {}
      map.texture=upload_image(store.portable_context,std::move(image),std::string("njin bgfx 3D: OBJ ")+role+" image "+file,false,role,false,prepared);
      owned.push_back(map.texture);
      map.linear=false; // raylib's LoadTexture default: nearest filtering, repeat
      return map;
    };
    for(size_t i=0;i<obj.materials.size();++i) {
      const auto &src=obj.materials[i];
      model_material m;
      if(src.diffuse_map.empty()) m.color={byte(src.diffuse[0]),byte(src.diffuse[1]),byte(src.diffuse[2]),1};
      m.emission_color={byte(src.emission[0]),byte(src.emission[1]),byte(src.emission[2]),1};
      data[i].albedo=file_texture(src.diffuse_map,"albedo");
      data[i].maps.normal=file_texture(src.normal_map,"normal");
      materials.push_back(m);names.push_back(src.name);
    }
    std::vector<CpuMesh> meshes;
    size_t total=0;
    for(const auto &group:obj.groups) {
      total+=group.vertices.size();
      if(total>max_elements) throw std::runtime_error("njin bgfx 3D: oversized OBJ model");
      // Triangle lists, split below the 16-bit index limit.
      for(size_t start=0;start<group.vertices.size();start+=65535-65535%3) {
        const size_t end=std::min(group.vertices.size(),start+65535-65535%3);
        CpuMesh mesh;mesh.material=int(group.material);
        for(size_t k=start;k<end;++k) {
          const auto &v=group.vertices[k];
          RenderVertex vertex{v.position[0],v.position[1],v.position[2],v.uv[0],v.uv[1]};
          vertex.normal=v.normal;
          mesh.vertices.push_back(vertex);mesh.normals.push_back({v.normal[0],v.normal[1],v.normal[2]});
          mesh.indices.push_back(uint16_t(k-start));
        }
        meshes.push_back(std::move(mesh));
      }
    }
    return commit(store,meshes,std::move(materials),std::move(names),std::move(data),std::move(owned),{},prepared);
  } catch(...) {
    if(!prepared) for(auto t:owned) renderer_destroy_texture(*store.portable_context,t);
    throw;
  }
}


// IQM, VOX and M3D (njin_model_formats.h): the CPU data raylib's LoadModel made, as a
// portable model. A skeleton becomes a PortableAnimation: one node per bone, its rest
// the local transform of raylib's model-space bind pose, and each clip one linear
// channel per bone and component, keys 1/60 s apart, the rate the legacy renderer plays
// every clip at (render3d.cpp anim_fps), with its last keyframe replaced by the one
// before as legacy does (njin_model.cpp load_skin).
static model_handle portable_imported_load(model_store &store,const imported_model &model,const char *name,PreparedModel *prepared) {
  const auto pose_matrix=[](const imported_bone &b) {
    return MatrixMultiply(MatrixMultiply(MatrixScale(b.scale[0],b.scale[1],b.scale[2]),
      QuaternionToMatrix(Quaternion{b.rotation[0],b.rotation[1],b.rotation[2],b.rotation[3]})),
      MatrixTranslate(b.translation[0],b.translation[1],b.translation[2]));
  };
  const auto local_of=[&](const std::vector<imported_bone> &pose,size_t i) {
    Matrix world=pose_matrix(pose[i]);
    const int parent=model.bones[i].parent;
    if(parent>=0) world=MatrixMultiply(world,MatrixInvert(pose_matrix(pose[size_t(parent)])));
    Transform t;MatrixDecompose(world,&t.translation,&t.rotation,&t.scale);
    t.rotation=QuaternionNormalize(t.rotation);
    return t;
  };
  std::shared_ptr<PortableAnimation> animation;
  std::vector<Matrix> inverse_bind;
  int root=-1;
  if(!model.bones.empty()) {
    if(model.bones.size()>128) {NJIN_WARN("model: %s has more than 128 bones",name);return {};}
    animation=std::make_shared<PortableAnimation>();
    for(size_t i=0;i<model.bones.size();++i) {
      const auto &b=model.bones[i];
      if(b.parent>=int(i)) {NJIN_WARN("model: %s: a bone comes before its parent",name);return {};}
      PortableAnimation::Node node;node.parent=b.parent;node.rest=local_of(model.bones,i);
      animation->nodes.push_back(node);animation->joints.push_back(int(i));
      BoneInfo info{};std::snprintf(info.name,sizeof(info.name),"%s",b.name.c_str());info.parent=b.parent;animation->bones.push_back(info);
      Transform bind{{b.translation[0],b.translation[1],b.translation[2]},{b.rotation[0],b.rotation[1],b.rotation[2],b.rotation[3]},{b.scale[0],b.scale[1],b.scale[2]}};
      animation->bind_pose.push_back(bind);
      const Matrix world=pose_matrix(b);
      if(std::abs(MatrixDeterminant(world))<1e-12f) {NJIN_WARN("model: %s: singular bind pose",name);return {};}
      inverse_bind.push_back(MatrixInvert(world));
    }
    root=int(animation->nodes.size());
    PortableAnimation::Node node;node.matrix=true;node.fixed=MatrixIdentity();animation->nodes.push_back(node);
    for(const auto &clip:model.clips) {
      if(clip.frames.empty()) continue;
      std::vector<std::vector<imported_bone>> frames=clip.frames;
      for(const auto &f:frames) if(f.size()!=model.bones.size()) {NJIN_WARN("model: %s: animation '%s' does not match the skeleton, skipped",name,clip.name.c_str());goto next_clip;}
      if(frames.size()>=2) frames.back()=frames[frames.size()-2];
      {
        PortableAnimation::Clip out;out.name=clip.name;
        const size_t keys=frames.size();
        out.duration=float(keys-1)/60.0f;
        for(size_t b=0;b<model.bones.size();++b) {
          PortableAnimation::Channel t,r,sc;
          t.node=r.node=sc.node=int(b);
          t.path=int(cgltf_animation_path_type_translation);r.path=int(cgltf_animation_path_type_rotation);sc.path=int(cgltf_animation_path_type_scale);
          t.interpolation=r.interpolation=sc.interpolation=int(cgltf_interpolation_type_linear);
          t.components=3;r.components=4;sc.components=3;
          Quaternion previous{0,0,0,1};
          for(size_t k=0;k<keys;++k) {
            const float time=float(k)/60.0f;
            t.times.push_back(time);r.times.push_back(time);sc.times.push_back(time);
            const Transform l=local_of(frames[k],b);
            Quaternion q=l.rotation;
            // Shortest path between keys, as raylib's slerp takes it.
            if(k&&q.x*previous.x+q.y*previous.y+q.z*previous.z+q.w*previous.w<0) q={-q.x,-q.y,-q.z,-q.w};
            previous=q;
            t.values.insert(t.values.end(),{l.translation.x,l.translation.y,l.translation.z});
            r.values.insert(r.values.end(),{q.x,q.y,q.z,q.w});
            sc.values.insert(sc.values.end(),{l.scale.x,l.scale.y,l.scale.z});
          }
          if(keys==1) {for(auto *c:{&t,&r,&sc}) c->times[0]=0;}
          out.channels.push_back(std::move(t));out.channels.push_back(std::move(r));out.channels.push_back(std::move(sc));
        }
        animation->clips.push_back(std::move(out));
      }
      next_clip:;
    }
  }
  // Materials: raylib's diffuse colour and texture (nearest filtering, as LoadTexture).
  std::vector<model_material> materials;std::vector<std::string> names;
  std::vector<model_slot::portable_material_data> data;std::vector<RenderTextureHandle> owned;
  try {
    for(const auto &src:model.materials) {
      model_material m;m.color={src.color[0],src.color[1],src.color[2],src.color[3]};
      model_slot::portable_material_data d;
      RenderImageAsset image;
      if(!src.albedo_rgba.empty()) image=RenderImageAsset{src.albedo_width,src.albedo_height,src.albedo_rgba};
      else if(!src.albedo_path.empty()) {try {image=image_io_load(src.albedo_path.c_str());} catch(const std::runtime_error &) {}}
      if(image.width&&image.height) {
        d.albedo.texture=upload_image(store.portable_context,std::move(image),std::string("njin bgfx 3D: ")+name+" texture",false,"albedo",false,prepared);
        d.albedo.linear=false;owned.push_back(d.albedo.texture);
      }
      materials.push_back(m);names.push_back("");data.push_back(std::move(d));
    }
    if(materials.empty()) {materials.push_back(model_material{});names.push_back("");data.emplace_back();}
    // Meshes: indexed meshes over 65535 vertices and unindexed ones are cut into
    // parts of whole triangles, each with at most 65535 vertices.
    std::vector<CpuMesh> meshes;
    for(const auto &src:model.meshes) {
      const size_t count=src.positions.size()/3;
      if(count==0) continue;
      if(count>max_elements) throw std::runtime_error("njin bgfx 3D: oversized model mesh");
      const size_t elements=src.indices.empty()?count:src.indices.size();
      const auto index=[&](size_t e){return src.indices.empty()?std::uint32_t(e):src.indices[e];};
      std::vector<int> remap(count,-1);
      size_t start=0;
      while(start+3<=elements) {
        CpuMesh mesh;mesh.material=std::clamp(src.material,0,int(materials.size())-1);
        PortableAnimation::Mesh skin;
        std::vector<std::uint32_t> used;size_t end=start;
        while(end+3<=elements) {
          size_t fresh=0;for(size_t k=0;k<3;++k) if(remap[index(end+k)]<0) ++fresh;
          if(used.size()+fresh>65535) break;
          for(size_t k=0;k<3;++k) {
            const auto i=index(end+k);
            if(i>=count) throw std::runtime_error("njin bgfx 3D: model index out of range");
            if(remap[i]<0) {
              remap[i]=int(used.size());used.push_back(i);
              RenderVertex v{src.positions[i*3],src.positions[i*3+1],src.positions[i*3+2],
                             src.texcoords.empty()?0.0f:src.texcoords[i*2],src.texcoords.empty()?0.0f:src.texcoords[i*2+1]};
              if(!src.colors.empty()) v.color={src.colors[i*4]/255.0f,src.colors[i*4+1]/255.0f,src.colors[i*4+2]/255.0f,src.colors[i*4+3]/255.0f};
              mesh.vertices.push_back(v);
              if(!src.normals.empty()) mesh.normals.push_back({src.normals[i*3],src.normals[i*3+1],src.normals[i*3+2]});
              if(animation) {
                std::array<unsigned char,4> ids{};std::array<float,4> weights{};float sum=0;
                if(!src.bone_ids.empty()&&!src.bone_weights.empty()) for(unsigned c=0;c<4;++c) {
                  ids[c]=src.bone_ids[i*4+c];weights[c]=std::max(src.bone_weights[i*4+c],0.0f);
                  // Unused influences (weight 0) may carry any id (M3D fills them so).
                  if(ids[c]>=model.bones.size()) {
                    if(weights[c]>0) throw std::runtime_error("njin bgfx 3D: model bone index out of range");
                    ids[c]=0;
                  }
                  sum+=weights[c];
                }
                if(sum<1e-8f) {ids={0,0,0,0};weights={1,0,0,0};sum=1;}
                for(auto &w:weights) w/=sum;
                skin.bone.push_back(ids);skin.weights.push_back(weights);
              }
            }
            mesh.indices.push_back(std::uint16_t(remap[i]));
          }
          end+=3;
        }
        for(auto i:used) remap[i]=-1;
        if(animation) {
          skin.node=root;skin.inverse_node=MatrixIdentity();skin.inverse_bind=inverse_bind;
          for(size_t j=0;j<model.bones.size();++j) skin.joints.push_back(int(j));
          animation->meshes.push_back(std::move(skin));
        }
        meshes.push_back(std::move(mesh));
        start=end;
      }
    }
    if(meshes.empty()) {NJIN_WARN("model: %s has no triangles",name);for(auto t:owned) renderer_destroy_texture(*store.portable_context,t);return {};}
    return commit(store,meshes,std::move(materials),std::move(names),std::move(data),std::move(owned),std::move(animation),prepared);
  } catch(...) {
    if(!prepared) for(auto t:owned) renderer_destroy_texture(*store.portable_context,t);
    throw;
  }
}

static model_handle load_portable_model(model_store &store,const model_load_desc &desc,PreparedModel *prepared=nullptr) {
  if(!desc.path || !file_exists(asset_path(desc.path).c_str())) { NJIN_WARN("model: file not found: %s",desc.path?desc.path:"(null)"); return {}; }

  if((desc.skip_count&&!desc.skip_nodes)||(desc.only_count&&!desc.only_nodes)) throw std::invalid_argument("njin bgfx 3D: null node filter");
  const std::string path=asset_path(desc.path);
  auto ext=std::filesystem::path(path).extension().string();
  std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
  if(ext==".obj") return portable_obj_load(store,path,desc.path,prepared);
  if(ext==".iqm"||ext==".vox"||ext==".m3d") {
    // As raylib's LoadModel read them on the legacy renderer (no node filter there either).
    if(desc.skip_count||desc.only_count) unsupported("node filters on IQM/VOX/M3D models");
    imported_model model;std::string error;
    const bool ok=ext==".iqm"?import_iqm(path,model,error):ext==".vox"?import_vox(path,model,error):import_m3d(path,model,error);
    if(!ok) {NJIN_WARN("model: invalid %s file: %s (%s)",ext.c_str()+1,desc.path,error.c_str());return {};}
    return portable_imported_load(store,model,desc.path,prepared);
  }
  if(ext!=".gltf"&&ext!=".glb") unsupported("model formats other than glTF/GLB/OBJ/IQM/VOX/M3D");
  cgltf_options options{}; cgltf_data *raw=nullptr;
  if(cgltf_parse_file(&options,path.c_str(),&raw)!=cgltf_result_success) { NJIN_WARN("model: invalid glTF: %s",desc.path); return {}; }
  std::unique_ptr<cgltf_data,void(*)(cgltf_data*)> data(raw,cgltf_free);
  if(cgltf_load_buffers(&options,raw,path.c_str())!=cgltf_result_success || cgltf_validate(raw)!=cgltf_result_success) {
    NJIN_WARN("model: invalid/missing glTF buffer: %s",desc.path); return {};
  }
  // Animation import is CPU-only; it never calls LoadModel or GL upload functions.
  bool require_coat=false,require_anisotropy=false;
  for(size_t i=0;i<raw->extensions_required_count;++i) {
    const std::string_view extension=raw->extensions_required[i];
    if(desc.pbr&&(extension=="KHR_materials_transmission"||extension=="KHR_materials_volume"||extension=="KHR_materials_ior")) continue;
    if(desc.pbr&&extension=="KHR_materials_clearcoat") {require_coat=true;continue;}
    if(desc.pbr&&extension=="KHR_materials_anisotropy") {require_anisotropy=true;continue;}
    if(extension!="KHR_materials_unlit"&&extension!="KHR_texture_transform"&&extension!="KHR_mesh_quantization"&&extension!="KHR_materials_emissive_strength"&&
       extension!="EXT_meshopt_compression"&&extension!="KHR_meshopt_compression")
      unsupported(("required glTF extension "+std::string(extension)).c_str());
  }
  decompress_meshopt(*raw);
  std::vector<model_material> materials; std::vector<std::string> names;
  std::vector<model_slot::portable_material_data> albedos(raw->materials_count+1);
  std::vector<RenderTextureHandle> owned;
  owned.reserve(raw->materials_count*5);
  try {
    materials.emplace_back(); materials.back().surface.pbr=desc.pbr;
    if(desc.pbr) { materials.back().surface.metallic=1; materials.back().surface.roughness=1; }
    names.emplace_back();
    for(size_t i=0;i<raw->materials_count;++i) {
      const auto &src=raw->materials[i];
      if(src.has_pbr_specular_glossiness) unsupported("specular/glossiness glTF materials");
      model_material m; m.surface.unlit=src.unlit; m.surface.pbr=desc.pbr&&!src.unlit; m.double_sided=src.double_sided;
      const auto &pbr=src.pbr_metallic_roughness;

      m.color={pbr.base_color_factor[0],pbr.base_color_factor[1],pbr.base_color_factor[2],src.alpha_mode==cgltf_alpha_mode_opaque?1.f:pbr.base_color_factor[3]};
      if(m.surface.pbr) { color_check(m.color); m.color={display_color(m.color.r),display_color(m.color.g),display_color(m.color.b),m.color.a}; }
      m.surface.metallic=pbr.metallic_factor; m.surface.roughness=pbr.roughness_factor;
      if(src.alpha_mode==cgltf_alpha_mode_mask) m.surface.alpha_cutoff=src.alpha_cutoff;
      auto &data=albedos[i+1];data.blend=src.alpha_mode==cgltf_alpha_mode_blend;data.mask=src.alpha_mode==cgltf_alpha_mode_mask;
      const auto texture_map=[&](const cgltf_texture_view &view,bool mask,const char *role="albedo") {
        RenderTextureMap map;
        if(!view.texture) return map;
        if(!view.texture->image) unsupported("compressed/alternate glTF texture sources");
        const auto sampler=view.texture->sampler;
        const bool mipmapped=sampler&&sampler->min_filter>=cgltf_filter_type_nearest_mipmap_nearest;
        const auto id=image_texture(store.portable_context,*view.texture->image,path,mask,role,mipmapped,prepared);
        owned.push_back(id); map.texture=id;
        const int set=view.has_transform&&view.transform.has_texcoord?view.transform.texcoord:view.texcoord;
        if(set<0||set>1) unsupported("glTF UV sets beyond TEXCOORD_1");
        map.uv.set=unsigned(set);
        if(view.has_transform) {
          const auto &t=view.transform; const float c=std::cos(t.rotation),sn=std::sin(t.rotation);
          map.uv.values={c*t.scale[0],-sn*t.scale[1],sn*t.scale[0],c*t.scale[1],t.offset[0],t.offset[1]};
          for(float v:map.uv.values) if(!std::isfinite(v)) throw std::runtime_error("njin bgfx 3D: invalid glTF UV transform");
        }
        if(const auto sampler=view.texture->sampler) {
          const auto wrap=[](cgltf_wrap_mode w) {
            if(w==cgltf_wrap_mode_repeat) return RenderWrap::Repeat;
            if(w==cgltf_wrap_mode_clamp_to_edge) return RenderWrap::Clamp;
            if(w==cgltf_wrap_mode_mirrored_repeat) return RenderWrap::Mirror;
            throw std::runtime_error("njin bgfx 3D: invalid glTF wrapping");
          };
          map.wrap_s=wrap(sampler->wrap_s); map.wrap_t=wrap(sampler->wrap_t);
          map.linear=sampler->mag_filter!=cgltf_filter_type_nearest;
          switch(sampler->min_filter) {
            case cgltf_filter_type_undefined:map.min_filter=RenderMinFilter::Inherit;break;
            case cgltf_filter_type_nearest:map.min_filter=RenderMinFilter::Nearest;break;
            case cgltf_filter_type_linear:map.min_filter=RenderMinFilter::Linear;break;
            case cgltf_filter_type_nearest_mipmap_nearest:map.min_filter=RenderMinFilter::NearestMipNearest;break;
            case cgltf_filter_type_linear_mipmap_nearest:map.min_filter=RenderMinFilter::LinearMipNearest;break;
            case cgltf_filter_type_nearest_mipmap_linear:map.min_filter=RenderMinFilter::NearestMipLinear;break;
            case cgltf_filter_type_linear_mipmap_linear:map.min_filter=RenderMinFilter::LinearMipLinear;break;
            default:throw std::runtime_error("njin bgfx 3D: invalid glTF minification filter");
          }
        }
        return map;
      };
      data.albedo=texture_map(pbr.base_color_texture,src.alpha_mode!=cgltf_alpha_mode_opaque);
      if(m.surface.pbr) {
        auto &surface=m.surface;
        if(src.has_ior) {
          if(src.ior.ior==0) unsupported("glTF IOR zero compatibility mode");
          surface.ior=src.ior.ior;
        }
        if(src.has_transmission) {
          surface.transmission=src.transmission.transmission_factor;
          data.maps.transmission=texture_map(src.transmission.transmission_texture,false,"transmission");
        }
        if(src.has_volume) {
          surface.thickness=src.volume.thickness_factor;
          if(src.volume.attenuation_distance<=0) throw std::invalid_argument("njin glTF: attenuation distance must be positive");
          surface.attenuation_distance=src.volume.attenuation_distance==FLT_MAX?0:src.volume.attenuation_distance;
          const auto &rgb=src.volume.attenuation_color;
          color_check({rgb[0],rgb[1],rgb[2],1});
          surface.attenuation_color={display_color(rgb[0]),display_color(rgb[1]),display_color(rgb[2]),1};
          data.maps.thickness=texture_map(src.volume.thickness_texture,false,"volume thickness");
        }
        if(src.has_clearcoat) {
          const auto &coat=src.clearcoat;
          surface.clearcoat=coat.clearcoat_factor;
          surface.clearcoat_roughness=coat.clearcoat_roughness_factor;
          // bgfx binds coat maps on the samplers transmission and water use, and
          // at most two distinct bindings; identical texture views share one.
          const auto same_view=[](const cgltf_texture_view &a,const cgltf_texture_view &b) {
            if(a.texture!=b.texture||a.texcoord!=b.texcoord||a.has_transform!=b.has_transform) return false;
            if(!a.has_transform) return true;
            const auto &x=a.transform,&y=b.transform;
            return x.offset[0]==y.offset[0]&&x.offset[1]==y.offset[1]&&x.rotation==y.rotation&&x.scale[0]==y.scale[0]&&
                   x.scale[1]==y.scale[1]&&x.has_texcoord==y.has_texcoord&&x.texcoord==y.texcoord;
          };
          const cgltf_texture_view *views[]{&coat.clearcoat_texture,&coat.clearcoat_roughness_texture,&coat.clearcoat_normal_texture};
          const cgltf_texture_view *distinct[3]{};unsigned distinct_count=0;
          for(const auto *view:views) {
            if(!view->texture) continue;
            bool known=false;
            for(unsigned k=0;k<distinct_count;++k) known=known||same_view(*distinct[k],*view);
            if(!known) distinct[distinct_count++]=view;
          }
          const char *conflict=nullptr;
          if(distinct_count&&src.has_transmission&&src.transmission.transmission_factor>0) conflict="glTF clearcoat maps combined with transmission";
          else if(distinct_count>2) conflict="three distinct glTF clearcoat textures";
          if(conflict) {
            if(require_coat) unsupported(conflict);
            NJIN_WARN("njin glTF material: %s are unsupported on bgfx; clearcoat maps skipped, using scalar factors",conflict);
          } else if(distinct_count) {
            RenderTextureMap *targets[]{&data.maps.clearcoat,&data.maps.clearcoat_roughness,&data.maps.clearcoat_normal};
            const char *roles[]{"clearcoat","clearcoat roughness","clearcoat normal"};
            for(unsigned k=0;k<3;++k) {
              if(!views[k]->texture) continue;
              for(unsigned j=0;j<k&&!targets[k]->texture;++j) if(views[j]->texture&&same_view(*views[j],*views[k])) *targets[k]=*targets[j];
              if(!targets[k]->texture) *targets[k]=texture_map(*views[k],false,roles[k]);
            }
            data.maps.clearcoat_normal_scale=coat.clearcoat_normal_texture.scale;
            if(!std::isfinite(data.maps.clearcoat_normal_scale)) throw std::runtime_error("njin bgfx 3D: invalid glTF clearcoat normal scale");
          }
        }
        // Strength and rotation as the legacy loader (degrees); the direction comes
        // from the mesh tangents, so primitives without them drop it below.
        if(src.has_anisotropy) {
          if(src.anisotropy.anisotropy_texture.texture) {
            if(require_anisotropy) unsupported("required glTF anisotropy maps");
            NJIN_WARN("njin glTF material: anisotropy map skipped; using scalar strength/rotation");
          }
          surface.anisotropy=src.anisotropy.anisotropy_strength;
          surface.anisotropy_rotation=src.anisotropy.anisotropy_rotation*RAD2DEG;
        }
        portable_surface_validate(surface);
      }
      if(!src.unlit) {
        data.maps.normal=texture_map(src.normal_texture,false,"normal");
        data.maps.metal_rough=texture_map(pbr.metallic_roughness_texture,false,"metallic/roughness");
        data.maps.occlusion=texture_map(src.occlusion_texture,false,"occlusion");
        data.maps.emission=texture_map(src.emissive_texture,false,"emissive");
        data.maps.normal_scale=src.normal_texture.scale;
        data.maps.occlusion_strength=src.occlusion_texture.scale;
        m.surface.occlusion_strength=src.occlusion_texture.scale;
        const float strength=src.has_emissive_strength?src.emissive_strength.emissive_strength:1;
        data.maps.emission_factor={src.emissive_factor[0],src.emissive_factor[1],src.emissive_factor[2],strength};
        data.maps.constant_emission=!data.maps.emission.texture && (src.emissive_factor[0]>0||src.emissive_factor[1]>0||src.emissive_factor[2]>0);
        if(!std::isfinite(data.maps.normal_scale)||!std::isfinite(data.maps.occlusion_strength)||data.maps.occlusion_strength<0||data.maps.occlusion_strength>1)
          throw std::runtime_error("njin bgfx 3D: invalid glTF normal/AO scale");
        for(float v:data.maps.emission_factor) if(!std::isfinite(v)||v<0) throw std::runtime_error("njin bgfx 3D: invalid glTF emissive factor");
      }
      materials.push_back(m); names.emplace_back(src.name?src.name:"");
    }
    std::vector<CpuMesh> meshes;
    std::vector<PortablePrimitive> animation_primitives;
    std::vector<const cgltf_node*> pending;
    const auto *scene=raw->scene?raw->scene:(raw->scenes_count?raw->scenes:nullptr);
    if(scene) pending.assign(scene->nodes,scene->nodes+scene->nodes_count);
    else for(size_t i=0;i<raw->nodes_count;++i) if(!raw->nodes[i].parent) pending.push_back(&raw->nodes[i]);
    std::unordered_set<const cgltf_node*> visited;
    size_t vertices_total=0;
    while(!pending.empty()) {
      const auto *node=pending.back(); pending.pop_back();
      if(!visited.insert(node).second) throw std::runtime_error("njin bgfx 3D: cyclic/shared glTF scene node");
      for(size_t i=0;i<node->children_count;++i) pending.push_back(node->children[i]);
      if(!node->mesh || name_matches(node->name,desc.skip_nodes,desc.skip_count) ||
         (desc.only_count&&!name_matches(node->name,desc.only_nodes,desc.only_count))) continue;
      size_t ancestry=0;
      for(auto *p=node;p;p=p->parent) if(++ancestry>raw->nodes_count)
        throw std::runtime_error("njin bgfx 3D: cyclic glTF parent chain");
      float f[16]; cgltf_node_transform_world(node,f);
      for(float x:f) if(!std::isfinite(x)) throw std::runtime_error("njin bgfx 3D: invalid glTF node transform");
      const Matrix transform{f[0],f[4],f[8],f[12],f[1],f[5],f[9],f[13],f[2],f[6],f[10],f[14],f[3],f[7],f[11],f[15]};
      for(size_t pi=0;pi<node->mesh->primitives_count;++pi) {
        const auto &primitive=node->mesh->primitives[pi];
        if(primitive.type!=cgltf_primitive_type_triangles||primitive.has_draco_mesh_compression)
          unsupported("non-triangle/compressed glTF primitives");
        const auto *position=cgltf_find_accessor(&primitive,cgltf_attribute_type_position,0);
        if(!position || !position->count || position->count>65535 || vertices_total+position->count>max_elements)
          throw std::runtime_error("njin bgfx 3D: invalid/oversized glTF mesh (split into <=65535 vertices)");
        const auto count=position->count; vertices_total+=count;
        const auto positions=unpack(position,3,count);
        const auto *normal=cgltf_find_accessor(&primitive,cgltf_attribute_type_normal,0);
        const auto normals=unpack(normal,3,count);
        const auto uvs=unpack(cgltf_find_accessor(&primitive,cgltf_attribute_type_texcoord,0),2,count);
        const auto uv1=unpack(cgltf_find_accessor(&primitive,cgltf_attribute_type_texcoord,1),2,count);
        const auto tangents=unpack(cgltf_find_accessor(&primitive,cgltf_attribute_type_tangent,0),4,count);
        const auto material_id=primitive.material?size_t(primitive.material-raw->materials)+1:0;
        const auto &data=albedos.at(material_id);
        for(const auto *map:{&data.albedo,&data.maps.normal,&data.maps.metal_rough,&data.maps.occlusion,&data.maps.emission,
                            &data.maps.transmission,&data.maps.thickness,&data.maps.clearcoat,&data.maps.clearcoat_roughness,&data.maps.clearcoat_normal})
          if(map->texture && (map->uv.set?uv1:uvs).empty()) throw std::runtime_error("njin bgfx 3D: glTF material UV set is missing");
        const auto *color=cgltf_find_accessor(&primitive,cgltf_attribute_type_color,0);
        const size_t color_components=color?cgltf_num_components(color->type):0;
        if(color && color_components!=3 && color_components!=4) throw std::runtime_error("njin bgfx 3D: invalid glTF COLOR_0");
        const auto colors=unpack(color,color_components,count);
        CpuMesh mesh; mesh.material=primitive.material?int(primitive.material-raw->materials)+1:0;
        for(size_t i=0;i<count;++i) {
          const auto p=Vector3Transform({positions[i*3],positions[i*3+1],positions[i*3+2]},transform);
          float u=uvs.empty()?0:uvs[i*2],v=uvs.empty()?0:uvs[i*2+1];
          RenderVertex vertex{p.x,p.y,p.z,u,v};
          if(!uv1.empty()) vertex.uv1={uv1[i*2],uv1[i*2+1]};
          if(!tangents.empty()) {
            auto linear=transform; linear.m12=linear.m13=linear.m14=0;
            const auto t=Vector3Transform({tangents[i*4],tangents[i*4+1],tangents[i*4+2]},linear);
            if(std::abs(std::abs(tangents[i*4+3])-1)>0.001f) throw std::runtime_error("njin bgfx 3D: invalid tangent handedness");
            vertex.tangent={t.x,t.y,t.z,tangents[i*4+3]*(MatrixDeterminant(transform)<0?-1.0f:1.0f)};
          }
          if(color) for(size_t c=0;c<color_components;++c) {
            const float value=colors[i*color_components+c];
            vertex.color[c]=desc.pbr&&(!primitive.material||!primitive.material->unlit)&&c<3?display_color(value):value;
          }
          mesh.vertices.push_back(vertex);
          if(!normals.empty()) {
            if(std::abs(MatrixDeterminant(transform))<1e-12f) unsupported("singular glTF normal transform");
            auto nm=MatrixTranspose(MatrixInvert(transform)); nm.m12=nm.m13=nm.m14=0;
            const auto n=Vector3Transform({normals[i*3],normals[i*3+1],normals[i*3+2]},nm);
            mesh.normals.push_back({n.x,n.y,n.z});
          }
        }
        const size_t index_count=primitive.indices?primitive.indices->count:count;
        if(!index_count||index_count%3||index_count>max_elements || (primitive.indices&&primitive.indices->is_sparse))
          throw std::runtime_error("njin bgfx 3D: invalid/sparse glTF triangle indices");
        for(size_t i=0;i<index_count;++i) {
          const size_t index=primitive.indices?cgltf_accessor_read_index(primitive.indices,i):i;
          if(index>=count) throw std::runtime_error("njin bgfx 3D: glTF index out of range");
          mesh.indices.push_back((uint16_t)index);
        }
        if(MatrixDeterminant(transform)<0) for(size_t i=0;i<mesh.indices.size();i+=3) std::swap(mesh.indices[i+1],mesh.indices[i+2]);
        meshes.push_back(std::move(mesh));animation_primitives.push_back({node,&primitive});
      }
    }
    if(meshes.empty()) { if(!prepared) for(auto t:owned) renderer_destroy_texture(*store.portable_context,t); return {}; }
    for(const auto &mesh:meshes) {
      auto &surface=materials.at(size_t(mesh.material)).surface;
      if(surface.anisotropy==0||std::all_of(mesh.vertices.begin(),mesh.vertices.end(),[](const RenderVertex &v){return v.tangent[3]!=0;})) continue;
      if(require_anisotropy) unsupported("required glTF anisotropy on a primitive without TANGENT");
      NJIN_WARN("njin glTF material %d: anisotropy needs TANGENT on every primitive; dropped",mesh.material);
      surface.anisotropy=0;
    }
    auto animation=portable_animation_build(*raw,animation_primitives);
    if(desc.merge) {
      if(animation) unsupported("merging animated glTF meshes");
      std::vector<CpuMesh> merged;std::unordered_map<int,size_t> last;
      for(auto &mesh:meshes) {
        if(mesh.normals.empty()) {
          mesh.normals.resize(mesh.vertices.size());
          for(size_t j=0;j<mesh.indices.size();j+=3) {
            const auto a=mesh.indices[j],b=mesh.indices[j+1],c=mesh.indices[j+2];
            const auto point=[&](size_t i){const auto &v=mesh.vertices[i];return vec3{v.x,v.y,v.z};};const auto n=cross(point(b)-point(a),point(c)-point(a));
            mesh.normals[a]+=n;mesh.normals[b]+=n;mesh.normals[c]+=n;
          }
        }
        auto found=last.find(mesh.material);
        if(found==last.end()||merged[found->second].vertices.size()+mesh.vertices.size()>65535) {
          last[mesh.material]=merged.size();merged.push_back(std::move(mesh));continue;
        }
        auto &out=merged[found->second];const auto base=out.vertices.size();
        out.vertices.insert(out.vertices.end(),mesh.vertices.begin(),mesh.vertices.end());out.normals.insert(out.normals.end(),mesh.normals.begin(),mesh.normals.end());
        for(auto i:mesh.indices) out.indices.push_back(u16(base+i));
      }
      meshes=std::move(merged);
    }
    const auto result=commit(store,meshes,std::move(materials),std::move(names),std::move(albedos),std::move(owned),std::move(animation),prepared);
    if(prepared) prepared->slot.merged=desc.merge;else store.slots.at(result.id-1).merged=desc.merge;return result;
  } catch(...) {
    if(!prepared) for(auto t:owned) renderer_destroy_texture(*store.portable_context,t);
    throw;
  }
}

model_handle portable_model_load(model_store &store,const model_load_desc &desc) {return load_portable_model(store,desc);}
namespace {
struct LodPayload:StreamPayload {
  std::unique_ptr<PreparedModel> source;
  std::vector<std::vector<RenderGeometryData>> levels;
  std::vector<std::vector<RenderMeshHandle>> resident_meshes;
  std::vector<RenderMeshHandle> pending;
  u32 resident=~u32(0),target=0,working=~u32(0),handle=0;
  float screen;
  explicit LodPayload(std::unique_ptr<PreparedModel> model,const model_lod_desc &desc):source(std::move(model)),screen(desc.screen) {
    std::vector<std::vector<RenderGeometryData>> simplified;
    size_t count=0;
    for(size_t i=0;i<source->meshes.size();++i) {
      const auto &m=source->meshes[i];
      simplified.push_back(portable_lod_prepare({m.vertices,m.indices},source->slot.portable_animation?&source->slot.portable_animation->meshes.at(i):nullptr,desc));
      count=std::max(count,simplified.back().size());
    }
    levels.resize(count+1);
    for(size_t level=0;level<=count;++level) for(size_t i=0;i<source->meshes.size();++i) {
      const auto &m=source->meshes[i];
      if(level&&level<=simplified[i].size()) levels[level].push_back(std::move(simplified[i][level-1]));
      else levels[level].push_back({m.vertices,m.indices});
    }
    resident_meshes.resize(levels.size());
  }
  bool persistent() const override {return true;}
  usize cpu_bytes() const override {
    usize n=source->cpu_bytes();for(const auto &level:levels) for(const auto &m:level) n+=m.vertices.size()*sizeof(RenderVertex)+m.indices.size()*2;return n;
  }
  usize gpu_bytes() const override {return source->gpu_bytes()-mesh_bytes(source->meshes)+level_bytes(levels.size()-1);}
  static usize mesh_bytes(const std::vector<CpuMesh> &meshes) {usize n=0;for(const auto &m:meshes) n+=m.vertices.size()*sizeof(RenderVertex)+m.indices.size()*2;return n;}
  usize level_bytes(size_t i) const {usize n=0;for(const auto &m:levels[i]) n+=m.vertices.size()*sizeof(RenderVertex)+m.indices.size()*2;return n;}
  bool images_pending() const {return source->cursor<source->images.size();}
  bool dropping() const {return resident!=~u32(0)&&resident<target&&working==~u32(0);}
  u32 next_level() const {return working!=~u32(0)?working:resident==~u32(0)?u32(levels.size()-1):resident-1;}
  usize next_bytes() const override {
    if(images_pending()) return source->next_bytes();
    if(dropping()) return 0;
    const auto &m=levels[next_level()].at(pending.size());return m.vertices.size()*sizeof(RenderVertex)+m.indices.size()*2;
  }
  usize resident_after_upload(usize previous) const override {
    if(!dropping()) return previous+next_bytes();
    usize freed=0;for(u32 i=0;i<target;++i) if(!resident_meshes[i].empty()) freed+=level_bytes(i);return previous-freed;
  }
  void detail(u32 level) override {target=std::min(level,u32(levels.size()-1));}
  bool coarsen() override {if(resident>=levels.size()-1||working!=~u32(0)) return false;target=resident+1;return true;}
  u32 resident_level() const override {return resident;}
  u32 target_level() const override {return target;}
  bool done() const override {return !images_pending()&&working==~u32(0)&&resident==target;}
  void upload(context &ctx) override {
    if(images_pending()) {source->upload(ctx);return;}
    if(dropping()) {
      auto &slot=ctx_ref<model_store>(ctx)->slots.at(handle-1);
      for(u32 i=0;i<target;++i) if(!resident_meshes[i].empty()) {
        for(auto m:resident_meshes[i]) renderer_destroy_mesh(ctx,m);resident_meshes[i].clear();
        if(i==0) std::fill(slot.portable_meshes.begin(),slot.portable_meshes.end(),RenderMeshHandle{});
        else std::fill(slot.portable_lods[i-1].begin(),slot.portable_lods[i-1].end(),RenderMeshHandle{});
      }
      resident=target;return;
    }
    working=next_level();const auto &m=levels[working].at(pending.size());
    auto gpu=renderer_create_mesh(ctx,m.vertices,m.indices);pending.push_back(gpu);
    ctx.window.graphics->ensure_mesh(gpu,m.vertices,m.indices);
  }
  bool publish_step(context &ctx,u32 id) override {
    if(working==~u32(0)) return handle!=0;
    if(pending.size()!=levels[working].size()) return false;
    if(!handle) {
      source->slot.streaming_lods=true;source->slot.lod_screen=screen;
      source->slot.portable_lods.resize(levels.size()-1,std::vector<RenderMeshHandle>(pending.size()));
      source->gpu_meshes.resize(pending.size());
      if(working) source->slot.portable_lods[working-1]=pending;else source->gpu_meshes=pending;
      source->publish(ctx,id);handle=id;
    } else {
      auto &slot=ctx_ref<model_store>(ctx)->slots.at(handle-1);
      if(working) slot.portable_lods[working-1]=pending;else slot.portable_meshes=pending;
    }
    resident_meshes[working]=std::move(pending);pending.clear();resident=working;working=~u32(0);return true;
  }
  void publish(context &,u32) override {}
  void discard(context &ctx) override {source->discard(ctx);for(auto m:pending) renderer_destroy_mesh(ctx,m);pending.clear();}
};
}
stream_handle model_request_lods(context &ctx,const char *path,const model_lod_desc &desc,int priority,bool pbr) {
  if(!path||!*path) throw std::invalid_argument("njin streaming: empty LOD model path");
  const auto resolved=asset_path(path);
  const auto key="lod:"+resolved+":"+std::to_string(desc.levels)+":"+std::to_string(std::bit_cast<u32>(desc.ratio))+":"+std::to_string(std::bit_cast<u32>(desc.max_error))+":"+std::to_string(std::bit_cast<u32>(desc.screen))+(pbr?":pbr":":plain");
  return stream_request(ctx,key,priority,true,[resolved,pbr,desc] {
    auto data=std::make_unique<PreparedModel>();model_store temporary;
    if(!load_portable_model(temporary,{.path=resolved.c_str(),.pbr=pbr},data.get()).id) throw std::runtime_error("stream LOD model decode failed: "+resolved);
    return std::make_unique<LodPayload>(std::move(data),desc);
  },[](context &c,u32 previous) {
    const vec3 positions[]{{-.5f,-.5f,0},{.5f,-.5f,0},{0,.5f,0}};model_store &store=ctx_ref<model_store>(c);
    auto h=portable_model_create(store,{.positions=positions,.vertex_count=3});
    if(previous) {store.slots.at(previous-1)=std::move(store.slots.at(h.id-1));store.slots.at(h.id-1)={};return previous;}return h.id;
  },[](context &c,u32 id){portable_model_unload(ctx_ref<model_store>(c),{id});});
}
stream_handle model_request(context &ctx,const char *path,int priority,bool pbr) {
  if(!path||!*path) throw std::invalid_argument("njin streaming: empty model path");
  const auto resolved=asset_path(path);
  return stream_request(ctx,"model:"+resolved+(pbr?":pbr":":plain"),priority,true,[resolved,pbr] {
    auto data=std::make_unique<PreparedModel>();
    model_store temporary;
    if(!load_portable_model(temporary,{.path=resolved.c_str(),.pbr=pbr},data.get()).id) throw std::runtime_error("stream glTF decode failed: "+resolved);
    return data;
  },[](context &c,u32 previous) {
    const vec3 positions[]{{-.5f,-.5f,0},{.5f,-.5f,0},{0,.5f,0}};
    model_store &store=ctx_ref<model_store>(c);auto handle=portable_model_create(store,{.positions=positions,.vertex_count=3});
    if(previous) {store.slots.at(previous-1)=std::move(store.slots.at(handle.id-1));store.slots.at(handle.id-1)={};return previous;}
    return handle.id;
  },[](context &c,u32 id) {portable_model_unload(ctx_ref<model_store>(c),{id});});
}
model_handle streaming_model(context &ctx,stream_handle handle) {return {stream_resource(ctx,handle,true)};}

void portable_begin_3d(context &ctx,const camera3d &camera) {
  model_store &store=ctx_ref<model_store>(ctx);
  if(store.portable_pass) throw std::logic_error("njin bgfx 3D: nested begin_3d");
  store.portable_atmosphere_drawn=false;
  if(store.portable_atmosphere) {store.portable_atmosphere->view_environment=false;store.portable_atmosphere->view_aerial=false;}
  // A second view may resize HDR while the first view's resolve still samples it.
  auto &device=*ctx_ref<window_guard>(ctx).graphics;
  std::erase_if(store.portable_hdr_retired,[&](RenderTargetHandle h) {
    if(device.target_in_use(h)) return false;
    renderer_destroy_target(ctx,h);return true;
  });
  if(!store.portable_light.hdr||!store.portable_light.auto_exposure) store.portable_exposure_valid=false;
  if(!ctx_ref<window_guard>(ctx).graphics->frame_open() || !ctx_ref<view_state>(ctx)->world_depth)
    throw std::logic_error("njin bgfx 3D: begin_3d requires phase_render");
  if(!finite(camera.position)||!finite(camera.target)||!finite(camera.up)||
     length(camera.target-camera.position)<0.00001f||length(cross(camera.target-camera.position,camera.up))<0.00001f ||
     !std::isfinite(camera.near_plane)||!std::isfinite(camera.far_plane)||camera.near_plane<=0||camera.far_plane<=camera.near_plane)
    throw std::invalid_argument("njin bgfx 3D: invalid camera");
  if(camera.projection==camera3d_orthographic&&(store.portable_post.shafts>0||store.portable_post.flare>0)) unsupported("sun post effects with an orthographic camera");
  auto size=window_size(ctx);
  const auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
  if(resources.active_target) { const auto &t=resources.targets.at(resources.active_target.id); size={(float)t.width,(float)t.height}; }
  if(size.x<=0||size.y<=0) throw std::runtime_error("njin bgfx 3D: empty viewport");
  Matrix projection;
  if(camera.projection==camera3d_perspective) {
    if(!std::isfinite(camera.fovy)||camera.fovy<=0||camera.fovy>=180) throw std::invalid_argument("njin bgfx 3D: invalid perspective FOV");
    projection=MatrixPerspective(camera.fovy*PI/180,size.x/size.y,camera.near_plane,camera.far_plane);
  } else if(camera.projection==camera3d_orthographic) {
    if(!std::isfinite(camera.ortho_height)||camera.ortho_height<=0) throw std::invalid_argument("njin bgfx 3D: invalid orthographic height");
    const float h=camera.ortho_height/2,w=h*size.x/size.y;
    projection=MatrixOrtho(-w,w,-h,h,camera.near_plane,camera.far_plane);
  } else throw std::invalid_argument("njin bgfx 3D: invalid projection");
  const auto draw_camera=resources.active_target?camera:render3d_shaken_camera(ctx,camera);
  const Matrix view=MatrixLookAt({draw_camera.position.x,draw_camera.position.y,draw_camera.position.z},
    {draw_camera.target.x,draw_camera.target.y,draw_camera.target.z},{draw_camera.up.x,draw_camera.up.y,draw_camera.up.z});
  const bool intermediate=store.portable_light.hdr || store.portable_light.volume_density>0 || store.portable_post.aa!=aa3d_none || store.portable_post.ssao>0 || store.portable_post.ssr>0 || store.portable_post.outline>0 || store.portable_post.shafts>0 || store.portable_post.flare>0 || store.portable_post.oit || store.portable_post.taa || store.portable_post.motion_blur>0;
  const bool virtual_target=ctx_ref<view_state>(ctx)->drawing&&resources.active_target.id==ctx_ref<view_state>(ctx)->portable_target.id;
  if(resources.active_target&&!virtual_target&&(store.portable_post.taa||store.portable_post.motion_blur>0))
    unsupported("temporal post effects into an explicit target require independent history");
  store.portable_destination=resources.active_target;
  store.portable_camera=draw_camera;
  if(!resources.active_target||virtual_target) portable_temporal_begin(ctx,store,projection,view,unsigned(size.x),unsigned(size.y));
  else store.portable_temporal.active=false;
  store.portable_view_projection=MatrixMultiply(view,projection);store.portable_projection=projection;store.portable_view=view;
  portable_environment_update(ctx,store);
  store.portable_hdr_active=false;
  if(intermediate) {
    const unsigned w=(unsigned)size.x,h=(unsigned)size.y;
    if(!store.portable_hdr || w!=store.portable_hdr_width || h!=store.portable_hdr_height) {
      auto target=renderer_create_hdr_target(ctx,w,h);
      try { if(store.portable_hdr) {
        if(device.target_in_use(store.portable_hdr)) store.portable_hdr_retired.push_back(store.portable_hdr);
        else renderer_destroy_target(ctx,store.portable_hdr);
      } }
      catch(...) { renderer_destroy_target(ctx,target); throw; }
      store.portable_hdr=target; store.portable_hdr_width=w; store.portable_hdr_height=h;
    }
    const auto c=ctx_ref<config>(ctx).clear_bg_color;
    const auto linear=[](float x){return x<=0.04045f?x/12.92f:std::pow((x+0.055f)/1.055f,2.4f);};
    // As legacy's hdr_begin: the HDR image starts from what the target already holds
    // (the 2D drawn before begin_3d), in linear light, not from the clear colour. With
    // nothing drawn into it yet, clearing to the clear colour is the same image.
    const bool seed=ctx_ref<window_guard>(ctx).renderer_resources.pass_dirty;
    if(seed && (!store.portable_hdr_seed || store.portable_hdr_seed_size!=std::array<unsigned,2>{w,h})) {
      if(store.portable_hdr_seed) {
        if(device.target_in_use(store.portable_hdr_seed)) store.portable_hdr_retired.push_back(store.portable_hdr_seed);
        else renderer_destroy_target(ctx,store.portable_hdr_seed);
      }
      store.portable_hdr_seed=renderer_create_target(ctx,w,h);store.portable_hdr_seed_size={w,h};
    }
    if(seed) {
      device.copy_frame(store.portable_hdr_seed);
      ctx_ref<window_guard>(ctx).renderer_resources.targets.at(store.portable_hdr_seed.id).initialized=true;
    }
    if(store.portable_destination) renderer_end_target(ctx);
    try {renderer_begin_target(ctx,store.portable_hdr,{linear(c.r),linear(c.g),linear(c.b),c.a});}
    catch(...) {if(store.portable_destination) renderer_resume_target(ctx,store.portable_destination);throw;}
    if(seed) {
      // Clip-space quad, top of the image at the top of the target (+y up in clip space).
      const RenderVertex seed[]{{-1,1,0,0,0},{1,1,0,1,0},{1,-1,0,1,1},{-1,1,0,0,0},{1,-1,0,1,1},{-1,-1,0,0,1}};
      MeshDraw copy{};copy.vertices=seed;copy.texture=renderer_target_texture(ctx,store.portable_hdr_seed);
      copy.depth_test=false;copy.blend=RenderBlend::Opaque;copy.linear_output=true;copy.wrap_s=copy.wrap_t=RenderWrap::Clamp;
      renderer_draw_mesh(ctx,copy);
    }
    store.portable_hdr_active=true;
  }
  store.portable_target=resources.active_target;
  store.portable_queue.clear(); store.portable_lights.clear(); store.portable_rt_lod.clear();
  store.portable_frame_light=store.portable_light;store.portable_frame_post=store.portable_post;
  if(!store.portable_temporal.first) {store.portable_frame_post.taa=false;store.portable_frame_post.motion_blur=0;}
  store.portable_material={}; store.portable_fx={}; store.portable_pass=true;
  ctx_ref<window_guard>(ctx).renderer_resources.world3d_open=true;
  audio3d_note_camera(ctx_ref<audio_store>(ctx),camera.position,camera.target,camera.up);
}
namespace {
unsigned model_lod_level(const context &ctx,const model_store &store,const model_slot &slot,vec3 center,float radius) {
  const auto &camera=store.portable_camera;
  const float distance=length(center-camera.position);
  const float share=camera.projection==camera3d_orthographic?radius/std::max(camera.ortho_height*.5f,.0001f):
    radius/std::max(distance*std::tan(camera.fovy*PI/360),.0001f);
  float below=slot.lod_screen*ctx_ref<quality_state>(ctx)->q.lod_scale;
  unsigned level=0;
  while(distance>radius&&level<slot.portable_lods.size()&&share<below) {++level;below*=.5f;}
  return level;
}
}
void portable_draw_model(const context &ctx,model_handle handle,const transform3d &transform,rgba tint,const model_pose *pose,
                         std::span<const model_recolor> recolors,int level_override,std::span<const LightmapBinding> lightmaps) {
  const model_store &store=ctx_ref<model_store>(ctx); require_pass(store);
  const auto *slot=model_slot_of(store,handle); if(!slot) return;
  if(!lightmaps.empty()) {
    if(pose||slot->skinned||!slot->morph_names.empty()||model_clip_count(*slot)>0)
      throw std::invalid_argument("njin lightmap: animated/dynamic model cannot receive static lightmap");
    level_override=0;
  }
  const Matrix world=transform_matrix(transform);
  unsigned level=level_override>=0?unsigned(level_override):0;
  if(level_override<0&&!slot->portable_lods.empty()) {
    const auto lo=slot->bounds.min,hi=slot->bounds.max;
    const auto center=Vector3Transform(Vector3Scale(Vector3Add(lo,hi),.5f),world);
    const float radius=Vector3Length(Vector3Subtract(hi,lo))*.5f*std::max({std::abs(transform.scale.x),std::abs(transform.scale.y),std::abs(transform.scale.z)});
    level=model_lod_level(ctx,store,*slot,{center.x,center.y,center.z},radius);
  }
  const auto motion_key=portable_temporal_key(const_cast<model_store&>(store),handle.id);
  for(int i=0;i<slot->model.meshCount;++i) {
    const int material_index=slot->model.meshMaterial[i];
    const auto &material=slot->materials.at(material_index); portable_material_validate(material);
    model_slot::portable_material_data data;
    if(size_t(material_index)<slot->portable_materials.size()) data=slot->portable_materials[material_index];
    const auto override_map=[&](texture_handle handle,RenderTextureMap &map) {
      if(!handle.id) return;
      const auto *t=texture_slot_of(ctx_ref<texture_store>(ctx),handle);
      if(!t||!t->portable||t->packed) throw std::invalid_argument("njin bgfx 3D: invalid or atlas-packed material map");
      map.texture=t->portable; map.linear=t->filter!=filter_nearest;
    };
    override_map(material.albedo,data.albedo); override_map(material.normal,data.maps.normal);
    override_map(material.metal_rough,data.maps.metal_rough); override_map(material.occlusion,data.maps.occlusion);
    override_map(material.emission,data.maps.emission);
    data.maps.occlusion_strength=material.surface.occlusion_strength;
    const auto &em=material.emission_color;
    if(material.emission.id) { data.maps.emission_factor={1,1,1,1}; data.maps.constant_emission=false; }
    const float tint_em[]{linear_color(em.r),linear_color(em.g),linear_color(em.b),em.a};
    for(unsigned c=0;c<4;++c) data.maps.emission_factor[c]*=tint_em[c];
    auto own=material.color;
    for(const auto &recolor:recolors) if(recolor.material==material_index) {
      if(!std::isfinite(recolor.color.r)||!std::isfinite(recolor.color.g)||!std::isfinite(recolor.color.b))
        throw std::invalid_argument("njin model recolor: non-finite RGB color");
      own.r=recolor.color.r;own.g=recolor.color.g;own.b=recolor.color.b;break;
    }
    rgba color{tint.r*own.r,tint.g*own.g,tint.b*own.b,tint.a*own.a};
    if(!material.surface.unlit && (material.surface.pbr||store.portable_hdr_active||store.portable_frame_light.tonemap!=tonemap3d_none)) {
      color.r=display_color(linear_color(tint.r)*linear_color(own.r));
      color.g=display_color(linear_color(tint.g)*linear_color(own.g));
      color.b=display_color(linear_color(tint.b)*linear_color(own.b));
    }
    const auto mesh=portable_model_lod_mesh(*slot,unsigned(i),level);
    const auto &geometry=ctx_ref<window_guard>(ctx).renderer_resources.meshes.at(mesh.id);
    if(material.surface.anisotropy!=0 && std::none_of(geometry.cpu.begin(),geometry.cpu.end(),[](const RenderVertex &v) {
      return v.tangent[0]!=0||v.tangent[1]!=0||v.tangent[2]!=0;
    })) unsupported("anisotropy on a mesh without tangents");
    const auto animated=slot->portable_animation?portable_animation_geometry(*slot,unsigned(i),pose,geometry.indices):nullptr;
    const bool image_alpha=data.albedo.texture&&ctx_ref<window_guard>(ctx).renderer_resources.textures.at(data.albedo.texture.id).alpha;
    const bool blend=tint.a<1||(data.blend&&(color.a<1||image_alpha));
    std::vector<const LightmapBinding*> charts;
    if(lightmaps.empty()) charts.push_back(nullptr);
    else {
      for(const auto &binding:lightmaps) if(binding.mesh_index==unsigned(i)) charts.push_back(&binding);
      if(charts.empty()) throw std::invalid_argument("njin lightmap: missing chart binding for model mesh");
    }
    std::shared_ptr<RenderGeometryData> original;
    if(!lightmaps.empty()) {original=std::make_shared<RenderGeometryData>();original->vertices=geometry.cpu;original->indices=geometry.indices;}
    for(const auto *binding:charts) if(binding) {
      MeshDraw check;check.geometry=original;const auto values=MatrixToFloatV(world);
      std::copy(std::begin(values.v),std::end(values.v),check.world.begin());check.lighting.metallic=material.surface.metallic;
      lightmap_bind(check,*binding);
    }
    for(const auto *binding:charts) {
    submit(ctx,mesh,data.albedo.texture,data.albedo.linear,world,color,material.surface,
      material.double_sided?RenderCull::None:(MatrixDeterminant(world)<0?RenderCull::CounterClockwise:RenderCull::Clockwise),material.shadow_only,data.maps,data.albedo,blend,data.mask,material.shader);
    if(binding) {auto &draw=const_cast<model_store&>(store).portable_queue.back();draw.geometry=original;lightmap_bind(draw,*binding);
      const_cast<rhi::RendererResources&>(ctx_ref<window_guard>(ctx).renderer_resources).pending_3d_textures.insert(binding->map.texture.id);}
    if(animated) {auto &draw=const_cast<model_store&>(store).portable_queue.back();draw.mesh={};draw.geometry=animated;}
    // light3d::shadow_lod: ray traced shadows meet a coarser level of a static model.
    else if(const int lod=store.portable_frame_light.shadow_lod;lod>0&&store.portable_frame_light.ray_traced_shadows&&!slot->portable_lods.empty()) {
      const unsigned coarse=std::min(level+unsigned(std::min(lod,4)),unsigned(slot->portable_lods.size()));
      if(coarse!=level) const_cast<model_store&>(store).portable_rt_lod.push_back({store.portable_queue.size()-1,portable_model_lod_mesh(*slot,unsigned(i),coarse)});
    }
    portable_temporal_record(const_cast<model_store&>(store),const_cast<model_store&>(store).portable_queue.back(),motion_key?motion_key^(0x9e3779b97f4a7c15ULL*(i+1))^(binding?binding->map.texture.id:0):0,world);
    }
  }
}
void draw_lightmapped_model(const context &ctx,model_handle model,const transform3d &transform,std::span<const LightmapBinding> bindings) {
  if(bindings.empty()) throw std::invalid_argument("njin lightmap: empty model binding");
  portable_draw_model(ctx,model,transform,{1,1,1,1},nullptr,{},0,bindings);
}
void draw_lightmapped_model(const context &ctx,model_handle model,const transform3d &transform,lightmap_handle asset,uint64_t object) {
  draw_lightmapped_model(ctx,model,transform,lightmap_object_bindings(ctx,asset,object));
}
void portable_draw_primitive(const context &ctx,int kind,const transform3d &transform,rgba color,const material3d &surface) {
  portable_draw_primitive_matrix(ctx,kind,transform_matrix(transform),color,surface);
}
void portable_draw_primitive_matrix(const context &ctx,int kind,const Matrix &world,rgba color,const material3d &surface) {
  const model_store &store=ctx_ref<model_store>(ctx); require_pass(store); portable_surface_validate(surface);
  if(surface.anisotropy!=0) unsupported("anisotropy on a primitive without tangents");
  if(kind<0||kind>5) unsupported("this 3D primitive");
  auto &handle=store.portable_primitives[kind];
  if(!handle) {
    std::vector<RenderVertex> vertices;
    if(kind==0) {
      const float p[8][3]{{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{-.5f,.5f,-.5f},
        {-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}};
      const int faces[6][4]{{0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{3,2,6,7},{4,5,1,0}};
      const float uv[4][2]{{0,1},{1,1},{1,0},{0,0}};
      for(auto &face:faces) for(int corner:{0,1,2,0,2,3}) {
        const auto &v=p[face[corner]]; vertices.push_back({v[0],v[1],v[2],uv[corner][0],uv[corner][1]});
      }
    } else if(kind==2) vertices={{-.5f,0,-.5f,0,0},{.5f,0,-.5f,1,0},{.5f,0,.5f,1,1},
      {-.5f,0,-.5f,0,0},{.5f,0,.5f,1,1},{-.5f,0,.5f,0,1}};
    else if(kind==3||kind==5) {
      // Legacy GenMeshCylinder(1, 1, 48), or 8 sides for mesh3d_cylinder_low: radius 1,
      // y 0..1, smooth sides, flat caps.
      const int sides=kind==3?48:8;
      const auto rim=[sides](int i,float y) {
        const float a=float(i%sides)/sides*2*PI;
        return RenderVertex{std::sin(a),y,std::cos(a),float(i)/sides,1-y};
      };
      for(int i=0;i<sides;++i) {
        const RenderVertex a=rim(i,0),b=rim(i+1,0),c=rim(i+1,1),d=rim(i,1);
        for(const auto &v:{a,b,c,a,c,d}) {auto w=v;w.normal={v.x,0,v.z};vertices.push_back(w);}
        for(float y:{0.f,1.f}) {
          RenderVertex centre{0,y,0,.5f,.5f},p=rim(i,y),q=rim(i+1,y);
          for(auto *v:{&centre,&p,&q}) v->normal={0,y>0?1.f:-1.f,0};
          if(y>0) vertices.insert(vertices.end(),{centre,p,q}); else vertices.insert(vertices.end(),{centre,q,p});
        }
      }
    }
    else {
      // 16 x 24 bands; mesh3d_sphere_low is legacy's GenMeshSphere(1, 6, 8).
      const int rings=kind==4?6:16,slices=kind==4?8:24;
      const auto point=[rings,slices](int y,int x) {
        const float v=float(y)/rings,u=float(x)/slices,lat=v*PI,lon=u*2*PI;
        return RenderVertex{std::sin(lat)*std::cos(lon),std::cos(lat),std::sin(lat)*std::sin(lon),u,v};
      };
      for(int y=0;y<rings;++y) for(int x=0;x<slices;++x) {
        for(auto [dy,dx]:{std::pair{0,0},std::pair{1,0},std::pair{1,1},std::pair{0,0},std::pair{1,1},std::pair{0,1}})
          vertices.push_back(point(y+dy,x+dx));
      }
    }
    if(kind!=3&&kind!=5) for(size_t i=0;i<vertices.size();i+=3) {
      const auto point=[&](size_t j){auto &v=vertices[j];return vec3{v.x,v.y,v.z};};
      const auto n=cross(point(i+1)-point(i),point(i+2)-point(i));
      for(size_t j=i;j<i+3;++j) {
        auto normal=kind==1||kind==4?point(j):n*-1.0f;
        if(length(normal)>0) normal=normalize(normal);
        vertices[j].normal={normal.x,normal.y,normal.z};
      }
    }
    handle=renderer_create_mesh(const_cast<context&>(ctx),vertices);
  }
  RenderTextureHandle texture; bool linear=true;
  if(surface.texture.id) {
    const auto *t=texture_slot_of(ctx_ref<texture_store>(ctx),surface.texture);
    if(!t||!t->portable) throw std::invalid_argument("njin bgfx 3D: invalid primitive texture");
    texture=t->portable; linear=t->filter!=filter_nearest;
  }
  RenderMaterialMaps maps; maps.occlusion_strength=surface.occlusion_strength;
  const auto map=[&](texture_handle id,RenderTextureMap &out) {
    if(!id.id) return;
    const auto *t=texture_slot_of(ctx_ref<texture_store>(ctx),id);
    if(!t||!t->portable||t->packed) throw std::invalid_argument("njin bgfx 3D: invalid primitive map");
    out.texture=t->portable; out.linear=t->filter!=filter_nearest;
  };
  map(surface.normal,maps.normal); map(surface.metal_rough,maps.metal_rough); map(surface.occlusion,maps.occlusion);
  // A see-through closed primitive shows only its near side, as on OpenGL (raylib culls
  // back faces): its far side would otherwise blend over it in no particular order.
  // The primitives' triangles turn clockwise seen from outside.
  RenderCull cull=RenderCull::None;
  if(kind!=2&&color.a<1) cull=MatrixDeterminant(world)<0?RenderCull::Clockwise:RenderCull::CounterClockwise;
  submit(ctx,handle,texture,linear,world,color,surface,cull,false,maps);
  auto &mutable_store=const_cast<model_store&>(store);
  portable_temporal_record(mutable_store,mutable_store.portable_queue.back(),portable_temporal_key(mutable_store,0x100000000ULL+kind),world);
}
// The legacy public instance records are converted once on the CPU; each
// material and shadow view submits a real GPU instance buffer, never N draws.
void portable_draw_instances(const context &ctx,model_handle model,int primitive,instance_buffer_handle buffer,u32 first,u32 count,
                             shader_handle shader,const model_pose *pose,const instanced3d_desc *options) {
  model_store &store=ctx_ref<model_store>(const_cast<context&>(ctx));require_pass(store);
  if(shader.id||ctx_ref<shader_store>(ctx)->active.id) unsupported("custom instanced shaders");
  if(!ctx_ref<window_guard>(ctx).graphics->instancing_available()) throw std::runtime_error("njin bgfx: GPU instancing is unavailable");
  const auto *slot=instance_slot_of(ctx_ref<instance_store>(const_cast<context&>(ctx)),buffer);
  if(!slot||first>=slot->count) return;
  count=std::min(count,slot->count-first);if(!count) return;
  if(slot->cpu.size()!=size_t(slot->count)*slot->floats) throw std::logic_error("njin bgfx: instance CPU snapshot is missing");
  const auto *model_data=primitive<0?model_slot_of(store,model):nullptr;
  if(primitive<0&&!model_data) return;
  const bool cull=options&&options->cull&&(!model_data||!model_data->portable_animation||options->radius>0);
  const bool lod=options&&options->lod&&model_data&&!model_data->portable_lods.empty();
  // Keep the existing auxiliary-pass snapshots for OIT/SSR frames. Opaque
  // descriptor groups in a simple camera pass can select frustum/LOD on GPU.
  const bool gpu=cull&&render3d_options_get(ctx).occlusion&&!store.portable_frame_post.oit&&store.portable_frame_post.ssr==0;
  if(options&&(!std::isfinite(options->radius)||options->radius<0))
    throw std::invalid_argument("njin bgfx: invalid instance radius");
  // Bounding sphere round the origin: unit sphere 1, unit cube and plane sqrt(3)/2,
  // cylinders (radius 1, y 0..1) sqrt(2).
  float radius=primitive==1||primitive==4?1.f:primitive==3||primitive==5?1.4142136f:.8660254f;
  if(model_data) {
    const auto lo=model_data->bounds.min,hi=model_data->bounds.max;
    radius=length(vec3{std::max(std::abs(lo.x),std::abs(hi.x)),std::max(std::abs(lo.y),std::abs(hi.y)),std::max(std::abs(lo.z),std::abs(hi.z))});
  }
  if(options&&options->radius>0) radius=options->radius;
  std::array<vec4,6> planes{};
  if(cull) {
    const auto m=MatrixToFloatV(store.portable_view_projection);
    for(int i=0;i<6;++i) {
      const int axis=i/2;const float sign=i%2?-1.f:1.f;
      auto &p=planes[i];p={m.v[3]+sign*m.v[axis],m.v[7]+sign*m.v[4+axis],m.v[11]+sign*m.v[8+axis],m.v[15]+sign*m.v[12+axis]};
      const float normal=length(vec3{p.x,p.y,p.z});
      if(normal>0) p={p.x/normal,p.y/normal,p.z/normal,p.w/normal};
    }
  }
  using Groups=std::array<std::shared_ptr<std::vector<RenderInstance>>,4>;
  const unsigned levels=lod?unsigned(model_data->portable_lods.size())+1:1;
  std::vector<Groups> groups(gpu?1:levels+(cull?1:0));
  unsigned culled=0;
  for(u32 i=first;i<first+count;++i) {
    const auto *p=slot->cpu.data()+size_t(i)*slot->floats;const float uniform=p[3]==0?1:p[3];
    transform3d transform;transform.position={p[0],p[1],p[2]};transform.scale={uniform,uniform,uniform};
    if(slot->floats>=12) transform.rotation={p[8],p[9],p[10]};
    if(slot->floats>=16) transform.scale={uniform*(p[12]==0?1:p[12]),uniform*(p[13]==0?1:p[13]),uniform*(p[14]==0?1:p[14])};
    const auto matrix=transform_matrix(transform);const float determinant=MatrixDeterminant(matrix);
    if(!std::isfinite(determinant)||std::abs(determinant)<1e-12f) throw std::invalid_argument("njin bgfx: singular instance transform");
    RenderInstance instance;const auto values=MatrixToFloatV(matrix);std::copy_n(values.v,16,instance.transform.begin());
    if(slot->floats>=8) std::copy_n(p+4,4,instance.color.begin());
    for(unsigned c=0;c<4;++c) if(instance.color[c]<0||(c==3&&instance.color[c]>1)) throw std::invalid_argument("njin bgfx: instance RGB must be nonnegative and alpha in [0,1]");
    const float r=radius*std::max({std::abs(transform.scale.x),std::abs(transform.scale.y),std::abs(transform.scale.z)});
    const auto center=transform.position;
    const bool outside=cull&&!gpu&&std::any_of(planes.begin(),planes.end(),[&](vec4 p){return dot(vec3{p.x,p.y,p.z},center)+p.w < -r;});
    culled+=outside;
    const unsigned level=gpu?0:outside?levels:lod?model_lod_level(ctx,store,*model_data,center,r):0;
    auto &group=groups[level][(determinant<0?1:0)|(instance.color[3]<1?2:0)];
    if(!group) group=std::make_shared<std::vector<RenderInstance>>();
    group->push_back(instance);
  }
  for(unsigned level=0;level<(gpu?levels:unsigned(groups.size()));++level) {
    const auto &set=groups[gpu?0:level];
    if(std::none_of(set.begin(),set.end(),[](const auto &group){return bool(group);})) continue;
    const bool shadow_only=level==levels;
    const size_t begin=store.portable_queue.size();
    if(primitive>=0) portable_draw_primitive(ctx,primitive,{}, {1,1,1,1},store.portable_material);
    else portable_draw_model(ctx,model,{}, {1,1,1,1},pose,{},options?int(shadow_only?0:level):-1);
    const std::vector<MeshDraw> prepared(store.portable_queue.begin()+begin,store.portable_queue.end());
    store.portable_queue.resize(begin);
    for(unsigned sign=0;sign<4;++sign) if(set[sign]) for(const auto &packet:prepared) {
      auto draw=packet;draw.instances=set[sign];
      if(sign&2) {draw.transparent=true;draw.opaque_alpha=false;}
      if(shadow_only&&!draw.cast_shadow) continue;
      draw.shadow_only|=shadow_only;
      if(cull&&!draw.transparent&&!draw.shadow_only) draw.instance_occlusion_bounds={0,0,0,radius};
      if(gpu&&!draw.transparent&&!draw.shadow_only) {
        auto config=std::make_shared<RenderGpuCull>();config->bounds={0,0,0,radius};config->occlusion=true;config->select_lod=lod;
        const auto vp=MatrixToFloatV(store.portable_view_projection);std::copy_n(vp.v,16,config->view_projection.begin());
        const auto &camera=store.portable_camera;const bool ortho=camera.projection==camera3d_orthographic;
        config->camera={camera.position.x,camera.position.y,camera.position.z,ortho?camera.ortho_height*.5f:std::tan(camera.fovy*PI/360)};
        config->lod={model_data?model_data->lod_screen*ctx_ref<quality_state>(ctx)->q.lod_scale:0,float(level),float(levels-1),ortho?1.f:0.f};
        for(unsigned plane=0;plane<planes.size();++plane) {const auto p=planes[plane];config->camera_planes[plane]={p.x,p.y,p.z,p.w};}
        draw.gpu_cull=std::move(config);
        // Each shadow bucket selects the camera LOD on GPU; off-camera
        // instances enter only the base bucket, as in the existing CPU path.
      }
      if((sign&1)&&draw.cull!=RenderCull::None) draw.cull=draw.cull==RenderCull::Clockwise?RenderCull::CounterClockwise:RenderCull::Clockwise;
      store.portable_queue.push_back(std::move(draw));
    }
  }
  if(options) {
    render_stats &stats=ctx_ref<render_stats>(const_cast<context&>(ctx));
    stats.instances3d+=count-culled;stats.instances3d_culled+=culled;
  }
}
void portable_world_submit(const context &ctx,MeshDraw packet,const material3d &surface,rgba color,bool opaque,std::uint64_t motion_key) {
  if(surface.transmission>0||surface.world_uv>0||surface.height.id||surface.lightmap.id||surface.under.id||surface.under_normal.id)
    unsupported("advanced mesh material maps/transmission on built-in terrain/grass/water");
  if(surface.snow>0||surface.glitter>0) unsupported("mesh snow/glitter on built-in terrain/grass/water");
  model_store &store=ctx_ref<model_store>(const_cast<context&>(ctx));
  submit(ctx,packet.mesh,packet.texture,packet.linear_sampling,MatrixIdentity(),color,surface,packet.cull,packet.shadow_only,packet.maps,{},!opaque,false,{},opaque,true,std::move(packet.terrain_imprint),std::move(packet.terrain_relief));
  auto &draw=store.portable_queue.back();draw.geometry=std::move(packet.geometry);draw.instances=std::move(packet.instances);
  draw.world_style=packet.world_style;draw.blend=packet.blend;draw.depth_write=packet.depth_write;
  draw.water=std::move(packet.water);draw.sky=std::move(packet.sky);
  draw.color_uv=packet.color_uv;
  if(opaque) {draw.transparent=false;draw.opaque_alpha=packet.opaque_alpha;}
  portable_temporal_record(store,draw,motion_key,MatrixIdentity());
}
namespace {
template<class Distance>
void submit_sdf(const context &ctx,std::vector<float> key,vec3 half,float detail,float radius,Distance distance,
                const transform3d &transform,rgba color,const material3d &surface) {
  model_store &store=ctx_ref<model_store>(const_cast<context&>(ctx));require_pass(store);
  portable_surface_validate(surface);
  const float extent=length(half);
  if(!finite(half)||!std::isfinite(extent)||extent<1e-6f||half.x<=0||half.y<=0||half.z<=0)
    throw std::invalid_argument("njin SDF: invalid bounds");
  const Matrix world=transform_matrix(transform);
  auto &cache=store.portable_sdf_meshes;
  const auto found=std::find_if(cache.begin(),cache.end(),[&](const auto &item){return item.first==key;});
  std::shared_ptr<const RenderGeometryData> geometry;
  if(found!=cache.end()) {
    geometry=found->second;
    std::rotate(found,std::next(found),cache.end());
  } else {
    geometry=sdf_mesh::tessellate(half,detail,std::max(radius,.0001f),distance);
    if(geometry->vertices.empty()) throw std::runtime_error("njin SDF: surface is below the mesh sampling resolution");
    if(cache.size()==8) cache.erase(cache.begin());
    cache.emplace_back(std::move(key),geometry);
  }
  auto material=surface;
  material.clay=0;
  material.texture={};material.normal={};material.metal_rough={};material.occlusion={};
  material.height={};material.lightmap={};material.under={};material.under_normal={};
  material.under_amount=0;material.world_uv=0;
  submit(ctx,{},{},true,world,color,material,RenderCull::None,false,{},{},false,false,{},false,true);
  auto &draw=store.portable_queue.back();
  draw.geometry=std::move(geometry);
  draw.world_style[1]={surface.clay,surface.clay_detail,std::max(radius,.0001f),0};
  // Retessellating can change correspondence even if the vertex count matches.
  const auto motion=portable_temporal_key(store,0x534446ULL^reinterpret_cast<std::uintptr_t>(draw.geometry.get()));
  portable_temporal_record(store,draw,motion,world);
}
}
void portable_draw_shape(const context &ctx,const shape3d &sh,rgba color,const material3d &surface) {
  if(sh.rounding==0&&surface.clay==0&&sh.kind==shape3d_box) {
    portable_draw_primitive(ctx,0,{sh.position,sh.rotation,sh.size},color,surface);return;
  }
  if(surface.clay==0&&sh.kind==shape3d_sphere) {
    portable_draw_primitive(ctx,1,{sh.position,sh.rotation,{sh.radius,sh.radius,sh.radius}},color,surface);return;
  }
  if(sh.kind<shape3d_sphere||sh.kind>shape3d_torus||!std::isfinite(sh.rounding))
    throw std::invalid_argument("njin SDF: invalid shape");
  const float r=sh.radius,h=sh.height*.5f,t=sh.thickness;
  vec3 half{r,r,r};
  if(sh.kind==shape3d_box) half=sh.size*.5f;
  if(sh.kind==shape3d_capsule) half.y=std::max(h,r);
  if(sh.kind==shape3d_cylinder) half.y=h;
  if(sh.kind==shape3d_torus) half={r+t,t,r+t};
  if(sh.kind==shape3d_torus&&(!std::isfinite(r)||r<0)) throw std::invalid_argument("njin SDF: invalid torus radius");
  const float round=std::clamp(sh.rounding,0.f,std::max(0.f,std::min({half.x,half.y,half.z})));
  const auto distance=[&](vec3 p) {
    using sdf_mesh::positive;
    switch(sh.kind) {
    case shape3d_box: {
      const vec3 q{std::abs(p.x)-half.x+round,std::abs(p.y)-half.y+round,std::abs(p.z)-half.z+round};
      return length(vec3{positive(q.x),positive(q.y),positive(q.z)})+std::min(0.f,std::max({q.x,q.y,q.z}))-round;
    }
    case shape3d_capsule:
      p.y-=std::clamp(p.y,r-half.y,half.y-r);return length(p)-r;
    case shape3d_cylinder: {
      const float x=std::hypot(p.x,p.z)-r+round,y=std::abs(p.y)-h+round;
      return std::hypot(positive(x),positive(y))+std::min(0.f,std::max(x,y))-round;
    }
    case shape3d_torus: return std::hypot(std::hypot(p.x,p.z)-r,p.y)-t;
    default: return length(p)-r;
    }
  };
  const float detail=std::min({half.x,half.y,half.z,round>0?round:INFINITY});
  submit_sdf(ctx,{float(sh.kind),half.x,half.y,half.z,round},half,detail,
             length(half)*.2f,distance,{sh.position,sh.rotation},color,surface);
}
void portable_draw_sdf_blend(const context &ctx,const sdf_part *parts,u32 count,float blend,rgba color,const material3d &surface) {
  if(!std::isfinite(blend)) throw std::invalid_argument("njin SDF: non-finite blend");
  vec3 low{INFINITY,INFINITY,INFINITY},high{-INFINITY,-INFINITY,-INFINITY};
  float radius=0,detail=INFINITY,padding=0;
  std::vector<sdf_part> local(parts,parts+count);
  for(auto &part:local) {
    if(!finite(part.a)||!finite(part.b)||!std::isfinite(part.ra)||!std::isfinite(part.rb)||!std::isfinite(part.blend)||part.ra<0||part.rb<0)
      throw std::invalid_argument("njin SDF: invalid cone endpoints or radii");
    part.blend=part.blend<0?std::max(0.f,blend):part.blend;
    padding+=part.blend*.25f;
    for(const auto &[p,r]:{std::pair{part.a,part.ra},std::pair{part.b,part.rb}}) {
      low={std::min(low.x,p.x-r),std::min(low.y,p.y-r),std::min(low.z,p.z-r)};
      high={std::max(high.x,p.x+r),std::max(high.y,p.y+r),std::max(high.z,p.z+r)};
      radius=std::max(radius,r);if(r>0) detail=std::min(detail,r);
    }
  }
  if(radius<=0) throw std::invalid_argument("njin SDF: blended group requires a positive radius");
  const vec3 center=(low+high)*.5f,half=(high-low)*.5f+vec3{padding,padding,padding};
  std::vector<float> key{-1};
  for(auto &part:local) {
    part.a-=center;part.b-=center;
    key.insert(key.end(),{part.a.x,part.a.y,part.a.z,part.b.x,part.b.y,part.b.z,part.ra,part.rb,part.blend});
  }
  const auto distance=[&](vec3 p) {
    float result=INFINITY;
    for(const auto &part:local) {
      const float d=sdf_mesh::cone(p,part.a,part.b,part.ra,part.rb),k=part.blend;
      const float h=k>0?std::max(0.f,1-std::abs(result-d)/k):0;
      result=std::min(result,d)-h*h*k*.25f;
    }
    return result;
  };
  submit_sdf(ctx,std::move(key),half,detail,radius,distance,{center},color,surface);
}
void portable_end_3d(context &ctx) {
  model_store &store=ctx_ref<model_store>(ctx); if(!store.portable_pass) return;
  try {
    if(ctx_ref<window_guard>(ctx).renderer_resources.active_target.id!=store.portable_target.id)
      throw std::logic_error("njin bgfx 3D: render target changed inside an open 3D pass");
    if(store.portable_camera.entities) {
      for(auto [e,t,l]:world(ctx).view<const transform3d,const light3d_source>().each()) {
        auto source=l; source.position=t.position; portable_light_add(ctx,source);
      }
      for(auto [e,t,m]:world(ctx).view<const transform3d,const model3d>().each()) if(m.visible) {

        store.portable_fx=m.fx;
        store.portable_temporal.next_id=std::uint64_t(entt::to_integral(e))+1;store.portable_temporal.next_namespace=1;
        if(const auto *baked=world(ctx).try_get<lightmap3d>(e))
          portable_draw_model(ctx,m.model,t,m.tint,nullptr,{},0,lightmap_object_bindings(ctx,baked->asset,baked->object));
        else portable_draw_model(ctx,m.model,t,m.tint,&m.pose);
      }
      for(auto [e,t,s]:world(ctx).view<const transform3d,const shape3d_render>().each()) if(s.visible) {
        store.portable_fx=s.fx;
        store.portable_temporal.next_id=std::uint64_t(entt::to_integral(e))+1;store.portable_temporal.next_namespace=2;
        auto shape=s.shape; shape.position=t.position; shape.rotation=t.rotation;
        if(shape.kind==shape3d_box) shape.size={shape.size.x*t.scale.x,shape.size.y*t.scale.y,shape.size.z*t.scale.z};
        else if(t.scale.x!=t.scale.y||t.scale.x!=t.scale.z) unsupported("nonuniform SDF scaling");
        else {shape.radius*=t.scale.x;shape.height*=t.scale.x;shape.thickness*=t.scale.x;shape.rounding*=t.scale.x;}
        portable_draw_shape(ctx,shape,s.color,s.material);
      }
    }
    std::array<RenderLocalLight,render_local_light_max> lights{};
    for(size_t i=0;i<std::min(store.portable_lights.size(),size_t(render_local_light_max));++i) {
      const auto &l=store.portable_lights[i]; auto &out=lights[i];
      out.position={l.position.x,l.position.y,l.position.z}; out.color={l.color.r,l.color.g,l.color.b};
      out.direction={l.direction.x,l.direction.y,l.direction.z}; out.radius=l.radius; out.intensity=l.intensity;
      out.spot=l.kind==light3d_spot;
      if(out.spot) { const float half=l.cone*PI/360.0f; out.cos_outer=std::cos(half); out.cos_inner=std::cos(half*(1-l.softness)); }
    }
    // 3D public API: the last uniform values before end_3d apply to the whole queue.
    for(auto &packet:store.portable_queue) if(packet.shader) {
      const auto program=packet.shader->program.id;shader_handle handle{};
      const auto &slots=ctx_ref<shader_store>(ctx)->slots;
      for(size_t i=0;i<slots.size();++i) if(slots[i].alive&&slots[i].portable.id==program) {handle.id=u32(i+1);break;}
      if(!handle.id) throw std::logic_error("njin shader: queued program was unloaded");
      packet.shader=portable_shader_bindings(ctx,handle);
    }
    if(auto target=store.portable_hdr_active?store.portable_hdr:store.portable_target;target&&ctx_ref<post_chain>(ctx)->settings.dof>0&&!ctx_ref<view_state>(ctx)->offscreen_depth) {
      auto &view=ctx_ref<view_state>(ctx);
      view->portable_depth=renderer_target_depth_texture(ctx,target);
      const auto inverse=MatrixToFloatV(MatrixInvert(store.portable_view_projection));
      std::copy_n(inverse.v,16,view->portable_depth_inverse.begin());
      view->portable_depth_planes={store.portable_camera.projection==camera3d_orthographic?-store.portable_camera.near_plane:store.portable_camera.near_plane,store.portable_camera.far_plane};
    }
    const auto shadows=portable_render_shadows(ctx,store);
    store.portable_shadows=shadows;store.portable_clusters=portable_light_clusters(ctx,store,shadows);
    portable_environment_sky(ctx,store);
    portable_sky_draw(ctx,store);
    const auto weather=portable_weather_cover(ctx,store);
    const bool ray_traced=portable_ray_shadows(ctx,store);
    portable_prepare_occlusion(ctx,store);
    for(auto &packet:store.portable_queue) {
      if(packet.shadow_only) continue;
      packet.shadows=shadows;
      packet.environment=store.portable_atmosphere_drawn&&store.portable_atmosphere&&store.portable_atmosphere->view_environment?
        store.portable_atmosphere->environment:store.portable_environment;
      const bool local_lights=!(packet.world_style[0][0]==3&&packet.world_style[2][2]==0);
      packet.clusters=local_lights?store.portable_clusters:nullptr;
      packet.local_lights=lights; packet.local_light_count=local_lights?unsigned(std::min(store.portable_lights.size(),size_t(render_local_light_max))):0;
      if(weather&&packet.world_style[0][0]==1&&packet.world_style[0][3]>0) packet.cover=weather;
      if(ray_traced&&!packet.transparent) packet.ray_shadow=true;
      if(!packet.transparent) renderer_draw_mesh(ctx,packet);
    }
    if(!store.portable_frame_post.oit) {
      // No post3d.oit: the transparent draws alpha blended, farthest first, over the opaque
      // ones without writing depth, as the OpenGL renderer draws them. Water's refraction,
      // reflection and caustics read OIT's scene copy, so they are left out here.
      thread_local std::vector<std::pair<float,size_t>> order;
      order.clear();
      const auto eye=store.portable_camera.position;
      for(size_t i=0;i<store.portable_queue.size();++i) {
        const auto &p=store.portable_queue[i];
        if(!p.transparent||p.shadow_only) continue;
        const float dx=p.world[12]-eye.x,dy=p.world[13]-eye.y,dz=p.world[14]-eye.z;
        order.push_back({dx*dx+dy*dy+dz*dz,i});
      }
      std::stable_sort(order.begin(),order.end(),[](const auto &a,const auto &b){return a.first>b.first;});
      for(const auto &[distance,i]:order) {
        auto draw=store.portable_queue[i];
        draw.blend=RenderBlend::Alpha;draw.depth_write=false;draw.opaque_alpha=false;
        if(draw.water) {
          auto water=std::make_shared<RenderWaterEffects>(*draw.water);
          water->parameters[0][2]=water->parameters[0][3]=water->parameters[1][3]=0;
          draw.water=std::move(water);
        }
        renderer_draw_mesh(ctx,draw);
      }
    }
    portable_precip_draw(ctx,store);
    portable_particles3d_draw(ctx);
    if(store.portable_hdr_active) {
      renderer_end_target(ctx); store.portable_hdr_active=false;
      portable_post_resolve(ctx,store);
    }
    if(ctx_ref<debug_state>(ctx)->running) {
      auto &snapshot=store.portable_debug;snapshot.time=elapsed(ctx);snapshot.camera=store.portable_camera;snapshot.sun=store.portable_frame_light.direction;
      snapshot.lights=store.portable_lights;snapshot.items.clear();snapshot.instanced=0;
      for(const auto &draw:store.portable_queue) {
        if(draw.instances) {snapshot.instanced+=unsigned(draw.instances->size());continue;}
        snapshot.items.push_back({debug3d_model,{draw.world[12],draw.world[13],draw.world[14]},{draw.tint[0],draw.tint[1],draw.tint[2],draw.tint[3]}});
      }
    }
    gizmo_draw_3d(ctx,store.portable_camera);
    portable_temporal_finish(store);
    store.portable_queue.clear();
    auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
    resources.pending_3d_meshes.clear();resources.pending_3d_textures.clear();
    ctx_ref<window_guard>(ctx).renderer_resources.world3d_open=false;
    store.portable_pass=false;
    if(store.portable_auto_end_target) {store.portable_auto_end_target=false;portable_render_texture_end(ctx);}
  } catch(...) {
    if(store.portable_hdr_active) { store.portable_hdr_active=false; try { renderer_end_target(ctx); } catch(...) {} }
    if(store.portable_destination&&!ctx_ref<window_guard>(ctx).renderer_resources.active_target) {
      try {renderer_resume_target(ctx,store.portable_destination);} catch(...) {}
    }
    ctx_ref<window_guard>(ctx).renderer_resources.world3d_open=false;
    store.portable_pass=false;store.portable_queue.clear();
    auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
    resources.pending_3d_meshes.clear();resources.pending_3d_textures.clear();
    if(store.portable_auto_end_target) {store.portable_auto_end_target=false;try {portable_render_texture_end(ctx);} catch(...) {}}
    throw;
  }
}
void portable_close_3d(context &ctx) {
  if(ctx_ref<model_store>(ctx)->portable_pass) {
    NJIN_WARN("3d: begin_3d without end_3d, closed at the end of phase_render"); portable_end_3d(ctx);
  }
}
}
