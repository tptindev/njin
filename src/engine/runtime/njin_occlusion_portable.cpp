#include "njin_occlusion_portable.h"
#include "njin_model.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_rlmath.h"
#include <algorithm>
#include <stdexcept>
namespace njin {
std::array<float,4> portable_cull_bounds(context &ctx,const MeshDraw &draw) {
  if(draw.instances&&draw.instance_occlusion_bounds[3]>0) return draw.instance_occlusion_bounds;
  const auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
  const auto vertices=draw.geometry?std::span<const RenderVertex>(draw.geometry->vertices):
    draw.mesh?std::span<const RenderVertex>(resources.meshes.at(draw.mesh.id).cpu):draw.vertices;
  if(vertices.empty()) return {};
  Vector3 lo{INFINITY,INFINITY,INFINITY},hi{-INFINITY,-INFINITY,-INFINITY};
  for(const auto &v:vertices) {const Vector3 p{v.x,v.y,v.z};lo=Vector3Min(lo,p);hi=Vector3Max(hi,p);}
  const auto center=Vector3Scale(Vector3Add(lo,hi),.5f);
  return {center.x,center.y,center.z,Vector3Distance(lo,hi)*.5f+0.0001f};
}
void portable_prepare_occlusion(context &ctx,model_store &store) {
  const bool enabled=render3d_options_get(ctx).occlusion;
  bool candidates=false;
  for(auto &draw:store.portable_queue) {
    if(!enabled||draw.transparent||draw.shadow_only||draw.shader||!draw.depth_test||draw.world_style[0][0]!=0) draw.instance_occlusion_bounds={};
    else if(!draw.instances) draw.instance_occlusion_bounds=portable_cull_bounds(ctx,draw);
    candidates|=draw.instance_occlusion_bounds[3]>0;
  }
  if(!candidates) return;
  auto &window=ctx_ref<window_guard>(ctx);
  if(!window.graphics->gpu_occlusion_available())
    throw std::runtime_error(window.graphics->gpu_occlusion_diagnostics());
  auto &resources=window.renderer_resources;
  const auto previous=resources.active_target;
  const auto size=previous?std::array<unsigned,2>{resources.targets.at(previous.id).width,resources.targets.at(previous.id).height}:window.graphics->frame_view_size();
  const auto serial=window.graphics->frame_serial();
  std::erase_if(store.portable_occlusion_targets,[&](const auto &entry) {
    if(serial-entry.frame<=120) return false;
    renderer_destroy_target(ctx,entry.target);return true;
  });
  auto found=std::find_if(store.portable_occlusion_targets.begin(),store.portable_occlusion_targets.end(),[&](const auto &entry){return entry.width==size[0]&&entry.height==size[1];});
  if(found==store.portable_occlusion_targets.end()) {
    const auto target=renderer_create_target(ctx,size[0],size[1]);
    try {store.portable_occlusion_targets.push_back({target,size[0],size[1],serial});}
    catch(...) {renderer_destroy_target(ctx,target);throw;}
    found=store.portable_occlusion_targets.end()-1;
  }
  found->frame=serial;store.portable_occlusion_depth=found->target;
  if(previous) renderer_end_target(ctx);
  try {
    renderer_begin_target(ctx,store.portable_occlusion_depth,{1,1,1,1});
    // Bounds contain all current-pose geometry. Their nearest depth is in front
    // of their own raster depth, so a model cannot occlude itself. No history.
    for(const auto &packet:store.portable_queue) {
      // Camera mask khoét màu nhưng cố ý không khoét shadow: không dùng depth
      // shadow của nó làm occluder, nếu không sẽ che nhầm vật nhìn qua lỗ.
      if(packet.instances||packet.transparent||packet.shadow_only||packet.shader||!packet.depth_write||!packet.depth_test||packet.world_style[0][0]>1.5f||packet.world_effects.mask[3]>0) continue;
      auto depth=packet;depth.gpu_cull.reset();depth.instance_occlusion_bounds={};depth.shadow_pass=true;depth.shadows.reset();depth.environment.reset();depth.clusters.reset();depth.cover.reset();
      depth.ray_shadow=false;depth.normal_pass=false;depth.velocity_pass=false;depth.lighting.enabled=false;
      depth.local_light_count=0;depth.linear_output=false;depth.resolve=false;depth.tonemap=RenderToneMap::None;
      depth.blend=RenderBlend::Opaque;renderer_draw_mesh(ctx,depth);
    }
    renderer_end_target(ctx);
    const auto vp=MatrixToFloatV(store.portable_view_projection);std::array<float,16> matrix;std::copy_n(vp.v,16,matrix.begin());
    window.graphics->prepare_occlusion(store.portable_occlusion_depth,matrix);
    if(previous) renderer_resume_target(ctx,previous);
  } catch(...) {
    if(resources.active_target&&resources.active_target.id!=previous.id) {try {renderer_end_target(ctx);} catch(...) {}}
    if(previous&&!resources.active_target) {try {renderer_resume_target(ctx,previous);} catch(...) {}}
    throw;
  }
}
}
