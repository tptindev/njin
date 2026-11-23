#include "njin_post_portable.h"
#include "njin_model.h"
#include "njin_atmosphere_portable.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include <algorithm>
#include <raymath.h>
#include <cmath>
#include <stdexcept>
#include <AreaTex.h>
#include <SearchTex.h>
#include <chrono>
namespace njin {
void portable_post_validate(const post3d &p) {
  if(!std::isfinite(p.motion_blur)||p.motion_blur<0||p.motion_blur>1||p.motion_blur_samples<2||p.motion_blur_samples>32||!std::isfinite(p.taa_sharpen)||p.taa_sharpen<0||p.taa_sharpen>1) throw std::invalid_argument("njin temporal: invalid blur strength/sample count or TAA sharpening");
  if(!std::isfinite(p.outline)||p.outline<0||p.outline>1||!std::isfinite(p.outline_width)||p.outline_width<.5f||p.outline_width>4||!std::isfinite(p.outline_crease)||p.outline_crease<1||p.outline_crease>180||!std::isfinite(p.outline_fade)||p.outline_fade<0)
    throw std::invalid_argument("njin bgfx post: invalid outline strength, width, crease or fade");
  for(float c:{p.outline_color.r,p.outline_color.g,p.outline_color.b,p.outline_color.a,p.shafts_color.r,p.shafts_color.g,p.shafts_color.b,p.shafts_color.a})
    if(!std::isfinite(c)||c<0||c>1) throw std::invalid_argument("njin bgfx post: invalid outline or shafts color");
  if(!std::isfinite(p.shafts)||p.shafts<0||p.shafts>2||!std::isfinite(p.shafts_length)||p.shafts_length<0||p.shafts_length>1||!std::isfinite(p.flare)||p.flare<0||p.flare>2||!std::isfinite(p.flare_halo)||p.flare_halo<0)
    throw std::invalid_argument("njin bgfx post: invalid shafts or flare settings");
  if(!std::isfinite(p.ssr)||p.ssr<0||p.ssr>1||!std::isfinite(p.ssr_distance)||p.ssr_distance<=0||p.ssr_steps<8||p.ssr_steps>128||!std::isfinite(p.ssr_thickness)||p.ssr_thickness<=0||!std::isfinite(p.ssr_sky)||p.ssr_sky<0||p.ssr_sky>1)
    throw std::invalid_argument("njin bgfx post: invalid SSR strength, distance, steps, thickness or sky");
  if(p.aa!=aa3d_none&&p.aa!=aa3d_fxaa&&p.aa!=aa3d_smaa) throw std::invalid_argument("njin bgfx post: invalid anti-aliasing mode");
  if(!std::isfinite(p.bloom)||p.bloom<0||p.bloom>1||!std::isfinite(p.bloom_radius)||p.bloom_radius<.25f||p.bloom_radius>2)
    throw std::invalid_argument("njin bgfx post: invalid bloom strength or radius");
  if(!std::isfinite(p.ssao)||p.ssao<0||p.ssao>1||!std::isfinite(p.ssao_radius)||p.ssao_radius<=0||p.ssao_samples<4||p.ssao_samples>32) throw std::invalid_argument("njin bgfx post: invalid SSAO strength, radius or samples");
}
namespace {
constexpr RenderVertex quad[]{{-1,1,0,0,0},{1,1,0,1,0},{1,-1,0,1,1},{-1,1,0,0,0},{1,-1,0,1,1},{-1,-1,0,0,1}};
void pass(context &ctx,RenderTextureHandle source,RenderPostProcess post,RenderBlend blend=RenderBlend::Alpha,RenderToneMap tone=RenderToneMap::None) {
  MeshDraw draw{};draw.vertices=quad;draw.texture=source;draw.depth_test=false;draw.opaque_alpha=true;
  draw.linear_sampling=post.mode!=RenderPostMode::SSAO&&post.mode!=RenderPostMode::SSR&&post.mode!=RenderPostMode::Outline&&post.mode!=RenderPostMode::ShaftMask&&post.mode!=RenderPostMode::ShaftComposite&&post.mode!=RenderPostMode::FlareVisibility&&post.mode!=RenderPostMode::FlareComposite;draw.wrap_s=draw.wrap_t=RenderWrap::Clamp;draw.post=post;draw.blend=blend;draw.tonemap=tone;
  renderer_draw_mesh(ctx,draw);
}
void ensure_hdr_pair(context &ctx,std::array<RenderTargetHandle,2> &cache,unsigned w,unsigned h) {
  if(cache[0]) return;
  std::array<RenderTargetHandle,2> created{};
  try {for(auto &t:created) t=renderer_create_hdr_target(ctx,w,h);}
  catch(...) {for(auto t:created) if(t) renderer_destroy_target(ctx,t);throw;}
  cache=created;
}
RenderTextureHandle smaa_lookup(context &ctx,const unsigned char *bytes,unsigned w,unsigned h,unsigned channels) {
  std::vector<unsigned char> pixels(size_t(w)*h*4);
  for(size_t i=0;i<size_t(w)*h;++i) {pixels[i*4]=bytes[i*channels];pixels[i*4+1]=bytes[i*channels+(channels>1?1:0)];pixels[i*4+3]=255;}
  return renderer_create_texture(ctx,{w,h,pixels});
}
void free_targets(context &ctx,model_store &store) {
  const auto &window=ctx_ref<window_guard>(ctx);
  const auto busy=[&](RenderTargetHandle t) {return t && (window.graphics->target_in_use(t)||window.graphics->texture_in_use(window.renderer_resources.targets.at(t.id).color));};
  for(auto t:store.portable_ao) if(busy(t)) throw std::logic_error("njin post: cannot resize SSAO targets used in this frame");
  if(busy(store.portable_ao_scene)) throw std::logic_error("njin post: cannot resize SSAO scene used in this frame");
  for(auto t:store.portable_bloom) if(busy(t)) throw std::logic_error("njin post: cannot resize targets already submitted in this frame");
  if(busy(store.portable_aa)) throw std::logic_error("njin post: cannot resize AA target already submitted in this frame");
  for(auto t:{store.portable_ssr_mask,store.portable_ssr_scene}) if(busy(t)) throw std::logic_error("njin post: cannot resize SSR target already submitted in this frame");
  for(auto t:store.portable_effect_scene) if(busy(t)) throw std::logic_error("njin post: cannot resize effect target already submitted in this frame");
  for(auto t:store.portable_shafts) if(busy(t)) throw std::logic_error("njin post: cannot resize shafts target already submitted in this frame");
  if(busy(store.portable_flare_visibility)) throw std::logic_error("njin post: cannot resize flare visibility target already submitted in this frame");
  for(auto t:store.portable_smaa) if(busy(t)) throw std::logic_error("njin post: cannot resize SMAA target already submitted in this frame");
  for(auto t:store.portable_luminance) if(busy(t)) throw std::logic_error("njin post: cannot resize luminance target used in this frame");
  for(auto t:store.portable_exposure) if(busy(t)) throw std::logic_error("njin post: cannot resize exposure history used in this frame");
  for(auto t:store.portable_oit) if(busy(t)) throw std::logic_error("njin post: cannot resize OIT targets used in this frame");
  for(auto t:store.portable_bloom) renderer_destroy_target(ctx,t);
  store.portable_bloom.clear();
  for(auto &t:store.portable_ao) if(t) {renderer_destroy_target(ctx,t);t={};}
  if(store.portable_ao_scene) {renderer_destroy_target(ctx,store.portable_ao_scene);store.portable_ao_scene={};}
  if(store.portable_aa) {renderer_destroy_target(ctx,store.portable_aa);store.portable_aa={};}
  for(auto *t:{&store.portable_ssr_mask,&store.portable_ssr_scene}) if(*t) {renderer_destroy_target(ctx,*t);*t={};}
  for(auto &t:store.portable_effect_scene) if(t) {renderer_destroy_target(ctx,t);t={};}
  for(auto &t:store.portable_shafts) if(t) {renderer_destroy_target(ctx,t);t={};}
  if(store.portable_flare_visibility) {renderer_destroy_target(ctx,store.portable_flare_visibility);store.portable_flare_visibility={};}
  for(auto &t:store.portable_smaa) if(t) {renderer_destroy_target(ctx,t);t={};}
  for(auto t:store.portable_luminance) renderer_destroy_target(ctx,t);
  store.portable_luminance.clear();
  for(auto &t:store.portable_exposure) if(t) {renderer_destroy_target(ctx,t);t={};}
  store.portable_exposure_valid=false;
  for(auto &t:store.portable_oit) if(t) {renderer_destroy_target(ctx,t);t={};}
}
}
void portable_post_resolve(context &ctx,model_store &store) {
  const auto &fx=store.portable_frame_post;
  const unsigned w=store.portable_hdr_width,h=store.portable_hdr_height;
  const bool bloom=store.portable_frame_light.hdr&&fx.bloom>0,aa=fx.aa!=aa3d_none;
  if(w!=store.portable_post_width||h!=store.portable_post_height||store.portable_ao_half!=fx.ssao_half) {
    free_targets(ctx,store);store.portable_post_width=w;store.portable_post_height=h;store.portable_ao_half=fx.ssao_half;
  }
  portable_temporal_velocity(ctx,store);
  auto source=renderer_target_texture(ctx,store.portable_hdr);
  try {
    if(fx.ssao>0) {
      const auto depth=renderer_target_depth_texture(ctx,store.portable_hdr);
      const unsigned aw=std::max(1u,w/(fx.ssao_half?2:1)),ah=std::max(1u,h/(fx.ssao_half?2:1));
      if(!store.portable_ao[0]) {
        std::array<RenderTargetHandle,2> targets{};RenderTargetHandle scene;
        try {targets[0]=renderer_create_target(ctx,aw,ah);targets[1]=renderer_create_target(ctx,aw,ah);scene=renderer_create_hdr_target(ctx,w,h);}
        catch(...) {for(auto t:targets) if(t) renderer_destroy_target(ctx,t);if(scene) renderer_destroy_target(ctx,scene);throw;}
        store.portable_ao=targets;store.portable_ao_scene=scene;
      }
      RenderPostProcess ao;ao.mode=RenderPostMode::SSAO;ao.amount=fx.ssao;ao.radius=fx.ssao_radius;ao.samples=float(fx.ssao_samples);ao.texel={1.f/w,1.f/h};
      const auto projection=MatrixToFloatV(store.portable_projection),inverse=MatrixToFloatV(MatrixInvert(store.portable_projection));
      std::copy_n(projection.v,16,ao.projection.begin());std::copy_n(inverse.v,16,ao.inverse_projection.begin());
      renderer_begin_target(ctx,store.portable_ao[0]);pass(ctx,depth,ao);renderer_end_target(ctx);
      for(unsigned axis=0;axis<2;++axis) {
        auto blur=ao;blur.mode=axis?RenderPostMode::AOBlurY:RenderPostMode::AOBlurX;blur.texel={1.f/aw,1.f/ah};blur.auxiliary=depth;blur.auxiliary_linear=false;
        renderer_begin_target(ctx,store.portable_ao[axis?0:1]);pass(ctx,renderer_target_texture(ctx,store.portable_ao[axis?1:0]),blur);renderer_end_target(ctx);
      }
      auto apply=ao;apply.mode=RenderPostMode::ApplyAO;apply.auxiliary=renderer_target_texture(ctx,store.portable_ao[0]);
      renderer_begin_target(ctx,store.portable_ao_scene);pass(ctx,source,apply);renderer_end_target(ctx);
      source=renderer_target_texture(ctx,store.portable_ao_scene);
    }
    if(fx.ssr>0) {
      if(!store.portable_ssr_mask) {
        RenderTargetHandle mask,scene;
        try {mask=renderer_create_hdr_target(ctx,w,h);scene=renderer_create_hdr_target(ctx,w,h);}
        catch(...) {if(mask) renderer_destroy_target(ctx,mask);if(scene) renderer_destroy_target(ctx,scene);throw;}
        store.portable_ssr_mask=mask;store.portable_ssr_scene=scene;
      }
      renderer_begin_target(ctx,store.portable_ssr_mask,{0,0,1,0});
      const auto view=MatrixToFloatV(store.portable_view);
      for(const auto &packet:store.portable_queue) if(!packet.shadow_only&&!packet.transparent) {
        auto mask=packet;mask.normal_pass=true;mask.shadow_pass=false;mask.shadows.reset();mask.environment.reset();mask.local_light_count=0;
        mask.blend=RenderBlend::Opaque;std::copy_n(view.v,16,mask.view.begin());
        renderer_draw_mesh(ctx,mask);
      }
      renderer_end_target(ctx);
      RenderPostProcess ssr;ssr.mode=RenderPostMode::SSR;ssr.texel={1.f/w,1.f/h};ssr.amount=fx.ssr;ssr.radius=fx.ssr_distance;ssr.samples=float(fx.ssr_steps);ssr.thickness=fx.ssr_thickness;
      ssr.auxiliary=renderer_target_depth_texture(ctx,store.portable_ssr_mask);ssr.auxiliary_linear=false;ssr.secondary=renderer_target_texture(ctx,store.portable_ssr_mask);
      const auto projection=MatrixToFloatV(store.portable_projection),inverse=MatrixToFloatV(MatrixInvert(store.portable_projection));
      std::copy_n(projection.v,16,ssr.projection.begin());std::copy_n(inverse.v,16,ssr.inverse_projection.begin());
      const auto linear=[](float x){return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);};
      const auto sky=store.portable_frame_light.fog_color;ssr.sky={linear(sky.r),linear(sky.g),linear(sky.b),fx.ssr_sky};
      renderer_begin_target(ctx,store.portable_ssr_scene);pass(ctx,source,ssr);renderer_end_target(ctx);
      source=renderer_target_texture(ctx,store.portable_ssr_scene);
    }
    source=portable_temporal_resolve(ctx,store,source);
    if(fx.outline>0||fx.shafts>0||fx.flare>0||fx.oit) {
      RenderPostProcess effect;effect.texel={1.f/w,1.f/h};effect.auxiliary_linear=false;
      const auto projection=MatrixToFloatV(store.portable_projection),inverse=MatrixToFloatV(MatrixInvert(store.portable_projection));
      std::copy_n(projection.v,16,effect.projection.begin());std::copy_n(inverse.v,16,effect.inverse_projection.begin());
      const auto linear=[](float x){return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);};
      const auto compose=[&](const RenderPostProcess &p) {
        ensure_hdr_pair(ctx,store.portable_effect_scene,w,h);
        const auto a=store.portable_effect_scene[0];
        const auto target=source.id==renderer_target_texture(ctx,a).id?store.portable_effect_scene[1]:a;
        renderer_begin_target(ctx,target);pass(ctx,source,p);renderer_end_target(ctx);source=renderer_target_texture(ctx,target);
      };
      if(fx.outline>0) {
        auto outline=effect;outline.mode=RenderPostMode::Outline;outline.auxiliary=renderer_target_depth_texture(ctx,store.portable_hdr);outline.amount=fx.outline;
        outline.radius=fx.outline_width;outline.edge_cos=fx.outline_crease>=180?-2.f:std::cos(fx.outline_crease*PI/180.f);outline.fade=fx.outline_fade;
        const auto c=fx.outline_color;outline.sky={linear(c.r),linear(c.g),linear(c.b),c.a};compose(outline);
      }
      if(fx.oit&&std::any_of(store.portable_queue.begin(),store.portable_queue.end(),[](const MeshDraw &d){return d.transparent&&!d.shadow_only;})) {
        ensure_hdr_pair(ctx,store.portable_oit,w,h);
        const auto depth=renderer_target_depth_texture(ctx,store.portable_hdr);
        const auto vp=MatrixToFloatV(store.portable_view_projection);
        for(unsigned stage=0;stage<2;++stage) {
          renderer_begin_target(ctx,store.portable_oit[stage],stage?std::array<float,4>{1,1,1,1}:std::array<float,4>{0,0,0,0});
          for(const auto &packet:store.portable_queue) if(packet.transparent&&!packet.shadow_only) {
            auto draw=packet;draw.transparency_pass=stage+1;draw.depth_test=false;draw.opaque_alpha=false;draw.opaque_depth=depth;
            if(draw.transmission[0][0]>0||(draw.water&&(draw.water->parameters[0][2]>0||draw.water->parameters[0][3]>0||draw.water->parameters[1][3]>0))) {
              draw.post.auxiliary=source;draw.post.auxiliary_linear=true;
              if(draw.water) {const auto c=ctx_ref<config>(ctx).clear_bg_color;draw.post.sky={linear(c.r),linear(c.g),linear(c.b),1};}
            }
            draw.blend=stage?RenderBlend::Reveal:RenderBlend::WeightedAdd;
            std::copy_n(vp.v,16,draw.transparency_projection.begin());renderer_draw_mesh(ctx,draw);
          }
          renderer_end_target(ctx);
        }
        auto oit=effect;oit.mode=RenderPostMode::OITComposite;oit.auxiliary=renderer_target_texture(ctx,store.portable_oit[0]);oit.secondary=renderer_target_texture(ctx,store.portable_oit[1]);compose(oit);
      }
      const auto &light=store.portable_frame_light;
      const auto sun=Vector3Scale(Vector3Normalize({light.direction.x,light.direction.y,light.direction.z}),-1);
      const auto &m=store.portable_view_projection;
      const float cx=m.m0*sun.x+m.m4*sun.y+m.m8*sun.z,cy=m.m1*sun.x+m.m5*sun.y+m.m9*sun.z,cw=m.m3*sun.x+m.m7*sun.y+m.m11*sun.z;
      if((fx.shafts>0||fx.flare>0)&&sun.y>=-.05f&&cw>1e-4f) {
        const float x=cx/cw*.5f+.5f,y=.5f-cy/cw*.5f;
        const float outside=std::max({-x,x-1.f,-y,y-1.f,0.f});
        float visibility=1.f-std::clamp(outside/.25f,0.f,1.f);visibility=visibility*visibility*(3-2*visibility);
        if(visibility>0) {
          effect.sun={x,y,visibility,float(w)/h};
          effect.sky={linear(light.color.r)*light.color.a,linear(light.color.g)*light.color.a,linear(light.color.b)*light.color.a,1};
          const auto depth=renderer_target_depth_texture(ctx,store.portable_hdr);
          if(fx.shafts>0) {
            ensure_hdr_pair(ctx,store.portable_shafts,std::max(1u,w/2),std::max(1u,h/2));
            auto shaft=effect;shaft.mode=RenderPostMode::ShaftMask;
            renderer_begin_target(ctx,store.portable_shafts[0]);pass(ctx,depth,shaft);renderer_end_target(ctx);
            shaft.mode=RenderPostMode::ShaftBlur;shaft.radius=fx.shafts_length;
            renderer_begin_target(ctx,store.portable_shafts[1]);pass(ctx,renderer_target_texture(ctx,store.portable_shafts[0]),shaft);renderer_end_target(ctx);
            shaft.mode=RenderPostMode::ShaftComposite;shaft.amount=fx.shafts*visibility;shaft.auxiliary=renderer_target_texture(ctx,store.portable_shafts[1]);shaft.auxiliary_linear=true;
            shaft.sky[0]*=linear(fx.shafts_color.r)*fx.shafts_color.a;shaft.sky[1]*=linear(fx.shafts_color.g)*fx.shafts_color.a;shaft.sky[2]*=linear(fx.shafts_color.b)*fx.shafts_color.a;
            compose(shaft);
          }
          if(fx.flare>0) {
            if(!store.portable_flare_visibility) store.portable_flare_visibility=renderer_create_target(ctx,1,1);
            auto flare=effect;flare.mode=RenderPostMode::FlareVisibility;
            renderer_begin_target(ctx,store.portable_flare_visibility);pass(ctx,depth,flare);renderer_end_target(ctx);
            flare.mode=RenderPostMode::FlareComposite;flare.amount=fx.flare;flare.halo=fx.flare_halo;flare.auxiliary=renderer_target_texture(ctx,store.portable_flare_visibility);
            compose(flare);
          }
        }
      }
    }
    if(store.portable_frame_light.volume_density>0) {
      ensure_hdr_pair(ctx,store.portable_effect_scene,w,h);
      const auto target=source.id==renderer_target_texture(ctx,store.portable_effect_scene[0]).id?store.portable_effect_scene[1]:store.portable_effect_scene[0];
      const auto &l=store.portable_frame_light;const auto &camera=store.portable_camera;
      RenderPostProcess volume;volume.mode=RenderPostMode::Volume;volume.auxiliary=renderer_target_depth_texture(ctx,store.portable_hdr);volume.texel={1.f/w,1.f/h};
      const auto inverse=MatrixToFloatV(MatrixInvert(store.portable_view_projection));std::copy_n(inverse.v,16,volume.inverse_projection.begin());
      volume.legacy[0]={camera.position.x,camera.position.y,camera.position.z,camera.projection==camera3d_orthographic?1.f:0.f};
      volume.legacy[1]={l.volume_color.r,l.volume_color.g,l.volume_color.b,l.volume_density};
      volume.legacy[2]={l.volume_base,l.volume_falloff,l.volume_noise,l.volume_noise_size};
      volume.legacy[3]={l.volume_wind.x,l.volume_wind.y,l.volume_wind.z,ctx_ref<time_state>(ctx).elapsed};
      volume.legacy[4]={l.volume_anisotropy,l.volume_distance,float(l.volume_steps),l.volume_step_max};
      volume.legacy[5]={l.volume_sun,l.volume_ambient,l.volume_lamps?1.f:0.f,l.volume_dark_outside?1.f:0.f};
      MeshDraw draw{};draw.vertices=quad;draw.texture=volume.auxiliary;
      volume.auxiliary=source;volume.auxiliary_linear=true;draw.post=volume;
      draw.depth_test=false;draw.blend=RenderBlend::Opaque;draw.linear_sampling=false;
      draw.lighting.direction={l.direction.x,l.direction.y,l.direction.z};draw.lighting.color={l.color.r,l.color.g,l.color.b};draw.lighting.intensity=l.color.a;
      draw.lighting.ambient={l.ambient.r,l.ambient.g,l.ambient.b};draw.lighting.camera={camera.position.x,camera.position.y,camera.position.z};
      if(!store.portable_queue.empty()) {draw.shadows=store.portable_queue.front().shadows;draw.local_lights=store.portable_queue.front().local_lights;draw.local_light_count=store.portable_queue.front().local_light_count;}
      draw.clusters=store.portable_clusters;draw.shadows=store.portable_shadows;
      renderer_begin_target(ctx,target);renderer_draw_mesh(ctx,draw);renderer_end_target(ctx);source=renderer_target_texture(ctx,target);
    }
    source=portable_atmosphere_aerial(ctx,store,source);
    source=portable_motion_blur(ctx,store,source);
    if(bloom) {
      if(store.portable_bloom.empty()) {
        store.portable_bloom.reserve(6);
        unsigned mw=w,mh=h;
        try {
          do {mw=std::max(1u,mw/2);mh=std::max(1u,mh/2);store.portable_bloom.push_back(renderer_create_hdr_target(ctx,mw,mh));}
          while(store.portable_bloom.size()<6 && std::min(mw,mh)>8);
        } catch(...) {free_targets(ctx,store);throw;}
      }
      auto previous=source;unsigned sw=w,sh=h;
      for(size_t i=0;i<store.portable_bloom.size();++i) {
        const auto t=store.portable_bloom[i];renderer_begin_target(ctx,t);
        RenderPostProcess p;p.mode=RenderPostMode::BloomDown;p.texel={1.f/sw,1.f/sh};p.karis=i==0;
        pass(ctx,previous,p);renderer_end_target(ctx);previous=renderer_target_texture(ctx,t);
        sw=std::max(1u,sw/2);sh=std::max(1u,sh/2);
      }
      for(size_t i=store.portable_bloom.size();i>1;--i) {
        const auto &small=ctx_ref<window_guard>(ctx).renderer_resources.targets.at(store.portable_bloom[i-1].id);
        RenderPostProcess p;p.mode=RenderPostMode::BloomUp;p.texel={fx.bloom_radius/small.width,fx.bloom_radius/small.height};
        renderer_resume_target(ctx,store.portable_bloom[i-2]);
        pass(ctx,small.color,p,RenderBlend::Additive);renderer_end_target(ctx);
      }
    }
    RenderTextureHandle exposure;
    const auto &meter=store.portable_frame_light;
    if(meter.hdr&&meter.auto_exposure) {
      if(store.portable_luminance.empty()) {
        std::vector<RenderTargetHandle> levels;levels.reserve(7);std::array<RenderTargetHandle,2> history{};
        try {for(unsigned n=64;n;n/=2) levels.push_back(renderer_create_hdr_target(ctx,n,n));for(auto &t:history) t=renderer_create_hdr_target(ctx,1,1);}
        catch(...) {for(auto t:levels) renderer_destroy_target(ctx,t);for(auto t:history) if(t) renderer_destroy_target(ctx,t);throw;}
        store.portable_luminance=std::move(levels);store.portable_exposure=history;
      }
      auto previous=source;unsigned size=64;
      for(size_t i=0;i<store.portable_luminance.size();++i) {
        RenderPostProcess p;p.mode=i?RenderPostMode::LuminanceDown:RenderPostMode::Luminance;p.texel={1.f/size,1.f/size};
        renderer_begin_target(ctx,store.portable_luminance[i]);pass(ctx,previous,p,RenderBlend::Opaque);renderer_end_target(ctx);
        previous=renderer_target_texture(ctx,store.portable_luminance[i]);if(i) size/=2;
      }
      const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
      const float dt=float(std::clamp(now-store.portable_exposure_at,0.0,.5));
      RenderPostProcess adapt;adapt.mode=RenderPostMode::ExposureAdapt;adapt.reset=!store.portable_exposure_valid;
      adapt.sky={meter.auto_exposure_key,meter.auto_exposure_min,meter.auto_exposure_max,1.f-std::exp(-dt*meter.auto_exposure_speed)};
      if(store.portable_exposure_valid) adapt.auxiliary=renderer_target_texture(ctx,store.portable_exposure[store.portable_exposure_index]);
      const unsigned next=1-store.portable_exposure_index;
      renderer_begin_target(ctx,store.portable_exposure[next]);pass(ctx,previous,adapt,RenderBlend::Opaque);renderer_end_target(ctx);
      store.portable_exposure_index=next;store.portable_exposure_valid=true;store.portable_exposure_at=now;
      exposure=renderer_target_texture(ctx,store.portable_exposure[next]);
    }
    if(fx.aa==aa3d_smaa) {
      if(!store.portable_smaa[0]) {
        std::array<RenderTargetHandle,2> targets{};
        try {for(auto &t:targets) t=renderer_create_target(ctx,w,h);} catch(...) {for(auto t:targets) if(t) renderer_destroy_target(ctx,t);throw;}
        store.portable_smaa=targets;
      }
      if(!store.portable_smaa_lookup[0]) {
        std::array<RenderTextureHandle,2> textures{};
        try {textures[0]=smaa_lookup(ctx,areaTexBytes,AREATEX_WIDTH,AREATEX_HEIGHT,2);textures[1]=smaa_lookup(ctx,searchTexBytes,SEARCHTEX_WIDTH,SEARCHTEX_HEIGHT,1);}
        catch(...) {for(auto t:textures) if(t) renderer_destroy_texture(ctx,t);throw;}
        store.portable_smaa_lookup=textures;
      }
    }
    if(aa) {
      if(!store.portable_aa) store.portable_aa=renderer_create_target(ctx,w,h);
      renderer_begin_target(ctx,store.portable_aa);
    }
    if(!aa&&store.portable_destination) renderer_resume_target(ctx,store.portable_destination);
    RenderPostProcess resolve;resolve.mode=RenderPostMode::Resolve;
    resolve.auto_exposure=bool(exposure);resolve.secondary=exposure;
    if(bloom) {resolve.auxiliary=renderer_target_texture(ctx,store.portable_bloom.front());resolve.amount=fx.bloom;resolve.levels=float(store.portable_bloom.size());}
    pass(ctx,source,resolve,RenderBlend::Alpha,static_cast<RenderToneMap>(store.portable_frame_light.tonemap));
    if(aa) {
      renderer_end_target(ctx);RenderPostProcess filter;filter.texel={1.f/w,1.f/h};
      if(fx.aa==aa3d_smaa) {
        filter.mode=RenderPostMode::SMAAEdge;
        renderer_begin_target(ctx,store.portable_smaa[0]);pass(ctx,renderer_target_texture(ctx,store.portable_aa),filter,RenderBlend::Opaque);renderer_end_target(ctx);
        filter.mode=RenderPostMode::SMAAWeights;filter.auxiliary=store.portable_smaa_lookup[0];filter.secondary=store.portable_smaa_lookup[1];
        renderer_begin_target(ctx,store.portable_smaa[1]);pass(ctx,renderer_target_texture(ctx,store.portable_smaa[0]),filter,RenderBlend::Opaque);renderer_end_target(ctx);
        filter.mode=RenderPostMode::SMAABlend;filter.auxiliary=renderer_target_texture(ctx,store.portable_smaa[1]);filter.secondary={};
        if(store.portable_destination) renderer_resume_target(ctx,store.portable_destination);
        pass(ctx,renderer_target_texture(ctx,store.portable_aa),filter,RenderBlend::Opaque);
      } else {if(store.portable_destination) renderer_resume_target(ctx,store.portable_destination);filter.mode=RenderPostMode::FXAA;pass(ctx,renderer_target_texture(ctx,store.portable_aa),filter);}
    }
  } catch(...) {
    if(ctx_ref<window_guard>(ctx).renderer_resources.active_target) {try {renderer_end_target(ctx);} catch(...) {}}
    throw;
  }
}
}
