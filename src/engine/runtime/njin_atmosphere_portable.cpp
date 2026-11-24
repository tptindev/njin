#include "njin_atmosphere_portable.h"
#include "njin_model.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_world3d.h"
#include "njin_rlmath.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace njin {
namespace {
using Rows=std::array<std::array<float,4>,18>;
void range(float v,float lo,float hi) {
  if(!std::isfinite(v)||v<lo||v>hi) throw std::invalid_argument("njin atmosphere: parameter outside documented range");
}
float moon_energy(float phase) {
  const float c=2*phase-1,angle=std::acos(c);
  return std::max(0.f,(std::sqrt(std::max(0.f,1-c*c))+(pi-angle)*c)/pi);
}
Rows parameters(const atmosphere3d_desc &d,float altitude,float exposure) {
  range(d.planet_radius_km,1000,10000);range(d.atmosphere_height_km,30,200);
  range(d.rayleigh_height_km,.1f,30);range(d.mie_height_km,.1f,30);range(d.mie_g,-.95f,.95f);
  range(d.mie_scattering_per_km,0,.1f);range(d.mie_absorption_per_km,0,.1f);
  range(d.ozone_center_km,0,100);range(d.ozone_half_width_km,.1f,100);
  range(d.sun_illuminance_lux,0,200000);range(d.sun_radius_rad,.001f,.02f);range(d.radiance_scale,0,1);
  range(altitude,1,20000);range(d.sea_level_m,-10000000,10000000);
  for(float v:{d.rayleigh_per_km.x,d.rayleigh_per_km.y,d.rayleigh_per_km.z,d.ozone_per_km.x,d.ozone_per_km.y,d.ozone_per_km.z}) range(v,0,.1f);
  for(float v:{d.sun_color.x,d.sun_color.y,d.sun_color.z}) range(v,0,1);
  for(float v:{d.sun_direction.x,d.sun_direction.y,d.sun_direction.z}) range(v,-1e6f,1e6f);
  const float len=length(d.sun_direction);
  if(len<1e-6f) throw std::invalid_argument("njin atmosphere: sun direction is zero");
  range(d.multiple_scattering,0,2);range(d.aerial_distance_km,1,128);
  range(d.environment_intensity,0,1000000);range(d.environment_update_seconds,0,60);
  range(d.environment_altitude_m,1,20000);
  range(d.moon_illuminance_lux,0,2);range(d.moon_phase,0,1);range(d.moon_radius_rad,.001f,.02f);
  range(d.night_luminance_cd_m2,0,1);range(d.star_luminance_cd_m2,0,100);range(d.star_rotation_rad,-1e6f,1e6f);
  for(float v:{d.moon_color.x,d.moon_color.y,d.moon_color.z}) range(v,0,1);
  for(float v:{d.ground_albedo.x,d.ground_albedo.y,d.ground_albedo.z}) range(v,0,.95f);
  for(float v:{d.moon_direction.x,d.moon_direction.y,d.moon_direction.z}) range(v,-1e6f,1e6f);
  const float moon_len=length(d.moon_direction);
  if(moon_len<1e-6f) throw std::invalid_argument("njin atmosphere: moon direction is zero");
  const auto sun=d.sun_direction/len,moon=d.moon_direction/moon_len;
  Rows rows{{{d.planet_radius_km,d.planet_radius_km+d.atmosphere_height_km,d.planet_radius_km+altitude*.001f,0},
    {d.rayleigh_per_km.x,d.rayleigh_per_km.y,d.rayleigh_per_km.z,d.rayleigh_height_km},
    {d.mie_scattering_per_km,d.mie_absorption_per_km,d.mie_height_km,d.mie_g},
    {d.ozone_per_km.x,d.ozone_per_km.y,d.ozone_per_km.z,d.ozone_center_km},
    {d.ozone_half_width_km,d.sun_illuminance_lux,d.moon_illuminance_lux,0},
    {sun.x,sun.y,sun.z,d.sun_disk?1.f:0.f},{d.sun_color.x,d.sun_color.y,d.sun_color.z,d.radiance_scale},
    {exposure,d.sun_radius_rad,0,0}}};
  rows[8]={moon.x,moon.y,moon.z,d.moon_disk?1.f:0.f};
  rows[9]={d.moon_color.x,d.moon_color.y,d.moon_color.z,d.moon_illuminance_lux*moon_energy(d.moon_phase)};
  rows[10]={d.ground_albedo.x,d.ground_albedo.y,d.ground_albedo.z,d.multiple_scattering};
  rows[11]={d.night_luminance_cd_m2,d.star_luminance_cd_m2,d.star_rotation_rad,d.moon_radius_rad};
  rows[12]={d.moon_phase,d.aerial_distance_km,0,0};rows[13]={64,20,64,0};
  return rows;
}
Rows sky_key(Rows rows) {
  rows[0][3]=rows[4][2]=rows[4][3]=rows[5][3]=rows[8][3]=0;
  rows[7]={};rows[11][1]=rows[11][2]=rows[11][3]=0;
  for(unsigned i=12;i<18;++i) rows[i]={};
  return rows;
}
Rows medium_key(Rows rows) {
  rows=sky_key(rows);rows[0][2]=0;rows[2][3]=0;rows[4][1]=0;rows[5]={};rows[6]={};rows[8]={};rows[9]={};rows[10][3]=0;rows[11]={};return rows;
}
std::shared_ptr<RenderShaderBindings> bindings(context &ctx,AtmosphereState &s,Rows rows,unsigned mode,RenderTextureHandle scene={},RenderTextureHandle depth={}) {
  rows[0][3]=float(mode);
  auto result=std::make_shared<RenderShaderBindings>();result->program=s.program;
  result->uniforms.push_back({"u_atm",{rows.begin(),rows.end()}});
  const auto texture=[&](RenderTargetHandle h){return h?renderer_target_texture(ctx,h):RenderTextureHandle{};};
  result->textures={{"s_atmTransmittance",0,mode!=1?texture(s.transmittance):RenderTextureHandle{},true},
    {"s_atmSkyView",1,(mode==3||mode==7)?texture(mode==7?s.environment_sky:s.sky_view):RenderTextureHandle{},true},
    {"s_atmMultiple",2,(mode==2||mode==5||mode==6)&&rows[10][3]>0?texture(s.multiple):RenderTextureHandle{},true},
    {"s_atmAerial",3,mode==9?texture(s.aerial_radiance):RenderTextureHandle{},true},
    {"s_atmAerialT",4,mode==9?texture(s.aerial_transmittance):RenderTextureHandle{},true},
    {"s_atmScene",5,mode==8?texture(s.environment_base):scene,true},{"s_atmDepth",6,depth,false}};
  return result;
}
std::array<RenderVertex,6> quad() {
  return {{{-1,1,.999999f,0,0},{1,1,.999999f,1,0},{1,-1,.999999f,1,1},
    {-1,1,.999999f,0,0},{1,-1,.999999f,1,1},{-1,-1,.999999f,0,1}}};
}
void build(context &ctx,model_store &store,AtmosphereState &s,Rows rows,RenderTargetHandle target,unsigned mode) {
  const auto previous=ctx_ref<window_guard>(ctx).renderer_resources.active_target;
  if(previous) renderer_end_target(ctx);
  try {
    renderer_begin_target(ctx,target,{});
    const auto vertices=quad();MeshDraw draw{vertices,{}};
    draw.texture=store.portable_white;draw.depth_test=false;draw.blend=RenderBlend::Opaque;
    draw.shader=bindings(ctx,s,rows,mode);
    renderer_draw_mesh(ctx,draw);renderer_end_target(ctx);
  } catch(...) {
    if(ctx_ref<window_guard>(ctx).renderer_resources.active_target) renderer_end_target(ctx);
    if(previous) renderer_resume_target(ctx,previous);
    throw;
  }
  if(previous) renderer_resume_target(ctx,previous);
}
}
void draw_atmosphere3d(context &ctx,const atmosphere3d_desc &d) {
  auto &window=ctx_ref<window_guard>(ctx);
  if(window.graphics->shader_profile().empty()) throw std::runtime_error("njin atmosphere: requires SDL3/bgfx with a compiled atmosphere shader profile");
  model_store &store=ctx_ref<model_store>(ctx);
  if(!store.portable_pass||!store.portable_hdr_active) throw std::logic_error("njin atmosphere: requires begin_3d and light3d.hdr=true");
  if(store.portable_camera.projection!=camera3d_perspective) throw std::invalid_argument("njin atmosphere: orthographic cameras are unsupported");
  if(store.portable_atmosphere_drawn||store.portable_frame_light.environment_visible)
    throw std::logic_error("njin atmosphere: only one sky background per view; disable environment_visible");
  if(d.aerial_perspective&&(store.portable_frame_light.fog_density>0||store.portable_frame_light.volume_density>0))
    throw std::logic_error("njin atmosphere: disable legacy fog/volumetric fog before enabling aerial perspective");
  const auto rows=parameters(d,store.portable_camera.position.y-d.sea_level_m,store.portable_frame_light.exposure);
  if(!store.portable_atmosphere) store.portable_atmosphere=std::make_shared<AtmosphereState>();
  auto &s=*store.portable_atmosphere;
  if(!s.program) s.program=window.graphics->create_atmosphere_shader();
  // Allocate lazily and keep the targets independent of viewport dimensions.
  if(!s.transmittance) s.transmittance=renderer_create_hdr_target(ctx,256,64);
  if(!s.sky_view) s.sky_view=renderer_create_hdr_target(ctx,192,108);
  if(d.multiple_scattering>0&&!s.multiple) s.multiple=renderer_create_hdr_target(ctx,32,32);
  if(d.aerial_perspective) {
    if(!s.aerial_radiance) s.aerial_radiance=renderer_create_hdr_target(ctx,512,32);
    if(!s.aerial_transmittance) s.aerial_transmittance=renderer_create_hdr_target(ctx,512,32);
  }
  if(!store.portable_white) {const unsigned char white[]{255,255,255,255};store.portable_white=renderer_create_texture(ctx,{1,1,white});}
  const auto start=std::chrono::steady_clock::now();
  const auto &p=s.previous;
  const bool medium=!s.valid||rows[0][0]!=p[0][0]||rows[0][1]!=p[0][1]||rows[1]!=p[1]||
    rows[2][0]!=p[2][0]||rows[2][1]!=p[2][1]||rows[2][2]!=p[2][2]||rows[3]!=p[3]||rows[4][0]!=p[4][0];
  const bool ground=rows[10][0]!=p[10][0]||rows[10][1]!=p[10][1]||rows[10][2]!=p[10][2];
  if(medium||ground) s.cpu.reset();
  const bool view=!s.valid||sky_key(rows)!=sky_key(p);
  const bool multiple=medium||ground||!s.multiple_valid;
  // Mark invalid before submitting, so a failed pass cannot leave a stale valid key.
  s.valid=false;
  if(medium) {build(ctx,store,s,rows,s.transmittance,1);++s.stats.transmittance_builds;}
  if(d.multiple_scattering>0&&multiple) {build(ctx,store,s,rows,s.multiple,4);++s.stats.multiple_scattering_builds;s.multiple_valid=true;}
  if((medium||ground)&&d.multiple_scattering<=0) s.multiple_valid=false;
  if(view) {build(ctx,store,s,rows,s.sky_view,2);++s.stats.sky_view_builds;}
  const bool aerial=view||!s.aerial_valid||rows[12][1]!=p[12][1];
  if(d.aerial_perspective&&aerial) {
    build(ctx,store,s,rows,s.aerial_radiance,5);build(ctx,store,s,rows,s.aerial_transmittance,6);
    ++s.stats.aerial_builds;s.aerial_valid=true;
  }
  if(aerial&&!d.aerial_perspective) s.aerial_valid=false;
  s.current=rows;s.view_aerial=d.aerial_perspective;s.view_environment=d.environment_lighting;
  if(d.environment_lighting) {
    auto environment_rows=rows;environment_rows[0][2]=rows[0][0]+d.environment_altitude_m*.001f;
    const auto now=std::chrono::steady_clock::now();
    if(atmosphere_cpu_ready(s)) {
      const auto sh=atmosphere_cpu_finish(s);
      if(medium_key(s.job_rows)==medium_key(rows)) {
        const auto env_start=std::chrono::steady_clock::now();
        if(!s.environment_base) s.environment_base=renderer_create_hdr_target(ctx,256,128);
        if(!s.environment_atlas) s.environment_atlas=renderer_create_hdr_target(ctx,128,384);
        if(!s.environment_sky) s.environment_sky=renderer_create_hdr_target(ctx,192,108);
        auto env=std::make_shared<RenderEnvironmentData>();
        env->sh=sh;env->base=renderer_target_texture(ctx,s.environment_base);env->atlas=renderer_target_texture(ctx,s.environment_atlas);
        build(ctx,store,s,s.job_rows,s.environment_sky,2);build(ctx,store,s,s.job_rows,s.environment_base,7);build(ctx,store,s,s.job_rows,s.environment_atlas,8);
        s.environment=std::move(env);s.environment_key=sky_key(s.job_rows);s.cpu_key=medium_key(s.job_rows);s.last_environment=now;
        ++s.stats.environment_builds;s.stats.last_environment_cpu_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-env_start).count();
      } else {s.cpu.reset();++s.stats.environment_discarded;}
    }
    const bool changed=!s.environment||sky_key(environment_rows)!=s.environment_key;
    if((changed||s.force_environment)&&!s.job) {
      if(!s.environment||s.force_environment||medium||ground||std::chrono::duration<double>(now-s.last_environment).count()>=d.environment_update_seconds) {
        atmosphere_cpu_begin(s,environment_rows,!s.cpu||s.cpu_key!=medium_key(environment_rows));s.force_environment=false;
      } else ++s.stats.environment_deferred;
    }
    if(s.environment) s.environment->intensity=d.environment_intensity;
    else s.view_environment=false;
  }
  s.previous=rows;s.valid=true;
  if(view) s.stats.last_update_cpu_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  s.stats.color_bytes=((256*64+192*108)+(s.multiple?32*32:0)+(s.aerial_radiance?2*512*32:0)+(s.environment_base?256*128+128*384+192*108:0))*8;
  const auto draw_start=std::chrono::steady_clock::now();
  auto vertices=quad();const auto inverse=MatrixInvert(store.portable_view_projection);const auto eye=store.portable_camera.position;
  for(auto &v:vertices) {
    const float w=inverse.m3*v.x+inverse.m7*v.y+inverse.m11*v.z+inverse.m15;
    const auto p3=Vector3Transform({v.x,v.y,v.z},inverse);
    v.normal={p3.x/w-eye.x,p3.y/w-eye.y,p3.z/w-eye.z};
  }
  MeshDraw draw{vertices,{}};draw.texture=store.portable_white;draw.depth_write=false;draw.blend=RenderBlend::Opaque;
  draw.shader=bindings(ctx,s,rows,3);
  renderer_draw_mesh(ctx,draw);++s.stats.sky_draws;store.portable_atmosphere_drawn=true;
  s.stats.last_draw_cpu_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-draw_start).count();
}
atmosphere3d_stats atmosphere3d_statistics(const context &ctx) {
  const model_store &store=ctx_ref<model_store>(ctx);
  return store.portable_atmosphere?store.portable_atmosphere->stats:atmosphere3d_stats{};
}
atmosphere3d_desc atmosphere3d_physical() {
  atmosphere3d_desc d;d.multiple_scattering=1;d.ground_albedo={.2f,.2f,.2f};d.aerial_perspective=true;
  d.environment_lighting=true;d.moon_illuminance_lux=.25f;d.night_luminance_cd_m2=.002f;d.star_luminance_cd_m2=.2f;return d;
}
atmosphere3d_desc atmosphere3d_at_hour(const atmosphere3d_desc &desc,float hour,float latitude,float season) {
  range(hour,0,24);range(latitude,-89,89);range(season,-1,1);
  auto result=desc;sky3d sky;sky.hour=hour;sky.latitude=latitude;sky.season=season;
  result.sun_direction=sky3d_sun_direction(sky);result.moon_direction=-result.sun_direction;result.star_rotation_rad=(hour-12)*pi/12;return result;
}
void atmosphere3d_environment_refresh(context &ctx) {
  model_store &store=ctx_ref<model_store>(ctx);
  if(!store.portable_atmosphere) store.portable_atmosphere=std::make_shared<AtmosphereState>();
  store.portable_atmosphere->force_environment=true;
}
light3d atmosphere3d_sun_light(const atmosphere3d_desc &d,float altitude_m,const light3d &base) {
  const auto rows=parameters(d,altitude_m,1);
  const double R=d.planet_radius_km,r=R+altitude_m*.001,top=R+d.atmosphere_height_km;
  const double mu=rows[5][1];
  light3d result=base;result.direction={-rows[5][0],-rows[5][1],-rows[5][2]};
  result.color={0,0,0,d.sun_illuminance_lux/100000};
  if(mu<0&&r*r*(1-mu*mu)<=R*R) return result;
  const double distance=-r*mu+std::sqrt(std::max(0.0,top*top-r*r*(1-mu*mu)));
  double optical[3]{};
  for(unsigned i=0;i<64;++i) {
    const double t=distance*(i+.5)/64,h=std::max(0.0,std::sqrt(r*r+t*t+2*r*mu*t)-R);
    const double ray=std::exp(-h/d.rayleigh_height_km),mie=std::exp(-h/d.mie_height_km);
    const double ozone=std::max(0.0,1-std::abs(h-d.ozone_center_km)/d.ozone_half_width_km);
    const float rs[]{d.rayleigh_per_km.x,d.rayleigh_per_km.y,d.rayleigh_per_km.z};
    const float oz[]{d.ozone_per_km.x,d.ozone_per_km.y,d.ozone_per_km.z};
    for(unsigned c=0;c<3;++c) optical[c]+=(rs[c]*ray+(d.mie_scattering_per_km+d.mie_absorption_per_km)*mie+oz[c]*ozone)*distance/64;
  }
  const auto display=[](double x){return float(x<=.0031308?12.92*x:1.055*std::pow(x,1/2.4)-.055);};
  result.color.r=display(d.sun_color.x*std::exp(-optical[0]));
  result.color.g=display(d.sun_color.y*std::exp(-optical[1]));
  result.color.b=display(d.sun_color.z*std::exp(-optical[2]));
  return result;
}
light3d atmosphere3d_moon_light(const atmosphere3d_desc &d,float altitude_m,const light3d &base) {
  parameters(d,altitude_m,1);auto moon=d;moon.sun_direction=d.moon_direction;moon.sun_color=d.moon_color;
  moon.sun_illuminance_lux=d.moon_illuminance_lux*moon_energy(d.moon_phase);
  return atmosphere3d_sun_light(moon,altitude_m,base);
}
light3d atmosphere3d_key_light(const atmosphere3d_desc &d,float altitude_m,const light3d &base) {
  const auto sun=atmosphere3d_sun_light(d,altitude_m,base),moon=atmosphere3d_moon_light(d,altitude_m,base);
  const auto energy=[](rgba c) {const auto linear=[](float v){return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);};return c.a*(.2126f*linear(c.r)+.7152f*linear(c.g)+.0722f*linear(c.b));};
  return energy(sun.color)>=energy(moon.color)?sun:moon;
}
RenderTextureHandle portable_atmosphere_aerial(context &ctx,model_store &store,RenderTextureHandle source) {
  if(!store.portable_atmosphere_drawn||!store.portable_atmosphere||!store.portable_atmosphere->view_aerial) return source;
  auto &s=*store.portable_atmosphere;auto &window=ctx_ref<window_guard>(ctx);
  std::erase_if(s.retired_scenes,[&](RenderTargetHandle h) {
    if(window.graphics->target_in_use(h)||window.graphics->texture_in_use(renderer_target_texture(ctx,h))) return false;
    renderer_destroy_target(ctx,h);return true;
  });
  const auto w=store.portable_hdr_width,h=store.portable_hdr_height;
  if(!s.aerial_scene||s.scene_width!=w||s.scene_height!=h) {
    const auto next=renderer_create_hdr_target(ctx,w,h);
    try {if(s.aerial_scene) {
      if(window.graphics->target_in_use(s.aerial_scene)||window.graphics->texture_in_use(renderer_target_texture(ctx,s.aerial_scene))) s.retired_scenes.push_back(s.aerial_scene);
      else renderer_destroy_target(ctx,s.aerial_scene);
    }} catch(...) {renderer_destroy_target(ctx,next);throw;}
    s.aerial_scene=next;s.scene_width=w;s.scene_height=h;
  }
  auto rows=s.current;const auto eye=store.portable_camera.position;rows[13]={eye.x,eye.y,eye.z,0};
  const auto m=MatrixInvert(store.portable_view_projection);
  rows[14]={m.m0,m.m4,m.m8,m.m12};rows[15]={m.m1,m.m5,m.m9,m.m13};
  rows[16]={m.m2,m.m6,m.m10,m.m14};rows[17]={m.m3,m.m7,m.m11,m.m15};
  const auto vertices=quad();MeshDraw draw{vertices,{}};draw.texture=store.portable_white;draw.depth_test=false;draw.blend=RenderBlend::Opaque;
  draw.shader=bindings(ctx,s,rows,9,source,renderer_target_depth_texture(ctx,store.portable_hdr));
  renderer_begin_target(ctx,s.aerial_scene,{});
  try {renderer_draw_mesh(ctx,draw);renderer_end_target(ctx);} catch(...) {renderer_end_target(ctx);throw;}
  ++s.stats.aerial_composites;return renderer_target_texture(ctx,s.aerial_scene);
}
void atmosphere3d_release(context &ctx) {
  model_store &store=ctx_ref<model_store>(ctx);if(!store.portable_atmosphere) return;
  auto &device=*ctx_ref<window_guard>(ctx).graphics;auto &s=*store.portable_atmosphere;
  const std::array targets{s.transmittance,s.sky_view,s.multiple,s.aerial_radiance,s.aerial_transmittance,s.environment_base,s.environment_atlas,s.environment_sky,s.aerial_scene};
  const auto busy=[&](RenderTargetHandle h){return h&&(device.target_in_use(h)||device.texture_in_use(renderer_target_texture(ctx,h)));};
  if(store.portable_pass||device.shader_in_use(s.program)||std::any_of(targets.begin(),targets.end(),busy)||std::any_of(s.retired_scenes.begin(),s.retired_scenes.end(),busy))
    throw std::logic_error("njin atmosphere: release after the frame using the atmosphere ends");
  for(auto h:targets) if(h) renderer_destroy_target(ctx,h);
  for(auto h:s.retired_scenes) renderer_destroy_target(ctx,h);
  if(s.program) device.destroy_shader(s.program);
  store.portable_atmosphere.reset();
}
}
