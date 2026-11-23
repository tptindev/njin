#include "modules/particles_gpu.h"
#include "modules/render_stats.h"
#include "modules/fx.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_camera.h"
#include "njin_ecs.h"
#include "njin_texture.h"
#include "njin_view.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace njin {
void portable_particles_gpu_draw(context &ctx,entt::entity entity,const transform &tr,const particle_emitter &em) {
  auto *buffer=world(ctx).try_get<particle_gpu_buffer>(entity);if(!buffer||em.particles.empty()) return;
  particle_gpu_state &gpu=ctx_ref<particle_gpu_state>(ctx);
  for(float value:{em.gravity.x,em.gravity.y,em.drag,em.size_start,em.size_end,buffer->clock,tr.pos.x,tr.pos.y,tr.rot,tr.scale})
    if(!std::isfinite(value)) throw std::invalid_argument("njin GPU particles: non-finite setting");
  if(em.drag<0) throw std::invalid_argument("njin GPU particles: negative drag");
  if(!gpu.portable_white) {const unsigned char white[]{255,255,255,255};gpu.portable_white=renderer_create_texture(ctx,{1,1,white});}
  if(!gpu.portable_quad) {
    RenderVertex vertices[]{{-.5f,-.5f,0,0,0},{.5f,-.5f,0,1,0},{.5f,.5f,0,1,1},{-.5f,-.5f,0,0,0},{.5f,.5f,0,1,1},{-.5f,.5f,0,0,1}};
    for(auto &v:vertices) v.uv1={v.u,v.v};
    gpu.portable_quad=renderer_create_mesh(ctx,vertices);
  }
  if(!buffer->portable_instances||buffer->uploaded!=em.particles.size()) {
    auto snapshot=std::make_shared<std::vector<RenderInstance>>();snapshot->reserve(em.particles.size());
    for(const auto &p:em.particles) {
      for(float value:{p.pos.x,p.pos.y,p.velocity.x,p.velocity.y,p.age,p.life,p.rot,p.spin,p.size})
        if(!std::isfinite(value)) throw std::invalid_argument("njin GPU particles: non-finite spawn state");
      if(p.life<=0) throw std::invalid_argument("njin GPU particles: non-positive life");
      const float angle=p.rot*3.14159265359f/180,c=std::cos(angle),s=std::sin(angle);
      RenderInstance instance;
      // The unused Z basis of this 2D quad carries angular speed and size jitter.
      // The affine matrix remains nonsingular; its XY basis is the initial spin.
      instance.transform={c,s,0,0,-s,c,0,0,p.spin,p.size,1,0,p.pos.x,p.pos.y,0,1};
      instance.data={p.velocity.x,p.velocity.y,p.age,p.life};snapshot->push_back(instance);
    }
    buffer->portable_instances=std::move(snapshot);buffer->uploaded=em.particles.size();
  }
  MeshDraw draw{};draw.mesh=gpu.portable_quad;draw.texture=gpu.portable_white;draw.instances=buffer->portable_instances;draw.persistent_instances=true;
  draw.depth_test=false;draw.depth_write=false;draw.linear_sampling=true;draw.wrap_s=draw.wrap_t=RenderWrap::Clamp;
  if(em.blend!=blend_alpha&&em.blend!=blend_additive&&em.blend!=blend_multiply) throw std::invalid_argument("njin GPU particles: invalid blend mode");
  draw.blend=em.blend==blend_additive?RenderBlend::Additive:em.blend==blend_multiply?RenderBlend::Multiply:RenderBlend::Alpha;
  int mode=em.shape==particle_square?1:em.shape==particle_soft?3:0;
  vec2 aspect{1,1};
  const auto *slot=texture_slot_of(ctx_ref<texture_store>(ctx),em.texture);
  if(em.texture.id&&!slot) throw std::invalid_argument("njin GPU particles: invalid texture");
  if(slot) {
    if(!slot->portable) throw std::invalid_argument("njin GPU particles: missing backend texture");
    const auto area=texture_area(*slot);const bool whole=em.source.size.x==0||em.source.size.y==0;
    const auto extent=whole?vec2{area.width,area.height}:em.source.size;const auto origin=vec2{area.x,area.y}+(whole?vec2{}:em.source.pos);
    if(extent.x<=0||extent.y<=0) throw std::invalid_argument("njin GPU particles: invalid source rectangle");
    aspect=extent/std::max(extent.x,extent.y);mode=2;draw.texture=slot->portable;draw.linear_sampling=slot->filter!=filter_nearest;
    draw.color_uv.values={extent.x/slot->texture.width,0,0,extent.y/slot->texture.height,origin.x/slot->texture.width,origin.y/slot->texture.height};
  } else if(em.shape<particle_circle||em.shape>particle_soft) throw std::invalid_argument("njin GPU particles: invalid shape");
  const auto color=[](rgba c){for(float value:{c.r,c.g,c.b,c.a}) if(!std::isfinite(value)||value<0||value>1) throw std::invalid_argument("njin GPU particles: invalid color");return std::array<float,4>{c.r,c.g,c.b,c.a};};
  draw.world_style[0]={4,float(mode),em.size_start,em.size_end};draw.world_style[1]={em.gravity.x,em.gravity.y,em.drag,buffer->clock};
  draw.world_style[2]=color(em.color_start);draw.world_style[3]=color(em.color_end);
  vec2 emitter{},ex{1,0},ey{0,1};
  if(em.local_space) {const float angle=tr.rot*3.14159265359f/180,c=std::cos(angle)*tr.scale,s=std::sin(angle)*tr.scale;emitter=tr.pos;ex={c,s};ey={-s,c};}
  draw.world_style[4]={emitter.x,emitter.y,ex.x,ex.y};draw.world_style[5]={ey.x,ey.y,aspect.x,aspect.y};
  auto camera=camera_active(ctx);vec2 shake{};float turn=0;if(fx_shake_sample(ctx,shake,turn)) {camera.offset+=shake;camera.rotation+=turn;}
  const auto &view=ctx_ref<view_state>(ctx);const auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;const auto &state=resources.draw_2d;
  const bool virtual_target=view->drawing&&resources.active_target.id==view->portable_target.id;
  vec2 screen=virtual_target?view_logical_size(view):window_size(ctx);
  if(resources.active_target&&!virtual_target) {const auto &target=resources.targets.at(resources.active_target.id);screen={float(target.width),float(target.height)};}
  const float angle=camera.rotation*3.14159265359f/180,c=std::cos(angle)*camera.zoom,s=std::sin(angle)*camera.zoom;
  const vec2 delta=state.offset-camera.target;
  draw.transform={2*c*state.scale/screen.x,-2*s*state.scale/screen.y,0,0,-2*s*state.scale/screen.x,-2*c*state.scale/screen.y,0,0,0,0,1,0,
    2*(camera.offset.x+c*delta.x-s*delta.y)/screen.x-1,1-2*(camera.offset.y+s*delta.x+c*delta.y)/screen.y,0,1};
  draw.tint[3]=state.alpha;draw.scissor_enabled=state.clipped;draw.scissor=state.clip;
  renderer_draw_mesh(ctx,draw);
  render_stats &stats=ctx_ref<render_stats>(ctx);stats.emitters++;stats.particles+=u32(em.particles.size());stats.particles_gpu+=u32(em.particles.size());stats.instanced_calls++;stats.batches++;
}
}
