#include "njin_image_io.h"
#include "njin_renderer.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_material_uniforms.h"
#include "njin_image_decode.h"
#include "njin_raytrace.h"
#if NJIN_BGFX
#include <bimg/bimg.h>
#include <bx/allocator.h>
#endif
#include "njin_rl.h"
#include <stdexcept>
#include <limits>
#include <atomic>
#include <cstring>
#include <cmath>
namespace njin {
namespace {
std::uint64_t next_texture_id() {
  static std::atomic<std::uint64_t> next{1};
  const auto id=next.fetch_add(1);
  if(!id) throw std::overflow_error("njin renderer: texture identifiers exhausted");
  return id;
}
void validate_vertices(std::span<const RenderVertex> vertices) {
  for(const auto &v:vertices) {
    for(float uv:v.uv1) if(!std::isfinite(uv)) throw std::invalid_argument("njin renderer: invalid second UV set");
    for(float uv:v.lightmap_uv) if(!std::isfinite(uv)) throw std::invalid_argument("njin renderer: invalid lightmap UV set");
    for(float t:v.tangent) if(!std::isfinite(t)) throw std::invalid_argument("njin renderer: invalid tangent");
    for(float n:v.normal) if(!std::isfinite(n)) throw std::invalid_argument("njin renderer: non-finite normal");
    for(float c:v.color) if(!std::isfinite(c) || c<0 || c>1)
      throw std::invalid_argument("njin renderer: vertex colors must be finite and in [0,1]");
    if(!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || !std::isfinite(v.u) || !std::isfinite(v.v) || v.w!=1)
      throw std::invalid_argument("njin renderer: vertices require finite position/UV and w=1");
  }
}
}
RenderMeshHandle renderer_create_mesh(context &ctx,std::span<const RenderVertex> vertices,std::span<const std::uint16_t> indices) {
  if(vertices.empty() || indices.size()>10000000 || (indices.empty() ? vertices.size()%3 : indices.size()%3) || vertices.size()>10000000 ||
     (!indices.empty() && vertices.size()>65536))
    throw std::invalid_argument("njin renderer: invalid triangle-list mesh");
  for(auto index:indices) if(index>=vertices.size()) throw std::invalid_argument("njin renderer: mesh index out of range");
  validate_vertices(vertices);
  static std::atomic<std::uint64_t> next{1};
  const auto id=next.fetch_add(1);
  if(!id) throw std::overflow_error("njin renderer: mesh identifiers exhausted");
  auto &meshes=ctx_ref<window_guard>(ctx).renderer_resources.meshes;
  rhi::RendererResources::MeshResource resource;
  resource.cpu.assign(vertices.begin(),vertices.end());
  resource.indices.assign(indices.begin(),indices.end());
  meshes.emplace(id,std::move(resource));
  return {id};
}
void renderer_destroy_mesh(context &ctx,RenderMeshHandle handle) {
  auto &window=ctx_ref<window_guard>(ctx);
  const auto it=window.renderer_resources.meshes.find(handle.id);
  if(it==window.renderer_resources.meshes.end()) throw std::invalid_argument("njin renderer: invalid or foreign mesh handle");
  if(window.renderer_resources.pending_3d_meshes.contains(handle.id)) throw std::logic_error("njin renderer: mesh is referenced by a queued 3D draw");
  window.graphics->destroy_mesh(handle);
  window.renderer_resources.meshes.erase(it);
}
RendererResourceStats renderer_resource_stats(context &ctx) {
  auto &window=ctx_ref<window_guard>(ctx);
  const auto uploads=window.graphics->texture_uploads();const auto instances=window.graphics->instance_stats();
  const auto occlusion=window.graphics->occlusion_stats();
  return {(std::uint64_t)window.renderer_resources.textures.size(),uploads,
    (std::uint64_t)window.renderer_resources.meshes.size(),window.graphics->mesh_uploads(),
    (std::uint64_t)window.renderer_resources.targets.size(),instances[0],instances[1],instances[2],window.graphics->deformed_mesh_uploads(),
    window.renderer_resources.ray_traces,window.renderer_resources.ray_bvh_builds,window.graphics->ray_query_dispatches(),occlusion[0],occlusion[1],occlusion[2],occlusion[3],occlusion[4],occlusion[5]};
}
bool renderer_gpu_occlusion_available(context &ctx) {return ctx_ref<window_guard>(ctx).graphics->gpu_occlusion_available();}
std::string renderer_gpu_occlusion_diagnostics(context &ctx) {return ctx_ref<window_guard>(ctx).graphics->gpu_occlusion_diagnostics();}
std::array<std::uint64_t,2> renderer_occlusion_readback(context &ctx) {return ctx_ref<window_guard>(ctx).graphics->occlusion_readback();}
std::array<std::uint64_t,2> renderer_shadow_culling_readback(context &ctx) {return ctx_ref<window_guard>(ctx).graphics->occlusion_readback(true);}
namespace {
void validate_image(const RenderImage &im) {
  if(!im.width || !im.height || im.width>16384 || im.height>16384 ||
     (im.format!=RenderImageFormat::RGBA8 && im.format!=RenderImageFormat::RGBA16F && im.format!=RenderImageFormat::RGBA32F) ||
     (im.mips!=RenderMipMode::None&&
      !((im.format==RenderImageFormat::RGBA8&&unsigned(im.mips)<=unsigned(RenderMipMode::Normal))||
        (im.format==RenderImageFormat::RGBA32F&&im.mips==RenderMipMode::Linear))) ||
     im.rgba.size()!=size_t(im.width)*im.height*render_image_pixel_bytes(im.format))
    throw std::invalid_argument("njin renderer: invalid RGBA image");
}
}
RenderTextureHandle renderer_create_texture(context &ctx,const RenderImage &image) {
  validate_image(image);
  if(image.width>ctx_ref<window_guard>(ctx).graphics->max_texture_size()||image.height>ctx_ref<window_guard>(ctx).graphics->max_texture_size()) throw std::invalid_argument("njin renderer: image exceeds device limits");
  const auto id=next_texture_id();
  auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
  rhi::RendererResources::Texture slot;
  slot.cpu={image.width,image.height,{image.rgba.begin(),image.rgba.end()},image.format,image.mips};
  if(image.format==RenderImageFormat::RGBA8) {
    for(size_t i=3;i<image.rgba.size();i+=4) if(image.rgba[i]!=255) {slot.alpha=true;break;}
  } else if(image.format==RenderImageFormat::RGBA32F) {
    for(size_t i=12;i<image.rgba.size();i+=16) {float alpha;std::memcpy(&alpha,image.rgba.data()+i,4);if(alpha!=1) {slot.alpha=true;break;}}
  } else {
    for(size_t i=6;i<image.rgba.size();i+=8) {std::uint16_t alpha;std::memcpy(&alpha,image.rgba.data()+i,2);if(alpha!=0x3c00) {slot.alpha=true;break;}}
  }
  resources.textures.emplace(id,std::move(slot));
  return {id};
}
void renderer_destroy_texture(context &ctx,RenderTextureHandle handle) {
  auto &window=ctx_ref<window_guard>(ctx);
  const auto it=window.renderer_resources.textures.find(handle.id);
  if(it==window.renderer_resources.textures.end()) throw std::invalid_argument("njin renderer: invalid or foreign texture handle");
  if(it->second.target) throw std::logic_error("njin renderer: target owns its color texture; destroy the target instead");
  if(window.renderer_resources.pending_3d_textures.contains(handle.id)) throw std::logic_error("njin renderer: texture is referenced by a queued 3D draw");
  window.graphics->destroy_texture(handle);
  window.renderer_resources.textures.erase(it);
}
void renderer_update_texture(context &ctx,RenderTextureHandle handle,const RenderImage &image) {
  validate_image(image);
  auto &window=ctx_ref<window_guard>(ctx);auto &textures=window.renderer_resources.textures;
  if(image.width>window.graphics->max_texture_size()||image.height>window.graphics->max_texture_size()) throw std::invalid_argument("njin renderer: image exceeds device limits");
  if(window.renderer_resources.pending_3d_textures.contains(handle.id)) throw std::logic_error("njin renderer: texture update while referenced by queued 3D draws");
  auto it=textures.find(handle.id);
  if(it==textures.end()||it->second.target) throw std::invalid_argument("njin renderer: update requires an owned image texture");
  // Allocate CPU storage first, then replace the GPU resource atomically.
  RenderImageAsset next{image.width,image.height,{image.rgba.begin(),image.rgba.end()},image.format,image.mips};
  bool alpha=false;
  if(image.format==RenderImageFormat::RGBA8) {for(size_t i=3;i<next.pixels.size();i+=4) if(next.pixels[i]!=255) {alpha=true;break;}}
  else if(image.format==RenderImageFormat::RGBA32F) {for(size_t i=12;i<next.pixels.size();i+=16) {float a;std::memcpy(&a,next.pixels.data()+i,4);if(a!=1) {alpha=true;break;}}}
  else {for(size_t i=6;i<next.pixels.size();i+=8) {std::uint16_t a;std::memcpy(&a,next.pixels.data()+i,2);if(a!=0x3c00) {alpha=true;break;}}}
  window.graphics->replace_texture(handle,next.view());
  it->second.cpu=std::move(next);it->second.alpha=alpha;
}
static RenderTargetHandle create_target(context &ctx,unsigned width,unsigned height,bool hdr) {
  if(!width || !height || width>16384 || height>16384)
    throw std::invalid_argument("njin renderer: invalid target dimensions");
  auto &window=ctx_ref<window_guard>(ctx);
  auto &resources=window.renderer_resources;
  const auto id=next_texture_id();
  rhi::RendererResources::Texture texture;
  texture.cpu.width=width; texture.cpu.height=height; texture.cpu.format=hdr?RenderImageFormat::RGBA16F:RenderImageFormat::RGBA8;texture.target={id};
  resources.textures.emplace(id,std::move(texture));
  try {
    resources.targets.emplace(id,rhi::RendererResources::Target{width,height,{id},{}});
    if(hdr) window.graphics->create_hdr_target({id},{id},width,height);
    else window.graphics->create_target({id},{id},width,height);
  } catch(...) { resources.targets.erase(id); resources.textures.erase(id); throw; }
  return {id};
}
RenderTargetHandle renderer_create_target(context &ctx,unsigned w,unsigned h) { return create_target(ctx,w,h,false); }
RenderTargetHandle renderer_create_hdr_target(context &ctx,unsigned w,unsigned h) { return create_target(ctx,w,h,true); }
RenderTextureHandle renderer_target_texture(context &ctx,RenderTargetHandle handle) {
  const auto &targets=ctx_ref<window_guard>(ctx).renderer_resources.targets;
  const auto it=targets.find(handle.id);
  if(it==targets.end()) throw std::invalid_argument("njin renderer: invalid or foreign target handle");
  return it->second.color;
}
RenderImageAsset renderer_read_target(context &ctx,RenderTargetHandle handle) {
  auto &window=ctx_ref<window_guard>(ctx);auto &r=window.renderer_resources;
  const auto it=r.targets.find(handle.id);
  if(it==r.targets.end()||!it->second.initialized) throw std::invalid_argument("njin renderer: readback requires an initialized target");
  if(r.active_target.id==handle.id||r.world3d_open) throw std::logic_error("njin renderer: readback inside an active target or 3D pass");
  return window.graphics->read_target(handle);
}
RenderTextureHandle renderer_target_depth_texture(context &ctx,RenderTargetHandle handle) {
  auto &window=ctx_ref<window_guard>(ctx);auto &resources=window.renderer_resources;
  const auto it=resources.targets.find(handle.id);
  if(it==resources.targets.end()) throw std::invalid_argument("njin renderer: invalid or foreign depth target");
  auto &target=it->second;if(target.depth) return target.depth;
  const RenderTextureHandle depth{next_texture_id()};rhi::RendererResources::Texture texture;
  texture.cpu.width=target.width;texture.cpu.height=target.height;texture.target=handle;
  resources.textures.emplace(depth.id,std::move(texture));
  try {window.graphics->expose_depth(handle,depth);} catch(...) {resources.textures.erase(depth.id);throw;}
  target.depth=depth;return depth;
}
void renderer_destroy_target(context &ctx,RenderTargetHandle handle) {
  auto &window=ctx_ref<window_guard>(ctx);
  auto &resources=window.renderer_resources;
  const auto it=resources.targets.find(handle.id);
  if(it==resources.targets.end()) throw std::invalid_argument("njin renderer: invalid or foreign target handle");
  if(resources.active_target.id==handle.id) throw std::logic_error("njin renderer: target is active");
  if(resources.pending_3d_textures.contains(it->second.color.id)) throw std::logic_error("njin renderer: target is referenced by a queued 3D draw");
  if(it->second.depth && resources.pending_3d_textures.contains(it->second.depth.id)) throw std::logic_error("njin renderer: depth is referenced by a queued 3D draw");
  window.graphics->destroy_target(handle,it->second.color);
  if(it->second.depth) resources.textures.erase(it->second.depth.id);
  resources.textures.erase(it->second.color.id); resources.targets.erase(it);
}
void renderer_begin_target(context &ctx,RenderTargetHandle handle,std::array<float,4> clear) {
  auto &window=ctx_ref<window_guard>(ctx);
  auto &resources=window.renderer_resources;
  if(!resources.targets.contains(handle.id)) throw std::invalid_argument("njin renderer: invalid or foreign target handle");
  if(resources.active_target) throw std::logic_error("njin renderer: nested render targets are unsupported");
  for(float c:clear) if(!std::isfinite(c) || c<0 || c>1) throw std::invalid_argument("njin renderer: invalid target clear color");
  window.graphics->begin_target(handle,{clear[0],clear[1],clear[2],clear[3]});
  resources.frame_dirty=resources.pass_dirty;resources.active_target=handle;resources.pass_dirty=false;
}
void renderer_resume_target(context &ctx,RenderTargetHandle handle) {
  auto &window=ctx_ref<window_guard>(ctx); auto &resources=window.renderer_resources;
  const auto it=resources.targets.find(handle.id);
  if(it==resources.targets.end()) throw std::invalid_argument("njin renderer: invalid or foreign target handle");
  if(resources.active_target) throw std::logic_error("njin renderer: nested render targets are unsupported");
  if(!it->second.initialized) throw std::logic_error("njin renderer: cannot resume an uninitialized target");
  window.graphics->begin_target(handle,{},false); resources.frame_dirty=resources.pass_dirty;resources.active_target=handle;resources.pass_dirty=true;
}
void renderer_end_target(context &ctx) {
  auto &window=ctx_ref<window_guard>(ctx);
  if(!window.renderer_resources.active_target) throw std::logic_error("njin renderer: no active render target");
  window.graphics->end_target();
  window.renderer_resources.targets.at(window.renderer_resources.active_target.id).initialized=true;
  window.renderer_resources.active_target={};window.renderer_resources.pass_dirty=window.renderer_resources.frame_dirty;
}
bool image_decompress_rgba8(Image &image) {
  if(!image.data||image.format<PIXELFORMAT_COMPRESSED_DXT1_RGB) return false;
#if NJIN_BGFX
  bimg::TextureFormat::Enum format=bimg::TextureFormat::Unknown;
  switch(image.format) {
    case PIXELFORMAT_COMPRESSED_DXT1_RGB:case PIXELFORMAT_COMPRESSED_DXT1_RGBA:format=bimg::TextureFormat::BC1;break;
    case PIXELFORMAT_COMPRESSED_DXT3_RGBA:format=bimg::TextureFormat::BC2;break;
    case PIXELFORMAT_COMPRESSED_DXT5_RGBA:format=bimg::TextureFormat::BC3;break;
    case PIXELFORMAT_COMPRESSED_ETC1_RGB:format=bimg::TextureFormat::ETC1;break;
    case PIXELFORMAT_COMPRESSED_ETC2_RGB:format=bimg::TextureFormat::ETC2;break;
    case PIXELFORMAT_COMPRESSED_ETC2_EAC_RGBA:format=bimg::TextureFormat::ETC2A;break;
    case PIXELFORMAT_COMPRESSED_PVRT_RGB:format=bimg::TextureFormat::PTC14;break;
    case PIXELFORMAT_COMPRESSED_PVRT_RGBA:format=bimg::TextureFormat::PTC14A;break;
    case PIXELFORMAT_COMPRESSED_ASTC_4x4_RGBA:format=bimg::TextureFormat::ASTC4x4;break;
    case PIXELFORMAT_COMPRESSED_ASTC_8x8_RGBA:format=bimg::TextureFormat::ASTC8x8;break;
    default:break;
  }
  if(format==bimg::TextureFormat::Unknown) throw std::runtime_error("njin renderer: unsupported compressed image format");
  if(image.width<=0||image.height<=0||image.width>16384||image.height>16384)
    throw std::runtime_error("njin renderer: compressed image dimensions must be in [1,16384]");
  auto *pixels=static_cast<unsigned char*>(MemAlloc(unsigned(image.width)*unsigned(image.height)*4));
  if(!pixels) throw std::bad_alloc();
  bx::DefaultAllocator allocator;
  bimg::imageDecodeToRgba8(&allocator,pixels,image.data,uint32_t(image.width),uint32_t(image.height),uint32_t(image.width)*4,format);
  MemFree(image.data); // what raylib's UnloadImage does (RL_FREE)
  image={pixels,image.width,image.height,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
  return true;
#else
  return false;
#endif
}
bool renderer_compute_available(context &ctx) {
  auto &window=ctx_ref<window_guard>(ctx);
  return window.graphics&&window.graphics->compute_available();
}
bool renderer_ray_query_available(context &ctx) {
  auto &window=ctx_ref<window_guard>(ctx);
  return window.graphics&&window.graphics->ray_query_available();
}
std::array<unsigned,2> renderer_frame_view_size(context &ctx) {return ctx_ref<window_guard>(ctx).graphics->frame_view_size();}
void renderer_trace_shadows(context &ctx,const rhi::RayShadowPass &pass) {
  auto &window=ctx_ref<window_guard>(ctx);
  if(!window.graphics->frame_open()) throw std::logic_error("njin ray tracing: trace outside an active render frame");
  if(window.renderer_resources.active_target) throw std::logic_error("njin ray tracing: end the active target before tracing");
  if(!pass.width||!pass.height||pass.width>16384||pass.height>16384) throw std::invalid_argument("njin ray tracing: invalid view size");
  const auto it=window.renderer_resources.textures.find(pass.depth.id);
  if(it==window.renderer_resources.textures.end()||!it->second.target) throw std::invalid_argument("njin ray tracing: the depth input must be a render target texture");
  if(!window.renderer_resources.targets.at(it->second.target.id).initialized) throw std::logic_error("njin ray tracing: depth target is uninitialized");
  for(const auto *m:{&pass.view_projection,&pass.inverse_view_projection}) for(float v:*m) if(!std::isfinite(v)) throw std::invalid_argument("njin ray tracing: non-finite matrix");
  for(float v:pass.sun) if(!std::isfinite(v)) throw std::invalid_argument("njin ray tracing: non-finite sun");
  if(!std::isfinite(pass.bias)||!std::isfinite(pass.denoise)||!std::isfinite(pass.max_distance)||pass.max_distance<=0) throw std::invalid_argument("njin ray tracing: invalid bias/denoise/distance");
  // w carries integer bits (child references, triangle ids): only xyz are coordinates.
  if(pass.nodes.get()!=window.renderer_resources.ray_validated) {
    for(const auto &data:{pass.nodes,pass.triangles}) if(data) for(const auto &row:*data) for(int c=0;c<3;++c) if(!std::isfinite(row[c])) throw std::invalid_argument("njin ray tracing: non-finite scene data");
    window.renderer_resources.ray_validated=pass.nodes.get();
  }
  for(const auto &lamp:pass.lamps) for(float v:lamp) if(!std::isfinite(v)) throw std::invalid_argument("njin ray tracing: non-finite lamp");
  for(float v:pass.eye) if(!std::isfinite(v)) throw std::invalid_argument("njin ray tracing: non-finite eye");
  if(pass.lamp_count>3||!std::isfinite(pass.skip)||pass.skip<0) throw std::invalid_argument("njin ray tracing: invalid lamps/skip");
  if(!std::isfinite(pass.reflection_roughness)||pass.reflection_roughness<0||pass.reflection_roughness>1) throw std::invalid_argument("njin ray tracing: invalid reflection roughness");
  if(pass.reflection_roughness>0) {
    // Each triangle's draw index (b_rtTris row 1 w) must name a material row pair.
    if(!pass.materials) throw std::invalid_argument("njin ray tracing: reflections without materials");
    for(const auto &row:*pass.materials) for(float v:row) if(!std::isfinite(v)) throw std::invalid_argument("njin ray tracing: non-finite material");
    if(pass.triangles) for(size_t i=1;i<pass.triangles->size();i+=3) {
      std::uint32_t draw;std::memcpy(&draw,&(*pass.triangles)[i][3],sizeof draw);
      if(size_t(draw)*2+1>=pass.materials->size()) throw std::invalid_argument("njin ray tracing: triangle draw index outside the materials");
    }
  }
  for(const auto &row:pass.lights) for(float v:row) if(!std::isfinite(v)) throw std::invalid_argument("njin ray tracing: non-finite light");
  if(pass.grid) {
    for(const auto &row:pass.grid_rows) for(float v:row) if(!std::isfinite(v)) throw std::invalid_argument("njin ray tracing: non-finite irradiance grid");
    const auto &c=pass.grid_rows[2];
    if(c[0]<1||c[0]>16||c[1]<1||c[1]>16||c[2]<1||c[2]>16||c[3]<1||c[3]>256||!pass.materials) throw std::invalid_argument("njin ray tracing: invalid irradiance grid");
  }
  window.graphics->trace_shadows(pass);
  ++window.renderer_resources.ray_traces;
}
rhi::RendererResources::~RendererResources() {
#if !defined(NJIN_RL_OWN)
  // The legacy OpenGL renderer's objects; a build without raylib has none.
  for(auto &[id,mesh]:meshes) if(mesh.gl.vertices) UnloadMesh(mesh.gl);
  for(auto &[id,texture]:textures) if(texture.gl.id && !texture.target) UnloadTexture(texture.gl);
  for(auto &[id,target]:targets) if(target.gl.id) UnloadRenderTexture(target.gl);
#endif
}
RenderImageAsset renderer_load_image(const char *path) {
  if(!path || !*path) throw std::invalid_argument("njin renderer: empty image path");
  return image_io_load(path,RenderImageFormat::RGBA8);
}
void renderer_draw_mesh(context &ctx, const MeshDraw &submission) {
  MeshDraw draw=submission;
  ctx_ref<window_guard>(ctx).renderer_resources.pass_dirty=true;
  if(draw.maps.lightmap_static_direct) {
    if(!ctx_ref<window_guard>(ctx).platform)
      throw std::invalid_argument("njin lightmap: static direct requires SDL3/bgfx");
#if !NJIN_BGFX
    throw std::invalid_argument("njin lightmap: static direct requires SDL3/bgfx");
#endif
    if(!draw.maps.lightmap.texture||!draw.maps.lightmap_linear||draw.maps.lightmap.uv.set!=2||
       !draw.lighting.enabled||draw.instances||(draw.previous_geometry&&draw.previous_geometry!=draw.geometry)||draw.transparent||draw.alpha_cutoff>0||
       draw.lighting.metallic>0||draw.transmission[0][0]>0||draw.water||draw.shader)
      throw std::invalid_argument("njin lightmap: unsupported static diffuse draw");
    for(const auto *map:rhi::all_material_maps(draw)) {
      if(map==&draw.maps.lightmap||!map->texture) continue;
      if(map!=&draw.maps.emission)
        throw std::invalid_argument("njin lightmap: static diffuse supports albedo/emission on material UV0/UV1");
    }
  }
  if(draw.tonemap!=RenderToneMap::None && draw.tonemap!=RenderToneMap::Linear && draw.tonemap!=RenderToneMap::ACES && draw.tonemap!=RenderToneMap::AgX && draw.tonemap!=RenderToneMap::Neutral)
    throw std::invalid_argument("njin renderer: invalid tone mapping mode");
  if(!std::isfinite(draw.exposure)||draw.exposure<0||!std::isfinite(draw.emission_strength)||draw.emission_strength<0)
    throw std::invalid_argument("njin renderer: invalid exposure/emission strength");
  if(draw.linear_output && draw.resolve) throw std::invalid_argument("njin renderer: resolve cannot write linear output");
  if(!std::isfinite(draw.lighting.intensity)||draw.lighting.intensity<0 ||
     !std::isfinite(draw.lighting.metallic)||draw.lighting.metallic<0||draw.lighting.metallic>1 ||
     !std::isfinite(draw.lighting.roughness)||draw.lighting.roughness<0||draw.lighting.roughness>1)
    throw std::invalid_argument("njin renderer: invalid PBR/intensity parameters");
  if(draw.cull!=RenderCull::None && draw.cull!=RenderCull::Clockwise && draw.cull!=RenderCull::CounterClockwise)
    throw std::invalid_argument("njin renderer: invalid face culling mode");
  if(!std::isfinite(draw.alpha_cutoff) || draw.alpha_cutoff<0 || draw.alpha_cutoff>1)
    throw std::invalid_argument("njin renderer: alpha cutoff must be in [0,1]");
  if(draw.scissor_enabled && (draw.scissor.width>65535 || draw.scissor.height>65535 ||
      draw.scissor.x<-65535 || draw.scissor.x>65535 || draw.scissor.y<-65535 || draw.scissor.y>65535))
    throw std::invalid_argument("njin renderer: scissor exceeds coordinate limits");
  if(draw.blend!=RenderBlend::Alpha && draw.blend!=RenderBlend::Additive && draw.blend!=RenderBlend::Multiply && draw.blend!=RenderBlend::Opaque && draw.blend!=RenderBlend::WeightedAdd && draw.blend!=RenderBlend::Reveal && draw.blend!=RenderBlend::Premultiplied)
    throw std::invalid_argument("njin renderer: invalid blend mode");
  if(draw.local_light_count>render_local_light_max) throw std::invalid_argument("njin renderer: local light count exceeds 16");
  for(unsigned i=0;i<draw.local_light_count;++i) {
    const auto &l=draw.local_lights[i];
    for(float v:l.position) if(!std::isfinite(v)) throw std::invalid_argument("njin renderer: invalid local light position");
    for(float v:l.direction) if(!std::isfinite(v)) throw std::invalid_argument("njin renderer: invalid local light direction");
    for(float v:l.color) if(!std::isfinite(v)||v<0) throw std::invalid_argument("njin renderer: invalid local light color");
    if(!std::isfinite(l.radius)||l.radius<=0||!std::isfinite(l.intensity)||l.intensity<0||
       !std::isfinite(l.cos_inner)||!std::isfinite(l.cos_outer)||l.cos_outer<-1||l.cos_inner>1||l.cos_inner<l.cos_outer ||
       (l.spot && double(l.direction[0])*l.direction[0]+double(l.direction[1])*l.direction[1]+double(l.direction[2])*l.direction[2]<1e-20))
      throw std::invalid_argument("njin renderer: invalid local light parameters");
  }
  if(draw.lighting.enabled) {
    for(float v:draw.world) if(!std::isfinite(v)) throw std::invalid_argument("njin renderer: invalid world transform");
    for(float v:draw.normal_transform) if(!std::isfinite(v)) throw std::invalid_argument("njin renderer: invalid normal transform");
    for(const auto *values:{&draw.lighting.direction,&draw.lighting.camera})
      for(float v:*values) if(!std::isfinite(v)) throw std::invalid_argument("njin renderer: invalid light direction/camera");
    for(const auto *values:{&draw.lighting.color,&draw.lighting.ambient})
      for(float v:*values) if(!std::isfinite(v)||v<0) throw std::invalid_argument("njin renderer: invalid light color");
    const auto &d=draw.lighting.direction;
    if(double(d[0])*d[0]+double(d[1])*d[1]+double(d[2])*d[2]<1e-20 ||
       !std::isfinite(draw.lighting.specular)||draw.lighting.specular<0||!std::isfinite(draw.lighting.shininess)||draw.lighting.shininess<=0)
      throw std::invalid_argument("njin renderer: invalid directional lighting parameters");
  }
  for(float value:draw.emission) if(!std::isfinite(value) || value<0)
    throw std::invalid_argument("njin renderer: emission must be finite and nonnegative");
  if(draw.shader&&(draw.sprite_effects.dissolving||draw.sprite_effects.flash[3]!=0))
    throw std::logic_error("njin sprite FX: custom shader and dissolve/flash cannot be active together");
  for(const auto *values:{&draw.sprite_effects.flash,&draw.sprite_effects.edge,&draw.sprite_effects.dissolve})
    for(float v:*values) if(!std::isfinite(v)) throw std::invalid_argument("njin sprite FX: non-finite parameter");
  for(const auto &row:draw.surface_effects) for(float v:row)
    if(!std::isfinite(v)) throw std::invalid_argument("njin renderer: non-finite surface parameter");
  const auto &wf=draw.world_effects;
  for(const auto *values:{&wf.flash,&wf.edge,&wf.dissolve,&wf.from,&wf.to,&wf.mask,&wf.camera})
    for(float v:*values) if(!std::isfinite(v)) throw std::invalid_argument("njin 3D FX: non-finite parameter");
  if(wf.dissolve[0]<0||wf.dissolve[0]>1||wf.dissolve[1]<=0||wf.dissolve[3]<0||wf.dissolve[3]>1||
     wf.from[3]<0||wf.from[3]>1||wf.mask[3]<0||wf.flash[3]<0||wf.flash[3]>1||wf.edge[3]<0||wf.edge[3]>1)
    throw std::invalid_argument("njin 3D FX: invalid range");
  for(float value:draw.tint) if(!std::isfinite(value) || value<0 || value>1)
    throw std::invalid_argument("njin renderer: tint must be finite and in [0,1]");
  for(float value:draw.transform) if(!std::isfinite(value))
    throw std::invalid_argument("njin renderer: transform must be finite");
  if(draw.geometry) {
    if(draw.mesh) throw std::invalid_argument("njin renderer: choose a mesh handle or owned geometry");
    draw.vertices=draw.geometry->vertices;draw.indices=draw.geometry->indices;
  }
  rhi::RendererResources::MeshResource *persistent_mesh=nullptr;
  if(draw.mesh) {
    auto &meshes=ctx_ref<window_guard>(ctx).renderer_resources.meshes;
    const auto it=meshes.find(draw.mesh.id);
    if(it==meshes.end()) throw std::invalid_argument("njin renderer: invalid or foreign mesh handle");
    persistent_mesh=&it->second;
    draw.vertices=persistent_mesh->cpu;
    draw.indices=persistent_mesh->indices;
  }
  rhi::RendererResources::Texture *persistent=nullptr;
  draw.virtual_texture={};
  if(draw.texture) {
    auto &textures=ctx_ref<window_guard>(ctx).renderer_resources.textures;
    const auto it=textures.find(draw.texture.id);
    if(it==textures.end()) throw std::invalid_argument("njin renderer: invalid or foreign texture handle");
    persistent=&it->second;
    if(persistent->target && persistent->target.id==ctx_ref<window_guard>(ctx).renderer_resources.active_target.id)
      throw std::logic_error("njin renderer: render target feedback is unsupported");
    if(persistent->target && !ctx_ref<window_guard>(ctx).renderer_resources.targets.at(persistent->target.id).initialized)
      throw std::logic_error("njin renderer: render target must be rendered before sampling");
    draw.image=persistent->cpu.view();
    draw.virtual_texture=persistent->virtual_params;
    if(draw.virtual_texture[0]>0) {
      if(draw.shader||draw.post.mode!=RenderPostMode::None||draw.world_style[0][0]>1) throw std::invalid_argument("NVT: built-in opaque model/terrain albedo only");
      if(draw.maps.advanced[0]>0) throw std::invalid_argument("NVT: model world-UV detiling requires a regular source texture");
      draw.linear_sampling=true;draw.color_min_filter=RenderMinFilter::Linear;
      const auto wrap=[](RenderWrap mode){return mode==RenderWrap::Clamp?1.f:mode==RenderWrap::Mirror?2.f:0.f;};
      draw.virtual_texture[7]=wrap(draw.wrap_s)+4*wrap(draw.wrap_t);
    }
  }
  for(const auto *map:rhi::all_material_maps(draw)) if(map->texture&&ctx_ref<window_guard>(ctx).renderer_resources.textures.at(map->texture.id).virtual_params[0]>0)
    throw std::invalid_argument("NVT: only base albedo supports virtual indirection");
  if(draw.post.mode<RenderPostMode::None||draw.post.mode>RenderPostMode::LightGBuffer||!std::isfinite(draw.post.amount)||draw.post.amount<0||draw.post.amount>(draw.post.mode>=RenderPostMode::ShaftMask?2:1)||!std::isfinite(draw.post.levels)||draw.post.levels<1)
    throw std::invalid_argument("njin post: invalid mode or blend parameters");
  if(draw.post.mode>=RenderPostMode::SSAO&&draw.post.mode<=RenderPostMode::SSR) {
    if(!std::isfinite(draw.post.radius)||draw.post.radius<=0||!std::isfinite(draw.post.samples)||draw.post.samples<4||draw.post.samples>(draw.post.mode==RenderPostMode::SSR?128:32)) throw std::invalid_argument("njin SSAO: invalid radius or samples");
    for(float v:draw.post.projection) if(!std::isfinite(v)) throw std::invalid_argument("njin SSAO: invalid projection");
    for(float v:draw.post.inverse_projection) if(!std::isfinite(v)) throw std::invalid_argument("njin SSAO: invalid inverse projection");
  }
  for(float v:draw.post.sun) if(!std::isfinite(v)) throw std::invalid_argument("njin post: invalid sun projection");
  if(draw.post.sun[2]<0||draw.post.sun[2]>1||draw.post.sun[3]<=0||!std::isfinite(draw.post.edge_cos)||draw.post.edge_cos<-2||draw.post.edge_cos>1||!std::isfinite(draw.post.fade)||draw.post.fade<0||!std::isfinite(draw.post.halo)||draw.post.halo<0) throw std::invalid_argument("njin post: invalid effect parameters");
  if(draw.post.mode==RenderPostMode::Outline&&(!draw.post.auxiliary||draw.post.radius<.5f||draw.post.radius>4)) throw std::invalid_argument("njin outline: depth and width .5..4 are required");
  for(float v:draw.post.texel) if(!std::isfinite(v)||v<0||v>2) throw std::invalid_argument("njin post: invalid filter texel size");
  if(!std::isfinite(draw.reflectivity)||draw.reflectivity<0||draw.reflectivity>1) throw std::invalid_argument("njin renderer: invalid reflectivity");
  for(float v:draw.view) if(!std::isfinite(v)) throw std::invalid_argument("njin renderer: invalid view matrix");
  if(!std::isfinite(draw.post.thickness)||draw.post.thickness<=0) throw std::invalid_argument("njin SSR: invalid thickness");
  for(float v:draw.post.sky) if(!std::isfinite(v)||v<0) throw std::invalid_argument("njin SSR: invalid sky");
  if(draw.post.mode==RenderPostMode::SSR&&(!draw.post.auxiliary||!draw.post.secondary||draw.post.samples<8||draw.post.texel[0]<=0||draw.post.texel[1]<=0))
    throw std::invalid_argument("njin SSR: depth, normal mask, positive texel size and at least eight samples are required");
  if(draw.post.mode>=RenderPostMode::SMAAEdge&&draw.post.mode<=RenderPostMode::SMAABlend&&(draw.post.texel[0]<=0||draw.post.texel[1]<=0||draw.instances)) throw std::invalid_argument("njin SMAA: positive pixel size and a non-instanced fullscreen pass are required");
  if(draw.post.mode==RenderPostMode::SMAAWeights&&(!draw.post.auxiliary||!draw.post.secondary)) throw std::invalid_argument("njin SMAA: area and search lookup textures are required");
  if(draw.post.mode==RenderPostMode::SMAABlend&&!draw.post.auxiliary) throw std::invalid_argument("njin SMAA: blending weights are required");
  if(draw.post.mode==RenderPostMode::Resolve&&draw.post.auto_exposure&&!draw.post.secondary) throw std::invalid_argument("njin auto exposure: resolve requires an exposure target");
  for(auto texture:{draw.post.auxiliary,draw.post.secondary,draw.post.depth,draw.post.velocity}) if(texture) {
    const auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
    const auto it=resources.textures.find(texture.id);
    if(it==resources.textures.end()) throw std::invalid_argument("njin post: invalid auxiliary texture");
    if(it->second.target) {
      if(!resources.targets.at(it->second.target.id).initialized) throw std::invalid_argument("njin post: auxiliary target is uninitialized");
      if(it->second.target.id==resources.active_target.id) throw std::logic_error("njin post: target feedback");
    } else if(draw.post.mode!=RenderPostMode::SMAAWeights&&draw.post.mode!=RenderPostMode::LightGBuffer&&
              !(draw.post.mode==RenderPostMode::LightDirect&&texture.id==draw.post.auxiliary.id&&it->second.cpu.format==RenderImageFormat::RGBA32F))
      throw std::invalid_argument("njin post: auxiliary texture must be a target or supported data image");
    if(draw.environment&&!(draw.transparency_pass&&(draw.transmission[0][0]>0||draw.water))) throw std::invalid_argument("njin post: post-processing and environment passes must be separate");
  }
  if(draw.post.mode==RenderPostMode::LightDirect&&draw.post.legacy[6][3]<0) {
    const auto &textures=ctx_ref<window_guard>(ctx).renderer_resources.textures;
    const auto it=textures.find(draw.post.auxiliary.id);
    if(it==textures.end()||it->second.target||it->second.cpu.format!=RenderImageFormat::RGBA32F||draw.post.auxiliary_linear||
       draw.post.radius!=float(it->second.cpu.width)||draw.post.samples!=float(it->second.cpu.height)||
       size_t(it->second.cpu.width)*it->second.cpu.height<33)
      throw std::invalid_argument("njin 2D lighting: invalid shadow bucket texture");
  }
  if(draw.velocity_pass) {
    if(draw.instances||draw.transparency_pass||draw.post.mode!=RenderPostMode::None) throw std::invalid_argument("njin temporal: incompatible velocity draw");
    for(const auto *matrix:{&draw.motion_current,&draw.motion_previous,&draw.motion_view}) for(float v:*matrix) if(!std::isfinite(v)) throw std::invalid_argument("njin temporal: invalid motion transform");
    if(draw.previous_geometry&&draw.previous_geometry->vertices.size()!=draw.vertices.size()) throw std::invalid_argument("njin temporal: previous geometry topology changed");
  }
  if(draw.post.mode==RenderPostMode::TAA||draw.post.mode==RenderPostMode::MotionBlur) {
    if(draw.post.mode==RenderPostMode::MotionBlur&&(!std::isfinite(draw.post.samples)||draw.post.samples<2||draw.post.samples>32||draw.post.samples!=std::floor(draw.post.samples)||draw.post.amount>1)) throw std::invalid_argument("njin temporal: invalid motion blur samples or strength");
    for(float v:draw.post.temporal) if(!std::isfinite(v)) throw std::invalid_argument("njin temporal: invalid temporal parameters");
    if(!draw.post.depth||!draw.post.velocity||draw.post.texel[0]<=0||draw.post.texel[1]<=0) throw std::invalid_argument("njin temporal: depth, velocity and pixel size required");
    if(draw.post.mode==RenderPostMode::TAA&&!draw.post.reset&&(!draw.post.auxiliary||!draw.post.secondary)) throw std::invalid_argument("njin temporal: history required");
    for(const auto *matrix:{&draw.post.previous_clip,&draw.post.previous_view,&draw.post.inverse_projection}) for(float v:*matrix) if(!std::isfinite(v)) throw std::invalid_argument("njin temporal: invalid reprojection matrix");
  }
  if(draw.transparency_pass>2) throw std::invalid_argument("njin OIT: invalid pass");
  if(draw.transparency_pass) {
    const auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
    const auto it=resources.textures.find(draw.opaque_depth.id);
    if(it==resources.textures.end()||!it->second.target||!resources.targets.at(it->second.target.id).initialized) throw std::invalid_argument("njin OIT: opaque scene depth is required");
    if(it->second.target.id==resources.active_target.id) throw std::logic_error("njin OIT: depth feedback");
    for(float v:draw.transparency_projection) if(!std::isfinite(v)) throw std::invalid_argument("njin OIT: invalid projection");
  }
  if(draw.instances) {
    if(!ctx_ref<window_guard>(ctx).graphics->instancing_available()) throw std::runtime_error("njin renderer: GPU instancing is unavailable on this device");
    if(draw.instances->size()>1000000) throw std::invalid_argument("njin renderer: too many instances");
    auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
    if(resources.instance_validation_frame!=ctx_ref<window_guard>(ctx).graphics->frame_serial()) {
      resources.instance_validation_frame=ctx_ref<window_guard>(ctx).graphics->frame_serial();
      std::erase_if(resources.validated_instances,[](const auto &entry){return entry.second.expired();});
    }
    const bool validated=draw.persistent_instances&&resources.validated_instances.contains(draw.instances.get())&&!resources.validated_instances.at(draw.instances.get()).expired();
    if(!validated) for(const auto &instance:*draw.instances) {
      for(float v:instance.transform) if(!std::isfinite(v)) throw std::invalid_argument("njin renderer: invalid instance transform");
      for(float v:instance.data) if(!std::isfinite(v)) throw std::invalid_argument("njin renderer: invalid instance data");
      // The game's own vertex stage reads the instance as raw floats (draw_instanced).
      if(draw.shader&&draw.shader->custom_vertex) continue;
    const auto &m=instance.transform;
      const float determinant=m[0]*(m[5]*m[10]-m[9]*m[6])-m[4]*(m[1]*m[10]-m[9]*m[2])+m[8]*(m[1]*m[6]-m[5]*m[2]);
      if(m[3]!=0||m[7]!=0||m[11]!=0||m[15]!=1||std::abs(determinant)<1e-12f) throw std::invalid_argument("njin renderer: instance requires a nonsingular affine transform");
      for(unsigned c=0;c<4;++c) if(!std::isfinite(instance.color[c])||instance.color[c]<0||(c==3&&instance.color[c]>1)) throw std::invalid_argument("njin renderer: invalid instance color");
    }
    if(draw.persistent_instances&&!validated) resources.validated_instances[draw.instances.get()]=draw.instances;
    if(draw.instances->empty()) return;
  }
  if(draw.environment) {
    const auto &e=*draw.environment;
    if(!std::isfinite(e.intensity)||e.intensity<0||!std::isfinite(e.rotation)||e.levels<2||e.levels>16||!e.level_height) throw std::invalid_argument("njin IBL: invalid environment parameters");
    for(const auto &c:e.sh) for(float v:c) if(!std::isfinite(v)) throw std::invalid_argument("njin IBL: non-finite SH coefficient");
    auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
    for(auto handle:{e.base,e.atlas}) {
      const auto it=resources.textures.find(handle.id);
      if(it==resources.textures.end()||it->second.cpu.format!=RenderImageFormat::RGBA16F) throw std::invalid_argument("njin IBL: invalid prepared environment texture");
      if(it->second.target) {
        if(!resources.targets.at(it->second.target.id).initialized) throw std::logic_error("njin IBL: generated environment target is uninitialized");
        if(resources.active_target.id==it->second.target.id) throw std::logic_error("njin IBL: environment target feedback is unsupported");
      }
    }
    const auto &atlas=resources.textures.at(e.atlas.id).cpu;
    if(atlas.height!=e.levels*e.level_height) throw std::invalid_argument("njin IBL: inconsistent atlas dimensions");
  }
  if(draw.environment_sky && !draw.environment) throw std::invalid_argument("njin IBL: sky requires an environment");
  if(draw.clusters) {
    const auto &c=*draw.clusters;const auto &textures=ctx_ref<window_guard>(ctx).renderer_resources.textures;
    const auto found=textures.find(c.texture.id);
    if(found==textures.end()||found->second.target||found->second.cpu.format!=RenderImageFormat::RGBA32F) throw std::invalid_argument("njin clustered lighting: an owned RGBA32F light-list texture is required");
    for(const auto &row:c.parameters) for(float value:row) if(!std::isfinite(value)||value<0) throw std::invalid_argument("njin clustered lighting: invalid parameters");
    if(c.parameters[0][0]<1||c.parameters[0][1]<1||c.parameters[0][2]<1||c.parameters[0][3]<=0||c.parameters[1][0]<=0||c.parameters[1][1]<=c.parameters[1][0]||c.parameters[3][3]>1024||
       c.parameters[2][0]!=found->second.cpu.width||c.parameters[2][1]!=found->second.cpu.height) throw std::invalid_argument("njin clustered lighting: inconsistent grid/depth/texture size");
    for(const auto *matrix:{&c.view,&c.view_projection}) for(float value:*matrix) if(!std::isfinite(value)) throw std::invalid_argument("njin clustered lighting: non-finite matrix");
    ctx_ref<window_guard>(ctx).graphics->ensure_texture(c.texture,found->second.cpu.view());
  }
  if(draw.shadows) {
    const auto &s=*draw.shadows;
    auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
    const auto it=resources.textures.find(s.texture.id);
    if(it==resources.textures.end() || !it->second.target || !resources.targets.at(it->second.target.id).initialized)
      throw std::invalid_argument("njin shadows: atlas must be an initialized target texture");
    const auto &target=resources.targets.at(it->second.target.id);
    if(resources.active_target.id==it->second.target.id) throw std::logic_error("njin shadows: atlas feedback is unsupported");
    if(!s.width||!s.height||s.width!=target.width||s.height!=target.height||s.views>28||!s.views||!std::isfinite(s.softness)||s.softness<0||s.softness>8)
      throw std::invalid_argument("njin shadows: invalid atlas layout");
    for(const auto &row:s.matrices) for(float v:row) if(!std::isfinite(v)) throw std::invalid_argument("njin shadows: invalid matrix");
    for(unsigned i=0;i<s.views;++i) {
      const auto &r=s.regions[i]; const auto &m=s.metadata[i];
      for(float v:r) if(!std::isfinite(v)||v<0||v>1) throw std::invalid_argument("njin shadows: invalid atlas region");
      for(float v:m) if(!std::isfinite(v)) throw std::invalid_argument("njin shadows: invalid metadata");
      if((r[2]-r[0])*s.width<0.999f||(r[3]-r[1])*s.height<0.999f||m[0]<0||m[1]<0||m[2]<0||m[2]>7||m[2]!=std::floor(m[2]))
        throw std::invalid_argument("njin shadows: invalid region or matrix index");
    }
    for(int base:s.local_base) if(base<-1||base>=int(s.views)) throw std::invalid_argument("njin shadows: invalid local view index");
    for(unsigned i=0;i<draw.local_light_count;++i) if(s.local_base[i]>=0 && !draw.local_lights[i].spot && s.local_base[i]+6>int(s.views))
      throw std::invalid_argument("njin shadows: point source requires six atlas faces");
    for(float v:s.forward) if(!std::isfinite(v)) throw std::invalid_argument("njin shadows: invalid cascade direction");
    for(float v:s.splits) if(!std::isfinite(v)||v<0) throw std::invalid_argument("njin shadows: invalid cascade distance");
    if(s.forward[3]!=std::floor(s.forward[3])) throw std::invalid_argument("njin shadows: cascade count must be integral");
    if(!std::isfinite(s.forward[3])||s.forward[3]<0||s.forward[3]>4||s.forward[3]>s.views)
      throw std::invalid_argument("njin shadows: invalid sun cascade count");
    for(unsigned i=0;i<unsigned(s.forward[3]);++i)
      if(s.splits[i]<=0||(i&&s.splits[i]<=s.splits[i-1])) throw std::invalid_argument("njin shadows: cascade distances must increase");
  }
  const auto &im = draw.image;
  if(!(persistent&&persistent->target)) validate_image(im);
  if (!im.width || !im.height || im.width > 16384 || im.height > 16384 ||
      (im.format!=RenderImageFormat::RGBA8 && im.format!=RenderImageFormat::RGBA16F && im.format!=RenderImageFormat::RGBA32F) ||
      (!(persistent && persistent->target) && im.rgba.size() != size_t(im.width)*im.height*render_image_pixel_bytes(im.format)) || (draw.indices.empty() ? draw.vertices.size()%3 : draw.indices.size()%3) ||
      draw.vertices.size() > (std::numeric_limits<std::uint32_t>::max()-im.rgba.size())/sizeof(RenderVertex))
    throw std::invalid_argument("njin renderer: invalid RGBA image or triangle list");
  if (draw.vertices.empty()) return;
  if(draw.scissor_enabled && (!draw.scissor.width || !draw.scissor.height)) return;
  if(draw.vertices.size()>10000000) throw std::invalid_argument("njin renderer: vertex count exceeds limit");
  if(!persistent_mesh) validate_vertices(draw.vertices);
  if(draw.indices.size()>10000000) throw std::invalid_argument("njin renderer: index count exceeds limit");
  if(!persistent_mesh) for(auto index:draw.indices) if(index>=draw.vertices.size()) throw std::invalid_argument("njin renderer: draw index out of range");
  auto &window=ctx_ref<window_guard>(ctx);
  const auto validate_uv=[](const RenderUvTransform &uv,unsigned maximum=1) {
    if(uv.set>maximum) throw std::invalid_argument("njin renderer: invalid UV set for this map");
    for(float v:uv.values) if(!std::isfinite(v)) throw std::invalid_argument("njin renderer: invalid UV transform");
  };
  const auto validate_wrap=[](RenderWrap mode) {
    if(mode!=RenderWrap::Repeat && mode!=RenderWrap::Clamp && mode!=RenderWrap::Mirror) throw std::invalid_argument("njin renderer: invalid texture wrapping");
  };
  const auto validate_min=[](RenderMinFilter m) {if(unsigned(m)>unsigned(RenderMinFilter::LinearMipLinear)) throw std::invalid_argument("njin renderer: invalid minification filter");};
  validate_min(draw.color_min_filter);
  if(draw.terrain_relief) {
    const auto &rows=draw.terrain_relief->rows;
    if(draw.world_style[0][0]!=1||draw.instances||draw.shader)
      throw std::invalid_argument("njin terrain: relief requires an uninstanced terrain draw");
    for(const auto &row:draw.terrain_relief->rows) for(float value:row)
      if(!std::isfinite(value)) throw std::invalid_argument("njin terrain: non-finite relief settings");
    if(rows[0][2]<=0||rows[0][3]<2||rows[0][3]!=std::floor(rows[0][3])||rows[1][3]<=0||rows[5][3]<=0||
       rows[4][2]<=0||rows[4][3]<1||rows[4][3]>4||rows[4][3]!=std::floor(rows[4][3]))
      throw std::invalid_argument("njin terrain: invalid relief grid or reach");
    for(unsigned l=0;l<4;++l) if(rows[2][l]<0||rows[3][l]<=0)
      throw std::invalid_argument("njin terrain: invalid relief depth or tile");
    for(unsigned i=0;i<3;++i) {
      const auto h=draw.terrain_relief->textures[i];
      const auto it=window.renderer_resources.textures.find(h.id);
      if(it==window.renderer_resources.textures.end()||it->second.target) throw std::invalid_argument("njin terrain: invalid relief texture");
      const auto &im=it->second.cpu;
      if(im.format!=(i==0?RenderImageFormat::RGBA32F:RenderImageFormat::RGBA8)||
         (i<2&&(float(im.width)!=rows[0][3]||float(im.height)!=rows[0][3]))||
         (i==2&&(float(im.width)!=rows[4][0]||float(im.height)!=rows[4][1])))
        throw std::invalid_argument("njin terrain: relief texture format or extent mismatch");
      window.graphics->ensure_texture(h,it->second.cpu.view());
    }
  }
  if(draw.terrain_imprint) {
    const auto &im=*draw.terrain_imprint;
    const auto it=window.renderer_resources.textures.find(im.texture.id);
    if(draw.world_style[0][0]!=1||draw.maps.height.texture||it==window.renderer_resources.textures.end()||
       it->second.target||it->second.cpu.format!=RenderImageFormat::RGBA8||
       im.window[2]<=0||im.window[3]!=float(it->second.cpu.width)||it->second.cpu.width!=it->second.cpu.height)
      throw std::invalid_argument("njin terrain: invalid imprint texture or window");
    for(const auto &row:{im.window,im.relief,im.tint})
      for(float value:row) if(!std::isfinite(value)) throw std::invalid_argument("njin terrain: non-finite imprint settings");
    window.graphics->ensure_texture(im.texture,it->second.cpu.view());
  }
  if(draw.water) {
    if(draw.world_style[0][0]!=3) throw std::invalid_argument("njin water: effects require a water draw");
    for(const auto &row:draw.water->parameters) for(float value:row)
      if(!std::isfinite(value)) throw std::invalid_argument("njin water: non-finite effects packet");
    for(const auto &row:{draw.water->absorption,draw.water->shallow_color,draw.water->deep_color})
      for(float value:row) if(!std::isfinite(value)) throw std::invalid_argument("njin water: non-finite optical material");
    if(draw.water->absorption[3]<=0) throw std::invalid_argument("njin water: optical depth fade must be positive");
  }
  validate_uv(draw.color_uv); validate_wrap(draw.wrap_s); validate_wrap(draw.wrap_t);
  if(!std::isfinite(draw.maps.normal_scale)||!std::isfinite(draw.maps.occlusion_strength)||draw.maps.occlusion_strength<0||draw.maps.occlusion_strength>1)
    throw std::invalid_argument("njin renderer: invalid map factors");
  for(float c:draw.maps.emission_factor) if(!std::isfinite(c)||c<0) throw std::invalid_argument("njin renderer: invalid map emission factor");
  for(float c:draw.maps.advanced) if(!std::isfinite(c)||c<0) throw std::invalid_argument("njin renderer: invalid advanced map factor");
  if(draw.maps.advanced[1]>1) throw std::invalid_argument("njin renderer: invalid under-layer fraction");
  for(const auto &row:draw.transmission) for(float value:row) if(!std::isfinite(value)||value<0) throw std::invalid_argument("njin renderer: invalid transmission factor");
  if(draw.transmission[0][0]>1||(draw.transmission[0][0]>0&&draw.transmission[0][1]<1)) throw std::invalid_argument("njin renderer: invalid transmission amount/IOR");
  for(unsigned i=0;i<3;++i) if(draw.transmission[1][i]>1) throw std::invalid_argument("njin renderer: invalid attenuation color");
  if((draw.maps.transmission.texture||draw.maps.thickness.texture)&&
     (draw.post.mode!=RenderPostMode::None||draw.world_style[0][0]!=0||draw.shader))
    throw std::invalid_argument("njin renderer: volume maps require a built-in mesh draw");
  if((draw.maps.transmission.texture&&draw.post.secondary)||(draw.maps.thickness.texture&&draw.maps.height.texture))
    throw std::invalid_argument("njin renderer: volume maps conflict with secondary post input or parallax map");
  if(!std::isfinite(draw.maps.clearcoat_normal_scale)) throw std::invalid_argument("njin renderer: invalid clearcoat normal scale");
  if(draw.ray_shadow&&(draw.opaque_depth||draw.post.depth||draw.transparency_pass||draw.post.mode!=RenderPostMode::None))
    throw std::invalid_argument("njin renderer: the ray traced shadow mask shares sampler 10 with opaque-depth and post inputs");
  if(draw.instance_count&&(!draw.instances||draw.instance_count>draw.instances->size()))
    throw std::invalid_argument("njin renderer: instance count exceeds the instance buffer");
  for(float v:draw.instance_occlusion_bounds) if(!std::isfinite(v)) throw std::invalid_argument("njin Hi-Z: non-finite instance bounds");
  if(draw.instance_occlusion_bounds[3]<0||(draw.instance_occlusion_bounds[3]>0&&(draw.transparent||draw.shader||draw.world_style[0][0]!=0)))
    throw std::invalid_argument("njin Hi-Z: bounds require an opaque built-in draw");
  if(draw.gpu_cull) {
    if(!window.graphics->gpu_occlusion_available()) throw std::runtime_error(window.graphics->gpu_occlusion_diagnostics());
    const auto &c=*draw.gpu_cull;
    for(const auto *values:{&c.bounds,&c.camera,&c.lod}) for(float v:*values)
      if(!std::isfinite(v)) throw std::invalid_argument("njin GPU culling: non-finite configuration");
    for(float v:c.view_projection) if(!std::isfinite(v)) throw std::invalid_argument("njin GPU culling: non-finite projection");
    for(const auto &plane:c.camera_planes) for(float v:plane) if(!std::isfinite(v)) throw std::invalid_argument("njin GPU culling: non-finite camera plane");
    if(c.bounds[3]<=0||c.lod[0]<0||c.lod[1]<0||c.lod[2]<c.lod[1]||c.lod[1]!=std::floor(c.lod[1])||c.lod[2]!=std::floor(c.lod[2])||c.lod[2]>16||
       (c.select_lod&&(!draw.instances||c.camera[3]<=0))||draw.world_style[0][0]!=0||(!draw.shadow_pass&&(draw.transparent||draw.shader)))
      throw std::invalid_argument("njin GPU culling: invalid bounds, LOD or draw variant");
  }
  if(draw.cover) {
    if(draw.cover->box_count>16) throw std::invalid_argument("njin renderer: weather cover holds at most 16 boxes");
    if(!std::isfinite(draw.cover->bias)) throw std::invalid_argument("njin renderer: non-finite cover bias");
    for(const auto &rows:{std::span<const std::array<float,4>>(draw.cover->rows),std::span<const std::array<float,4>>(draw.cover->boxes)})
      for(const auto &row:rows) for(float v:row) if(!std::isfinite(v)) throw std::invalid_argument("njin renderer: non-finite weather cover");
    if(draw.cover->texture&&(draw.post.auxiliary||draw.transmission[0][0]>0||rhi::ClearcoatBinding(draw).any))
      throw std::invalid_argument("njin renderer: the weather cover map shares sampler 8 with transmission, clearcoat maps and post inputs");
    if(draw.cover->texture&&window.renderer_resources.textures.find(draw.cover->texture.id)==window.renderer_resources.textures.end())
      throw std::invalid_argument("njin renderer: invalid or foreign weather cover map");
  }
  if(draw.sky) {
    for(const auto &row:draw.sky->rows) for(float v:row) if(!std::isfinite(v)) throw std::invalid_argument("njin sky: non-finite sky packet");
    if(draw.sky->backdrop&&(draw.environment_sky||draw.post.mode!=RenderPostMode::None||draw.world_style[0][0]!=0||draw.shader||draw.instances))
      throw std::invalid_argument("njin sky: the procedural sky needs its own plain draw");
  }
  if(const rhi::ClearcoatBinding coat(draw);coat.any) {
    if(draw.post.mode!=RenderPostMode::None||draw.world_style[0][0]!=0||draw.shader||draw.water)
      throw std::invalid_argument("njin renderer: clearcoat maps require a built-in mesh draw");
    if(draw.transmission[0][0]>0||draw.maps.transmission.texture||draw.post.auxiliary||draw.post.secondary)
      throw std::invalid_argument("njin renderer: clearcoat maps share samplers with transmission and post inputs; they cannot be combined");
  }
  for(auto map:rhi::all_material_maps(draw)) {
    validate_min(map->min_filter); validate_uv(map->uv,map==&draw.maps.lightmap&&draw.maps.lightmap_static_direct?2:1); validate_wrap(map->wrap_s); validate_wrap(map->wrap_t);
    if(!map->texture) continue;
    const auto it=window.renderer_resources.textures.find(map->texture.id);
    if(it==window.renderer_resources.textures.end()) throw std::invalid_argument("njin renderer: invalid or foreign material map");
    if(it->second.target) {
      if(it->second.target.id==window.renderer_resources.active_target.id) throw std::logic_error("njin renderer: material map feedback is unsupported");
      if(!window.renderer_resources.targets.at(it->second.target.id).initialized) throw std::logic_error("njin renderer: material map target is uninitialized");
    }
  }
  if(draw.shader) for(const auto &binding:draw.shader->textures) {
    if(!binding.texture) continue;
    const auto it=window.renderer_resources.textures.find(binding.texture.id);
    if(it==window.renderer_resources.textures.end()) throw std::invalid_argument("njin shader: invalid or foreign sampler texture");
    if(it->second.target) {
      if(it->second.target.id==window.renderer_resources.active_target.id) throw std::logic_error("njin shader: sampler target feedback is unsupported");
      if(!window.renderer_resources.targets.at(it->second.target.id).initialized) throw std::logic_error("njin shader: sampler target is uninitialized");
    } else window.graphics->ensure_texture(binding.texture,it->second.cpu.view());
  }
  if(!window.graphics->frame_open()) throw std::logic_error("njin renderer: draw/submission outside an active render frame");
  for(auto t:{draw.post.auxiliary,draw.post.secondary,draw.post.depth,draw.post.velocity}) if(t&&!window.renderer_resources.textures.at(t.id).target) window.graphics->ensure_texture(t,window.renderer_resources.textures.at(t.id).cpu.view());
  for(auto map:rhi::all_material_maps(draw)) if(map->texture) window.graphics->ensure_texture(map->texture,window.renderer_resources.textures.at(map->texture.id).cpu.view());
  if(draw.environment) for(auto h:{draw.environment->base,draw.environment->atlas}) {
    const auto &texture=window.renderer_resources.textures.at(h.id);
    if(!texture.target) window.graphics->ensure_texture(h,texture.cpu.view());
  }
  ctx_ref<window_guard>(ctx).graphics->draw(draw);
}
void renderer_draw_sprite(context &ctx, const RenderImage &image, float x, float y, float w, float h) {
  const RenderVertex v[]{{x,y,0,0,0},{x+w,y,0,1,0},{x+w,y+h,0,1,1},
                         {x,y,0,0,0},{x+w,y+h,0,1,1},{x,y+h,0,0,1}};
  MeshDraw draw{v,image}; draw.depth_test=false;
  renderer_draw_mesh(ctx,draw);
}
void renderer_draw_sprite(context &ctx,RenderTextureHandle texture,float x,float y,float w,float h) {
  const RenderVertex v[]{{x,y,0,0,0},{x+w,y,0,1,0},{x+w,y+h,0,1,1},
                         {x,y,0,0,0},{x+w,y+h,0,1,1},{x,y+h,0,0,1}};
  MeshDraw draw{v,{}}; draw.texture=texture; draw.depth_test=false;
  renderer_draw_mesh(ctx,draw);
}
}
