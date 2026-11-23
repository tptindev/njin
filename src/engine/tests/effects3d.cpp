#include <njin.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#if defined(__ANDROID__)
#include <android/log.h>
#endif
namespace {
int frame=0;
bool mobile=false;
std::string capture;
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  njin::post3d invalid;invalid.outline=std::numeric_limits<float>::quiet_NaN();
  bool rejected=false;try {njin::post3d_set(ctx,invalid);} catch(const std::invalid_argument &) {rejected=true;}
  if(!rejected||njin::post3d_get(ctx).outline!=0) throw std::runtime_error("invalid effects changed the settings");
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  const int stage=frame/6;
  if(!mobile&&frame==96) njin::window_set_size(ctx,{800,600});
  if(!mobile&&frame==102) njin::window_set_size(ctx,{640,480});
  auto light=njin::light3d_get(ctx);light.direction=stage==13?njin::vec3{0,-.18f,-1}:njin::vec3{0,-.18f,1};light.color={1,1,1,stage==14?0.f:1.f};light.ambient={1,1,1,1};light.hdr=stage!=21;light.tonemap=njin::tonemap3d_linear;light.shadows=stage==15;light.shadow_size=256;light.shadow_cascades=1;light.shadow_range=30;njin::light3d_set(ctx,light);
  njin::post3d p;
  p.outline=stage==1||stage==2||stage==3||stage==4||stage==5||stage==7||stage==15?1.f:0.f;
  p.outline_width=stage==2?3.f:1.f;p.outline_crease=stage==3?30.f:180.f;p.outline_fade=stage==4?1.f:0.f;p.outline_color=stage==5?njin::rgba{1,0,0,1}:njin::rgba{0,0,0,1};
  p.shafts=stage==9||stage==13||stage==14||stage==15||stage==16||stage==18?1.f:0.f;
  p.flare=stage==10||stage==11||stage==13||stage==14||stage==15||stage==16||stage==19||stage==20||stage==21?1.f:0.f;
  if(stage==19) p.flare_halo=0;if(stage==20) p.flare=.5f;
  if(stage==15) {p.ssao=.5f;p.ssr=.5f;p.ssr_sky=0;p.bloom=.04f;p.aa=njin::aa3d_fxaa;}
  njin::post3d_set(ctx,p);
  njin::camera3d camera;camera.position=stage<8?njin::vec3{4,3,6}:njin::vec3{0,0,5};camera.target=stage<8?njin::vec3{0,.5f,0}:njin::vec3{0,0,0};camera.fovy=60;camera.entities=false;camera.far_plane=100;
  camera.projection=stage==6||stage==7?njin::camera3d_orthographic:njin::camera3d_perspective;camera.ortho_height=6;
  njin::begin_3d(ctx,camera);
  njin::material3d surface;surface.unlit=true;njin::material3d_set(ctx,surface);
  if(stage<8) njin::draw_cube3d(ctx,{0,1,0},{2,2,2},{1,1,1,1});
  if(stage==11||stage==12||stage==15||stage==18) njin::draw_cube3d(ctx,{0,.9f,0},{1,1,.2f},{.25f,.25f,.25f,1});
  njin::end_3d(ctx);
  njin::draw_rect(ctx,{{4,4},{12,12}},{1,0,0,1});
  if(frame%6==4) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
  if(++frame==132) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  const auto image=[](int i){return njin::renderer_load_image((capture+"."+std::to_string(i)+".png").c_str());};
  const auto off=image(0),thin=image(1),wide=image(2),crease=image(3),fade=image(4),colored=image(5),ortho=image(6),orthoOn=image(7);
  const auto lines=[](const auto &a,const auto &b) {
    unsigned count=0,spill=0;
    for(size_t i=0;i<a.pixels.size();i+=4) {count+=a.pixels[i]>250&&b.pixels[i]<100;spill+=a.pixels[i]==0&&(b.pixels[i]!=0||b.pixels[i+1]!=0||b.pixels[i+2]!=0);}
    if(spill) throw std::runtime_error("outline spilled onto sky pixels");return count;
  };
  const auto a=lines(off,thin),b=lines(off,wide),c=lines(off,crease);
  if(a<50||b<a+100||c<a+50||lines(ortho,orthoOn)<50) throw std::runtime_error("outline width/crease/projection control failed: "+std::to_string(a)+","+std::to_string(b)+","+std::to_string(c));
  if(off.pixels!=fade.pixels) throw std::runtime_error("outline distance fade changed the image");
  unsigned red=0;for(size_t i=0;i<off.pixels.size();i+=4) red+=off.pixels[i]>250&&colored.pixels[i]>250&&colored.pixels[i+1]<10;
  if(red<50) throw std::runtime_error("outline color did not apply");
  const auto base=image(8),shafts=image(9),flare=image(10),hidden=image(11),blocked=image(12),behind=image(13),dark=image(14),resized=image(16),reset=image(17),occludedShafts=image(18),noHalo=image(19),half=image(20),noHdr=image(21);
  const auto changed=[](const auto &a,const auto &b) {unsigned n=0;for(size_t i=0;i<a.pixels.size();i+=4) n+=std::abs(int(a.pixels[i])-int(b.pixels[i]))>4||std::abs(int(a.pixels[i+1])-int(b.pixels[i+1]))>4||std::abs(int(a.pixels[i+2])-int(b.pixels[i+2]))>4;return n;};
  if(changed(base,shafts)<1000||changed(base,flare)<1000) throw std::runtime_error("sun effects produced no visible light");
  if(changed(hidden,blocked)||changed(base,behind)||changed(base,dark)||base.pixels!=reset.pixels) throw std::runtime_error("sun occlusion/behind-camera/zero-intensity/reset control failed");
  if(changed(flare,noHalo)<100||changed(flare,half)<100||flare.pixels!=noHdr.pixels) throw std::runtime_error("flare halo/strength/non-HDR control failed");
  unsigned shadowed=0;for(size_t i=0;i<base.pixels.size();i+=4) shadowed+=blocked.pixels[i]==0&&int(shafts.pixels[i])-int(occludedShafts.pixels[i])>8;
  if(shadowed<100) throw std::runtime_error("shaft occlusion did not reduce illumination");
  if(!mobile&&(resized.width!=800||resized.height!=600)) throw std::runtime_error("effect resize failed");
  const auto combined=image(15);const size_t ui=(size_t(8)*combined.width+8)*4;
  if(combined.pixels[ui]!=255||combined.pixels[ui+1]!=0) throw std::runtime_error("post effects filtered later UI");
  const auto stats=njin::renderer_resource_stats(ctx);
  // Desktop resize drops earlier AO/SSR/bloom/AA caches; mobile retains them.
  // 3D and the later 2D/UI draw each retain their own white texture cache.
  if(stats.live_meshes!=1||stats.live_targets!=(mobile?19:7)||stats.live_textures!=(mobile?23:10)) throw std::runtime_error("effect resource accounting failed: "+std::to_string(stats.live_targets)+","+std::to_string(stats.live_textures));
  std::printf("PASS: outlines %u/%u/%u pixels, depth-occluded shafts/flare, composition and cleanup\n",a,b,c);
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin portable effects regression",.width=640,.height=480,.target_fps=120};cfg.audio=false;cfg.resizable=true;cfg.clear_bg_color={0,0,0,1};cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;else if(std::strcmp(argv[i],"--mobile")==0) mobile=true;
    capture=(std::filesystem::path(argv[0]).parent_path()/"captures"/("effects-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(int repeat=0;repeat<2;++repeat) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="portable effects regression",.setup=setup});njin::run(*ctx);}
    return 0;
  } catch(const std::exception &e) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR,"NjinEffects","%s",e.what());
#endif
    std::fprintf(stderr,"%s\n",e.what());return 1;
  }
}
