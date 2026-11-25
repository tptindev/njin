#pragma once
#include "njin_internal_only.h"
#include "njin_renderer.h"
#include "njin_raytrace.h"
#include "_types.h"
#include <memory>
#include <stdexcept>
namespace njin::rhi {
struct RendererResources;
struct ShaderUniformInfo {
  enum class Type { Vec4, Sampler, Other };
  std::string name;
  Type type=Type::Other;
  unsigned count=0;
};
struct ShaderProgramInfo {
  RenderShaderHandle handle;
  std::vector<ShaderUniformInfo> uniforms;
};
// Internal device contract. Renderer resolves CPU asset handles and emits
// triangle commands; gameplay never owns a device or native GPU resource.
// Resources are immutable, uploaded lazily, and released before the device.
class GraphicsDevice {
public:
  virtual ~GraphicsDevice() = default;
  bool frame_open() const noexcept { return frame_active; }
  std::uint64_t frame_serial() const noexcept { return frame_counter; }
  virtual void begin_frame(rgba clear) = 0;
  virtual void end_frame(RenderImageAsset *capture=nullptr) = 0;
  // Diagnostic identity of the submission returned by the backend's frame().
  virtual std::uint32_t submitted_frame_number() const { return UINT32_MAX; }
  virtual void draw(const MeshDraw &) = 0;
  virtual std::string shader_profile() const { return {}; }
  virtual RenderShaderHandle create_atmosphere_shader() {throw std::runtime_error("njin atmosphere: requires a compiled SDL3/bgfx atmosphere shader");}
  virtual ShaderProgramInfo create_shader(std::span<const std::uint8_t>,std::span<const std::uint8_t>) { throw std::runtime_error("njin renderer: packaged shaders are unavailable on this device"); }
  virtual void destroy_shader(RenderShaderHandle) { throw std::runtime_error("njin renderer: packaged shaders are unavailable on this device"); }
  virtual bool shader_in_use(RenderShaderHandle) const { return false; }
  virtual void ensure_texture(RenderTextureHandle,const RenderImage &) = 0;
  virtual void ensure_texture_writable(RenderTextureHandle,const RenderImage &) {throw std::runtime_error("njin: writable texture unavailable");}
  virtual void virtual_texture_shader(std::span<const std::uint8_t>) {throw std::runtime_error("njin: virtual texture pipeline unavailable");}
  virtual void update_texture_region(RenderTextureHandle,unsigned,unsigned,unsigned,unsigned,std::span<const std::uint8_t>) {throw std::runtime_error("njin: region texture upload unavailable");}
  virtual void ensure_texture_prepared(RenderTextureHandle handle,const RenderImage &image,std::span<const std::uint8_t>,unsigned) {ensure_texture(handle,image);}
  virtual void ensure_mesh(RenderMeshHandle,std::span<const RenderVertex>,std::span<const std::uint16_t>) {
    throw std::runtime_error("njin renderer: explicit mesh upload unavailable");
  }
  virtual void replace_texture(RenderTextureHandle,const RenderImage &) { throw std::runtime_error("njin renderer: texture replacement is unavailable"); }
  virtual RenderImageAsset read_target(RenderTargetHandle) { throw std::runtime_error("njin renderer: render-target readback is unavailable"); }
  // Readback without stalling the frame: begin copies the target as it stands at the
  // end of this frame; finish gives the pixels once the GPU has them (a frame or two
  // later), false until then. Every begun readback must be finished or it leaks.
  virtual std::uint64_t begin_readback(RenderTargetHandle) { throw std::runtime_error("njin renderer: asynchronous readback is unavailable"); }
  virtual bool finish_readback(std::uint64_t, RenderImageAsset &) { return false; }
  // GPU snapshot of the current color surface, preserving its draw order.
  virtual void copy_frame(RenderTargetHandle) {throw std::runtime_error("njin renderer: frame snapshots are unavailable");}
  virtual void destroy_texture(RenderTextureHandle) = 0;
  virtual void destroy_mesh(RenderMeshHandle) = 0;
  virtual void create_target(RenderTargetHandle, RenderTextureHandle, unsigned width, unsigned height) = 0;
  virtual void create_hdr_target(RenderTargetHandle, RenderTextureHandle, unsigned, unsigned) { throw std::runtime_error("njin renderer: HDR targets are unsupported by this device"); }
  virtual void expose_depth(RenderTargetHandle,RenderTextureHandle) {throw std::runtime_error("njin renderer: sampled target depth is unavailable on this device");}
  virtual void destroy_target(RenderTargetHandle, RenderTextureHandle) = 0;
  virtual void begin_target(RenderTargetHandle, rgba clear, bool clear_enabled=true) = 0;
  virtual std::uint64_t deformed_mesh_uploads() const {return 0;}
  virtual std::array<std::uint64_t,3> instance_stats() const { return {}; }
  virtual bool target_in_use(RenderTargetHandle) const {return false;}
  virtual bool texture_in_use(RenderTextureHandle) const {return false;}
  virtual bool mesh_in_use(RenderMeshHandle) const {return false;}
  virtual bool instancing_available() const { return false; }
  // Compute ray tracing (light3d::ray_traced_shadows); see njin_raytrace.h.
  virtual bool compute_available() const { return false; }
  virtual bool gpu_occlusion_available() const {return false;}
  virtual std::string gpu_occlusion_diagnostics() const {return "GPU occlusion path=unavailable: renderer has no GPU culling implementation";}
  virtual void prepare_occlusion(RenderTargetHandle,const std::array<float,16>&) {throw std::runtime_error("njin: GPU occlusion is unavailable");}
  virtual std::array<std::uint64_t,6> occlusion_stats() const {return {};}
  virtual std::array<std::uint64_t,2> occlusion_readback(bool=false) {throw std::runtime_error("njin: GPU occlusion readback is unavailable");}
  // The GPU's ray tracing hardware runs the traces (bgfx Vulkan ray queries).
  virtual bool ray_query_available() const { return false; }
  virtual std::uint64_t ray_query_dispatches() const { return 0; }
  // Pixel size of what a draw with no active target renders into this frame.
  virtual std::array<unsigned,2> frame_view_size() const { return {0,0}; }
  virtual void trace_shadows(const RayShadowPass &) { throw std::runtime_error("njin renderer: compute ray tracing is unavailable on this device"); }
  virtual unsigned max_texture_size() const { return 16384; }
  virtual void end_target() = 0;
  virtual std::uint64_t texture_uploads() const = 0;
  virtual std::uint64_t mesh_uploads() const = 0;
protected:
  bool frame_active=false;
  std::uint64_t frame_counter=0;
  void require_frame() const {
    if(!frame_active) throw std::logic_error("njin renderer: draw/submission outside an active render frame");
  }
};
std::unique_ptr<GraphicsDevice> make_raylib_device(RendererResources &);
}
