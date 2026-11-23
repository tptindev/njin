#pragma once
#include "njin_internal_only.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
// Hardware ray queries on bgfx Vulkan (light3d::ray_traced_*, irradiance3d_desc::ray_traced).
// The bgfx compute kernels of cs_njin_rtshadow.sc that trace (stages 0, 2, 3) are swapped,
// inside bgfx's own command buffer (cmake/njin_bgfx_rayquery_fix.cmake), for the same
// kernels compiled by glslc with rayQueryEXT against one acceleration structure built from
// the triangles the compute traversal reads, so both paths see the same scene and write
// the same images. Only njin_bgfx.cpp uses this.
namespace njin::rhi {
// Read-only diagnostics for the physical device bgfx selected; no native submission.
std::string vulkan_occlusion_diagnostics();
class VulkanRayQuery {
public:
  // Before bgfx::init: ask bgfx for the ray query extensions and features.
  static void request(bool want);
  // After bgfx::init on Vulkan: null when the device has no ray queries (or the build no
  // kernels); the reason is logged once.
  static std::unique_ptr<VulkanRayQuery> create();
  virtual ~VulkanRayQuery()=default;
  // The scene: b_rtTris rows (vertex, edge, edge per triangle). Rebuilt when the rows change
  // (the renderer keeps the same vector for an unchanged scene).
  virtual void scene(const std::shared_ptr<const std::vector<std::array<float,4>>> &triangles)=0;
  // The materials of this frame (b_rtMats rows); empty for none.
  virtual void materials(const std::vector<std::array<float,4>> &rows)=0;
  // The uniform block of the kernels (NjinRtUniforms in njin_rt_hw.glsl).
  struct Uniforms {
    std::array<float,16> inverse{};
    std::array<float,4> sun{},params{},extra{};
    std::array<std::array<float,4>,5> lamps{};
    std::array<std::array<float,4>,26> lights{};
    std::array<std::array<float,4>,13> grid{};
  };
  // A bgfx dispatch of `program` (a bgfx program index) that the next bgfx frame hands to
  // the hardware kernel of `stage`. Textures are bgfx texture indices (0xffff none): the
  // written images (binding 0, 1) and the sampled depth, grid and grid depth.
  struct Dispatch {
    std::uint16_t program=0xffff;
    unsigned stage=0;
    Uniforms uniforms;
    std::array<std::uint16_t,2> images{{0xffff,0xffff}};
    std::uint16_t depth=0xffff,grid=0xffff,grid_depth=0xffff;
  };
  virtual void queue(const Dispatch &)=0;
  // Called once per njin frame before its dispatches are queued.
  virtual void begin_frame()=0;
  // Dispatches that ran on the hardware since creation.
  virtual std::uint64_t dispatches() const=0;
};
}
