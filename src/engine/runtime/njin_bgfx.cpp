#include "njin_bgfx.h"
#include "njin_image_mips.h"
#include "njin_shader_binary.h"
#if NJIN_BGFX
#include "njin_sdl_platform.h"
#include "njin_shadow_uniforms.h"
#include "njin_material_uniforms.h"
#include "njin_environment_uniforms.h"
#include "njin_raytrace_vk.h"
#include <bgfx/bgfx.h>
#include "njin_bgfx_occlusion.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <algorithm>
#include <atomic>
#include <cmath>
#if defined(__ANDROID__)
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl31.h>
#endif

namespace njin::rhi {
namespace {
template<class T> T checked(T handle,const char *operation) {
  if(!bgfx::isValid(handle)) throw std::runtime_error(std::string("njin bgfx: ")+operation);
  return handle;
}
uint32_t packed(rgba c) {
  return uint32_t(c.r*255)<<24 | uint32_t(c.g*255)<<16 | uint32_t(c.b*255)<<8 | uint32_t(c.a*255);
}
bgfx::RendererType::Enum renderer_type(GraphicsBackend backend) {
  switch(backend) {
    case GraphicsBackend::Vulkan: return bgfx::RendererType::Vulkan;
    case GraphicsBackend::BgfxOpenGL: return bgfx::RendererType::OpenGL;
    case GraphicsBackend::Direct3D12: return bgfx::RendererType::Direct3D12;
    case GraphicsBackend::Metal: return bgfx::RendererType::Metal;
    case GraphicsBackend::OpenGLES: case GraphicsBackend::WebGL2: return bgfx::RendererType::OpenGLES;
    case GraphicsBackend::Auto:
#if defined(__APPLE__)
      return bgfx::RendererType::Metal;
#elif defined(__EMSCRIPTEN__)
      return bgfx::RendererType::OpenGLES;
#elif defined(_WIN32)
      return bgfx::RendererType::Direct3D12;
#else
      return bgfx::RendererType::Vulkan;
#endif
    default: throw std::invalid_argument("njin bgfx: invalid renderer selection");
  }
}
struct Callbacks final : bgfx::CallbackI {
  RenderImageAsset captured;
  std::string capture_error;
  void fatal(const char *file,uint16_t line,bgfx::Fatal::Enum code,const char *message) override {
    std::fprintf(stderr,"njin bgfx fatal %u %s:%u: %s\n",unsigned(code),file,line,message);
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_FATAL,"NjinBgfx","fatal %u %s:%u: %s",unsigned(code),file,line,message);
#endif
    // bgfx requires termination for unrecoverable renderer/device errors.
    std::abort();
  }
  void traceVargs(const char *,uint16_t,const char *format,va_list args) override {
#if defined(__ANDROID__)
    __android_log_vprint(ANDROID_LOG_DEBUG,"NjinBgfx",format,args);
#else
    std::vfprintf(stderr,format,args);
#endif
  }
  void profilerBegin(const char *,uint32_t,const char *,uint16_t) override {}
  void profilerBeginLiteral(const char *,uint32_t,const char *,uint16_t) override {}
  void profilerEnd() override {}
  uint32_t cacheReadSize(uint64_t) override { return 0; }
  bool cacheRead(uint64_t,void *,uint32_t) override { return false; }
  void cacheWrite(uint64_t,const void *,uint32_t) override {}
  void screenShot(const char *,uint32_t w,uint32_t h,uint32_t pitch,bgfx::TextureFormat::Enum format,
                  const void *data,uint32_t size,bool yflip) override {
    // D3D12 may omit padding after the last row in its readback allocation.
    const uint64_t row_bytes=uint64_t(w)*4;
    if(!data || !w || !h || w>16384 || h>16384 ||
       (format!=bgfx::TextureFormat::BGRA8 && format!=bgfx::TextureFormat::RGBA8) ||
       pitch<row_bytes || uint64_t(pitch)*(h-1)+row_bytes>size) {
      capture_error="unsupported screenshot layout"; return;
    }
    try {
      captured={w,h,{}}; captured.pixels.resize(size_t(w)*h*4);
      for(unsigned y=0;y<h;++y) {
        auto *dst=captured.pixels.data()+size_t(y)*w*4;
        const auto *src=static_cast<const unsigned char*>(data)+size_t(yflip ? h-1-y:y)*pitch;
        std::memcpy(dst,src,size_t(w)*4);
        if(format==bgfx::TextureFormat::BGRA8) for(unsigned x=0;x<w;++x) std::swap(dst[x*4],dst[x*4+2]);
      }
    } catch(...) { capture_error="screenshot CPU allocation failed"; }
  }
  void captureBegin(uint32_t,uint32_t,uint32_t,bgfx::TextureFormat::Enum,bool) override {}
  void captureEnd() override {}
  void captureFrame(const void *,uint32_t) override {}
};
class BgfxDevice final : public GraphicsDevice {
  static inline bool live_device=false; // engine/window thread only
  SdlPlatform &platform;
  Callbacks callback;
  bool initialized=false,vsync=false;
  std::uint32_t submitted_frame=UINT32_MAX;
  // begin_readback: a staging copy each, read when the frame it names is done.
  struct PendingRead {bgfx::TextureHandle staging=BGFX_INVALID_HANDLE;std::uint32_t ready=0;RenderImageAsset image;};
  std::unordered_map<std::uint64_t,PendingRead> pending_reads;std::uint64_t next_read=1;
  unsigned width=0,height=0;
  static constexpr uint64_t screen_id=UINT64_MAX;
  bool presenting=false;
  bgfx::SwapChain swapchain;
  bgfx::ProgramHandle program=BGFX_INVALID_HANDLE,instance_program=BGFX_INVALID_HANDLE;
  std::array<bgfx::ProgramHandle,3> smaa_programs{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
  std::array<bgfx::ProgramHandle,3> virtual_programs{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
  std::span<const std::uint8_t> virtual_fragment;
  std::string shader_format;
  struct CustomProgram {
    bgfx::ProgramHandle program;
    std::unordered_map<std::string,bgfx::UniformHandle> uniforms;
    std::vector<ShaderUniformInfo> info;
    std::vector<ShaderBinaryUniform> declarations;
    bool vertex=false; // the game's own vertex stage (reads i_data0..3 when instanced)
  };
  std::unordered_map<std::uint64_t,CustomProgram> custom_programs;
  std::vector<ShaderBinaryUniform> engine_uniforms;
  std::unordered_set<std::uint64_t> used_shaders;
  struct InstanceUpload {std::shared_ptr<const std::vector<RenderInstance>> cpu;bgfx::VertexBufferHandle gpu;};
  std::unordered_map<const void*,InstanceUpload> instance_uploads;
  struct PersistentInstances {std::weak_ptr<const std::vector<RenderInstance>> cpu;bgfx::VertexBufferHandle gpu;};
  std::unordered_map<const void*,PersistentInstances> persistent_uploads;
  std::array<std::uint64_t,3> instance_counters{};
  void clear_instances() {for(auto &[id,upload]:instance_uploads) bgfx::destroy(upload.gpu);instance_uploads.clear();}
  bgfx::UniformHandle mvp=BGFX_INVALID_HANDLE,tint=BGFX_INVALID_HANDLE,emission=BGFX_INVALID_HANDLE,world=BGFX_INVALID_HANDLE,normal=BGFX_INVALID_HANDLE,light=BGFX_INVALID_HANDLE,material=BGFX_INVALID_HANDLE,output=BGFX_INVALID_HANDLE,local=BGFX_INVALID_HANDLE,params=BGFX_INVALID_HANDLE,sampler=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle shadow_rows=BGFX_INVALID_HANDLE,shadow_rect=BGFX_INVALID_HANDLE,shadow_meta=BGFX_INVALID_HANDLE,shadow_info=BGFX_INVALID_HANDLE,shadow_splits=BGFX_INVALID_HANDLE,shadow_forward=BGFX_INVALID_HANDLE,shadow_sampler=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle post_uniform=BGFX_INVALID_HANDLE,post_extra=BGFX_INVALID_HANDLE,post_aux=BGFX_INVALID_HANDLE,post_projection=BGFX_INVALID_HANDLE,post_inverse=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle post_second=BGFX_INVALID_HANDLE,post_config=BGFX_INVALID_HANDLE,post_sky=BGFX_INVALID_HANDLE,post_sun=BGFX_INVALID_HANDLE,mask_uniform=BGFX_INVALID_HANDLE,view_uniform=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle env_sh=BGFX_INVALID_HANDLE,env_params=BGFX_INVALID_HANDLE,env_mode=BGFX_INVALID_HANDLE,env_base=BGFX_INVALID_HANDLE,env_atlas=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle map_uv=BGFX_INVALID_HANDLE,map_factors=BGFX_INVALID_HANDLE,map_emission=BGFX_INVALID_HANDLE;
  std::array<bgfx::UniformHandle,4> map_samplers{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
  std::array<bgfx::UniformHandle,4> advanced_samplers{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
  bgfx::UniformHandle advanced_uv=BGFX_INVALID_HANDLE,advanced_params=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle transmit_params=BGFX_INVALID_HANDLE;
  // Compute ray traced sun shadows (njin_raytrace.h).
  bgfx::ProgramHandle rt_trace=BGFX_INVALID_HANDLE,rt_blur=BGFX_INVALID_HANDLE,rt_reflect=BGFX_INVALID_HANDLE,rt_gi=BGFX_INVALID_HANDLE,rt_gi_light=BGFX_INVALID_HANDLE;
  // irradiance3d_desc::ray_traced: the grid's light (points x 6) and depth tiles, twice
  // (last frame's read while this frame's is written); 1-point stand-ins without a grid.
  std::array<bgfx::TextureHandle,2> rt_grid{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}},rt_grid_depth{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
  std::uint64_t rt_grid_key=~0ull;unsigned rt_grid_points=0;int rt_grid_latest=0;
  bgfx::UniformHandle rt_grid_rows=BGFX_INVALID_HANDLE,rt_grid_sampler=BGFX_INVALID_HANDLE,rt_grid_depth_sampler=BGFX_INVALID_HANDLE;
  std::array<bgfx::TextureHandle,2> rt_mask{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
  unsigned rt_width=0,rt_height=0;
  bgfx::DynamicVertexBufferHandle rt_nodes=BGFX_INVALID_HANDLE,rt_tris=BGFX_INVALID_HANDLE,rt_mats=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle rt_lights=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle rt_inverse=BGFX_INVALID_HANDLE,rt_sun=BGFX_INVALID_HANDLE,rt_params=BGFX_INVALID_HANDLE,rt_extra=BGFX_INVALID_HANDLE,rt_lamps=BGFX_INVALID_HANDLE;
  std::unique_ptr<VulkanRayQuery> rt_hw; // ray queries replacing the traversal (Vulkan)
  const void *rt_uploaded=nullptr; // the scene buffers' source, to skip re-uploading an unchanged BVH
  bgfx::UniformHandle rt_depth_sampler=BGFX_INVALID_HANDLE,rt_mask_sampler=BGFX_INVALID_HANDLE,rt_shadow_rows=BGFX_INVALID_HANDLE;
  std::array<std::array<float,4>,6> rt_frame{}; // view-projection rows; shadows traced, texel size (of all three parts), reflection roughness; grid light on
  std::uint64_t rt_frame_serial=0;int rt_latest=-1;
  bgfx::UniformHandle coat_params=BGFX_INVALID_HANDLE,coat_uv=BGFX_INVALID_HANDLE,sky_rows=BGFX_INVALID_HANDLE,cover_rows=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle cluster_params=BGFX_INVALID_HANDLE,cluster_view=BGFX_INVALID_HANDLE,cluster_vp=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle light_edges=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle particle_fx=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle legacy_post=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle surface_fx=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle world_style=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle water_effects=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle terrain_imprint=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle terrain_relief=BGFX_INVALID_HANDLE;
  std::array<bgfx::UniformHandle,3> terrain_relief_samplers{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
  std::array<bgfx::ProgramHandle,3> terrain_relief_programs{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
  bgfx::UniformHandle world_fx=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle sprite_fx=BGFX_INVALID_HANDLE,mip_filter=BGFX_INVALID_HANDLE,virtual_uniform=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle transparency=BGFX_INVALID_HANDLE,transparency_vp=BGFX_INVALID_HANDLE,opaque_depth=BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle volume_program=BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle water_program=BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle light2d_program=BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle velocity_program=BGFX_INVALID_HANDLE,depth_program=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle motion_current=BGFX_INVALID_HANDLE,motion_previous=BGFX_INVALID_HANDLE,motion_view=BGFX_INVALID_HANDLE,temporal_params=BGFX_INVALID_HANDLE,previous_clip=BGFX_INVALID_HANDLE,previous_view=BGFX_INVALID_HANDLE,velocity_sampler=BGFX_INVALID_HANDLE;
  bgfx::VertexLayoutHandle previous_layout_handle=BGFX_INVALID_HANDLE;
  bgfx::VertexLayout layout;
  struct Mesh { bgfx::VertexBufferHandle vertices=BGFX_INVALID_HANDLE; bgfx::IndexBufferHandle indices=BGFX_INVALID_HANDLE; };
  struct GeometryUpload {std::shared_ptr<const RenderGeometryData> cpu;Mesh gpu;};
  std::unordered_map<const void*,GeometryUpload> geometry_uploads;
  struct PreviousUpload {std::shared_ptr<const RenderGeometryData> cpu;bgfx::VertexBufferHandle gpu;};
  std::unordered_map<const void*,PreviousUpload> previous_positions;
  std::uint64_t geometry_count=0;
  void clear_geometry() {for(auto &[id,g]:previous_positions) bgfx::destroy(g.gpu);previous_positions.clear();for(auto &[id,g]:geometry_uploads) {bgfx::destroy(g.gpu.vertices);if(bgfx::isValid(g.gpu.indices)) bgfx::destroy(g.gpu.indices);}geometry_uploads.clear();}
  struct Target {
    bgfx::FrameBufferHandle framebuffer=BGFX_INVALID_HANDLE;
    bgfx::TextureHandle depth=BGFX_INVALID_HANDLE;
    RenderTextureHandle color; unsigned width,height; bool rendered=false;RenderTextureHandle depth_alias{};
    bgfx::TextureFormat::Enum format=bgfx::TextureFormat::RGBA8;
  };
  std::unordered_map<uint64_t,bgfx::TextureHandle> textures;
  std::unordered_set<uint64_t> borrowed_depth;
  std::unordered_map<uint64_t,Mesh> meshes;
  std::unordered_map<uint64_t,Target> targets;
  std::unordered_set<uint64_t> used_textures,used_meshes,used_targets;
  uint64_t texture_count=0,mesh_count=0,active_target=0;
  BgfxOcclusion occlusion;
  bgfx::ViewId view=0;
  void next_view(uint64_t target,rgba clear,bool clear_enabled) {
    if(uint32_t(view)+1>=bgfx::getCaps()->limits.maxViews) throw std::runtime_error("njin bgfx: frame exceeds view/pass limit");
    ++view; configure_view(target,clear,clear_enabled);
  }
  void configure_view(uint64_t target,rgba clear,bool clear_enabled) {
    const auto *offscreen=target ? &targets.at(target):!presenting&&targets.contains(screen_id)?&targets.at(screen_id):nullptr;
    bgfx::setViewFrameBuffer(view,offscreen ? offscreen->framebuffer:bgfx::FrameBufferHandle BGFX_INVALID_HANDLE);
    bgfx::setViewRect(view,0,0,(uint16_t)(offscreen ? offscreen->width:width),(uint16_t)(offscreen ? offscreen->height:height));
    bgfx::setViewMode(view,bgfx::ViewMode::Sequential);
    bgfx::setViewClear(view,clear_enabled ? BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH:BGFX_CLEAR_NONE,packed(clear),1,0);
    bgfx::touch(view);
  }
  void cleanup() {
    if(!initialized) { live_device=false; return; }
    occlusion.release();
    for(auto &[id,t]:targets) { bgfx::destroy(t.framebuffer); bgfx::destroy(t.depth); }
    for(auto &[id,t]:textures) if(!borrowed_depth.contains(id)) bgfx::destroy(t);
    for(auto &[id,m]:meshes) { bgfx::destroy(m.vertices); if(bgfx::isValid(m.indices)) bgfx::destroy(m.indices); }
    clear_geometry();clear_instances();
    for(auto &[id,upload]:persistent_uploads) bgfx::destroy(upload.gpu);
    persistent_uploads.clear();
    for(auto &[id,p]:custom_programs) bgfx::destroy(p.program);
    custom_programs.clear();
    if(bgfx::isValid(instance_program)) bgfx::destroy(instance_program);
    for(auto p:virtual_programs) if(bgfx::isValid(p)) bgfx::destroy(p);
    if(bgfx::isValid(previous_layout_handle)) bgfx::destroy(previous_layout_handle);
    if(bgfx::isValid(velocity_program)) bgfx::destroy(velocity_program);
    if(bgfx::isValid(depth_program)) bgfx::destroy(depth_program);
    if(bgfx::isValid(water_program)) bgfx::destroy(water_program);
    if(bgfx::isValid(volume_program)) bgfx::destroy(volume_program);
    if(bgfx::isValid(light2d_program)) bgfx::destroy(light2d_program);
    if(bgfx::isValid(program)) bgfx::destroy(program);
    for(auto p:smaa_programs) if(bgfx::isValid(p)) bgfx::destroy(p);
    for(auto u:{mvp,tint,emission,world,normal,light,material,output,local,params,sampler,shadow_rows,shadow_rect,shadow_meta,shadow_info,shadow_splits,shadow_forward,shadow_sampler,map_uv,map_factors,map_emission,env_sh,env_params,env_mode,env_base,env_atlas,post_uniform,post_extra,post_aux,post_projection,post_inverse,post_second,post_config,post_sky,post_sun,mask_uniform,view_uniform,sprite_fx,world_fx,world_style,surface_fx,legacy_post,particle_fx,light_edges,mip_filter,virtual_uniform,motion_current,motion_previous,motion_view,temporal_params,previous_clip,previous_view,velocity_sampler,transparency,transparency_vp,opaque_depth}) if(bgfx::isValid(u)) bgfx::destroy(u);
    for(auto u:map_samplers) if(bgfx::isValid(u)) bgfx::destroy(u);
    for(auto u:advanced_samplers) if(bgfx::isValid(u)) bgfx::destroy(u);
    if(bgfx::isValid(water_effects)) bgfx::destroy(water_effects);
    if(bgfx::isValid(terrain_imprint)) bgfx::destroy(terrain_imprint);
    if(bgfx::isValid(terrain_relief)) bgfx::destroy(terrain_relief);
    for(auto h:terrain_relief_samplers) if(bgfx::isValid(h)) bgfx::destroy(h);
    for(auto h:terrain_relief_programs) if(bgfx::isValid(h)) bgfx::destroy(h);
    if(bgfx::isValid(advanced_uv)) bgfx::destroy(advanced_uv);
    if(bgfx::isValid(advanced_params)) bgfx::destroy(advanced_params);
    if(bgfx::isValid(transmit_params)) bgfx::destroy(transmit_params);
    for(auto p:{rt_trace,rt_blur,rt_reflect,rt_gi,rt_gi_light}) if(bgfx::isValid(p)) bgfx::destroy(p);
    for(auto &[id,read]:pending_reads) if(bgfx::isValid(read.staging)) bgfx::destroy(read.staging);
    pending_reads.clear();
    for(auto t:rt_grid) if(bgfx::isValid(t)) bgfx::destroy(t);
    for(auto t:rt_grid_depth) if(bgfx::isValid(t)) bgfx::destroy(t);
    for(auto t:rt_mask) if(bgfx::isValid(t)) bgfx::destroy(t);
    for(auto v:{rt_nodes,rt_tris,rt_mats}) if(bgfx::isValid(v)) bgfx::destroy(v);
    for(auto u:{rt_inverse,rt_sun,rt_params,rt_extra,rt_lamps,rt_lights,rt_grid_rows,rt_grid_sampler,rt_grid_depth_sampler,rt_depth_sampler,rt_mask_sampler,rt_shadow_rows}) if(bgfx::isValid(u)) bgfx::destroy(u);
    for(auto handle:{coat_params,coat_uv,sky_rows,cover_rows}) if(bgfx::isValid(handle)) bgfx::destroy(handle);
    for(auto handle:{cluster_params,cluster_view,cluster_vp}) if(bgfx::isValid(handle)) bgfx::destroy(handle);
    rt_hw.reset();
    bgfx::frame(); bgfx::shutdown(); initialized=false; live_device=false;
  }
public:
  BgfxDevice(SdlPlatform &p,const config &cfg):platform(p) {
    if(live_device) throw std::logic_error("njin bgfx: only one active graphics device is supported");
    live_device=true;
    try {
      const auto type=renderer_type(cfg.graphics_backend);
      bgfx::RendererType::Enum supported[bgfx::RendererType::Count];
      const auto count=bgfx::getSupportedRenderers(bgfx::RendererType::Count,supported);
      if(std::find(supported,supported+count,type)==supported+count)
        // bgfx returns "Noop" for an uncompiled renderer; preserve the user's
        // requested backend in the diagnostic instead.
        throw std::runtime_error(std::string("njin bgfx: requested backend was not compiled: ")+std::string(graphics_backend_name(cfg.graphics_backend)));
      bgfx::Init init; init.type=type; init.fallback=false; init.callback=&callback;
      init.debug=cfg.graphics_validation;
      init.swapChain.nwh=platform.native_handle(); init.swapChain.ndt=platform.native_display();
      init.platformData.type=platform.wayland() ? bgfx::NativeWindowHandleType::Wayland:bgfx::NativeWindowHandleType::Default;
      platform.drawable_size(width,height); width=std::max(width,1u); height=std::max(height,1u);
      init.swapChain.width=width; init.swapChain.height=height;
      init.swapChain.formatDepthStencil=bgfx::TextureFormat::D24S8;
      vsync=platform.vsync(); init.reset=vsync ? BGFX_RESET_VSYNC:BGFX_RESET_NONE;
      VulkanRayQuery::request(type==bgfx::RendererType::Vulkan&&cfg.graphics_ray_query);
      if(!bgfx::init(init)) throw std::runtime_error(std::string("njin bgfx: backend unavailable: ")+bgfx::getRendererName(type));
      initialized=true; swapchain=init.swapChain;
      if(type==bgfx::RendererType::Vulkan&&cfg.graphics_ray_query) rt_hw=VulkanRayQuery::create();
      if(bgfx::getRendererType()!=type) throw std::runtime_error("njin bgfx: renderer override was not honored");
      const std::string_view format=type==bgfx::RendererType::Vulkan ? "spirv":
        type==bgfx::RendererType::OpenGL ? "glsl":type==bgfx::RendererType::Metal ? "metal":
        type==bgfx::RendererType::OpenGLES ? "essl":"dx11";
      const auto vs=bgfx_shader_bytes(format,true),fs=bgfx_shader_bytes(format,false);
      shader_format=format;
      if(vs.empty() || fs.empty()) throw std::runtime_error("njin bgfx: shader profile missing from build: "+std::string(format));
      const auto vertex=checked(bgfx::createShader(bgfx::copy(vs.data(),(uint32_t)vs.size())),"vertex shader creation failed");
      const auto fragment=bgfx::createShader(bgfx::copy(fs.data(),(uint32_t)fs.size()));
      if(!bgfx::isValid(fragment)) { bgfx::destroy(vertex); throw std::runtime_error("njin bgfx: fragment shader creation failed"); }
      program=checked(bgfx::createProgram(vertex,fragment,true),"pipeline program creation failed");
      if(bgfx::getCaps()->limits.maxInstanceData>=5 && bgfx::getCaps()->limits.maxVertexAttributes>=11) {
        const auto bytes=bgfx_shader_bytes(format,true,true);
        if(bytes.empty()) throw std::runtime_error("njin bgfx: missing instance shader");
        const auto iv=checked(bgfx::createShader(bgfx::copy(bytes.data(),uint32_t(bytes.size()))),"instance vertex shader failed");
        const auto frag=bgfx::createShader(bgfx::copy(fs.data(),uint32_t(fs.size())));
        if(!bgfx::isValid(frag)) {bgfx::destroy(iv);throw std::runtime_error("njin bgfx: instance fragment shader failed");}
        instance_program=checked(bgfx::createProgram(iv,frag,true),"instance pipeline failed");
      }
      motion_current=checked(bgfx::createUniform("u_njinMotionCurrent",bgfx::UniformType::Mat4),"motion current uniform failed");
      motion_previous=checked(bgfx::createUniform("u_njinMotionPrevious",bgfx::UniformType::Mat4),"motion previous uniform failed");
      motion_view=checked(bgfx::createUniform("u_njinMotionView",bgfx::UniformType::Mat4),"motion view uniform failed");
      temporal_params=checked(bgfx::createUniform("u_njinTemporal",bgfx::UniformType::Vec4),"temporal params failed");
      previous_clip=checked(bgfx::createUniform("u_njinPreviousClip",bgfx::UniformType::Mat4),"previous clip failed");
      previous_view=checked(bgfx::createUniform("u_njinPreviousView",bgfx::UniformType::Mat4),"previous view failed");
      velocity_sampler=checked(bgfx::createUniform("s_njinVelocity",bgfx::UniformType::Sampler),"velocity sampler failed");
      mip_filter=checked(bgfx::createUniform("u_njinMipFilter",bgfx::UniformType::Vec4,2),"mip filter uniform failed");
      transparency=checked(bgfx::createUniform("u_njinTransparency",bgfx::UniformType::Vec4),"transparency params failed");
      transparency_vp=checked(bgfx::createUniform("u_njinTransparencyVP",bgfx::UniformType::Mat4),"transparency projection failed");
      opaque_depth=checked(bgfx::createUniform("s_njinOpaqueDepth",bgfx::UniformType::Sampler),"opaque depth sampler failed");
      mvp=checked(bgfx::createUniform("u_njinMVP",bgfx::UniformType::Mat4),"MVP uniform failed");
      tint=checked(bgfx::createUniform("u_njinTint",bgfx::UniformType::Vec4),"tint uniform failed");
      emission=checked(bgfx::createUniform("u_njinEmission",bgfx::UniformType::Vec4),"emission uniform failed");
      params=checked(bgfx::createUniform("u_njinParams",bgfx::UniformType::Vec4),"params uniform failed");
      world=checked(bgfx::createUniform("u_njinWorld",bgfx::UniformType::Mat4),"world uniform failed");
      normal=checked(bgfx::createUniform("u_njinNormal",bgfx::UniformType::Mat4),"normal uniform failed");
      light=checked(bgfx::createUniform("u_njinLight",bgfx::UniformType::Vec4,4),"light uniform failed");
      material=checked(bgfx::createUniform("u_njinMaterial",bgfx::UniformType::Vec4),"material uniform failed");
      output=checked(bgfx::createUniform("u_njinOutput",bgfx::UniformType::Vec4),"output uniform failed");
      local=checked(bgfx::createUniform("u_njinLocal",bgfx::UniformType::Vec4,64),"local light uniform failed");
      sampler=checked(bgfx::createUniform("s_njinColor",bgfx::UniformType::Sampler),"sampler uniform failed");
      shadow_rows=checked(bgfx::createUniform("u_njinShadowRows",bgfx::UniformType::Vec4,32),"shadow rows failed");
      shadow_rect=checked(bgfx::createUniform("u_njinShadowRect",bgfx::UniformType::Vec4,28),"shadow rect failed");
      shadow_meta=checked(bgfx::createUniform("u_njinShadowMeta",bgfx::UniformType::Vec4,28),"shadow metadata failed");
      shadow_info=checked(bgfx::createUniform("u_njinShadowInfo",bgfx::UniformType::Vec4),"shadow info failed");
      shadow_splits=checked(bgfx::createUniform("u_njinShadowSplits",bgfx::UniformType::Vec4),"shadow splits failed");
      shadow_forward=checked(bgfx::createUniform("u_njinShadowForward",bgfx::UniformType::Vec4),"shadow forward failed");
      shadow_sampler=checked(bgfx::createUniform("s_njinShadow",bgfx::UniformType::Sampler),"shadow sampler failed");
      post_projection=checked(bgfx::createUniform("u_njinPostProjection",bgfx::UniformType::Mat4),"post projection failed");
      post_inverse=checked(bgfx::createUniform("u_njinPostInverse",bgfx::UniformType::Mat4),"post inverse projection failed");
      post_second=checked(bgfx::createUniform("s_njinPostSecond",bgfx::UniformType::Sampler),"post second sampler failed");
      post_config=checked(bgfx::createUniform("u_njinPostConfig",bgfx::UniformType::Vec4),"post config failed");
      post_sky=checked(bgfx::createUniform("u_njinPostSky",bgfx::UniformType::Vec4),"post sky failed");
      post_sun=checked(bgfx::createUniform("u_njinPostSun",bgfx::UniformType::Vec4),"post sun failed");
      mask_uniform=checked(bgfx::createUniform("u_njinMask",bgfx::UniformType::Vec4),"normal mask failed");
      view_uniform=checked(bgfx::createUniform("u_njinView",bgfx::UniformType::Mat4),"normal view failed");
      post_uniform=checked(bgfx::createUniform("u_njinPost",bgfx::UniformType::Vec4),"post params failed");
      post_extra=checked(bgfx::createUniform("u_njinPostExtra",bgfx::UniformType::Vec4),"post extra failed");
      post_aux=checked(bgfx::createUniform("s_njinPostAux",bgfx::UniformType::Sampler),"post sampler failed");
      env_sh=checked(bgfx::createUniform("u_njinEnvSH",bgfx::UniformType::Vec4,9),"environment SH failed");
      env_params=checked(bgfx::createUniform("u_njinEnvParams",bgfx::UniformType::Vec4),"environment params failed");
      env_mode=checked(bgfx::createUniform("u_njinEnvMode",bgfx::UniformType::Vec4),"environment mode failed");
      env_base=checked(bgfx::createUniform("s_njinEnvironmentBase",bgfx::UniformType::Sampler),"environment base sampler failed");
      env_atlas=checked(bgfx::createUniform("s_njinEnvironmentAtlas",bgfx::UniformType::Sampler),"environment atlas sampler failed");
      map_uv=checked(bgfx::createUniform("u_njinMapUv",bgfx::UniformType::Vec4,10),"material UV failed");
      map_factors=checked(bgfx::createUniform("u_njinMapFactors",bgfx::UniformType::Vec4),"material factors failed");
      map_emission=checked(bgfx::createUniform("u_njinMapEmission",bgfx::UniformType::Vec4),"material emission failed");
      const char *map_names[]{"s_njinNormalMap","s_njinMetalRoughMap","s_njinOcclusionMap","s_njinEmissionMap"};
      for(unsigned i=0;i<4;++i) map_samplers[i]=checked(bgfx::createUniform(map_names[i],bgfx::UniformType::Sampler),"material sampler failed");
      light_edges=checked(bgfx::createUniform("u_njinLightEdges",bgfx::UniformType::Vec4,64),"2D shadow edges uniform failed");
      particle_fx=checked(bgfx::createUniform("u_njinParticleFx",bgfx::UniformType::Vec4),"particle effects uniform failed");
      legacy_post=checked(bgfx::createUniform("u_njinLegacyPost",bgfx::UniformType::Vec4,10),"legacy post uniform failed");
      surface_fx=checked(bgfx::createUniform("u_njinSurfaceFx",bgfx::UniformType::Vec4,4),"surface effects uniform failed");
      world_style=checked(bgfx::createUniform("u_njinWorldStyle",bgfx::UniformType::Vec4,6),"world style uniform failed");
      water_effects=checked(bgfx::createUniform("u_njinWater",bgfx::UniformType::Vec4,16),"water effects uniform failed");
      terrain_imprint=checked(bgfx::createUniform("u_njinTerrainImprint",bgfx::UniformType::Vec4,3),"terrain imprint uniform failed");
      terrain_relief=checked(bgfx::createUniform("u_njinTerrainRelief",bgfx::UniformType::Vec4,6),"terrain relief uniform failed");
      advanced_uv=checked(bgfx::createUniform("u_njinAdvancedUv",bgfx::UniformType::Vec4,12),"advanced map UV uniform failed");
      advanced_params=checked(bgfx::createUniform("u_njinAdvanced",bgfx::UniformType::Vec4,3),"advanced material uniform failed");
      transmit_params=checked(bgfx::createUniform("u_njinTransmit",bgfx::UniformType::Vec4,2),"transmission uniform failed");
      rt_shadow_rows=checked(bgfx::createUniform("u_njinRtShadow",bgfx::UniformType::Vec4,9),"ray traced shadow uniform failed");
      coat_params=checked(bgfx::createUniform("u_njinCoat",bgfx::UniformType::Vec4),"clearcoat map uniform failed");
      coat_uv=checked(bgfx::createUniform("u_njinCoatUv",bgfx::UniformType::Vec4,4),"clearcoat map UV uniform failed");
      sky_rows=checked(bgfx::createUniform("u_njinSky",bgfx::UniformType::Vec4,11),"sky uniform failed");
      cover_rows=checked(bgfx::createUniform("u_njinCover",bgfx::UniformType::Vec4,37),"weather cover uniform failed");
      cluster_params=checked(bgfx::createUniform("u_njinClusters",bgfx::UniformType::Vec4,4),"cluster parameters failed");
      cluster_view=checked(bgfx::createUniform("u_njinClusterView",bgfx::UniformType::Mat4),"cluster view failed");
      cluster_vp=checked(bgfx::createUniform("u_njinClusterVP",bgfx::UniformType::Mat4),"cluster view projection failed");
      const char *advanced_names[]{"s_njinHeightMap","s_njinLightmap","s_njinUnderMap","s_njinUnderNormal"};
      for(unsigned i=0;i<4;++i) advanced_samplers[i]=checked(bgfx::createUniform(advanced_names[i],bgfx::UniformType::Sampler),"advanced material sampler failed");
      world_fx=checked(bgfx::createUniform("u_njinWorldFx",bgfx::UniformType::Vec4,7),"world effects uniform failed");
      sprite_fx=checked(bgfx::createUniform("u_njinSpriteFx",bgfx::UniformType::Vec4,4),"sprite effects uniform failed");
      layout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float).add(bgfx::Attrib::TexCoord0,2,bgfx::AttribType::Float).skip(4).add(bgfx::Attrib::Color0,4,bgfx::AttribType::Float).add(bgfx::Attrib::Normal,3,bgfx::AttribType::Float).add(bgfx::Attrib::TexCoord1,2,bgfx::AttribType::Float).add(bgfx::Attrib::Tangent,4,bgfx::AttribType::Float).add(bgfx::Attrib::TexCoord3,2,bgfx::AttribType::Float).end();
      std::fprintf(stderr,"Njin bgfx renderer: %s (SDL3 native window; fallback disabled)\n",bgfx::getRendererName(type));
      const auto culling_diagnostics=BgfxOcclusion::diagnostics();
      std::fprintf(stderr,"%s\n",culling_diagnostics.c_str());
      if(type==bgfx::RendererType::Vulkan) {
        const auto native=vulkan_occlusion_diagnostics();
        std::fprintf(stderr,"%s\n",native.c_str());
#if defined(__ANDROID__)
        __android_log_print(ANDROID_LOG_INFO,"NjinBgfx","%s",native.c_str());
#endif
      }
#if defined(__ANDROID__)
      __android_log_print(ANDROID_LOG_INFO,"NjinBgfx","Renderer: %s (SDL3 native window; fallback disabled)",bgfx::getRendererName(type));
      __android_log_print(ANDROID_LOG_INFO,"NjinBgfx","%s",culling_diagnostics.c_str());
      if(type==bgfx::RendererType::OpenGLES) {
        const auto get_string=reinterpret_cast<PFNGLGETSTRINGPROC>(eglGetProcAddress("glGetString"));
        const auto get_int=reinterpret_cast<PFNGLGETINTEGERVPROC>(eglGetProcAddress("glGetIntegerv"));
        if(get_string&&get_int) {
          __android_log_print(ANDROID_LOG_INFO,"NjinBgfx","GLES vendor=%s GPU=%s version=%s GLSL=%s",get_string(GL_VENDOR),get_string(GL_RENDERER),get_string(GL_VERSION),get_string(GL_SHADING_LANGUAGE_VERSION));
          if(bgfx::getCaps()->supported&BGFX_CAPS_COMPUTE) for(const auto entry:std::array<std::pair<GLenum,const char*>,6>{{
            {GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS,"computeInvocations"},{GL_MAX_COMPUTE_SHARED_MEMORY_SIZE,"sharedMemory"},
            {GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS,"computeStorageBuffers"},{GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS,"storageBufferBindings"},
            {GL_MAX_COMPUTE_IMAGE_UNIFORMS,"computeImages"},{GL_MAX_IMAGE_UNITS,"imageUnits"}}}) {
            GLint value=0;get_int(entry.first,&value);
            __android_log_print(ANDROID_LOG_INFO,"NjinBgfx","GLES %s=%d",entry.second,value);
          }
        }
      }
#endif
    } catch(...) { cleanup(); throw; }
  }
  ~BgfxDevice() override { cleanup(); }
  std::string shader_profile() const override { return shader_format; }
  RenderShaderHandle create_atmosphere_shader() override {
    const auto bytes=bgfx_atmosphere_shader_bytes(shader_format);
    if(bytes.empty()) throw std::runtime_error("njin atmosphere: missing compiled shader profile");
    return create_shader({},bytes).handle;
  }
  bool shader_in_use(RenderShaderHandle handle) const override { return used_shaders.contains(handle.id); }
  ShaderProgramInfo create_shader(std::span<const std::uint8_t> vs,std::span<const std::uint8_t> fs) override {
    const bool own_vertex=!vs.empty();
    if(vs.empty()) {
      vs=bgfx_shader_bytes(shader_format,true);
      if(fs.size()>=8&&vs.size()>=12&&std::memcmp(vs.data()+8,fs.data()+4,4)!=0)
        vs=bgfx_compat_vertex_shader_bytes(shader_format);
    }
    const auto embedded=bgfx_shader_bytes(shader_format,true);
    if(engine_uniforms.empty()) {
      engine_uniforms=shader_binary_uniforms(embedded,'V',embedded[3],shader_format);
      const auto fragment_uniforms=shader_binary_uniforms(bgfx_shader_bytes(shader_format,false),'F',embedded[3],shader_format);
      engine_uniforms.insert(engine_uniforms.end(),fragment_uniforms.begin(),fragment_uniforms.end());
    }
    auto declarations=shader_binary_uniforms(vs,'V',embedded[3],shader_format);
    const auto fragment_declarations=shader_binary_uniforms(fs,'F',embedded[3],shader_format);
    declarations.insert(declarations.end(),fragment_declarations.begin(),fragment_declarations.end());
    for(const auto &uniform:declarations) {
      const auto compatible=[&](const auto &existing) {
        for(const auto &u:existing) if(u.name==uniform.name&&u.type!=uniform.type)
          throw std::invalid_argument("njin shader: conflicting global uniform type: "+u.name);
      };
      compatible(declarations);compatible(engine_uniforms);
      for(const auto &[id,custom]:custom_programs) compatible(custom.declarations);
      if((uniform.name.starts_with("u_njin")||uniform.name.starts_with("s_njin"))&&
         std::none_of(engine_uniforms.begin(),engine_uniforms.end(),[&](const auto &u){return u.name==uniform.name&&u.count>=uniform.count;}))
        throw std::invalid_argument("njin shader: undeclared/resized engine uniform: "+uniform.name);
    }
    // bgfx requires identical declared VS outputs and FS inputs, including unused varyings.
    if(std::memcmp(vs.data()+8,fs.data()+4,4)!=0) throw std::invalid_argument("njin shader: vertex/fragment varying declarations do not match");
    const auto vertex=checked(bgfx::createShader(bgfx::copy(vs.data(),uint32_t(vs.size()))),"custom vertex shader failed");
    const auto fragment=bgfx::createShader(bgfx::copy(fs.data(),uint32_t(fs.size())));
    if(!bgfx::isValid(fragment)) {bgfx::destroy(vertex);throw std::runtime_error("njin shader: custom fragment shader failed");}
    const auto program=bgfx::createProgram(vertex,fragment,false);
    // Keep shader references until reflection is copied. Program owns its own references.
    CustomProgram result{program,{},{},std::move(declarations),own_vertex};
    try {
      if(!bgfx::isValid(program)) throw std::runtime_error("njin shader: custom program link failed");
      for(auto stage:{vertex,fragment}) {
        std::vector<bgfx::UniformHandle> handles(bgfx::getShaderUniforms(stage));
        bgfx::getShaderUniforms(stage,handles.data(),uint16_t(handles.size()));
        for(auto handle:handles) {
          bgfx::UniformInfo info;bgfx::getUniformInfo(handle,info);
          if(result.uniforms.emplace(info.name,handle).second) result.info.push_back({info.name,
            info.type==bgfx::UniformType::Vec4?ShaderUniformInfo::Type::Vec4:info.type==bgfx::UniformType::Sampler?ShaderUniformInfo::Type::Sampler:ShaderUniformInfo::Type::Other,info.num});
        }
      }
      static std::atomic<std::uint64_t> next{1};const auto id=next.fetch_add(1);
      if(!id) throw std::overflow_error("njin shader: identifiers exhausted");
      ShaderProgramInfo info{{id},result.info};
      custom_programs.emplace(id,std::move(result));
      bgfx::destroy(vertex);bgfx::destroy(fragment);
      return info;
    } catch(...) {
      if(bgfx::isValid(program)) bgfx::destroy(program);
      bgfx::destroy(vertex);bgfx::destroy(fragment);throw;
    }
  }
  void destroy_shader(RenderShaderHandle handle) override {
    const auto it=custom_programs.find(handle.id);if(it==custom_programs.end()) return;
    if(used_shaders.contains(handle.id)) throw std::logic_error("njin shader: program is referenced by current frame");
    bgfx::destroy(it->second.program);custom_programs.erase(it);
  }
  std::array<std::uint64_t,3> instance_stats() const override {return instance_counters;}
  std::uint64_t deformed_mesh_uploads() const override {return geometry_count;}
  bool target_in_use(RenderTargetHandle h) const override {return used_targets.contains(h.id);}
  bool texture_in_use(RenderTextureHandle h) const override {return used_textures.contains(h.id);}
  bool mesh_in_use(RenderMeshHandle h) const override {return used_meshes.contains(h.id);}
  bool instancing_available() const override {return bgfx::isValid(instance_program);}
  std::array<unsigned,2> frame_view_size() const override {
    if(!presenting&&targets.contains(screen_id)) {const auto &t=targets.at(screen_id);return {unsigned(t.width),unsigned(t.height)};}
    return {unsigned(width),unsigned(height)};
  }
  bool compute_available() const override {
    return (bgfx::getCaps()->supported&BGFX_CAPS_COMPUTE)!=0&&!bgfx_compute_shader_bytes(shader_format,0).empty();
  }
  bool ray_query_available() const override {return rt_hw!=nullptr;}
  std::uint64_t ray_query_dispatches() const override {return rt_hw?rt_hw->dispatches():0;}
  // Traces the sun's shadow rays into rt_mask[0] (and denoises into rt_mask[1]) in
  // views of their own; the caller has ended its target and resumes it afterwards.
  void trace_shadows(const RayShadowPass &pass) override {
    if(!compute_available()) throw std::runtime_error("njin ray tracing: this bgfx backend has no compute shaders");
    const auto make=[&](unsigned stage) {
      const auto bytes=bgfx_compute_shader_bytes(shader_format,stage);
      const auto shader=checked(bgfx::createShader(bgfx::copy(bytes.data(),uint32_t(bytes.size()))),"ray tracing compute shader failed");
      return checked(bgfx::createProgram(shader,true),"ray tracing compute program failed");
    };
    if(!bgfx::isValid(rt_trace)) {
      rt_trace=make(0);rt_blur=make(1);rt_reflect=make(2);rt_gi=make(3);rt_gi_light=make(4);
      rt_grid_rows=checked(bgfx::createUniform("u_rtGrid",bgfx::UniformType::Vec4,13),"ray tracing uniform failed");
      rt_grid_sampler=checked(bgfx::createUniform("s_rtGrid",bgfx::UniformType::Sampler),"ray tracing sampler failed");
      rt_grid_depth_sampler=checked(bgfx::createUniform("s_rtGridDepth",bgfx::UniformType::Sampler),"ray tracing sampler failed");
      rt_lights=checked(bgfx::createUniform("u_rtLights",bgfx::UniformType::Vec4,26),"ray tracing uniform failed");
      rt_inverse=checked(bgfx::createUniform("u_rtInverse",bgfx::UniformType::Mat4),"ray tracing uniform failed");
      rt_sun=checked(bgfx::createUniform("u_rtSun",bgfx::UniformType::Vec4),"ray tracing uniform failed");
      rt_params=checked(bgfx::createUniform("u_rtParams",bgfx::UniformType::Vec4),"ray tracing uniform failed");
      rt_extra=checked(bgfx::createUniform("u_rtExtra",bgfx::UniformType::Vec4),"ray tracing uniform failed");
      rt_lamps=checked(bgfx::createUniform("u_rtLamps",bgfx::UniformType::Vec4,5),"ray tracing uniform failed");
      rt_depth_sampler=checked(bgfx::createUniform("s_rtDepth",bgfx::UniformType::Sampler),"ray tracing sampler failed");
      rt_mask_sampler=checked(bgfx::createUniform("s_rtMask",bgfx::UniformType::Sampler),"ray tracing sampler failed");
    }
    if(rt_width!=pass.width||rt_height!=pass.height||!bgfx::isValid(rt_mask[0])) {
      for(auto &t:rt_mask) {if(bgfx::isValid(t)) bgfx::destroy(t);t=BGFX_INVALID_HANDLE;}
      // The view three times: the shadows, the reflections, the grid's light.
      for(auto &t:rt_mask) t=checked(bgfx::createTexture2D(uint16_t(pass.width),uint16_t(pass.height*3),false,1,bgfx::TextureFormat::RGBA16F,
        BGFX_TEXTURE_COMPUTE_WRITE|BGFX_SAMPLER_POINT|BGFX_SAMPLER_UVW_CLAMP),"ray traced shadow mask failed");
      rt_width=pass.width;rt_height=pass.height;
    }
    bgfx::VertexLayout layout;layout.begin().add(bgfx::Attrib::TexCoord0,4,bgfx::AttribType::Float).end();
    const auto upload=[&](bgfx::DynamicVertexBufferHandle &buffer,const std::vector<std::array<float,4>> &data) {
      const auto count=uint32_t(std::max<size_t>(data.size(),1));
      if(!bgfx::isValid(buffer)) buffer=checked(bgfx::createDynamicVertexBuffer(count,layout,BGFX_BUFFER_COMPUTE_READ|BGFX_BUFFER_ALLOW_RESIZE),"ray tracing buffer failed");
      static const std::array<float,4> empty{};
      bgfx::update(buffer,0,data.empty()?bgfx::copy(&empty,sizeof(empty)):bgfx::copy(data.data(),uint32_t(data.size()*sizeof(data[0]))));
    };
    const bool empty=!pass.nodes||!pass.triangles||pass.nodes->empty()||pass.triangles->empty();
    if(!bgfx::isValid(rt_nodes)||rt_uploaded!=pass.nodes.get()) {
      upload(rt_nodes,empty?std::vector<std::array<float,4>>{}:*pass.nodes);
      upload(rt_tris,empty?std::vector<std::array<float,4>>{}:*pass.triangles);
      rt_uploaded=pass.nodes.get();
    }
    // u_rtLamps: three lamps (position, size), then eye xyz and the sun flag; count, skip.
    const std::array<std::array<float,4>,5> lamp_rows{{pass.lamps[0],pass.lamps[1],pass.lamps[2],pass.eye,
      {float(std::min(pass.lamp_count,3u)),pass.skip,0,0}}};
    const float params[]{float(pass.width),float(pass.height),float(std::clamp(pass.rays,1u,16u)),float(pass.frame%4096)};
    const float extra[]{pass.bias,std::clamp(pass.denoise,0.f,8.f),empty?1.f:0.f,pass.max_distance};
    const auto groups_x=uint32_t((pass.width+7)/8),groups_y=uint32_t((pass.height+7)/8);
    const auto depth=textures.at(pass.depth.id);
    const auto bind_common=[&] {
      bgfx::setUniform(rt_inverse,pass.inverse_view_projection.data());bgfx::setUniform(rt_sun,pass.sun.data());
      bgfx::setUniform(rt_params,params);bgfx::setUniform(rt_extra,extra);bgfx::setUniform(rt_lamps,lamp_rows.data(),5);
      bgfx::setTexture(1,rt_depth_sampler,depth,BGFX_SAMPLER_POINT|BGFX_SAMPLER_UVW_CLAMP);
    };
    // The grid (a new or reset one starts from zero: no point counts yet).
    const unsigned points=pass.grid?unsigned(pass.grid_rows[2][0]*pass.grid_rows[2][1]*pass.grid_rows[2][2]):1;
    const std::uint64_t grid_key=pass.grid?pass.grid_key:~1ull;
    if(grid_key!=rt_grid_key||!bgfx::isValid(rt_grid[0])) {
      for(auto &t:rt_grid) {if(bgfx::isValid(t)) bgfx::destroy(t);t=BGFX_INVALID_HANDLE;}
      for(auto &t:rt_grid_depth) {if(bgfx::isValid(t)) bgfx::destroy(t);t=BGFX_INVALID_HANDLE;}
      const unsigned tiles_x=std::min(points,256u),tiles_y=(points+255)/256;
      const std::vector<float> zero_light(size_t(points)*6*4,0.f),zero_depth(size_t(tiles_x)*16*tiles_y*16*4,0.f);
      for(auto &t:rt_grid) t=checked(bgfx::createTexture2D(uint16_t(points),6,false,1,bgfx::TextureFormat::RGBA32F,
        BGFX_TEXTURE_COMPUTE_WRITE|BGFX_SAMPLER_POINT|BGFX_SAMPLER_UVW_CLAMP,bgfx::copy(zero_light.data(),uint32_t(zero_light.size()*sizeof(float)))),"ray traced grid failed");
      for(auto &t:rt_grid_depth) t=checked(bgfx::createTexture2D(uint16_t(tiles_x*16),uint16_t(tiles_y*16),false,1,bgfx::TextureFormat::RGBA32F,
        BGFX_TEXTURE_COMPUTE_WRITE|BGFX_SAMPLER_POINT|BGFX_SAMPLER_UVW_CLAMP,bgfx::copy(zero_depth.data(),uint32_t(zero_depth.size()*sizeof(float)))),"ray traced grid failed");
      rt_grid_key=grid_key;rt_grid_points=points;rt_grid_latest=0;
    }
    auto grid_rows=pass.grid_rows;
    if(!pass.grid) grid_rows[3][0]=0;
    const auto bind_grid=[&](int index) {
      bgfx::setUniform(rt_grid_rows,grid_rows.data(),13);
      bgfx::setTexture(5,rt_grid_sampler,rt_grid[size_t(index)],BGFX_SAMPLER_POINT|BGFX_SAMPLER_UVW_CLAMP);
      bgfx::setTexture(6,rt_grid_depth_sampler,rt_grid_depth[size_t(index)],BGFX_SAMPLER_POINT|BGFX_SAMPLER_UVW_CLAMP);
    };
    const bool shading=pass.materials&&(pass.reflection_roughness>0||pass.grid);
    if(shading) upload(rt_mats,pass.materials->empty()?std::vector<std::array<float,4>>{}:*pass.materials);
    // Ray queries: the same dispatches, handed to the hardware kernels when bgfx records them.
    if(rt_hw) {
      rt_hw->begin_frame();
      rt_hw->scene(empty?nullptr:pass.triangles);
      if(shading) rt_hw->materials(*pass.materials);
    }
    const auto hardware=[&](bgfx::ProgramHandle program,unsigned stage,std::array<std::uint16_t,2> images,int grid_index) {
      if(!rt_hw||empty) return;
      VulkanRayQuery::Dispatch d;
      d.program=program.idx;d.stage=stage;d.images=images;d.depth=depth.idx;
      if(grid_index>=0) {d.grid=rt_grid[size_t(grid_index)].idx;d.grid_depth=rt_grid_depth[size_t(grid_index)].idx;}
      auto &u=d.uniforms;
      std::copy_n(pass.inverse_view_projection.data(),16,u.inverse.data());
      u.sun=pass.sun;std::copy_n(params,4,u.params.data());std::copy_n(extra,4,u.extra.data());
      u.lamps=lamp_rows;u.lights=pass.lights;u.grid=grid_rows;
      rt_hw->queue(d);
    };
    if(pass.grid) {
      // One workgroup a point: last frame's grid in, this frame's out.
      const int read=rt_grid_latest,write=1-rt_grid_latest;
      next_view(0,{},false);
      bind_common();bind_grid(read);
      bgfx::setUniform(rt_lights,pass.lights.data(),26);
      bgfx::setBuffer(2,rt_nodes,bgfx::Access::Read);bgfx::setBuffer(3,rt_tris,bgfx::Access::Read);bgfx::setBuffer(4,rt_mats,bgfx::Access::Read);
      bgfx::setImage(0,rt_grid[size_t(write)],0,bgfx::Access::Write,bgfx::TextureFormat::RGBA32F);
      bgfx::setImage(1,rt_grid_depth[size_t(write)],0,bgfx::Access::Write,bgfx::TextureFormat::RGBA32F);
      hardware(rt_gi,3,{rt_grid[size_t(write)].idx,rt_grid_depth[size_t(write)].idx},read);
      bgfx::dispatch(view,rt_gi,points,1,1);
      rt_grid_latest=write;
      grid_rows[3][0]=1;
    }
    rt_latest=0;
    if(pass.shadows) {
      next_view(0,{},false);
      bind_common();
      bgfx::setBuffer(2,rt_nodes,bgfx::Access::Read);bgfx::setBuffer(3,rt_tris,bgfx::Access::Read);
      bgfx::setImage(0,rt_mask[0],0,bgfx::Access::Write,bgfx::TextureFormat::RGBA16F);
      hardware(rt_trace,0,{rt_mask[0].idx,0xffff},-1);
      bgfx::dispatch(view,rt_trace,groups_x,groups_y,1);
      if(pass.denoise>0) {
        next_view(0,{},false);
        bind_common();
        bgfx::setTexture(2,rt_mask_sampler,rt_mask[0],BGFX_SAMPLER_POINT|BGFX_SAMPLER_UVW_CLAMP);
        bgfx::setImage(0,rt_mask[1],0,bgfx::Access::Write,bgfx::TextureFormat::RGBA16F);
        bgfx::dispatch(view,rt_blur,groups_x,groups_y,1);
        rt_latest=1;
      }
    }
    // light3d::ray_traced_reflections: into the lower half of the image the draws read.
    if(pass.reflection_roughness>0&&pass.materials) {
      next_view(0,{},false);
      bind_common();bind_grid(rt_grid_latest);
      bgfx::setUniform(rt_lights,pass.lights.data(),26);
      bgfx::setBuffer(2,rt_nodes,bgfx::Access::Read);bgfx::setBuffer(3,rt_tris,bgfx::Access::Read);bgfx::setBuffer(4,rt_mats,bgfx::Access::Read);
      bgfx::setImage(0,rt_mask[size_t(rt_latest)],0,bgfx::Access::Write,bgfx::TextureFormat::RGBA16F);
      hardware(rt_reflect,2,{rt_mask[size_t(rt_latest)].idx,0xffff},rt_grid_latest);
      bgfx::dispatch(view,rt_reflect,groups_x,groups_y,1);
    }
    // The grid's light at each pixel, into the third part.
    if(pass.grid) {
      next_view(0,{},false);
      bind_common();bind_grid(rt_grid_latest);
      bgfx::setImage(0,rt_mask[size_t(rt_latest)],0,bgfx::Access::Write,bgfx::TextureFormat::RGBA16F);
      bgfx::dispatch(view,rt_gi_light,groups_x,groups_y,1);
    }
    const auto &m=pass.view_projection;
    rt_frame={{{m[0],m[4],m[8],m[12]},{m[1],m[5],m[9],m[13]},{m[2],m[6],m[10],m[14]},{m[3],m[7],m[11],m[15]},
               {pass.shadows?1.f:0.f,1.f/float(pass.width),1.f/float(pass.height*3),pass.materials?pass.reflection_roughness:0.f},
               {pass.grid?1.f:0.f,0,0,0}}};
    rt_frame_serial=frame_serial();
  }
  void begin_frame(rgba clear) override {
    if(frame_active) throw std::logic_error("njin bgfx: nested frame");
    for(auto it=persistent_uploads.begin();it!=persistent_uploads.end();) {
      if(it->second.cpu.expired()) {bgfx::destroy(it->second.gpu);it=persistent_uploads.erase(it);}else ++it;
    }
    unsigned w,h; platform.drawable_size(w,h);
    const auto native_window=platform.native_handle();
    const auto native_display=platform.native_display();
    // Keep a valid back buffer while minimized; SDL retains pending capture.
    if(w && h && native_window && (w!=width || h!=height || vsync!=platform.vsync() ||
       native_window!=swapchain.nwh || native_display!=swapchain.ndt)) {
      width=w; height=h; vsync=platform.vsync(); swapchain.width=w; swapchain.height=h;
      // Android may replace ANativeWindow on resume without changing its size.
      // bgfx resets the swapchain/surface while retaining device resources.
      swapchain.nwh=native_window; swapchain.ndt=native_display;
      bgfx::reset(vsync ? BGFX_RESET_VSYNC:BGFX_RESET_NONE,&swapchain);
    }
    if(!targets.contains(screen_id)||targets.at(screen_id).width!=width||targets.at(screen_id).height!=height) {
      if(targets.contains(screen_id)) destroy_target({screen_id},{screen_id});
      create_target({screen_id},{screen_id},width,height);
    }
    occlusion.begin_frame();
    presenting=false;view=0; configure_view(0,clear,true); frame_active=true;++frame_counter;
  }
  void end_frame(RenderImageAsset *capture) override {
    require_frame();
    if(active_target) throw std::logic_error("njin bgfx: target must end before frame");
    // A sampled window surface makes backdrop effects possible without a CPU
    // readback, and keeps the same source available on all bgfx backends.
    presenting=true;next_view(0,{},false);
    draw_copy({screen_id});
    callback.captured={}; callback.capture_error.clear();
    // The pinned D3D12 backend reads the last presented swapchain image
    // (m_backBufferColorIdx - 1). Present this submission before requesting
    // its screenshot, so Njin captures the requested frame on every backend.
    if(capture && bgfx::getRendererType()==bgfx::RendererType::Direct3D12) bgfx::frame();
    if(capture) bgfx::requestScreenShot(BGFX_INVALID_HANDLE,"njin-engine-capture");
    submitted_frame=bgfx::frame();
    if(capture) {
      for(int i=0;i<8 && callback.captured.pixels.empty() && callback.capture_error.empty();++i) bgfx::frame();
      if(!callback.capture_error.empty() || callback.captured.pixels.empty()) throw std::runtime_error("njin bgfx: screenshot callback failed: "+callback.capture_error);
      *capture=std::move(callback.captured);
    }
    clear_geometry();clear_instances();
    used_textures.clear(); used_meshes.clear(); used_targets.clear(); used_shaders.clear(); frame_active=false;
  }
  std::uint32_t submitted_frame_number() const override { return submitted_frame; }
  void ensure_texture(RenderTextureHandle handle,const RenderImage &image) override {
    if(textures.contains(handle.id)) return;
    const ImageMipChain chain(image);
    ensure_texture_prepared(handle,image,chain.pixels,chain.levels);
  }
  void ensure_texture_prepared(RenderTextureHandle handle,const RenderImage &image,std::span<const std::uint8_t> pixels,unsigned levels) override {
    if(textures.contains(handle.id)) return;
    if(image.width>max_texture_size()||image.height>max_texture_size()) throw std::invalid_argument("njin bgfx: map exceeds device limits");
    const auto format=image.format==RenderImageFormat::RGBA32F?bgfx::TextureFormat::RGBA32F:image.format==RenderImageFormat::RGBA16F?bgfx::TextureFormat::RGBA16F:bgfx::TextureFormat::RGBA8;
    if(!bgfx::isTextureValid(0,false,1,format,0)) throw std::runtime_error("njin bgfx: requested texture format is unavailable");
    const auto texture=checked(bgfx::createTexture2D(uint16_t(image.width),uint16_t(image.height),levels>1,1,format,0,
      bgfx::copy(pixels.data(),uint32_t(pixels.size()))),"material texture allocation failed");
    try { textures.emplace(handle.id,texture); } catch(...) { bgfx::destroy(texture); throw; }
    ++texture_count;
  }
  void ensure_texture_writable(RenderTextureHandle handle,const RenderImage &image) override {
    if(textures.contains(handle.id)) return;
    if(image.format!=RenderImageFormat::RGBA8||image.mips!=RenderMipMode::None) throw std::invalid_argument("njin: writable page atlas requires RGBA8 without hardware mips");
    const auto texture=checked(bgfx::createTexture2D(uint16_t(image.width),uint16_t(image.height),false,1,bgfx::TextureFormat::RGBA8,0),"writable page atlas allocation failed");
    try {textures.emplace(handle.id,texture);}catch(...) {bgfx::destroy(texture);throw;}
    bgfx::updateTexture2D(texture,0,0,0,0,uint16_t(image.width),uint16_t(image.height),bgfx::copy(image.rgba.data(),uint32_t(image.rgba.size())));
    ++texture_count;
  }
  void virtual_texture_shader(std::span<const std::uint8_t> fragment) override {virtual_fragment=fragment;}
  void update_texture_region(RenderTextureHandle handle,unsigned x,unsigned y,unsigned w,unsigned h,std::span<const std::uint8_t> pixels) override {
    if(frame_active||used_textures.contains(handle.id)) throw std::logic_error("njin: page upload requires frame boundary");
    if(pixels.size()!=size_t(w)*h*4||!w||!h) throw std::invalid_argument("njin: invalid RGBA page");
    bgfx::updateTexture2D(textures.at(handle.id),0,0,uint16_t(x),uint16_t(y),uint16_t(w),uint16_t(h),bgfx::copy(pixels.data(),uint32_t(pixels.size())));
  }
  void draw_copy(RenderTextureHandle source) {
    const RenderVertex quad[]{{-1,1,0,0,0},{1,1,0,1,0},{1,-1,0,1,1},{-1,1,0,0,0},{1,-1,0,1,1},{-1,-1,0,0,1}};
    MeshDraw packet{};packet.vertices=quad;packet.texture=source;packet.depth_test=false;
    packet.depth_write=false;packet.blend=RenderBlend::Opaque;packet.opaque_alpha=true;
    packet.linear_sampling=false;packet.wrap_s=packet.wrap_t=RenderWrap::Clamp;
    draw(packet);
  }
  void copy_frame(RenderTargetHandle destination) override {
    require_frame();if(destination.id==active_target||destination.id==screen_id) throw std::logic_error("njin bgfx: snapshot feedback");
    const auto previous=active_target;
    const auto source=targets.at(previous?previous:screen_id).color;
    next_view(destination.id,{},false);active_target=destination.id;used_targets.insert(destination.id);
    try {draw_copy(source);targets.at(destination.id).rendered=true;}
    catch(...) {active_target=previous;next_view(previous,{},false);throw;}
    active_target=previous;next_view(previous,{},false);
  }
  void replace_texture(RenderTextureHandle handle,const RenderImage &image) override {
    const auto it=textures.find(handle.id);if(it==textures.end()) return; // still CPU-only
    const ImageMipChain chain(image);
    const auto format=image.format==RenderImageFormat::RGBA32F?bgfx::TextureFormat::RGBA32F:image.format==RenderImageFormat::RGBA16F?bgfx::TextureFormat::RGBA16F:bgfx::TextureFormat::RGBA8;
    const auto next=checked(bgfx::createTexture2D(uint16_t(image.width),uint16_t(image.height),chain.levels>1,1,format,0,
      bgfx::copy(chain.pixels.data(),uint32_t(chain.pixels.size()))),"texture replacement failed");
    const auto old=it->second;it->second=next;++texture_count;
    bgfx::destroy(old); // bgfx defers destruction past draws already submitted with old
  }
  RenderImageAsset read_target(RenderTargetHandle handle) override {
    const auto &target=targets.at(handle.id);
    RenderImageAsset result{target.width,target.height,{},target.format==bgfx::TextureFormat::RGBA16F?RenderImageFormat::RGBA16F:RenderImageFormat::RGBA8};
    result.pixels.resize(size_t(target.width)*target.height*(result.format==RenderImageFormat::RGBA16F?8:4));
    const auto flags=BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK;
    if(!bgfx::isTextureValid(0,false,1,target.format,flags)) throw std::runtime_error("njin bgfx: target readback format is unsupported");
    const auto staging=checked(bgfx::createTexture2D(uint16_t(target.width),uint16_t(target.height),false,1,target.format,flags),"readback staging allocation failed");
    bool released=false;
    try {
      const auto copy_view=bgfx::ViewId(bgfx::getCaps()->limits.maxViews-1);
      bgfx::TextureRegion destination,source;destination.init(staging);source.init(textures.at(target.color.id));
      bgfx::blit(copy_view,destination,source);
      const auto ready=bgfx::read(destination,result.pixels.data());
      bool complete=false;
      for(unsigned i=0;i<32;++i) if(int32_t(bgfx::frame(BGFX_FRAME_FLUSH)-ready)>=0) {complete=true;break;}
      if(!complete) throw std::runtime_error("njin bgfx: target readback did not complete");
      bgfx::destroy(staging);released=true;
      if(frame_active) {view=0;configure_view(active_target,{},false);used_targets.insert(handle.id);}
      return result;
    } catch(...) {if(!released) bgfx::destroy(staging);if(frame_active) {view=0;configure_view(active_target,{},false);}throw;}
  }
  std::uint64_t begin_readback(RenderTargetHandle handle) override {
    require_frame();
    const auto &target=targets.at(handle.id);
    PendingRead read;
    read.image={target.width,target.height,{},target.format==bgfx::TextureFormat::RGBA16F?RenderImageFormat::RGBA16F:RenderImageFormat::RGBA8};
    read.image.pixels.resize(size_t(target.width)*target.height*(read.image.format==RenderImageFormat::RGBA16F?8:4));
    const auto flags=BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK;
    if(!bgfx::isTextureValid(0,false,1,target.format,flags)) throw std::runtime_error("njin bgfx: target readback format is unsupported");
    read.staging=checked(bgfx::createTexture2D(uint16_t(target.width),uint16_t(target.height),false,1,target.format,flags),"readback staging allocation failed");
    const auto id=next_read++;
    auto &stored=pending_reads.emplace(id,std::move(read)).first->second;
    // After every render view of the frame (the last view), so the target is complete.
    bgfx::TextureRegion destination,source;destination.init(stored.staging);source.init(textures.at(target.color.id));
    bgfx::blit(bgfx::ViewId(bgfx::getCaps()->limits.maxViews-1),destination,source);
    stored.ready=bgfx::read(destination,stored.image.pixels.data());
    used_targets.insert(handle.id);
    return id;
  }
  bool finish_readback(std::uint64_t id,RenderImageAsset &out) override {
    const auto it=pending_reads.find(id);
    if(it==pending_reads.end()) throw std::invalid_argument("njin bgfx: unknown readback");
    if(submitted_frame==UINT32_MAX||int32_t(submitted_frame-it->second.ready)<0) return false;
    out=std::move(it->second.image);
    bgfx::destroy(it->second.staging);
    pending_reads.erase(it);
    return true;
  }
  static uint32_t sampling(bool linear,RenderWrap s,RenderWrap t,RenderMinFilter min=RenderMinFilter::Inherit) {
    uint32_t flags=linear?0:BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT;
    if(min!=RenderMinFilter::Inherit) {
      flags&=~(BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MIP_POINT);
      if(min==RenderMinFilter::Nearest||min==RenderMinFilter::NearestMipNearest||min==RenderMinFilter::NearestMipLinear) flags|=BGFX_SAMPLER_MIN_POINT;
      if(min==RenderMinFilter::NearestMipNearest||min==RenderMinFilter::LinearMipNearest) flags|=BGFX_SAMPLER_MIP_POINT;
    }
    if(s==RenderWrap::Clamp) flags|=BGFX_SAMPLER_U_CLAMP; else if(s==RenderWrap::Mirror) flags|=BGFX_SAMPLER_U_MIRROR;
    if(t==RenderWrap::Clamp) flags|=BGFX_SAMPLER_V_CLAMP; else if(t==RenderWrap::Mirror) flags|=BGFX_SAMPLER_V_MIRROR;
    return flags;
  }
  void draw(const MeshDraw &draw) override {
    require_frame();
    BgfxOcclusion::Job *culled=nullptr;
    if(draw.gpu_cull||(draw.instance_occlusion_bounds[3]>0&&!draw.shadow_pass&&!draw.normal_pass&&!draw.velocity_pass&&!draw.shadow_only&&draw.post.mode==RenderPostMode::None)) {
      culled=&occlusion.compact(draw,view,shader_format);
      configure_view(active_target,{},false);
    }
    if(draw.shader && !draw.shadow_pass && !draw.normal_pass && !draw.velocity_pass) {
      const auto it=custom_programs.find(draw.shader->program.id);
      if(it==custom_programs.end()) throw std::invalid_argument("njin shader: invalid or foreign program");
      for(const auto &u:draw.shader->uniforms) {
        const auto info=std::find_if(it->second.info.begin(),it->second.info.end(),[&](const auto &v){return v.name==u.name;});
        if(info==it->second.info.end()||info->type!=ShaderUniformInfo::Type::Vec4||u.values.empty()||u.values.size()>info->count)
          throw std::invalid_argument("njin shader: invalid uniform binding");
        for(const auto &v:u.values) for(float c:v) if(!std::isfinite(c)) throw std::invalid_argument("njin shader: non-finite uniform binding");
      }
      std::unordered_set<unsigned> stages;
      for(const auto &b:draw.shader->textures) {
        const auto info=std::find_if(it->second.info.begin(),it->second.info.end(),[&](const auto &v){return v.name==b.name;});
        if(b.stage>=bgfx::getCaps()->limits.maxTextureSamplers||!stages.insert(b.stage).second||info==it->second.info.end()||info->type!=ShaderUniformInfo::Type::Sampler)
          throw std::invalid_argument("njin shader: invalid sampler binding");
      }
    }
    if(draw.image.width>bgfx::getCaps()->limits.maxTextureSize || draw.image.height>bgfx::getCaps()->limits.maxTextureSize)
      throw std::invalid_argument("njin bgfx: texture exceeds device limits");
    bgfx::TextureHandle texture=BGFX_INVALID_HANDLE;
    bool temporary_texture=!draw.texture;
    if(draw.texture && textures.contains(draw.texture.id)) texture=textures.at(draw.texture.id);
    else {
      const ImageMipChain chain(draw.image);
      texture=checked(bgfx::createTexture2D((uint16_t)draw.image.width,(uint16_t)draw.image.height,chain.levels>1,1,
        draw.image.format==RenderImageFormat::RGBA32F?bgfx::TextureFormat::RGBA32F:draw.image.format==RenderImageFormat::RGBA16F?bgfx::TextureFormat::RGBA16F:bgfx::TextureFormat::RGBA8,0,bgfx::copy(chain.pixels.data(),(uint32_t)chain.pixels.size())),"texture allocation failed");
      ++texture_count;
      if(draw.texture) { try { textures.emplace(draw.texture.id,texture); } catch(...) { bgfx::destroy(texture); throw; } }
    }
    // Target and texture IDs are separate namespaces. Their ownership and
    // initialization were already validated by renderer_draw_mesh; indexing
    // targets by a texture ID could reject an unrelated ordinary texture.
    Mesh mesh;
    bool mesh_owned=false;
    try {
      if(draw.mesh && meshes.contains(draw.mesh.id)) mesh=meshes.at(draw.mesh.id);
      else if(draw.geometry && geometry_uploads.contains(draw.geometry.get())) mesh=geometry_uploads.at(draw.geometry.get()).gpu;
      else {
        mesh.vertices=checked(bgfx::createVertexBuffer(bgfx::copy(draw.vertices.data(),(uint32_t)draw.vertices.size_bytes()),layout),"vertex buffer allocation failed");
        mesh_owned=true;
        if(!draw.indices.empty()) mesh.indices=checked(bgfx::createIndexBuffer(bgfx::copy(draw.indices.data(),(uint32_t)draw.indices.size_bytes())),"index buffer allocation failed");
        if(draw.mesh) { meshes.emplace(draw.mesh.id,mesh); mesh_owned=false; ++mesh_count; }
      }
      if(draw.geometry && mesh_owned) {
        geometry_uploads.emplace(draw.geometry.get(),GeometryUpload{draw.geometry,mesh});mesh_owned=false;++geometry_count;
      }
      bgfx::setVertexBuffer(0,mesh.vertices);
      if(bgfx::isValid(mesh.indices)) bgfx::setIndexBuffer(mesh.indices);
      uint64_t state=BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_MSAA|
        BGFX_STATE_BLEND_FUNC_SEPARATE(BGFX_STATE_BLEND_SRC_ALPHA,BGFX_STATE_BLEND_INV_SRC_ALPHA,BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA);
      if(draw.depth_test) {state|=BGFX_STATE_DEPTH_TEST_LEQUAL;if(draw.depth_write) state|=BGFX_STATE_WRITE_Z;}
      if(draw.blend!=RenderBlend::Alpha) {
        state&=~BGFX_STATE_BLEND_MASK;
        if(draw.blend==RenderBlend::WeightedAdd) state|=BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_ONE);
        else if(draw.blend==RenderBlend::Reveal) state|=BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ZERO,BGFX_STATE_BLEND_INV_SRC_ALPHA);
        else if(draw.blend==RenderBlend::Premultiplied) state|=BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA);
        else if(draw.blend!=RenderBlend::Opaque) state|=draw.blend==RenderBlend::Additive ?
          BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,BGFX_STATE_BLEND_ONE):
          BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR,BGFX_STATE_BLEND_INV_SRC_ALPHA);
      }
      if(draw.cull!=RenderCull::None) {
        bool clockwise=draw.cull==RenderCull::Clockwise;
        if((active_target||!presenting) && bgfx::getCaps()->originBottomLeft) clockwise=!clockwise;
        state|=clockwise ? BGFX_STATE_CULL_CW:BGFX_STATE_CULL_CCW;
      }
      bgfx::setState(state);
      if(draw.scissor_enabled) {
        const auto &clip=draw.scissor;
        float logical_w,logical_h; platform.size(logical_w,logical_h);
        const auto *target=active_target ? &targets.at(active_target):nullptr;
        const double sx=target ? 1.0:width/std::max(logical_w,1.0f),sy=target ? 1.0:height/std::max(logical_h,1.0f);
        const int limit_w=target ? (int)target->width:(int)width,limit_h=target ? (int)target->height:(int)height;
        const int x=(int)std::clamp(std::floor(clip.x*sx),0.0,double(limit_w));
        int y=(int)std::clamp(std::floor(clip.y*sy),0.0,double(limit_h));
        const int right=(int)std::clamp(std::ceil((double(clip.x)+clip.width)*sx),0.0,double(limit_w));
        const int bottom=(int)std::clamp(std::ceil((double(clip.y)+clip.height)*sy),0.0,double(limit_h));
        const int w=std::max(0,right-x),h=std::max(0,bottom-y);
        if((target||!presenting) && bgfx::getCaps()->originBottomLeft) y=limit_h-y-h;
        bgfx::setScissor((uint16_t)x,(uint16_t)y,(uint16_t)w,(uint16_t)h);
      } else bgfx::setScissor(UINT16_MAX);
      const auto &effect=draw.sprite_effects;
      const float effect_values[]{effect.flash[0],effect.flash[1],effect.flash[2],effect.flash[3],
        effect.edge[0],effect.edge[1],effect.edge[2],effect.edge[3],
        effect.dissolve[0],effect.dissolve[1],effect.dissolve[2],effect.dissolve[3],
        effect.dissolving?1.0f:0.0f,float(draw.image.width),float(draw.image.height),0};
      bgfx::setUniform(sprite_fx,effect_values,4);
      const auto &wf=draw.world_effects;
      const std::array<std::array<float,4>,7> world_values{wf.flash,wf.edge,wf.dissolve,wf.from,wf.to,wf.mask,wf.camera};
      bgfx::setUniform(world_fx,world_values.data(),7);
      bgfx::setUniform(world_style,draw.world_style.data(),6);
      if(draw.world_style[0][0]==1) {
        std::array<std::array<float,4>,3> imprint{};
        if(draw.terrain_imprint) imprint={draw.terrain_imprint->window,draw.terrain_imprint->relief,draw.terrain_imprint->tint};
        bgfx::setUniform(terrain_imprint,imprint.data(),3);
      }
      if(draw.world_style[0][0]==3) {
        const RenderWaterEffects empty{};
        const auto &water=draw.water?*draw.water:empty;
        std::array<std::array<float,4>,16> data{};
        std::copy(water.parameters.begin(),water.parameters.end(),data.begin());
        data[13]=water.absorption;data[14]=water.shallow_color;data[15]=water.deep_color;
        bgfx::setUniform(water_effects,data.data(),16);
      }
      bgfx::setUniform(surface_fx,draw.surface_effects.data(),4);
      bgfx::setUniform(legacy_post,draw.post.legacy.data(),10);
      bgfx::setUniform(particle_fx,draw.particle_effects.data());
      if(draw.light_edges) bgfx::setUniform(light_edges,draw.light_edges->data(),64);
      bgfx::setUniform(mvp,draw.transform.data()); bgfx::setUniform(tint,draw.tint.data());
      const float p[4]{bgfx::getCaps()->homogeneousDepth ? 0.0f:1.0f,
        (active_target||!presenting) && bgfx::getCaps()->originBottomLeft ? -1.0f:1.0f,draw.alpha_cutoff,draw.opaque_alpha ? 1.0f:0.0f};
      bgfx::setUniform(params,p);
      const float glow[]{draw.emission[0],draw.emission[1],draw.emission[2],draw.emission_strength};
      bgfx::setUniform(emission,glow);
      bgfx::setUniform(world,draw.world.data()); bgfx::setUniform(normal,draw.normal_transform.data());
      const auto &l=draw.lighting;
      const float lighting[16]{l.direction[0],l.direction[1],l.direction[2],l.enabled?1.0f:0.0f,
        l.color[0],l.color[1],l.color[2],l.specular,l.ambient[0],l.ambient[1],l.ambient[2],l.shininess,
        l.camera[0],l.camera[1],l.camera[2],l.intensity};
      bgfx::setUniform(light,lighting,4);
      const float mat[]{l.metallic,l.roughness,l.pbr?1.0f:0.0f,float(draw.tonemap)};
      const float out[]{draw.exposure,draw.linear_output?1.0f:0.0f,draw.resolve?1.0f:0.0f,float(draw.local_light_count)};
      std::array<float,256> locals{};
      for(unsigned i=0;i<draw.local_light_count;++i) {
        const auto &a=draw.local_lights[i]; const unsigned k=i*16;
        const float values[]{a.position[0],a.position[1],a.position[2],a.radius,
          a.color[0],a.color[1],a.color[2],a.intensity,a.direction[0],a.direction[1],a.direction[2],a.cos_outer,
          a.cos_inner,a.spot?1.0f:0.0f,draw.shadows?float(draw.shadows->local_base[i]+1):0.0f,draw.shadows?float(draw.shadows->ray_channel[i]):0.0f};
        std::copy_n(values,16,locals.begin()+k);
      }
      bgfx::setUniform(local,locals.data(),64);
      bgfx::setUniform(motion_current,draw.motion_current.data());bgfx::setUniform(motion_previous,draw.motion_previous.data());bgfx::setUniform(motion_view,draw.motion_view.data());
      bgfx::setUniform(temporal_params,draw.post.temporal.data());bgfx::setUniform(previous_clip,draw.post.previous_clip.data());bgfx::setUniform(previous_view,draw.post.previous_view.data());
      const auto post_depth=draw.post.depth?draw.post.depth:draw.opaque_depth;
      const auto cluster_texture=draw.clusters?draw.clusters->texture:RenderTextureHandle{};
      bgfx::setTexture(11,velocity_sampler,cluster_texture?textures.at(cluster_texture.id):draw.post.velocity?textures.at(draw.post.velocity.id):texture,sampling(false,RenderWrap::Clamp,RenderWrap::Clamp));
      const std::array<std::array<float,4>,4> no_clusters{};
      bgfx::setUniform(cluster_params,draw.clusters?draw.clusters->parameters.data():no_clusters.data(),4);
      bgfx::setUniform(cluster_view,draw.clusters?draw.clusters->view.data():draw.view.data());
      bgfx::setUniform(cluster_vp,draw.clusters?draw.clusters->view_projection.data():draw.transform.data());
      if(cluster_texture) used_textures.insert(cluster_texture.id);
      if(draw.post.velocity) used_textures.insert(draw.post.velocity.id);
      const float transparency_values[]{float(draw.transparency_pass),0,0,0};
      bgfx::setUniform(transparency,transparency_values);bgfx::setUniform(transparency_vp,draw.transparency_projection.data());
      // Opaque draws of a ray traced frame read the sun's mask on sampler 10 (validated free).
      const bool ray_shadow=draw.ray_shadow&&rt_latest>=0&&rt_frame_serial==frame_serial()&&bgfx::isValid(rt_mask[size_t(rt_latest)]);
      // A captured irradiance grid takes sampler 10 when the traced mask and the post depth do not.
      const bool captured_grid=!ray_shadow&&!post_depth&&draw.irradiance_grid&&draw.irradiance_grid->texture&&!draw.transparent;
      if(ray_shadow) bgfx::setTexture(10,opaque_depth,rt_mask[size_t(rt_latest)],BGFX_SAMPLER_POINT|BGFX_SAMPLER_UVW_CLAMP);
      else if(captured_grid) {
        bgfx::setTexture(10,opaque_depth,textures.at(draw.irradiance_grid->texture.id),BGFX_SAMPLER_POINT|BGFX_SAMPLER_UVW_CLAMP);
        used_textures.insert(draw.irradiance_grid->texture.id);
      }
      else bgfx::setTexture(10,opaque_depth,post_depth?textures.at(post_depth.id):texture,sampling(false,RenderWrap::Clamp,RenderWrap::Clamp));
      {
        std::array<std::array<float,4>,9> rows{};
        std::copy(rt_frame.begin(),rt_frame.end(),rows.begin());
        if(!ray_shadow) rows[4][0]=rows[4][3]=rows[5][0]=0;
        // Row 5 x: 1 the traced grid's light in the ray traced image, 2 a captured grid on
        // sampler 10, placed by rows 6..8.
        if(captured_grid) {rows[5][0]=2;for(unsigned r=0;r<3;++r) rows[6+r]=draw.irradiance_grid->rows[r];}
        bgfx::setUniform(rt_shadow_rows,rows.data(),9);
      }
      if(post_depth) used_textures.insert(post_depth.id);
      const float mask_values[]{draw.normal_pass?1.f:0.f,draw.reflectivity,0,0};
      const float post_config_values[]{draw.post.thickness,draw.post.edge_cos,draw.post.fade,draw.post.halo};
      bgfx::setUniform(mask_uniform,mask_values);bgfx::setUniform(view_uniform,draw.view.data());
      bgfx::setUniform(post_config,post_config_values);bgfx::setUniform(post_sky,draw.post.sky.data());bgfx::setUniform(post_sun,draw.post.sun.data());
      const auto &transmission_map=draw.maps.transmission;
      // Clearcoat maps take samplers 8/9 only when transmission, water and post inputs are absent (validated by the renderer).
      const rhi::ClearcoatBinding coat(draw);
      const auto *coat_second=coat.slots[1];
      const auto secondary=coat_second?coat_second->texture:transmission_map.texture?transmission_map.texture:draw.post.secondary;
      const auto secondary_flags=coat_second?sampling(coat_second->linear,coat_second->wrap_s,coat_second->wrap_t,coat_second->min_filter):
        transmission_map.texture?sampling(transmission_map.linear,transmission_map.wrap_s,transmission_map.wrap_t,transmission_map.min_filter):
        sampling(false,RenderWrap::Clamp,RenderWrap::Clamp);
      bgfx::setTexture(9,post_second,secondary?textures.at(secondary.id):texture,secondary_flags);
      if(secondary) used_textures.insert(secondary.id);
      const float post_values[]{float(draw.post.mode),draw.post.texel[0],draw.post.texel[1],draw.post.amount};
      const float post_info[]{draw.post.levels,(draw.post.mode==RenderPostMode::Resolve?draw.post.auto_exposure:(draw.post.mode==RenderPostMode::ExposureAdapt||draw.post.mode==RenderPostMode::TAA)?draw.post.reset:draw.post.karis)?1.f:0.f,draw.post.radius,draw.post.samples};
      bgfx::setUniform(post_uniform,post_values);bgfx::setUniform(post_extra,post_info);bgfx::setUniform(post_projection,draw.post.projection.data());bgfx::setUniform(post_inverse,draw.post.inverse_projection.data());
      if(draw.cover&&draw.cover->texture) {
        // The weather cover map is point-sampled packed depth (validated free of other slot-8 users).
        bgfx::setTexture(8,post_aux,textures.at(draw.cover->texture.id),sampling(false,RenderWrap::Clamp,RenderWrap::Clamp));
        used_textures.insert(draw.cover->texture.id);
      } else if(const auto *coat_first=coat.slots[0]) {
        bgfx::setTexture(8,post_aux,textures.at(coat_first->texture.id),sampling(coat_first->linear,coat_first->wrap_s,coat_first->wrap_t,coat_first->min_filter));
        used_textures.insert(coat_first->texture.id);
      } else {
        bgfx::setTexture(8,post_aux,draw.post.auxiliary?textures.at(draw.post.auxiliary.id):texture,sampling(draw.post.auxiliary_linear,RenderWrap::Clamp,RenderWrap::Clamp));
        if(draw.post.auxiliary) used_textures.insert(draw.post.auxiliary.id);
      }
      const EnvironmentUniforms environment(draw);
      bgfx::setUniform(env_sh,environment.sh.data(),9);bgfx::setUniform(env_params,environment.params.data());bgfx::setUniform(env_mode,environment.mode.data());
      bgfx::setTexture(6,env_base,draw.environment?textures.at(draw.environment->base.id):texture,sampling(true,RenderWrap::Repeat,RenderWrap::Clamp));
      bgfx::setTexture(7,env_atlas,draw.environment?textures.at(draw.environment->atlas.id):texture,sampling(true,RenderWrap::Repeat,RenderWrap::Clamp));
      if(draw.environment) {used_textures.insert(draw.environment->base.id);used_textures.insert(draw.environment->atlas.id);}
      const ShadowUniforms shadows(draw);
      bgfx::setUniform(shadow_rows,shadows.rows.data(),32); bgfx::setUniform(shadow_rect,shadows.regions.data(),28);
      bgfx::setUniform(shadow_meta,shadows.metadata.data(),28); bgfx::setUniform(shadow_info,shadows.info.data());
      bgfx::setUniform(shadow_splits,shadows.splits.data()); bgfx::setUniform(shadow_forward,shadows.forward.data());
      bgfx::setTexture(1,shadow_sampler,draw.shadows?textures.at(draw.shadows->texture.id):texture,
        BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP);
      if(draw.shadows) used_textures.insert(draw.shadows->texture.id);

      const auto base_only=[](RenderMinFilter f) {return f==RenderMinFilter::Nearest||f==RenderMinFilter::Linear?1.f:0.f;};
      const auto inputs=material_maps(draw);float mip_flags[8]{};
      for(unsigned i=0;i<4;++i) mip_flags[i]=base_only(inputs[i]->min_filter);
      mip_flags[4]=base_only(draw.color_min_filter);
      bgfx::setUniform(mip_filter,mip_flags,2);
      const MaterialUniforms mapped(draw);
      bgfx::setUniform(map_uv,mapped.uv.data(),10); bgfx::setUniform(map_factors,mapped.factors.data());
      bgfx::setUniform(map_emission,mapped.emission.data());
      const auto maps=material_maps(draw);
      for(unsigned i=0;i<4;++i) {
        const auto &map=*maps[i];
        bgfx::setTexture(uint8_t(i+2),map_samplers[i],map.texture?textures.at(map.texture.id):texture,sampling(map.linear,map.wrap_s,map.wrap_t,map.min_filter));
        if(map.texture) used_textures.insert(map.texture.id);
      }
      const auto advanced_maps=advanced_material_maps(draw);
      const auto &thickness_map=draw.maps.thickness;
      std::array<std::array<float,4>,12> advanced_transforms{};
      std::array<std::array<float,4>,3> advanced_settings{{{},draw.maps.advanced,
        {draw.maps.lightmap_linear?1.f:0.f,transmission_map.texture?1.f:0.f,thickness_map.texture?1.f:0.f,draw.maps.lightmap_static_direct?1.f:0.f}}};
      const auto store_uv=[&](unsigned i,const RenderTextureMap &map) {
        const auto &a=map.uv.values;
        advanced_transforms[i*2]={a[0],a[1],a[2],a[3]};
        advanced_transforms[i*2+1]={a[4],a[5],float(map.uv.set),base_only(map.min_filter)};
      };
      store_uv(4,transmission_map);store_uv(5,thickness_map);
      for(unsigned i=0;i<4;++i) {
        const auto &map=*advanced_maps[i];
        store_uv(i,map);
        advanced_settings[0][i]=map.texture?1.f:0.f;
        // Terrain imprints and volume thickness share the otherwise unused parallax slot.
        const auto imprint=i==0&&draw.terrain_imprint?draw.terrain_imprint->texture:RenderTextureHandle{};
        const auto &bound=i==0&&thickness_map.texture?thickness_map:map;
        const auto handle=imprint?imprint:bound.texture;
        if(i>0&&draw.terrain_relief) continue; // the relief fields take stages 13-15 below
        const auto flags=imprint?sampling(false,RenderWrap::Clamp,RenderWrap::Clamp,RenderMinFilter::Nearest):
          sampling(bound.linear,bound.wrap_s,bound.wrap_t,bound.min_filter);
        bgfx::setTexture(uint8_t(i+12),advanced_samplers[i],handle?textures.at(handle.id):texture,flags);
        if(handle) used_textures.insert(handle.id);
      }
      bgfx::setUniform(advanced_uv,advanced_transforms.data(),12);bgfx::setUniform(advanced_params,advanced_settings.data(),3);
      bgfx::setUniform(transmit_params,draw.transmission.data(),2);
      {
        std::array<std::array<float,4>,4> coat_transforms{{{1,0,0,1},{},{1,0,0,1},{}}};
        for(unsigned i=0;i<2;++i) if(const auto *map=coat.slots[i]) {
          const auto &a=map->uv.values;
          coat_transforms[i*2]={a[0],a[1],a[2],a[3]};
          coat_transforms[i*2+1]={a[4],a[5],float(map->uv.set),base_only(map->min_filter)};
        }
        bgfx::setUniform(coat_params,coat.select.data());bgfx::setUniform(coat_uv,coat_transforms.data(),4);
      }
      {
        // Row 10 w tells the shaders a sky is present (for reflections).
        std::array<std::array<float,4>,11> sky{};
        if(draw.sky) {sky=draw.sky->rows;sky[10][3]=1;}
        bgfx::setUniform(sky_rows,sky.data(),11);
      }
      {
        // Rows 0-3 cover map clip transform; 4: map on, bias, box count; 5-36 boxes.
        std::array<std::array<float,4>,37> cover{};
        if(draw.cover) {
          const auto &c=*draw.cover;
          std::copy(c.rows.begin(),c.rows.end(),cover.begin());
          cover[4]={c.texture?1.f:0.f,c.bias,float(c.box_count),0};
          std::copy(c.boxes.begin(),c.boxes.end(),cover.begin()+5);
        }
        bgfx::setUniform(cover_rows,cover.data(),37);
      }
      bgfx::setUniform(material,mat); bgfx::setUniform(output,out);
      bgfx::setTexture(0,sampler,texture,sampling(draw.linear_sampling,draw.wrap_s,draw.wrap_t,draw.color_min_filter));
      if(culled&&draw.instances) {
        bgfx::setInstanceDataBuffer(culled->visible,0,culled->input);++instance_counters[2];
      } else if(draw.instances) {
        const auto create=[&]() {
          static_assert(sizeof(RenderInstance)==96);
          bgfx::VertexLayout instance_layout;instance_layout.begin();
          for(auto attribute:{bgfx::Attrib::TexCoord7,bgfx::Attrib::TexCoord6,bgfx::Attrib::TexCoord5,bgfx::Attrib::TexCoord4,bgfx::Attrib::TexCoord3,bgfx::Attrib::TexCoord2}) instance_layout.add(attribute,4,bgfx::AttribType::Float);
          instance_layout.end();
          const auto buffer=checked(bgfx::createVertexBuffer(bgfx::copy(draw.instances->data(),uint32_t(draw.instances->size()*sizeof(RenderInstance))),instance_layout),"instance buffer allocation failed");
          ++instance_counters[2];
          return buffer;
        };
        bgfx::VertexBufferHandle buffer=BGFX_INVALID_HANDLE;
        if(draw.persistent_instances) {
          auto it=persistent_uploads.find(draw.instances.get());
          if(it!=persistent_uploads.end()&&it->second.cpu.expired()) {bgfx::destroy(it->second.gpu);persistent_uploads.erase(it);it=persistent_uploads.end();}
          if(it==persistent_uploads.end()) {const auto gpu=create();try {it=persistent_uploads.emplace(draw.instances.get(),PersistentInstances{draw.instances,gpu}).first;} catch(...) {bgfx::destroy(gpu);throw;}}
          buffer=it->second.gpu;
        } else {
          auto it=instance_uploads.find(draw.instances.get());
          if(it==instance_uploads.end()) {const auto gpu=create();try {it=instance_uploads.emplace(draw.instances.get(),InstanceUpload{draw.instances,gpu}).first;} catch(...) {bgfx::destroy(gpu);throw;}}
          buffer=it->second.gpu;
        }
        bgfx::setInstanceDataBuffer(buffer,0,uint32_t(draw.instance_count?draw.instance_count:draw.instances->size()));
      }
      auto selected=draw.instances?instance_program:program;
      if(draw.velocity_pass||draw.post.mode==RenderPostMode::VelocityDepth) {
        auto &pipeline=draw.velocity_pass?velocity_program:depth_program;
        if(!bgfx::isValid(pipeline)) {
          const auto vs=draw.velocity_pass?bgfx_temporal_shader_bytes(shader_format,0):bgfx_shader_bytes(shader_format,true);
          const auto fs=bgfx_temporal_shader_bytes(shader_format,draw.velocity_pass?1:2);
          if(vs.empty()||fs.empty()) throw std::runtime_error("njin temporal: missing shader profile");
          const auto vertex=checked(bgfx::createShader(bgfx::copy(vs.data(),uint32_t(vs.size()))),"temporal vertex shader failed");
          const auto fragment=bgfx::createShader(bgfx::copy(fs.data(),uint32_t(fs.size())));
          if(!bgfx::isValid(fragment)) {bgfx::destroy(vertex);throw std::runtime_error("njin temporal: fragment shader failed");}
          pipeline=checked(bgfx::createProgram(vertex,fragment,true),"temporal pipeline failed");
        }
        selected=pipeline;
        if(draw.velocity_pass) {
          bgfx::VertexLayout previous_layout;previous_layout.begin();previous_layout.add(bgfx::Attrib::TexCoord2,3,bgfx::AttribType::Float);
          if(draw.previous_geometry) {
            auto it=previous_positions.find(draw.previous_geometry.get());
            if(it==previous_positions.end()) {
              std::vector<float> positions;positions.reserve(draw.previous_geometry->vertices.size()*3);
              for(const auto &v:draw.previous_geometry->vertices) positions.insert(positions.end(),{v.x,v.y,v.z});
              previous_layout.end();
              const auto buffer=checked(bgfx::createVertexBuffer(bgfx::copy(positions.data(),uint32_t(positions.size()*sizeof(float))),previous_layout),"previous pose upload failed");
              try {it=previous_positions.emplace(draw.previous_geometry.get(),PreviousUpload{draw.previous_geometry,buffer}).first;} catch(...) {bgfx::destroy(buffer);throw;}
            } else previous_layout.end();
            bgfx::setVertexBuffer(1,it->second.gpu);
          } else {
            previous_layout.skip(uint8_t(sizeof(RenderVertex)-3*sizeof(float)));previous_layout.end();
            if(!bgfx::isValid(previous_layout_handle)) previous_layout_handle=checked(bgfx::createVertexLayout(previous_layout),"previous position layout failed");
            bgfx::setVertexBuffer(1,mesh.vertices,0,UINT32_MAX,previous_layout_handle);
          }
        }
      }
      if(draw.terrain_relief) {
        const auto &relief=*draw.terrain_relief;
        // Stages 13-15 carry the relief fields in place of the lightmap and under maps.
        if(advanced_settings[0][1]>0||advanced_settings[0][2]>0||advanced_settings[0][3]>0)
          throw std::runtime_error("njin terrain: relief cannot be combined with a lightmap or under maps");
        for(unsigned i=0;i<3;++i) {
          const auto h=relief.textures[i];
          // OpenGL 3.3/GLES 3.0 luôn có vertex texture fetch; bgfx GL không gắn bit này.
          const auto format=i==0?bgfx::TextureFormat::RGBA32F:bgfx::TextureFormat::RGBA8;
          const auto type=bgfx::getRendererType();
          if(type!=bgfx::RendererType::OpenGL&&type!=bgfx::RendererType::OpenGLES&&
             !(bgfx::getCaps()->formats[format]&BGFX_CAPS_FORMAT_TEXTURE_VERTEX))
            throw std::runtime_error("njin terrain: backend lacks vertex texture support for relief");
          bgfx::setTexture(uint8_t(13+i),advanced_samplers[1+i],textures.at(h.id),
            sampling(i==2,i==2?RenderWrap::Repeat:RenderWrap::Clamp,i==2?RenderWrap::Repeat:RenderWrap::Clamp));
          used_textures.insert(h.id);
        }
        bgfx::setUniform(terrain_relief,relief.rows.data(),6);
        const unsigned stage=draw.velocity_pass?1:draw.post.mode==RenderPostMode::VelocityDepth?2:0;
        auto &pipeline=terrain_relief_programs[stage];
        if(!bgfx::isValid(pipeline)) {
          const auto vs=bgfx_temporal_shader_bytes(shader_format,stage==1?7:6);
          const auto fs=stage==0?bgfx_shader_bytes(shader_format,false):bgfx_temporal_shader_bytes(shader_format,stage==1?1:2);
          if(vs.empty()||fs.empty()) throw std::runtime_error("njin terrain: missing relief shader profile");
          const auto vertex=checked(bgfx::createShader(bgfx::copy(vs.data(),uint32_t(vs.size()))),"terrain relief vertex shader failed");
          const auto fragment=bgfx::createShader(bgfx::copy(fs.data(),uint32_t(fs.size())));
          if(!bgfx::isValid(fragment)) {bgfx::destroy(vertex);throw std::runtime_error("njin terrain: relief fragment shader failed");}
          pipeline=checked(bgfx::createProgram(vertex,fragment,true),"terrain relief pipeline failed");
        }
        selected=pipeline;
      }
      if(draw.world_style[0][0]==3&&!draw.velocity_pass&&draw.post.mode==RenderPostMode::None) {
        if(draw.instances) throw std::invalid_argument("njin water: instanced water packets are unsupported");
        if(!bgfx::isValid(water_program)) {
          const auto vs=bgfx_shader_bytes(shader_format,true),fs=bgfx_temporal_shader_bytes(shader_format,5);
          if(vs.empty()||fs.empty()) throw std::runtime_error("njin water: missing shader profile");
          const auto vertex=checked(bgfx::createShader(bgfx::copy(vs.data(),uint32_t(vs.size()))),"water vertex shader failed");
          const auto fragment=bgfx::createShader(bgfx::copy(fs.data(),uint32_t(fs.size())));
          if(!bgfx::isValid(fragment)) {bgfx::destroy(vertex);throw std::runtime_error("njin water: fragment shader failed");}
          water_program=checked(bgfx::createProgram(vertex,fragment,true),"water pipeline failed");
        }
        selected=water_program;
      }
      if(draw.post.mode==RenderPostMode::Volume) {
        if(!bgfx::isValid(volume_program)) {
          const auto vs=bgfx_shader_bytes(shader_format,true),fs=bgfx_temporal_shader_bytes(shader_format,3);
          if(vs.empty()||fs.empty()) throw std::runtime_error("njin volume: missing shader profile");
          const auto vertex=checked(bgfx::createShader(bgfx::copy(vs.data(),uint32_t(vs.size()))),"volume vertex shader failed");
          const auto fragment=bgfx::createShader(bgfx::copy(fs.data(),uint32_t(fs.size())));
          if(!bgfx::isValid(fragment)) {bgfx::destroy(vertex);throw std::runtime_error("njin volume: fragment shader failed");}
          volume_program=checked(bgfx::createProgram(vertex,fragment,true),"volume pipeline failed");
        }
        selected=volume_program;
      }
      if(draw.post.mode>=RenderPostMode::LightAmbient&&draw.post.mode<=RenderPostMode::LightResolve) {
        if(!bgfx::isValid(light2d_program)) {
          const auto vs=bgfx_shader_bytes(shader_format,true),fs=bgfx_temporal_shader_bytes(shader_format,4);
          if(vs.empty()||fs.empty()) throw std::runtime_error("njin 2D lighting: missing shader profile");
          const auto vertex=checked(bgfx::createShader(bgfx::copy(vs.data(),uint32_t(vs.size()))),"2D light vertex shader failed");
          const auto fragment=bgfx::createShader(bgfx::copy(fs.data(),uint32_t(fs.size())));
          if(!bgfx::isValid(fragment)) {bgfx::destroy(vertex);throw std::runtime_error("njin 2D lighting: fragment shader failed");}
          light2d_program=checked(bgfx::createProgram(vertex,fragment,true),"2D lighting pipeline failed");
        }
        selected=light2d_program;
      }
      if(draw.post.mode>=RenderPostMode::SMAAEdge&&draw.post.mode<=RenderPostMode::SMAABlend) {
        const unsigned stage=unsigned(draw.post.mode)-unsigned(RenderPostMode::SMAAEdge);
        if(!bgfx::isValid(smaa_programs[stage])) {
          const auto vs=bgfx_shader_bytes(shader_format,true),fs=bgfx_smaa_shader_bytes(shader_format,stage);
          if(vs.empty()||fs.empty()) throw std::runtime_error("njin SMAA: missing shader profile");
          const auto vertex=checked(bgfx::createShader(bgfx::copy(vs.data(),uint32_t(vs.size()))),"SMAA vertex shader failed");
          const auto fragment=bgfx::createShader(bgfx::copy(fs.data(),uint32_t(fs.size())));
          if(!bgfx::isValid(fragment)) {bgfx::destroy(vertex);throw std::runtime_error("njin SMAA: fragment shader failed");}
          smaa_programs[stage]=checked(bgfx::createProgram(vertex,fragment,true),"SMAA pipeline failed");
        }
        selected=smaa_programs[stage];
      }
      if(draw.virtual_texture[0]>0&&!draw.shadow_pass&&!draw.normal_pass&&!draw.velocity_pass&&draw.post.mode==RenderPostMode::None) {
        const unsigned variant=draw.terrain_relief?2:draw.instances?1:0;
        auto &pipeline=virtual_programs[variant];
        if(!bgfx::isValid(pipeline)) {
          const auto vs=variant==2?bgfx_temporal_shader_bytes(shader_format,6):bgfx_shader_bytes(shader_format,true,variant==1);
          const auto fs=virtual_fragment;
          if(vs.empty()||fs.empty()) throw std::runtime_error("NVT: missing virtual shader profile");
          const auto vertex=checked(bgfx::createShader(bgfx::copy(vs.data(),uint32_t(vs.size()))),"NVT vertex shader failed");
          const auto fragment=bgfx::createShader(bgfx::copy(fs.data(),uint32_t(fs.size())));
          if(!bgfx::isValid(fragment)) {bgfx::destroy(vertex);throw std::runtime_error("NVT fragment shader failed");}
          pipeline=checked(bgfx::createProgram(vertex,fragment,true),"NVT pipeline failed");
        }
        if(!bgfx::isValid(virtual_uniform)) virtual_uniform=checked(bgfx::createUniform("u_njinVirtual",bgfx::UniformType::Vec4,2),"NVT uniform failed");
        bgfx::setUniform(virtual_uniform,draw.virtual_texture.data(),2);selected=pipeline;
      }
      if(draw.shader && !draw.shadow_pass && !draw.normal_pass && !draw.velocity_pass) {
        const auto &custom=custom_programs.at(draw.shader->program.id);
        // Instanced draws need the game's vertex stage: it places each instance from i_data0..3.
        if((draw.instances && !custom.vertex) || draw.transparent || draw.post.mode!=RenderPostMode::None)
          throw std::invalid_argument("njin shader: custom instancing needs the shader's own vertex stage; transparency and post variants require a dedicated pipeline");
        selected=custom.program;
        for(const auto &u:draw.shader->uniforms) bgfx::setUniform(custom.uniforms.at(u.name),u.values.data(),uint16_t(u.values.size()));
        for(const auto &binding:draw.shader->textures) {
          const auto sampled=binding.texture?textures.at(binding.texture.id):texture;
          const auto flags=uint32_t(BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|(binding.linear?0:BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT));
          bgfx::setTexture(uint8_t(binding.stage),custom.uniforms.at(binding.name),sampled,flags);
          if(binding.texture) used_textures.insert(binding.texture.id);
        }
        used_shaders.insert(draw.shader->program.id);
      }
      if(culled) {bgfx::submit(view,selected,culled->indirect,0,1);++occlusion.counters[2];}
      else bgfx::submit(view,selected);
      if(draw.instances) {++instance_counters[0];instance_counters[1]+=draw.instance_count?draw.instance_count:draw.instances->size();}
      if(draw.texture) used_textures.insert(draw.texture.id);
      if(draw.mesh) used_meshes.insert(draw.mesh.id);
    } catch(...) {
      if(mesh_owned && bgfx::isValid(mesh.vertices)) bgfx::destroy(mesh.vertices);
      if(mesh_owned && bgfx::isValid(mesh.indices)) bgfx::destroy(mesh.indices);
      if(temporary_texture) bgfx::destroy(texture);
      throw;
    }
    if(mesh_owned) { bgfx::destroy(mesh.vertices); if(bgfx::isValid(mesh.indices)) bgfx::destroy(mesh.indices); }
    if(temporary_texture) bgfx::destroy(texture);
  }
  void destroy_texture(RenderTextureHandle handle) override {
    if(borrowed_depth.contains(handle.id)) throw std::logic_error("njin bgfx: target owns its depth view");
    if(used_textures.contains(handle.id)) throw std::logic_error("njin bgfx: texture is referenced by current frame");
    if(auto it=textures.find(handle.id);it!=textures.end()) { bgfx::destroy(it->second); textures.erase(it); }
  }
  void ensure_mesh(RenderMeshHandle handle,std::span<const RenderVertex> vertices,std::span<const std::uint16_t> indices) override {
    if(meshes.contains(handle.id)) return;
    Mesh mesh;
    mesh.vertices=checked(bgfx::createVertexBuffer(bgfx::copy(vertices.data(),uint32_t(vertices.size_bytes())),layout),"stream vertex allocation failed");
    try {
      if(!indices.empty()) mesh.indices=checked(bgfx::createIndexBuffer(bgfx::copy(indices.data(),uint32_t(indices.size_bytes()))),"stream index allocation failed");
      meshes.emplace(handle.id,mesh);++mesh_count;
    } catch(...) {
      bgfx::destroy(mesh.vertices);if(bgfx::isValid(mesh.indices)) bgfx::destroy(mesh.indices);throw;
    }
  }
  void destroy_mesh(RenderMeshHandle handle) override {
    if(used_meshes.contains(handle.id)) throw std::logic_error("njin bgfx: mesh is referenced by current frame");
    if(auto it=meshes.find(handle.id);it!=meshes.end()) {
      bgfx::destroy(it->second.vertices); if(bgfx::isValid(it->second.indices)) bgfx::destroy(it->second.indices); meshes.erase(it);
    }
  }
  void create_target(RenderTargetHandle handle,RenderTextureHandle color,unsigned w,unsigned h) override {
    create_target_format(handle,color,w,h,bgfx::TextureFormat::RGBA8);
  }
  void create_hdr_target(RenderTargetHandle handle,RenderTextureHandle color,unsigned w,unsigned h) override {
    create_target_format(handle,color,w,h,bgfx::TextureFormat::RGBA16F);
  }
  void create_target_format(RenderTargetHandle handle,RenderTextureHandle color,unsigned w,unsigned h,bgfx::TextureFormat::Enum format) {
    if(w>bgfx::getCaps()->limits.maxTextureSize || h>bgfx::getCaps()->limits.maxTextureSize)
      throw std::invalid_argument("njin bgfx: target exceeds device limits");
    if(!(bgfx::getCaps()->formats[format]&BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER))
      throw std::runtime_error("njin bgfx: requested render-target format is unsupported");
    auto texture=checked(bgfx::createTexture2D((uint16_t)w,(uint16_t)h,false,1,format,BGFX_TEXTURE_RT),"target color failed");
    bgfx::TextureHandle depth=BGFX_INVALID_HANDLE; bgfx::FrameBufferHandle fb=BGFX_INVALID_HANDLE;
    try {
      depth=checked(bgfx::createTexture2D((uint16_t)w,(uint16_t)h,false,1,bgfx::TextureFormat::D24S8,BGFX_TEXTURE_RT),"target depth failed");
      const bgfx::TextureHandle attachments[]{texture,depth};
      fb=checked(bgfx::createFrameBuffer(2,attachments,false),"framebuffer failed");
      textures.emplace(color.id,texture); targets.emplace(handle.id,Target{fb,depth,color,w,h,false,{},format});
    } catch(...) {
      textures.erase(color.id); if(bgfx::isValid(fb)) bgfx::destroy(fb);
      if(bgfx::isValid(depth)) bgfx::destroy(depth);
      bgfx::destroy(texture); throw;
    }
  }
  void expose_depth(RenderTargetHandle handle,RenderTextureHandle depth) override {
    if(!bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::D24S8,0)) throw std::runtime_error("njin bgfx: sampled D24S8 depth is unsupported");
    auto &target=targets.at(handle.id);
    if(target.depth_alias) throw std::logic_error("njin bgfx: depth alias already exists");
    textures.emplace(depth.id,target.depth);
    try {borrowed_depth.insert(depth.id);} catch(...) {textures.erase(depth.id);throw;}
    target.depth_alias=depth;
  }
  void destroy_target(RenderTargetHandle handle,RenderTextureHandle color) override {
    if(used_targets.contains(handle.id)) throw std::logic_error("njin bgfx: target is referenced by current frame");
    const auto target=targets.at(handle.id);
    if(target.depth_alias&&used_textures.contains(target.depth_alias.id)) throw std::logic_error("njin bgfx: depth is referenced by current frame");
    destroy_texture(color);
    if(target.depth_alias) {textures.erase(target.depth_alias.id);borrowed_depth.erase(target.depth_alias.id);}
    bgfx::destroy(target.framebuffer); bgfx::destroy(target.depth); targets.erase(handle.id);
  }
  unsigned max_texture_size() const override { return bgfx::getCaps()->limits.maxTextureSize; }
  void begin_target(RenderTargetHandle handle,rgba clear,bool clear_enabled) override {
    require_frame(); if(active_target) throw std::logic_error("njin bgfx: nested targets are unsupported");
    next_view(handle.id,clear,clear_enabled); active_target=handle.id; used_targets.insert(handle.id);
  }
  void end_target() override {
    require_frame(); if(!active_target) throw std::logic_error("njin bgfx: no active target");
    targets.at(active_target).rendered=true; next_view(0,{},false); active_target=0;
  }
  uint64_t texture_uploads() const override { return texture_count; }
  uint64_t mesh_uploads() const override { return mesh_count; }
  bool gpu_occlusion_available() const override {return BgfxOcclusion::available();}
  std::string gpu_occlusion_diagnostics() const override {
    auto out=BgfxOcclusion::diagnostics();
    if(bgfx::getRendererType()==bgfx::RendererType::Vulkan) out+="; "+vulkan_occlusion_diagnostics();
    return out;
  }
  void prepare_occlusion(RenderTargetHandle target,const std::array<float,16>&) override {
    require_frame();const auto &source=targets.at(target.id);
    occlusion.build(textures.at(source.color.id),source.width,source.height,view,shader_format);
    configure_view(active_target,{},false);used_textures.insert(source.color.id);used_targets.insert(target.id);
  }
  std::array<std::uint64_t,6> occlusion_stats() const override {return occlusion.counters;}
  std::array<std::uint64_t,2> occlusion_readback(bool shadow=false) override {
    const auto result=occlusion.readback(shadow);
    if(frame_active) {view=0;configure_view(active_target,{},false);}
    return result;
  }
};
}
std::unique_ptr<GraphicsDevice> make_bgfx_device(SdlPlatform &platform,const config &cfg) {
  return std::make_unique<BgfxDevice>(platform,cfg);
}
}
#endif
