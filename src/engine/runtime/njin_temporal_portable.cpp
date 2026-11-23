#include "njin_temporal_portable.h"
#include "njin_model.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include <chrono>
#include <stdexcept>
namespace njin {
namespace {
void copy_matrix(std::array<float,16> &out,Matrix m) {const auto f=MatrixToFloatV(m);std::copy_n(f.v,16,out.begin());}
std::uint64_t hash(std::uint64_t v) {v+=0x9e3779b97f4a7c15ULL;v=(v^(v>>30))*0xbf58476d1ce4e5b9ULL;v=(v^(v>>27))*0x94d049bb133111ebULL;return v^(v>>31);}
void fullscreen(context &ctx,RenderTextureHandle source,const RenderPostProcess &post,bool depth=false) {
  const RenderVertex vertices[]{{-1,1,0,0,0},{1,1,0,1,0},{1,-1,0,1,1},{-1,1,0,0,0},{1,-1,0,1,1},{-1,-1,0,0,1}};
  MeshDraw draw;draw.vertices=vertices;draw.texture=source;draw.post=post;draw.depth_test=depth;draw.blend=RenderBlend::Opaque;renderer_draw_mesh(ctx,draw);
}
RenderPostProcess parameters(context &ctx,model_store &store) {
  const auto &t=store.portable_temporal;RenderPostProcess p;
  p.depth=renderer_target_depth_texture(ctx,store.portable_hdr);p.velocity=renderer_target_texture(ctx,t.velocity);
  p.texel={1.f/t.width,1.f/t.height};copy_matrix(p.inverse_projection,MatrixInvert(store.portable_projection));
  copy_matrix(p.previous_clip,MatrixMultiply(MatrixInvert(store.portable_view),t.previous_jittered_vp));
  copy_matrix(p.previous_view,MatrixMultiply(MatrixInvert(store.portable_view),t.previous_view));
  p.temporal={t.previous_jitter.x-t.jitter.x,t.previous_jitter.y-t.jitter.y,t.valid?1.f:0.f,store.portable_frame_post.taa_sharpen};return p;
}
}
void portable_temporal_free(context &ctx,model_store &store) {
  auto &t=store.portable_temporal;auto &gpu=*ctx_ref<window_guard>(ctx).graphics;
  const RenderTargetHandle handles[]{t.velocity,t.blur,t.history[0],t.history[1],t.depth[0],t.depth[1]};
  for(auto h:handles) if(h&&gpu.target_in_use(h)) throw std::logic_error("njin temporal: cannot resize targets used this frame");
  for(auto h:handles) if(h) renderer_destroy_target(ctx,h);
  t.velocity={};t.blur={};t.history={};t.depth={};t.valid=t.taa_valid=false;
}
void portable_temporal_begin(context &ctx,model_store &store,Matrix &projection,const Matrix &view,unsigned w,unsigned h) {
  auto &t=store.portable_temporal;const auto serial=ctx_ref<window_guard>(ctx).graphics->frame_serial();
  t.first=t.frame!=serial;t.frame=serial;t.active=t.first&&(store.portable_post.taa||store.portable_post.motion_blur>0);
  t.next_id=0;t.next_namespace=0;t.occurrences.clear();t.current.clear();t.jitter={};
  if(!t.first) return;
  if(!t.active) {t.valid=t.taa_valid=false;t.previous.clear();return;}
  if(t.width!=w||t.height!=h) {portable_temporal_free(ctx,store);t.width=w;t.height=h;}
  const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  const auto &c=store.portable_camera;
  const auto vector=[](vec3 p){return Vector3{p.x,p.y,p.z};};
  const auto direction=[&](const camera3d &p){return Vector3Normalize(Vector3Subtract(vector(p.target),vector(p.position)));};
  const bool cut=Vector3Distance(vector(c.position),vector(t.previous_camera.position))>std::max(2.f,Vector3Distance(vector(c.position),vector(c.target))*.5f)||Vector3DotProduct(direction(c),direction(t.previous_camera))<.5f;
  if(t.last_frame+1!=serial||now-t.time>.25||cut) t.valid=t.taa_valid=false;
  if(!store.portable_post.taa||t.exposure!=store.portable_light.exposure) t.taa_valid=false;
  t.time=now;t.current_vp=MatrixMultiply(view,projection);
  if(store.portable_post.taa) {
    const auto halton=[](unsigned i,unsigned base){float f=1,r=0;while(i) {f/=base;r+=f*(i%base);i/=base;}return r;};
    t.jitter_index=t.jitter_index%8+1;
    const float dx=2*(halton(t.jitter_index,2)-.5f)/w,dy=2*(halton(t.jitter_index,3)-.5f)/h;
    // Add clip-space translation, valid for both perspective and orthographic.
    projection.m0+=dx*projection.m3;projection.m4+=dx*projection.m7;projection.m8+=dx*projection.m11;projection.m12+=dx*projection.m15;
    projection.m1+=dy*projection.m3;projection.m5+=dy*projection.m7;projection.m9+=dy*projection.m11;projection.m13+=dy*projection.m15;
    t.jitter={dx*.5f,-dy*.5f};
  }
}
std::uint64_t portable_temporal_key(model_store &store,std::uint64_t model) {
  auto &t=store.portable_temporal;if(!t.active) return 0;
  const auto id=t.next_id,space=t.next_namespace;t.next_id=t.next_namespace=0;
  const auto key=hash(model^0xb834791dc3ULL)^(id?hash(id)^hash(space^0x31d790b1ULL):hash(++t.occurrences[model]^0x6891a34dULL));
  return key?key:1;
}
void portable_temporal_record(model_store &store,MeshDraw &draw,std::uint64_t key,const Matrix &world) {
  auto &t=store.portable_temporal;if(!t.active||!key||draw.instances) return;
  const auto previous=t.previous.find(key);const bool valid=t.valid&&previous!=t.previous.end();
  const auto &old=valid?previous->second:PortableMotionRecord{world,draw.geometry};
  copy_matrix(draw.motion_current,MatrixMultiply(world,t.current_vp));
  copy_matrix(draw.motion_previous,MatrixMultiply(old.world,valid?t.previous_vp:t.current_vp));
  copy_matrix(draw.motion_view,MatrixMultiply(old.world,valid?t.previous_view:store.portable_view));
  draw.motion_recorded=true;draw.previous_geometry=old.geometry;t.current[key]={world,draw.geometry};
}
void portable_temporal_velocity(context &ctx,model_store &store) {
  auto &t=store.portable_temporal;if(!t.active) return;
  if(!t.velocity) t.velocity=renderer_create_hdr_target(ctx,t.width,t.height);
  RenderPostProcess init;init.mode=RenderPostMode::VelocityDepth;
  renderer_begin_target(ctx,t.velocity);fullscreen(ctx,renderer_target_depth_texture(ctx,store.portable_hdr),init,true);
  if(t.valid) for(const auto &packet:store.portable_queue) if(!packet.transparent&&!packet.shadow_only&&!packet.instances&&packet.motion_recorded) {
    auto draw=packet;draw.velocity_pass=true;draw.shadows.reset();draw.environment.reset();draw.local_light_count=0;draw.blend=RenderBlend::Opaque;renderer_draw_mesh(ctx,draw);
  }
  renderer_end_target(ctx);
}
RenderTextureHandle portable_temporal_resolve(context &ctx,model_store &store,RenderTextureHandle source) {
  auto &t=store.portable_temporal;if(!t.active||!store.portable_frame_post.taa) return source;
  if(!t.history[0]) {
    std::array<RenderTargetHandle,4> targets{};
    try {for(auto &h:targets) h=renderer_create_hdr_target(ctx,t.width,t.height);} catch(...) {for(auto h:targets) if(h) renderer_destroy_target(ctx,h);throw;}
    t.history={targets[0],targets[1]};t.depth={targets[2],targets[3]};
  }
  const unsigned next=1-t.history_index;auto p=parameters(ctx,store);p.mode=RenderPostMode::TAA;p.reset=!t.valid||!t.taa_valid;
  if(!p.reset) {p.auxiliary=renderer_target_texture(ctx,t.history[t.history_index]);p.secondary=renderer_target_texture(ctx,t.depth[t.history_index]);}
  renderer_begin_target(ctx,t.history[next]);fullscreen(ctx,source,p);renderer_end_target(ctx);
  p.mode=RenderPostMode::LinearDepth;renderer_begin_target(ctx,t.depth[next]);fullscreen(ctx,p.depth,p);renderer_end_target(ctx);
  t.history_index=next;t.taa_valid=true;return renderer_target_texture(ctx,t.history[next]);
}
RenderTextureHandle portable_motion_blur(context &ctx,model_store &store,RenderTextureHandle source) {
  auto &t=store.portable_temporal;if(!t.active||!t.valid||store.portable_frame_post.motion_blur<=0) return source;
  if(!t.blur) t.blur=renderer_create_hdr_target(ctx,t.width,t.height);
  auto p=parameters(ctx,store);p.mode=RenderPostMode::MotionBlur;p.amount=store.portable_frame_post.motion_blur;p.samples=float(store.portable_frame_post.motion_blur_samples);
  // Keep the stationary center exact; a second sampler filters moving taps.
  p.auxiliary=source;p.auxiliary_linear=true;
  renderer_begin_target(ctx,t.blur);fullscreen(ctx,source,p);renderer_end_target(ctx);return renderer_target_texture(ctx,t.blur);
}
void portable_temporal_finish(model_store &store) {
  auto &t=store.portable_temporal;if(!t.active) return;
  t.previous=std::move(t.current);t.previous_view=store.portable_view;t.previous_vp=t.current_vp;
  t.previous_jittered_vp=store.portable_view_projection;t.previous_jitter=t.jitter;
  t.previous_camera=store.portable_camera;t.exposure=store.portable_frame_light.exposure;t.last_frame=t.frame;t.valid=true;
}
}
