// API fixture: sky transport, dependency counters, stable game IBL, targets and lifetime.
#include <njin.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
namespace {
unsigned frame=0;
std::string capture;
njin::texture_handle environment;
njin::RenderTargetHandle small,wide;
void require(bool v,const char *why) {if(!v) throw std::runtime_error(why);}
template<class F> void refuses(F fn) {bool caught=false;try{fn();}catch(const std::exception &){caught=true;}require(caught,"invalid atmosphere call was accepted");}
void startup(njin::context &ctx) {
  const unsigned char pixels[]{180,160,140,255,180,160,140,255};
  environment=njin::texture_from_pixels(ctx,pixels,2,1);
  njin::light3d base;base.environment_intensity=.37f;
  njin::atmosphere3d_desc desc;desc.sun_direction={0,-1,0};
  const auto sun=njin::atmosphere3d_sun_light(desc,2,base);
  require(sun.color.r==0&&sun.color.g==0&&sun.color.b==0&&sun.environment_intensity==base.environment_intensity,"sun helper changed IBL or lit through planet");
  small=njin::renderer_create_target(ctx,96,64);wide=njin::renderer_create_target(ctx,192,80);
  refuses([&]{njin::draw_atmosphere3d(ctx);});
}
void scene(njin::context &ctx,njin::atmosphere3d_desc d,float altitude,unsigned stage,bool legacy=false) {
  njin::light3d light;light.hdr=true;light.exposure=frame==63?.8f:1.f;light.tonemap=njin::tonemap3d_aces;
  light.color={0,0,0,1};light.ambient={0,0,0,1};light.environment=environment;light.environment_visible=false;
  light.environment_intensity=stage==8?0.f:1.f;
  if(stage>=16) {light.environment_intensity=0;light=njin::atmosphere3d_sun_light(d,altitude,light);}
  njin::light3d_set(ctx,light);
  njin::camera3d camera;camera.position={0,altitude,0};camera.target={0,altitude+.5f,10};camera.entities=false;
  if(stage==6) camera.target={-10,altitude+2,0};
  njin::begin_3d(ctx,camera);
  if(legacy) {njin::sky3d sky;sky.drive_light=false;sky.weather.clouds=0;njin::draw_sky3d(ctx,sky);}
  else {
    if(frame==0) {
      auto bad=d;bad.mie_g=1;refuses([&]{njin::draw_atmosphere3d(ctx,bad);});
      bad=d;bad.sun_direction={};refuses([&]{njin::draw_atmosphere3d(ctx,bad);});
    }
    njin::draw_atmosphere3d(ctx,d);
    if(frame==0) refuses([&]{njin::atmosphere3d_release(ctx);});
  }
  njin::material3d material;material.pbr=true;material.roughness=.5f;njin::material3d_set(ctx,material);
  njin::draw_cube3d(ctx,{0,altitude+.5f,5},{1.5f,1.5f,1.5f},{.8f,.8f,.8f,1});
  njin::end_3d(ctx);
}
void render(njin::context &ctx) {
  const unsigned stage=frame/4;
  njin::atmosphere3d_desc d;float altitude=2;
  if(stage==1) d.sun_direction={0,.01f,1};
  if(stage==2) d.sun_direction={0,-.15f,1};
  if(stage>=4&&stage<=6) d.rayleigh_per_km=d.rayleigh_per_km*1.5f;
  if(frame>=44&&frame<60) d.sun_direction={0,-.12f+.02f*float(frame-44),1};
  if(frame>=60) {d.mie_g=.7f;d.sun_disk=frame==60;d.sun_radius_rad=frame>=62?.006f:.004675f;}
  if(stage>=16) d.sun_direction={0,stage==16?.707f:stage==17?.01f:-.15f,-.707f};
  if(stage==5||stage==6) altitude=20000;
#if !defined(__ANDROID__)
  if(frame==36) njin::window_set_size(ctx,{400,240});
#endif
  if(frame==40) {
    const auto before=njin::renderer_resource_stats(ctx);njin::atmosphere3d_release(ctx);
    const auto after=njin::renderer_resource_stats(ctx);
    require(before.live_targets==after.live_targets+2&&before.live_textures==after.live_textures+2,"atmosphere release leaked LUTs");
    require(njin::atmosphere3d_statistics(ctx).sky_draws==0,"atmosphere stats did not reset");
  }
  if(stage==9) {
    njin::renderer_begin_target(ctx,small,{});scene(ctx,d,2,0);njin::renderer_end_target(ctx);
    njin::renderer_begin_target(ctx,wide,{});scene(ctx,d,20000,0);njin::renderer_end_target(ctx);
  }
  const auto before=njin::atmosphere3d_statistics(ctx);
  scene(ctx,d,altitude,stage,stage==7);
  const auto after=njin::atmosphere3d_statistics(ctx);
  if(frame==0) require(after.transmittance_builds==1&&after.sky_view_builds==1,"first LUT build missing");
  if(frame==4||frame==8||frame==12||frame==20)
    require(after.transmittance_builds==before.transmittance_builds&&after.sky_view_builds==before.sky_view_builds+1,"sun/altitude invalidation wrong");
  if(frame==16||frame==32) require(after.transmittance_builds==before.transmittance_builds+1,"medium did not invalidate transmittance");
  if((frame%4!=0&&stage!=9&&(frame<44||frame>=64))||frame==24||(frame>=61&&frame<64))
    require(after.transmittance_builds==before.transmittance_builds&&after.sky_view_builds==before.sky_view_builds,"stationary/rotated view rebuilt LUT");
  if(frame>=44&&frame<=60)
    require(after.transmittance_builds==before.transmittance_builds&&after.sky_view_builds==before.sky_view_builds+1,"continuous sun/phase update invalidated wrong LUT");
  if(frame%4==2) {
    njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
    std::printf("atmosphere stage=%u trans=%llu sky=%llu bytes=%llu update_cpu_ms=%.4f draw_cpu_ms=%.4f\n",stage,
      (unsigned long long)after.transmittance_builds,(unsigned long long)after.sky_view_builds,
      (unsigned long long)after.color_bytes,after.last_update_cpu_ms,after.last_draw_cpu_ms);
  }
  ++frame;if(frame==76) njin::quit(ctx);
}
void shutdown(njin::context &) {
  const auto load=[](unsigned stage){return njin::renderer_load_image((capture+"."+std::to_string(stage)+".png").c_str());};
  const auto patch=[](const njin::RenderImageAsset &im,float u,float v) {
    double sum=0;const int x=int(im.width*u),y=int(im.height*v);
    for(int dy=-3;dy<=3;++dy) for(int dx=-3;dx<=3;++dx) {
      const auto *p=&im.pixels[(size_t(y+dy)*im.width+x+dx)*4];sum+=p[0]+p[1]+p[2];
    }return sum/147;
  };
  const auto day=load(0),horizon=load(1),night=load(2),restored=load(3),noibl=load(8);
  std::printf("sky patch day=%.2f horizon=%.2f below=%.2f; lit IBL=%.2f without=%.2f\n",
    patch(day,.75f,.25f),patch(horizon,.75f,.25f),patch(night,.75f,.25f),patch(day,.5f,.5f),patch(noibl,.5f,.5f));
  require(patch(day,.75f,.25f)>patch(night,.75f,.25f)+10,"sky does not respond to sun below horizon");
  require(std::abs(patch(day,.5f,.5f)-patch(night,.5f,.5f))<1,"sky silently changed game lighting/IBL");
  require(patch(day,.5f,.5f)>patch(noibl,.5f,.5f)+10,"fixture did not exercise lit IBL");
  require(day.pixels==restored.pixels,"sky parameter round trip is unstable");
  require(load(9).pixels==load(10).pixels,"multiple targets or cache reinitialization changed the sky");
  std::printf("atmosphere direct sun: high=%.2f horizon=%.2f below=%.2f\n",patch(load(16),.5f,.5f),patch(load(17),.5f,.5f),patch(load(18),.5f,.5f));
  require(patch(load(16),.5f,.5f)>patch(load(18),.5f,.5f)+10,"attenuated atmosphere sun did not light geometry separately");
}
void setup(njin::context &ctx) {
  njin::ecs_register(ctx,njin::phase_startup,startup);
  njin::ecs_register(ctx,njin::phase_render,render);
  njin::ecs_register(ctx,njin::phase_shutdown,shutdown);
}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin atmosphere LUT",.width=320,.height=240,.target_fps=60};cfg.audio=false;
    cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
    for(int i=1;i<argc;++i) {
      if(std::strncmp(argv[i],"--renderer=",11)==0) require(njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend),"invalid backend");
      if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    }
    capture=(std::filesystem::absolute(argv[0]).parent_path()/"captures"/("atmosphere-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();
    std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(int repeat=0;repeat<2;++repeat) {
      frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);
      njin::mod_register(*ctx,{.name="atmosphere regression",.setup=setup});njin::run(*ctx);
    }
    return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
