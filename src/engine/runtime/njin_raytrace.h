#pragma once
// light3d::ray_traced_shadows on the bgfx renderer: software ray tracing in a compute
// shader against a CPU-built BVH (tinybvh BVH_GPU layout). The hardware path
// (DXR/Vulkan ray queries) is a later addition behind the same pass.
#include "njin_internal_only.h"
#include "njin_renderer.h"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace njin {
struct context;
namespace rhi {
// One frame's ray tracing: the shadows of the sun into the mask's r, of up to three
// lamps into g, b, a (RenderShadowData::ray_channel), and the reflections. The image
// the device keeps (the shadows above, the reflections below) is read by draws with
// MeshDraw::ray_shadow during the same frame.
struct RayShadowPass {
  RenderTextureHandle depth;  // packed prepass depth (shadow-pass format) of the frame's view
  unsigned width=0,height=0;  // the view's size in pixels
  std::array<float,16> view_projection{},inverse_view_projection{}; // column-major, OpenGL clip
  std::array<float,4> sun{0,1,0,0}; // towards the sun xyz, cone half-angle (radians)
  std::array<float,4> eye{0,0,0,0}; // camera position xyz, 1 when the sun's shadow is traced
  std::array<std::array<float,4>,3> lamps{}; // position xyz, radius of the light's sphere
  unsigned lamp_count=0;
  unsigned rays=2,frame=0;
  float bias=0.02f,denoise=3,max_distance=1000;
  float skip=0; // metres a ray ignores from its start (light3d::shadow_lod_gap)
  bool shadows=true; // the sun or a lamp is traced (draws read the mask's shadows)
  // light3d::ray_traced_reflections: reflection_roughness (0: no reflection rays),
  // (materials and lights also shade the grid's rays)
  // per draw (queue index) linear albedo and emission rows, and the light of what the
  // rays meet: sun colour + lit flag, ambient + lamp count, then 8 lamps x 3 rows.
  float reflection_roughness=0;
  std::shared_ptr<const std::vector<std::array<float,4>>> materials;
  std::array<std::array<float,4>,26> lights{};
  // irradiance3d_desc::ray_traced: the grid the device keeps and updates every frame
  // (rows as cs_njin_rtshadow.sc's u_rtGrid: corner + history, size + intensity,
  // counts + rays, on/environment/frame/blend, then the environment's SH9 + rotation).
  // `grid_key` changes when the grid must start over (new desc, irradiance3d_refresh).
  bool grid=false;
  std::uint64_t grid_key=0;
  std::array<std::array<float,4>,13> grid_rows{};
  // tinybvh BVH_GPU: 4 vec4 per node, then 3 vec4 per triangle (v0, edge1, edge2).
  std::shared_ptr<const std::vector<std::array<float,4>>> nodes,triangles;
};
} // namespace rhi
// Whether this context's renderer can run the compute trace (bgfx with compute shaders).
bool renderer_compute_available(context &);
// The traces run on the GPU's ray tracing hardware (bgfx Vulkan ray queries).
bool renderer_ray_query_available(context &);
// Pixel size of the frame's own view (no active target).
std::array<unsigned,2> renderer_frame_view_size(context &);
struct model_store;
// Traces this pass's sun shadows when light3d::ray_traced_shadows asks and the
// renderer can; true when opaque draws should read the mask (MeshDraw::ray_shadow).
bool portable_ray_shadows(context &,model_store &);
void renderer_trace_shadows(context &,const rhi::RayShadowPass &);
} // namespace njin
