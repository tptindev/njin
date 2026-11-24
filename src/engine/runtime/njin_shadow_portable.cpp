#include "njin_shadow_portable.h"
#include "njin_model.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_window.h"
#include "njin_raytrace.h"
#include "njin_log_impl.h"
#include "njin_occlusion_portable.h"
#include "njin_rlmath.h"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <stdexcept>

namespace njin {
namespace {
struct ShadowView { Matrix projection; unsigned size,x=0,y=0; int matrix=-1; float normal_bias=0; };
Vector3 ray(vec3 v) { return {v.x,v.y,v.z}; }
vec3 axis_up(vec3 direction) { return std::abs(direction.y)>0.95f?vec3{0,0,1}:vec3{0,1,0}; }
Matrix look(vec3 p,vec3 target,vec3 up) { return MatrixLookAt(ray(p),ray(target),ray(up)); }
void copy_rows(RenderShadowData &data,int index,const Matrix &m) {
  data.matrices[index*4+0]={m.m0,m.m4,m.m8,m.m12};
  data.matrices[index*4+1]={m.m1,m.m5,m.m9,m.m13};
  data.matrices[index*4+2]={m.m2,m.m6,m.m10,m.m14};
  data.matrices[index*4+3]={m.m3,m.m7,m.m11,m.m15};
}
Matrix from_array(const std::array<float,16> &m) {
  return {m[0],m[4],m[8],m[12],m[1],m[5],m[9],m[13],m[2],m[6],m[10],m[14],m[3],m[7],m[11],m[15]};
}
Matrix cascade(const camera3d &camera,vec3 direction,float near_plane,float far_plane,float aspect,float margin,unsigned size) {
  const auto forward=normalize(camera.target-camera.position),right=normalize(cross(forward,camera.up)),up=cross(right,forward);
  // Cascade sizing/texel anchoring adapted from Bevy v0.18.0 cascade.rs (MIT).
  // https://github.com/bevyengine/bevy/blob/v0.18.0/crates/bevy_light/src/cascade.rs
  // See THIRD_PARTY_NOTICES.md. Use camera-space dimensions so rotating the view
  // does not change the light projection scale.
  auto view=look({0,0,0},direction,axis_up(direction));
  Vector3 minimum{INFINITY,INFINITY,INFINITY},maximum{-INFINITY,-INFINITY,-INFINITY};
  const auto half_height=[&](float d) { return camera.projection==camera3d_perspective?
    std::tan(camera.fovy*PI/360)*d:camera.ortho_height*0.5f; };
  const float hn=half_height(near_plane),hf=half_height(far_plane);
  const float diagonal_far=2*hf*std::sqrt(1+aspect*aspect);
  const float diagonal_body=std::sqrt((hn+hf)*(hn+hf)*(1+aspect*aspect)+(far_plane-near_plane)*(far_plane-near_plane));
  // Integer extent / power-of-two resolution produces exactly representable
  // texel sizes. Padding leaves room for snapping the center by half a texel.
  const float extent=std::ceil(std::max(diagonal_far,diagonal_body)*size/(size-4));
  const float texel=extent/size;
  for(float d:{near_plane,far_plane}) {
    const float h=half_height(d);
    for(float x:{-1.0f,1.0f}) for(float y:{-1.0f,1.0f}) {
      const auto p=Vector3Transform(ray(camera.position+forward*d+right*(h*aspect*x)+up*(h*y)),view);
      minimum=Vector3Min(minimum,p); maximum=Vector3Max(maximum,p);
    }
  }
  view.m12=-std::round((minimum.x+maximum.x)*0.5f/texel)*texel;
  view.m13=-std::round((minimum.y+maximum.y)*0.5f/texel)*texel;
  view.m14=-maximum.z-margin;
  return MatrixMultiply(view,MatrixOrtho(-extent*0.5f,extent*0.5f,-extent*0.5f,extent*0.5f,
    0.001f,maximum.z-minimum.z+margin*2));
}
}
std::shared_ptr<const RenderShadowData> portable_render_shadows(context &ctx,model_store &store) {
  const auto &cfg=store.portable_frame_light; const auto &camera=store.portable_camera;
  if(!cfg.shadows && (cfg.source_shadows==0 || std::none_of(store.portable_lights.begin(),store.portable_lights.end(),[](const auto &l){return l.shadows;}))) return {};
  auto data=std::make_shared<RenderShadowData>(); std::vector<ShadowView> views;
  const auto direction=normalize(cfg.direction); int matrix_count=0;
  if(cfg.shadows) {
    const auto forward=normalize(camera.target-camera.position);
    data->forward={forward.x,forward.y,forward.z,float(cfg.shadow_cascades)};
    const auto size=window_size(ctx);
    const auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
    const float aspect=resources.active_target?float(resources.targets.at(resources.active_target.id).width)/resources.targets.at(resources.active_target.id).height:size.x/size.y;
    const float end=std::min(camera.far_plane,cfg.shadow_range);
    if(end<=camera.near_plane) throw std::invalid_argument("njin shadows: shadow range must exceed the camera near plane");
    float start=camera.near_plane;
    for(int i=0;i<cfg.shadow_cascades;++i) {
      const float t=float(i+1)/cfg.shadow_cascades;
      const float stop=(1-cfg.shadow_cascade_split)*(camera.near_plane+(end-camera.near_plane)*t)+
        cfg.shadow_cascade_split*camera.near_plane*std::pow(end/camera.near_plane,t);
      Matrix projection;
      if(cfg.shadow_cascades==1) {
        const auto center=camera.target;
        projection=MatrixMultiply(look(center-direction*(cfg.shadow_range*2),center,axis_up(direction)),
          MatrixOrtho(-cfg.shadow_range,cfg.shadow_range,-cfg.shadow_range,cfg.shadow_range,0.01f,cfg.shadow_range*4));
        data->splits[i]=camera.far_plane;
      } else {
        projection=cascade(camera,direction,start,stop,aspect,cfg.shadow_range,unsigned(cfg.shadow_size));
        data->splits[i]=stop;
      }
      views.push_back({projection,unsigned(cfg.shadow_size),0,0,matrix_count++,cfg.shadow_range/cfg.shadow_size*0.35f});
      start=stop;
    }
  }
  int sources=0;
  const vec3 directions[]{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
  const vec3 ups[]{{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
  for(size_t i=0;i<store.portable_lights.size();++i) {
    const auto &l=store.portable_lights[i];
    if(!l.shadows) continue;
    if(sources>=cfg.source_shadows) {
      if(cfg.source_shadows>0) NJIN_WARN_LIMITED("bgfx shadows: configured source_shadows budget (%d) reached",cfg.source_shadows);
      continue;
    }
    ++sources; data->local_base[i]=int(views.size());
    // The first three shadowed lamps of a ray traced frame read the traced mask's
    // g, b, a (njin_raytrace_portable.cpp); their maps stay for transparent receivers.
    if(cfg.ray_traced_shadows&&sources<=3&&renderer_compute_available(ctx)) data->ray_channel[i]=std::uint8_t(sources);
    const float near_plane=std::min(0.05f,l.radius*0.01f);
    const float normal_bias=l.radius/cfg.source_shadow_size*0.15f;
    if(l.kind==light3d_spot) {
      const auto dir=normalize(l.direction);
      views.push_back({MatrixMultiply(look(l.position,l.position+dir,axis_up(dir)),
        MatrixPerspective(l.cone*PI/180,1,near_plane,l.radius)),unsigned(cfg.source_shadow_size),0,0,matrix_count++,normal_bias});
    } else for(int face=0;face<6;++face)
      views.push_back({MatrixMultiply(look(l.position,l.position+directions[face],ups[face]),
        MatrixPerspective(PI/2,1,near_plane,l.radius)),unsigned(cfg.source_shadow_size),0,0,-1,normal_bias});
  }
  if(views.empty()) return {};
  const bool gpu_cull=render3d_options_get(ctx).occlusion;
  if(gpu_cull&&!renderer_gpu_occlusion_available(ctx)) throw std::runtime_error("njin GPU shadow culling requires compute, indirect draws and R32F images");
  std::vector<std::array<float,4>> bounds;
  if(gpu_cull) for(const auto &packet:store.portable_queue) bounds.push_back(portable_cull_bounds(ctx,packet));
  if(views.size()>28 || matrix_count>8) throw std::runtime_error("njin shadows: atlas view budget exceeded");
  const unsigned limit=ctx_ref<window_guard>(ctx).graphics->max_texture_size();
  std::vector<size_t> order(views.size()); std::iota(order.begin(),order.end(),0);
  std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return views[a].size>views[b].size;});
  unsigned width=views[order[0]].size,height=0;
  for(;;) {
    unsigned x=0,y=0,row=0;
    for(auto index:order) { auto &v=views[index];
      if(x+v.size>width) { x=0; y+=row; row=0; }
      v.x=x; v.y=y; x+=v.size; row=std::max(row,v.size);
    }
    height=y+row;
    if(width<=limit && height<=limit) break;
    if(width>limit/2) throw std::runtime_error("njin shadows: requested atlas exceeds device texture limits; reduce shadow sizes/counts");
    width*=2;
  }
  if(!store.portable_shadow || store.portable_shadow_width!=width || store.portable_shadow_height!=height) {
    const auto target=renderer_create_target(ctx,width,height);
    try { if(store.portable_shadow) renderer_destroy_target(ctx,store.portable_shadow); }
    catch(...) { renderer_destroy_target(ctx,target); throw; }
    store.portable_shadow=target; store.portable_shadow_width=width; store.portable_shadow_height=height;
  }
  data->texture=renderer_target_texture(ctx,store.portable_shadow); data->width=width; data->height=height;
  data->softness=cfg.shadow_softness; data->views=unsigned(views.size());
  auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
  const auto previous=resources.active_target;
  if(previous) renderer_end_target(ctx);
  try {
    renderer_begin_target(ctx,store.portable_shadow,{1,1,1,1});
    for(size_t i=0;i<views.size();++i) {
      const auto &v=views[i]; Matrix tile=MatrixIdentity();
      tile.m0=float(v.size)/width; tile.m5=float(v.size)/height;
      tile.m12=float(v.x*2+v.size)/width-1; tile.m13=1-float(v.y*2+v.size)/height;
      const auto projection=MatrixMultiply(v.projection,tile);
      if(v.matrix>=0) copy_rows(*data,v.matrix,projection);
      data->regions[i]={float(v.x)/width,float(v.y)/height,float(v.x+v.size)/width,float(v.y+v.size)/height};
      data->metadata[i]={v.normal_bias,0.00002f,float(std::max(v.matrix,0)),0};
      for(size_t packet_index=0;packet_index<store.portable_queue.size();++packet_index) {
        const auto &packet=store.portable_queue[packet_index];if(!packet.cast_shadow) continue;
        MeshDraw draw=packet; draw.shadows.reset(); draw.shadow_pass=true;
        draw.gpu_cull.reset();
        // Use the light's full clip volume, before atlas tiling. Camera visibility
        // and camera LOD never remove an off-camera caster from this input.
        if(gpu_cull&&packet.world_style[0][0]==0&&bounds[packet_index][3]>0) {
          auto cull=packet.gpu_cull?std::make_shared<RenderGpuCull>(*packet.gpu_cull):std::make_shared<RenderGpuCull>();
          cull->bounds=bounds[packet_index];cull->view_key=unsigned(i+1);cull->occlusion=false;cull->frustum=true;cull->shadow_lod=cull->select_lod;
          const auto clip=MatrixToFloatV(MatrixMultiply(from_array(packet.world),v.projection));
          std::copy_n(clip.v,16,cull->view_projection.begin());draw.gpu_cull=std::move(cull);
        }
        if(packet.transparent) draw.alpha_cutoff=std::max(draw.alpha_cutoff,.5f);
        draw.lighting.enabled=false; draw.local_light_count=0; draw.linear_output=false;
        draw.resolve=false; draw.tonemap=RenderToneMap::None;
        draw.scissor_enabled=true; draw.scissor={int(v.x),int(v.y),v.size,v.size};
        const auto mvp=MatrixToFloatV(MatrixMultiply(from_array(packet.world),projection));
        std::copy_n(mvp.v,16,draw.transform.begin()); renderer_draw_mesh(ctx,draw);
      }
    }
    renderer_end_target(ctx);
    if(previous) renderer_resume_target(ctx,previous);
  } catch(...) {
    if(resources.active_target.id==store.portable_shadow.id) { try { renderer_end_target(ctx); } catch(...) {} }
    if(previous && !resources.active_target) { try { renderer_resume_target(ctx,previous); } catch(...) {} }
    throw;
  }
  return data;
}
}
