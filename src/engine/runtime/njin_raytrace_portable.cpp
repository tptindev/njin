// light3d::ray_traced_shadows on the bgfx renderer (njin_raytrace.h): each frame the
// pass's shadow casters become world-space triangles, tinybvh builds a BVH of them
// (scalar builder, so every platform builds it the same way), the opaque draws'
// depth is drawn from the camera in the shadow pass's packed format, and the device
// traces the sun's rays in a compute shader. Opaque draws then read the mask.
#include "njin_raytrace.h"
#include "njin_model.h"
#include "njin_model_portable.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_window.h"
#include "njin_log.h"
#include "njin_log_impl.h"
#define TINYBVH_NO_SIMD
#define TINYBVH_IMPLEMENTATION
#include <tiny_bvh.h>
#include <raymath.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace njin {
namespace {
constexpr size_t max_triangles=4000000; // beyond this the frame keeps its shadow maps
Matrix matrix_of(const std::array<float,16> &a) {
  return {a[0],a[4],a[8],a[12],a[1],a[5],a[9],a[13],a[2],a[6],a[10],a[14],a[3],a[7],a[11],a[15]};
}
// World-space triangles of the casters: built-in shapes, models (their posed snapshot
// when animated), instances and terrain chunks. Grass, water, particles and transparent
// draws do not cast ray traced shadows (as on the legacy renderer).
// Each triangle's second corner carries its draw's queue index in w (tinybvh keeps it
// in the edge's w), for the shading of what a ray meets.
bool gather(context &ctx,const model_store &store,std::vector<tinybvh::bvhvec4> &out) {
  const auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
  size_t next_lod=0;
  for(size_t queue=0;queue<store.portable_queue.size();++queue) {
    const auto &packet=store.portable_queue[queue];
    if(packet.terrain_relief)
      throw std::runtime_error("njin ray tracing: terrain relief displacement is not yet represented in the BVH; disable relief or ray tracing");
    // light3d::shadow_lod: the coarser level recorded by portable_draw_model.
    RenderMeshHandle mesh=packet.mesh;
    while(next_lod<store.portable_rt_lod.size()&&store.portable_rt_lod[next_lod].first<queue) ++next_lod;
    if(next_lod<store.portable_rt_lod.size()&&store.portable_rt_lod[next_lod].first==queue) mesh=store.portable_rt_lod[next_lod].second;
    if(!packet.cast_shadow||packet.transparent||packet.world_style[0][0]>1.5f) continue;
    const std::vector<RenderVertex> *vertices=nullptr;const std::vector<std::uint16_t> *indices=nullptr;
    if(packet.geometry) {vertices=&packet.geometry->vertices;indices=&packet.geometry->indices;}
    else if(mesh) {
      const auto it=resources.meshes.find(mesh.id);
      if(it==resources.meshes.end()) continue;
      vertices=&it->second.cpu;indices=&it->second.indices;
    }
    std::vector<RenderVertex> immediate;std::vector<std::uint16_t> none;
    if(!vertices) {immediate.assign(packet.vertices.begin(),packet.vertices.end());vertices=&immediate;indices=&none;}
    if(vertices->empty()) continue;
    const size_t corners=indices->empty()?vertices->size():indices->size();
    const Matrix world=matrix_of(packet.world);
    const size_t instances=packet.instances?(packet.instance_count?packet.instance_count:packet.instances->size()):1;
    if(out.size()/3+corners/3*instances>max_triangles) return false;
    for(size_t k=0;k<instances;++k) {
      const Matrix m=packet.instances?MatrixMultiply(matrix_of((*packet.instances)[k].transform),world):world;
      for(size_t c=0;c+2<corners;c+=3) for(size_t j=0;j<3;++j) {
        const size_t index=indices->empty()?c+j:(*indices)[c+j];
        if(index>=vertices->size()) return false;
        const auto &v=(*vertices)[index];
        const Vector3 p=Vector3Transform({v.x,v.y,v.z},m);
        std::uint32_t tag=std::uint32_t(queue);float w;std::memcpy(&w,&tag,sizeof w);
        out.push_back(tinybvh::bvhvec4(p.x,p.y,p.z,j==1?w:0.f));
      }
    }
  }
  return true;
}
float linear(float c) {return c<=0.04045f?c/12.92f:std::pow((c+0.055f)/1.055f,2.4f);}
// light3d::ray_traced_reflections: each draw's colour and glow, and the light that
// shades what a mirror ray meets (legacy rt/common.glsl shade_hit).
void shade_inputs(const model_store &store,rhi::RayShadowPass &pass) {
  const auto &light=store.portable_frame_light;
  auto materials=std::make_shared<std::vector<std::array<float,4>>>(store.portable_queue.size()*2);
  for(size_t i=0;i<store.portable_queue.size();++i) {
    const auto &packet=store.portable_queue[i];
    // w: drawn from both sides (no inside for the grid's rays to be in).
    (*materials)[i*2]={linear(packet.tint[0]),linear(packet.tint[1]),linear(packet.tint[2]),packet.cull==RenderCull::None?1.f:0.f};
    const float glow=std::isfinite(packet.emission_strength)?std::max(packet.emission_strength,0.f):0.f;
    (*materials)[i*2+1]={linear(packet.emission[0])*glow,linear(packet.emission[1])*glow,linear(packet.emission[2])*glow,0};
  }
  pass.materials=materials;
  const bool sun=light.color.r>0||light.color.g>0||light.color.b>0;
  pass.lights[0]={linear(light.color.r),linear(light.color.g),linear(light.color.b),sun?1.f:0.f};
  const size_t lamps=std::min<size_t>(store.portable_lights.size(),8);
  pass.lights[1]={linear(light.ambient.r),linear(light.ambient.g),linear(light.ambient.b),float(lamps)};
  for(size_t i=0;i<lamps;++i) {
    const auto &l=store.portable_lights[i];
    const float half=l.cone*PI/360.0f;
    const Vector3 dir=Vector3Normalize({l.direction.x,l.direction.y,l.direction.z});
    pass.lights[2+i]={l.position.x,l.position.y,l.position.z,std::max(l.radius,0.0001f)};
    pass.lights[10+i]={linear(l.color.r)*l.intensity,linear(l.color.g)*l.intensity,linear(l.color.b)*l.intensity,std::cos(half*(1-l.softness))};
    pass.lights[18+i]={dir.x,dir.y,dir.z,l.kind==light3d_spot?std::cos(half):-2.f};
  }
}
// irradiance3d_desc::ray_traced (irradiance3d_set on the bgfx renderer): the grid's
// rows for the device, or false without one.
bool grid_inputs(context &ctx,model_store &store,rhi::RayShadowPass &pass) {
  if(!store.portable_grid_set) return false;
  if(!store.portable_grid.ray_traced) {NJIN_WARN_LIMITED("bgfx 3D: a captured irradiance grid is not ported; irradiance3d_desc::ray_traced is");return false;}
  const auto &g=store.portable_grid;
  const float nx=float(std::clamp(g.count_x,1,16)),ny=float(std::clamp(g.count_y,1,16)),nz=float(std::clamp(g.count_z,1,16));
  const vec3 size{std::max(g.size.x,0.001f),std::max(g.size.y,0.001f),std::max(g.size.z,0.001f)};
  const vec3 lo=g.center-size*0.5f;
  pass.grid=true;
  pass.grid_rows[0]={lo.x,lo.y,lo.z,std::clamp(std::isfinite(g.history)?g.history:0.95f,0.f,0.99f)};
  pass.grid_rows[1]={size.x,size.y,size.z,std::isfinite(g.intensity)?std::max(g.intensity,0.f):1.f};
  pass.grid_rows[2]={nx,ny,nz,float(std::clamp(g.rays,16,256))};
  pass.grid_rows[3]={1,0,float(store.portable_rt_frame%4096),std::isfinite(g.blend)?std::max(g.blend,0.f):0.5f};
  // Rays that meet nothing see the environment (its irradiance), else the ambient colour.
  if(const auto &env=store.portable_environment) {
    pass.grid_rows[3][1]=1;
    for(unsigned k=0;k<9;++k) for(unsigned c=0;c<3;++c) pass.grid_rows[4+k][c]=env->sh[k][c]*env->intensity;
    pass.grid_rows[4][3]=env->rotation;
  }
  // A new desc or irradiance3d_refresh starts the grid over.
  pass.grid_key=store.portable_rt_grid_generation*65536+std::uint64_t(nx+16*ny+256*nz);
  return true;
}
} // namespace

bool portable_ray_shadows(context &ctx,model_store &store) {
  const auto &light=store.portable_frame_light;
  if(!renderer_compute_available(ctx)) return false;
  // The lamps given a channel of the mask by portable_render_shadows.
  rhi::RayShadowPass pass;
  if(const auto &shadows=store.portable_shadows;shadows&&light.ray_traced_shadows) for(size_t i=0;i<store.portable_lights.size()&&i<shadows->ray_channel.size();++i)
    if(const unsigned k=shadows->ray_channel[i];k>=1&&k<=3) {
      const auto &l=store.portable_lights[i];
      pass.lamps[k-1]={l.position.x,l.position.y,l.position.z,std::isfinite(l.source_size)?std::max(l.source_size,0.f):0.f};
      pass.lamp_count=std::max(pass.lamp_count,k);
    }
  const bool sun_traced=light.ray_traced_shadows&&light.shadows;
  pass.shadows=sun_traced||pass.lamp_count;
  const bool grid=grid_inputs(ctx,store,pass);
  if(!pass.shadows&&!light.ray_traced_reflections&&!grid) return false;
  auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
  const auto previous=resources.active_target;
  // The view drawn into: the pass's target (HDR, render texture) or the frame itself.
  const auto frame=renderer_frame_view_size(ctx);
  const unsigned width=previous?resources.targets.at(previous.id).width:frame[0];
  const unsigned height=previous?resources.targets.at(previous.id).height:frame[1];
  if(!width||!height) return false;
  std::vector<tinybvh::bvhvec4> corners;
  if(!gather(ctx,store,corners)) {
    NJIN_WARN_LIMITED("bgfx ray tracing: the casters exceed %zu triangles; shadow maps used",max_triangles);
    return false;
  }
  // A scene whose casters did not move keeps last frame's BVH (and the device skips
  // the upload of the same buffers).
  static_assert(sizeof(tinybvh::bvhvec4)==sizeof(std::array<float,4>));
  const bool same=store.portable_rt_nodes&&store.portable_rt_corners.size()==corners.size()&&
    (corners.empty()||std::memcmp(store.portable_rt_corners.data(),corners.data(),corners.size()*sizeof(corners[0]))==0);
  if(!same) {
    auto nodes=std::make_shared<std::vector<std::array<float,4>>>(),triangles=std::make_shared<std::vector<std::array<float,4>>>();
    if(corners.size()>=3) {
      tinybvh::BVH_GPU bvh;
      bvh.Build(corners.data(),uint32_t(corners.size()/3));
      nodes->resize(size_t(bvh.usedNodes)*4);
      std::memcpy(nodes->data(),bvh.bvhNode,nodes->size()*sizeof(float)*4);
      triangles->resize(bvh.orderedVerts.count);
      std::memcpy(triangles->data(),bvh.orderedVerts.data,triangles->size()*sizeof(float)*4);
    }
    store.portable_rt_corners.resize(corners.size());
    if(!corners.empty()) std::memcpy(store.portable_rt_corners.data(),corners.data(),corners.size()*sizeof(corners[0]));
    store.portable_rt_nodes=nodes;store.portable_rt_triangles=triangles;++resources.ray_bvh_builds;
  }
  pass.nodes=store.portable_rt_nodes;pass.triangles=store.portable_rt_triangles;
  // The opaque draws' depth from the camera, packed like the shadow atlas.
  if(!store.portable_rt_depth||store.portable_rt_width!=width||store.portable_rt_height!=height) {
    const auto target=renderer_create_target(ctx,width,height);
    try {if(store.portable_rt_depth) renderer_destroy_target(ctx,store.portable_rt_depth);}
    catch(...) {renderer_destroy_target(ctx,target);throw;}
    store.portable_rt_depth=target;store.portable_rt_width=width;store.portable_rt_height=height;
  }
  const Matrix vp=store.portable_view_projection;
  if(previous) renderer_end_target(ctx);
  try {
    renderer_begin_target(ctx,store.portable_rt_depth,{1,1,1,1});
    for(const auto &packet:store.portable_queue) {
      if(packet.transparent||packet.shadow_only||packet.world_style[0][0]>2.5f) continue;
      MeshDraw draw=packet;draw.shadows.reset();draw.shadow_pass=true;draw.clusters.reset();draw.cover.reset();
      draw.lighting.enabled=false;draw.local_light_count=0;draw.linear_output=false;draw.resolve=false;draw.tonemap=RenderToneMap::None;
      if(packet.world_style[0][0]==1&&packet.world_style[0][1]>0) continue; // terrain layers past the first add colour only
      const auto mvp=MatrixToFloatV(MatrixMultiply(matrix_of(packet.world),vp));
      std::copy_n(mvp.v,16,draw.transform.begin());renderer_draw_mesh(ctx,draw);
    }
    renderer_end_target(ctx);
    pass.depth=renderer_target_texture(ctx,store.portable_rt_depth);pass.width=width;pass.height=height;
    const auto m=MatrixToFloatV(vp),inverse=MatrixToFloatV(MatrixInvert(vp));
    std::copy_n(m.v,16,pass.view_projection.begin());std::copy_n(inverse.v,16,pass.inverse_view_projection.begin());
    const Vector3 d=Vector3Normalize({-light.direction.x,-light.direction.y,-light.direction.z});
    const float cone=std::clamp(std::isfinite(light.sun_angle)?light.sun_angle:0.f,0.f,20.f)*0.5f*PI/180.0f;
    pass.sun={d.x,d.y,d.z,cone};
    const auto eye=store.portable_camera.position;
    pass.eye={eye.x,eye.y,eye.z,sun_traced?1.f:0.f};
    if(light.ray_traced_reflections)
      pass.reflection_roughness=std::clamp(std::isfinite(light.reflection_roughness)?light.reflection_roughness:0.5f,0.001f,1.f);
    if(light.ray_traced_reflections||grid) shade_inputs(store,pass);
    // light3d::shadow_lod_gap: the coarser casters must not shadow their own surface.
    if(light.shadow_lod>0&&!store.portable_rt_lod.empty()) pass.skip=std::clamp(std::isfinite(light.shadow_lod_gap)?light.shadow_lod_gap:0.f,0.f,10.f);
    pass.rays=unsigned(std::clamp(light.shadow_rays,1,16));
    pass.denoise=float(std::clamp(light.shadow_denoise,0,8));
    pass.frame=unsigned(++store.portable_rt_frame);
    pass.bias=0.03f;pass.max_distance=std::max(store.portable_camera.far_plane*2,10.f);
    renderer_trace_shadows(ctx,pass);
    if(previous) renderer_resume_target(ctx,previous);
  } catch(...) {
    if(resources.active_target&&resources.active_target.id!=previous.id) {try {renderer_end_target(ctx);} catch(...) {}}
    if(previous&&!resources.active_target) {try {renderer_resume_target(ctx,previous);} catch(...) {}}
    throw;
  }
  return true;
}
} // namespace njin
