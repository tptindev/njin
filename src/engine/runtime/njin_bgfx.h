#pragma once
#include "njin_graphics_device.h"
#include "njin_cfg.h"
#include <span>
#include <string_view>
namespace njin { class SdlPlatform; }
namespace njin::rhi {
std::unique_ptr<GraphicsDevice> make_bgfx_device(SdlPlatform &,const config &);
// Build-generated shader package, with shaderc's backend-specific containers.
std::span<const unsigned char> bgfx_shader_bytes(std::string_view format,bool vertex,bool instanced=false);
std::span<const unsigned char> bgfx_compat_vertex_shader_bytes(std::string_view format);
std::span<const unsigned char> bgfx_temporal_shader_bytes(std::string_view format,unsigned stage);
std::span<const unsigned char> bgfx_smaa_shader_bytes(std::string_view format,unsigned stage);
std::span<const unsigned char> bgfx_atmosphere_shader_bytes(std::string_view format);
std::span<const unsigned char> bgfx_virtual_shader_bytes(std::string_view format);
// Compute: 0 ray traced shadow trace, 1 its denoise.
std::span<const unsigned char> bgfx_compute_shader_bytes(std::string_view format,unsigned stage);
// Vulkan ray query kernels (SPIR-V for stages 0, 2, 3); empty when glslc was not found.
std::span<const unsigned char> bgfx_raytrace_hw_bytes(unsigned stage);
}
