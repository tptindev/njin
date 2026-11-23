// Normal Njin entry points: independent controls for MS, AP, SH/GGX and moon/stars.
#include <njin.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#if defined(__ANDROID__)
#include <android/log.h>
#endif
namespace {
unsigned frame=0;
unsigned prepared_frame=~0u;
std::string capture;
njin::texture_handle black;
njin::RenderTargetHandle small,wide;
void require(bool v,const char *why) {if(!v) throw std::runtime_error(why);}
void report(const char *format,...) {
  char text[1024];va_list args;va_start(args,format);std::vsnprintf(text,sizeof(text),format,args);va_end(args);std::fputs(text,stdout);
#if defined(__ANDROID__)
  __android_log_write(ANDROID_LOG_INFO,"NjinAtmosphere",text);
#endif
}
void startup(njin::context &ctx) {
  prepared_frame=~0u;
  const unsigned char pixels[]{0,0,0,255,0,0,0,255};black=njin::texture_from_pixels(ctx,pixels,2,1);
  small=njin::renderer_create_target(ctx,96,64);wide=njin::renderer_create_target(ctx,192,80);
  const auto day=njin::atmosphere3d_at_hour(njin::atmosphere3d_physical(),12);
  const auto night=njin::atmosphere3d_at_hour(day,0);
  require(day.sun_direction.y>0&&night.sun_direction.y<0&&night.moon_direction.y>0,"time of day directions wrong");
}
njin::atmosphere3d_desc descriptor(unsigned stage) {
  auto d=njin::atmosphere3d_physical();d.aerial_perspective=false;d.environment_lighting=false;
  d.environment_update_seconds=0;d.moon_illuminance_lux=0;d.night_luminance_cd_m2=0;d.star_luminance_cd_m2=0;
  if(stage==0||stage==2) d.multiple_scattering=0;
  if((stage>=4&&stage<=8)||stage==10||stage>=16) d.environment_lighting=true;
  if(stage==5) d.sun_direction={0,.01f,-1};
  if(stage==6) d.sun_direction={0,-.15f,1};
  if(stage==7) d.environment_update_seconds=60;
  if(stage>=12&&stage<=15) d.aerial_perspective=true;
  if(stage==13) d.sun_illuminance_lux=0;
  if(stage>=16&&stage<=20) {
    d.sun_direction={0,-.3f,-1};d.moon_direction={.4f,.4f,1};d.moon_illuminance_lux=.25f;
    d.night_luminance_cd_m2=.002f;d.star_luminance_cd_m2=.2f;d.moon_radius_rad=.018f;
    if(stage==17) d.moon_phase=.5f;
    if(stage>=18) d.moon_phase=0;
    if(stage==19) d.star_luminance_cd_m2=0;
    if(stage==20) d.star_rotation_rad=.4f;
  }
  if(stage==21) {d.sun_direction={0,-.15f+.1f*(frame%4),1};d.moon_direction=-njin::normalize(d.sun_direction);d.moon_illuminance_lux=.25f;}
  if(stage>=22) {d.aerial_perspective=true;d.environment_lighting=true;}
  return d;
}
void scene(njin::context &ctx,njin::atmosphere3d_desc d,unsigned stage,float altitude=2,bool conflict=false) {
  const bool distant=stage>=11&&stage<=15;
  njin::light3d light;light.hdr=true;light.color={0,0,0,1};light.ambient={0,0,0,1};light.environment=black;light.environment_visible=false;
  light.tonemap=njin::tonemap3d_aces;light.exposure=stage>=16&&stage<=20?20000.f:1.f;
  if(stage>=16&&stage<=20) light=njin::atmosphere3d_key_light(d,altitude,light);
  if(conflict) light.fog_density=.01f;
  njin::light3d_set(ctx,light);
  njin::post3d post;if(stage==22) {post.ssao=.5f;post.bloom=.2f;post.aa=njin::aa3d_fxaa;}if(stage==26) post.oit=true;njin::post3d_set(ctx,post);
  njin::camera3d camera;camera.position={0,altitude,0};camera.target={0,altitude+.5f,10};camera.entities=false;
  camera.near_plane=1;camera.far_plane=50000;
  njin::begin_3d(ctx,camera);
  if(conflict) {
    bool rejected=false;try{njin::draw_atmosphere3d(ctx,d);}catch(const std::logic_error &){rejected=true;}
    require(rejected,"AP silently combined with legacy fog");njin::end_3d(ctx);return;
  }
  njin::draw_atmosphere3d(ctx,d);
  njin::material3d mat;mat.pbr=true;mat.roughness=.8f;
  if(distant) {
    mat.unlit=true;njin::material3d_set(ctx,mat);
    njin::draw_cube3d(ctx,{0,altitude+300,6000},{1600,1600,1600},{.8f,.04f,.02f,1});
  } else {
    njin::material3d_set(ctx,mat);njin::draw_cube3d(ctx,{-1.1f,altitude+.5f,5},{1.4f,1.4f,1.4f},{.8f,.8f,.8f,1});
    mat.metallic=1;mat.roughness=.15f;njin::material3d_set(ctx,mat);njin::draw_cube3d(ctx,{1.1f,altitude+.5f,5},{1.4f,1.4f,1.4f},{.8f,.8f,.8f,1});
    if(stage==26) njin::draw_cube3d(ctx,{0,altitude+.5f,3},{1,1,1},{.2f,.8f,1,.3f});
  }
  njin::end_3d(ctx);
}
void render(njin::context &ctx) {
  const unsigned stage=frame/4;auto d=descriptor(stage);
  if(frame==32&&prepared_frame!=frame) njin::atmosphere3d_environment_refresh(ctx);
  if(frame==96&&prepared_frame!=frame) {
    const auto before=njin::renderer_resource_stats(ctx);njin::atmosphere3d_release(ctx);
    const auto after=njin::renderer_resource_stats(ctx);
    require(before.live_targets>=after.live_targets+9,"full atmosphere release missed owned targets");
    require(njin::atmosphere3d_statistics(ctx).environment_builds==0,"full atmosphere statistics did not reset");
  }
  if(frame==100&&prepared_frame!=frame) {auto stale=d;stale.rayleigh_per_km=stale.rayleigh_per_km*1.1f;scene(ctx,stale,stage);}
  prepared_frame=frame;
  if(stage==15) {
    njin::renderer_begin_target(ctx,small,{});scene(ctx,d,stage,2);njin::renderer_end_target(ctx);
    njin::renderer_begin_target(ctx,wide,{});scene(ctx,d,stage,20000);njin::renderer_end_target(ctx);
  }
  if(frame==92) scene(ctx,d,stage,2,true);
  const auto before=njin::atmosphere3d_statistics(ctx);
  scene(ctx,d,stage,stage==14?20000.f:2.f);
  const auto after=njin::atmosphere3d_statistics(ctx);
  // Keep each capture state until its asynchronous IBL snapshot is published.
  if(d.environment_lighting&&after.environment_pending) return;
  if(frame==4) require(after.transmittance_builds==before.transmittance_builds&&after.multiple_scattering_builds==1,"MS did not build independently of transmittance");
  if(frame==12) require(after.multiple_scattering_builds==before.multiple_scattering_builds,"MS strength toggle rebuilt unit LUT");
  if(stage==7) require(after.environment_builds==before.environment_builds&&after.environment_deferred>before.environment_deferred,"IBL update budget was ignored");
  if(frame==32) require(after.environment_builds==before.environment_builds+1,"forced IBL refresh did not execute");
  if(frame==76||frame==80) require(after.environment_builds==before.environment_builds&&after.sky_view_builds==before.sky_view_builds,"stars invalidated transport/IBL");
  if(frame==100) require(after.environment_discarded>0,"stale medium job was not discarded");
  if(frame%4!=0&&stage!=15&&stage!=21)
    require(after.transmittance_builds==before.transmittance_builds&&after.multiple_scattering_builds==before.multiple_scattering_builds&&after.aerial_builds==before.aerial_builds,"stationary full atmosphere rebuilt LUT");
  if(frame%4==2) {
    njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
    report("full-atmosphere stage=%u T=%llu MS=%llu sky=%llu AP=%llu IBL=%llu bytes=%llu env_cpu_ms=%.3f worker_ms=%.3f\n",stage,
      (unsigned long long)after.transmittance_builds,(unsigned long long)after.multiple_scattering_builds,(unsigned long long)after.sky_view_builds,
      (unsigned long long)after.aerial_builds,(unsigned long long)after.environment_builds,(unsigned long long)after.color_bytes,after.last_environment_cpu_ms,after.last_environment_worker_ms);
  }
  if(++frame==108) {auto pending=d;pending.mie_absorption_per_km*=1.2f;scene(ctx,pending,stage);njin::quit(ctx);}
}
void shutdown(njin::context &) {
  const auto image=[](unsigned i){return njin::renderer_load_image((capture+"."+std::to_string(i)+".png").c_str());};
  const auto patch=[](const njin::RenderImageAsset &im,float u,float v,unsigned c=3) {
    // The desktop reference is 4:3; keep the same camera ray on wide phone screens.
    u=.5f+(u-.5f)*(4.f/3)/(float(im.width)/im.height);
    double sum=0;const int x=int(im.width*u),y=int(im.height*v);
    for(int dy=-3;dy<=3;++dy) for(int dx=-3;dx<=3;++dx) {
      const auto *p=&im.pixels[(size_t(y+dy)*im.width+x+dx)*4];sum+=c<3?p[c]:(p[0]+p[1]+p[2])/3.;
    }return sum/49;
  };
  const auto v=[&](unsigned i,float x=.3f,float y=.5f,unsigned c=3){return patch(image(i),x,y,c);};
  report("full-atmosphere controls MS %.2f/%.2f IBL %.2f/%.2f other_material %.2f AP red %.2f/%.2f blue %.2f/%.2f night %.2f/%.2f\n",
    v(0,.75f,.15f),v(1,.75f,.15f),v(3),v(4),v(4,.7f),v(11,.5f,.5f,0),v(12,.5f,.5f,0),v(11,.5f,.5f,2),v(12,.5f,.5f,2),v(16),v(18));
  require(image(0).pixels==image(2).pixels,"single scattering round trip changed");
  require(v(1,.75f,.15f)>v(0,.75f,.15f)+1,"multiple scattering has no visible energy");
  require(v(4)>v(3)+10&&v(4,.7f)>v(3,.7f)+5,"atmosphere SH/GGX did not light diffuse/specular geometry");
  require(v(6)<v(4)-10&&v(7)<v(8)-10,"night IBL or deferred/forced update did not affect geometry");
  require(v(9)<v(10)-10,"disabling atmosphere IBL did not restore game environment");
  require(v(12,.5f,.5f,2)>v(11,.5f,.5f,2)+5,"aerial radiance did not haze distant geometry");
  require(v(13,.5f,.5f,0)<v(11,.5f,.5f,0)-5,"RGB aerial extinction did not attenuate distant geometry");
  require(v(16)>v(18)+1,"moon did not light night geometry through key light/IBL");
  const auto lunar_pixels=[](const njin::RenderImageAsset &im) {
    size_t count=0;for(size_t p=0;p<im.pixels.size();p+=4) count+=im.pixels[p]>240&&im.pixels[p+1]>240&&im.pixels[p+2]>240;return count;
  };
  const auto full=lunar_pixels(image(16)),half=lunar_pixels(image(17)),dark=lunar_pixels(image(18));
  report("lunar disk bright pixels full=%zu half=%zu new=%zu\n",full,half,dark);
  require(full>half+3&&half>dark+3,"lunar disk phase/full/new controls failed");
  const auto stars=image(18),no_stars=image(19),rotated=image(20);size_t changed=0,moved=0;
  for(size_t i=0;i<stars.pixels.size();i+=4) {changed+=std::abs(int(stars.pixels[i])-int(no_stars.pixels[i]))>8;moved+=std::abs(int(stars.pixels[i])-int(rotated.pixels[i]))>8;}
  require(changed>3&&moved>3,"stars were absent or did not rotate");
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin complete atmosphere",.width=320,.height=240,.target_fps=60};cfg.audio=false;cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
    for(int i=1;i<argc;++i) {if(std::strncmp(argv[i],"--renderer=",11)==0) require(njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend),"invalid backend");if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;}
    capture=(std::filesystem::absolute(argv[0]).parent_path()/"captures"/("atmosphere-full-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(unsigned repeat=0;repeat<2;++repeat) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="complete atmosphere fixture",.setup=setup});njin::run(*ctx);}
    return 0;
  } catch(const std::exception &e) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR,"NjinAtmosphere","%s",e.what());
#endif
    std::fprintf(stderr,"%s\n",e.what());return 1;
  }
}
