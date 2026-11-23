#include "modules/post_fx.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_view.h"
#include "njin_shader.h"
#include "modules/camera.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace njin {
void portable_post_fx_validate(const post_fx &f) {
  for(float x:{f.brightness,f.contrast,f.saturation,f.sepia,f.vignette,f.vignette_radius,f.vignette_softness,
      f.bloom,f.bloom_threshold,f.bloom_radius,f.blur,f.dof,f.dof_focus,f.dof_range,f.dof_falloff,
      f.dof_center.x,f.dof_center.y,f.dof_center.z,f.dof_radius,f.chromatic,f.scanlines,f.scanline_size,
      f.crt_curve,f.pixelate,f.grain})
    if(!std::isfinite(x)) throw std::invalid_argument("njin post_fx: non-finite setting");
  for(auto color:{f.tint,f.vignette_color,f.dof_haze}) for(float x:{color.r,color.g,color.b,color.a})
    if(!std::isfinite(x)||x<0||x>1) throw std::invalid_argument("njin post_fx: invalid color");
  if(f.contrast<0||f.saturation<0||f.sepia<0||f.sepia>1||f.vignette<0||f.vignette>1||f.vignette_softness<=0||
     f.bloom<0||f.bloom_radius<=0||f.blur<0||f.dof<0||f.dof_range<0||f.dof_falloff<=0||f.dof_radius<0||
     f.chromatic<0||f.scanlines<0||f.scanlines>1||f.scanline_size<=0||f.crt_curve<0||f.pixelate<0||f.grain<0)
    throw std::invalid_argument("njin post_fx: setting outside supported range");
}
namespace {
constexpr RenderVertex quad[]{{-1,1,0,0,0},{1,1,0,1,0},{1,-1,0,1,1},{-1,1,0,0,0},{1,-1,0,1,1},{-1,-1,0,0,1}};
void draw(context &ctx,RenderTextureHandle source,const RenderPostProcess &post) {
  MeshDraw packet{};packet.vertices=quad;packet.texture=source;packet.post=post;
  packet.depth_test=false;packet.opaque_alpha=true;packet.blend=RenderBlend::Opaque;
  packet.linear_sampling=true;packet.wrap_s=packet.wrap_t=RenderWrap::Clamp;
  renderer_draw_mesh(ctx,packet);
}
}
void portable_post_world(context &ctx) {
  auto &view=ctx_ref<view_state>(ctx);const auto &chain=ctx_ref<post_chain>(ctx);
  if(!post_chain_active(chain)) return;
  if(!view->drawing) throw std::logic_error("njin post_fx: enable effects before phase_render so the world target can be allocated");
  auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
  if(resources.active_target.id!=view->portable_target.id) throw std::logic_error("njin post_fx: unclosed render target");
  const unsigned w=view->portable_width,h=view->portable_height;
  if(view->portable_post_targets[0]) {
    const auto &old=resources.targets.at(view->portable_post_targets[0].id);
    if(old.width!=w||old.height!=h) {
      for(auto t:view->portable_post_targets) if(ctx_ref<window_guard>(ctx).graphics->target_in_use(t))
        throw std::logic_error("njin post_fx: cannot resize a submitted target");
      for(auto &t:view->portable_post_targets) {renderer_destroy_target(ctx,t);t={};}
    }
  }
  if(!view->portable_post_targets[0]) {
    std::array<RenderTargetHandle,4> made{};
    try {for(auto &t:made) t=renderer_create_target(ctx,w,h);}
    catch(...) {for(auto t:made) if(t) renderer_destroy_target(ctx,t);throw;}
    view->portable_post_targets=made;
  }
  renderer_end_target(ctx);
  const auto &fx=chain->settings;
  RenderPostProcess p;p.texel={1.f/w,1.f/h};
  p.legacy={{{fx.brightness,fx.contrast,fx.saturation,fx.sepia},
    {fx.tint.r,fx.tint.g,fx.tint.b,fx.tint.a},
    {fx.vignette,fx.vignette_radius,fx.vignette_softness,fx.bloom},
    {fx.vignette_color.r,fx.vignette_color.g,fx.vignette_color.b,fx.vignette_color.a},
    {fx.chromatic,fx.scanlines,fx.scanline_size,fx.crt_curve},
    {fx.pixelate,fx.grain,ctx_ref<time_state>(ctx).elapsed,0},
    {fx.dof_focus,fx.dof_range,fx.dof_falloff,0},
    {fx.dof_center.x,fx.dof_center.y,fx.dof_center.z,fx.dof_radius},
    {fx.dof_haze.r,fx.dof_haze.g,fx.dof_haze.b,fx.dof_haze.a},{}}};
  auto source=renderer_target_texture(ctx,view->portable_target);
  const auto texture=[&](unsigned n){return renderer_target_texture(ctx,view->portable_post_targets[n]);};
  const auto pass=[&](unsigned n,RenderTextureHandle image,const RenderPostProcess &settings) {
    renderer_begin_target(ctx,view->portable_post_targets[n]);
    try {draw(ctx,image,settings);renderer_end_target(ctx);}
    catch(...) {try {renderer_end_target(ctx);} catch(...) {}throw;}
  };
  const auto blur=[&](RenderTextureHandle image,float radius,unsigned out) {
    auto b=p;b.mode=RenderPostMode::LegacyBlur;b.legacy[9]={std::max(radius/3.23f,.01f),0,0,0};
    pass(0,image,b);b.legacy[9]={0,std::max(radius/3.23f,.01f),0,0};pass(out,texture(0),b);
  };
  try {
    if(fx.blur>0) {blur(source,fx.blur,1);source=texture(1);}
    if(fx.dof>0&&view->portable_depth) {
      blur(source,fx.dof,2);auto d=p;d.mode=RenderPostMode::LegacyDOF;
      d.auxiliary=texture(2);d.secondary=view->portable_depth;d.inverse_projection=view->portable_depth_inverse;
      d.legacy[9]={view->portable_depth_planes[0],view->portable_depth_planes[1],0,0};
      pass(3,source,d);source=texture(3);
    }
    if(fx.bloom>0) {
      if(source.id==texture(1).id) {pass(3,source,{});source=texture(3);}
      auto b=p;b.mode=RenderPostMode::LegacyBright;b.amount=fx.bloom_threshold;pass(0,source,b);
      b.mode=RenderPostMode::LegacyBlur;b.legacy[9]={fx.bloom_radius*2,0,0,0};pass(1,texture(0),b);
      b.legacy[9]={0,fx.bloom_radius*2,0,0};pass(2,texture(1),b);p.auxiliary=texture(2);
    }
    p.mode=RenderPostMode::LegacyPost;
    pass(0,source,p);
    renderer_resume_target(ctx,view->portable_target);draw(ctx,texture(0),{});
  } catch(...) {
    if(!resources.active_target) try {renderer_resume_target(ctx,view->portable_target);} catch(...) {}
    throw;
  }
}
bool portable_camera_post_wanted(const context &ctx) {
  return shader_slot_of(ctx_ref<shader_store>(ctx),ctx_ref<camera_post>(ctx)->shader)!=nullptr;
}
void portable_camera_post(context &ctx) {
  auto &view=ctx_ref<view_state>(ctx);
  const auto shader=ctx_ref<camera_post>(ctx)->shader;
  if(!view->drawing||!portable_camera_post_wanted(ctx)) return;
  auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
  if(resources.active_target.id!=view->portable_target.id) throw std::logic_error("njin camera post shader: unclosed render target");
  const unsigned w=view->portable_width,h=view->portable_height;
  if(view->portable_camera_target) {
    const auto &old=resources.targets.at(view->portable_camera_target.id);
    if(old.width!=w||old.height!=h) {
      if(ctx_ref<window_guard>(ctx).graphics->target_in_use(view->portable_camera_target))
        throw std::logic_error("njin camera post shader: cannot resize a submitted target");
      renderer_destroy_target(ctx,view->portable_camera_target);view->portable_camera_target={};
    }
  }
  if(!view->portable_camera_target) view->portable_camera_target=renderer_create_target(ctx,w,h);
  renderer_end_target(ctx);
  try {
    // The world through the game's shader (its texture0 is the frame), then copied back.
    renderer_begin_target(ctx,view->portable_camera_target);
    try {
      MeshDraw packet{};packet.vertices=quad;packet.texture=renderer_target_texture(ctx,view->portable_target);
      packet.depth_test=false;packet.opaque_alpha=true;packet.blend=RenderBlend::Opaque;
      packet.linear_sampling=true;packet.wrap_s=packet.wrap_t=RenderWrap::Clamp;
      packet.shader=portable_shader_bindings(ctx,shader);
      renderer_draw_mesh(ctx,packet);
      renderer_end_target(ctx);
    } catch(...) {try {renderer_end_target(ctx);} catch(...) {}throw;}
    renderer_resume_target(ctx,view->portable_target);
    draw(ctx,renderer_target_texture(ctx,view->portable_camera_target),{});
  } catch(...) {
    if(!resources.active_target) try {renderer_resume_target(ctx,view->portable_target);} catch(...) {}
    throw;
  }
}
}
