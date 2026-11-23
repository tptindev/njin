#include <njin.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
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
int frame=0;bool mobile=false;
std::string capture;
njin::model_handle plane;
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  const njin::vec3 p[]{{-.5f,-.5f,0},{.5f,-.5f,0},{.5f,.5f,0},{-.5f,.5f,0}},n[]{{0,0,1},{0,0,1},{0,0,1},{0,0,1}};
  const njin::u32 indices[]{0,1,2,0,2,3};plane=njin::model_create(ctx,{.positions=p,.normals=n,.vertex_count=4,.indices=indices,.index_count=6});
  auto invalid=njin::light3d_get(ctx);invalid.auto_exposure=true;invalid.auto_exposure_min=2;invalid.auto_exposure_max=1;bool rejected=false;
  try {njin::light3d_set(ctx,invalid);} catch(const std::invalid_argument &) {rejected=true;}
  if(!rejected||njin::light3d_get(ctx).auto_exposure) throw std::runtime_error("invalid exposure settings were accepted");
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  const int stage=std::min(frame/6,18);
  if(!mobile&&frame==66) njin::window_set_size(ctx,{800,600});
  if(!mobile&&frame==72) njin::window_set_size(ctx,{640,480});
  auto light=njin::light3d_get(ctx);light.hdr=stage!=12;light.auto_exposure=stage!=0&&stage!=5&&stage!=16;light.auto_exposure_key=.18f;light.auto_exposure_min=.001f;light.auto_exposure_max=64;light.auto_exposure_speed=stage==3||stage==6||stage==11||stage==13?0.f:stage==18?1.f:1000.f;light.exposure=stage==9||stage==10?2.f:1.f;light.color={0,0,0,1};light.ambient={0,0,0,1};light.shadows=false;light.tonemap=njin::tonemap3d_linear;
  if(stage==7) light.auto_exposure_min=light.auto_exposure_max=.25f;
  if(stage==8) light.auto_exposure_min=light.auto_exposure_max=2;
  njin::light3d_set(ctx,light);
  njin::post3d post;if(stage==10) {post.bloom=.1f;post.aa=njin::aa3d_smaa;}njin::post3d_set(ctx,post);
  auto material=njin::model_material_get(ctx,plane,0);material.color={0,0,0,1};material.double_sided=true;material.surface.pbr=true;material.surface.emission={1,1,1,stage==0||stage==1||stage==3||stage==4||stage==6||stage==13||stage==16||stage==17?4.f:.02f};njin::model_material_set(ctx,plane,0,material);
  njin::camera3d camera;camera.position={0,0,5};camera.target={0,0,0};camera.projection=njin::camera3d_orthographic;camera.ortho_height=8;camera.entities=false;
  njin::begin_3d(ctx,camera);njin::draw_model(ctx,plane,{.scale={100,100,1}});
  if(stage==14||stage==15) {material.surface.emission.a=8;njin::model_material_set(ctx,plane,0,material);njin::draw_model(ctx,plane,{.position=stage==14?njin::vec3{0,0,.1f}:njin::vec3{-4,-2.5f,.1f},.scale={2,2,1}});}
  njin::end_3d(ctx);njin::draw_rect(ctx,{{4,4},{12,12}},{1,0,0,1});
  if((frame<108&&frame%6==4)||frame==112||frame==162) njin::screenshot(ctx,(capture+"."+std::to_string(frame==162?19:stage)+".png").c_str());
  if(++frame==168) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  const auto image=[](int i){return njin::renderer_load_image((capture+"."+std::to_string(i)+".png").c_str());};
  const auto value=[&](int i,float y=.5f){auto im=image(i);return int(im.pixels[(size_t(unsigned(im.height*y))*im.width+im.width/2)*4]);};
  for(int i:{1,2,4,6,9,10,13}) if(std::abs(value(i)-118)>3) throw std::runtime_error("auto exposure target/reset failed at "+std::to_string(i)+": "+std::to_string(value(i)));
  // Mobile omits the resize at 11: zero speed must retain the prior exposure
  // factor when manual exposure changes, rather than reset it as desktop does.
  if(std::abs(value(11)-(mobile?85:118))>3) throw std::runtime_error("exposure resize/history control failed");
  if(value(0)!=255||value(3)!=255) throw std::runtime_error("manual exposure or zero-speed history control failed");
  if(std::abs(value(5)-39)>2||std::abs(value(12)-39)>2||std::abs(value(7)-16)>2||std::abs(value(8)-56)>2) throw std::runtime_error("manual/HDR-off/exposure bounds failed");
  if(value(15,.75f)-value(14,.75f)<8) throw std::runtime_error("center-weighted luminance control failed");
  if(value(18)>=110||value(19)<=value(18)+3) throw std::runtime_error("exposure did not adapt progressively over time");
  if(!mobile) {auto im=image(11);if(im.width!=800||im.height!=600) throw std::runtime_error("exposure resize failed");}
  for(int i:{1,10,11,13,15}) {auto im=image(i);const auto at=(size_t(8)*im.width+8)*4;if(im.pixels[at]!=255||im.pixels[at+1]) throw std::runtime_error("auto exposure modified later UI");}
  njin::model_unload(ctx,plane);const auto stats=njin::renderer_resource_stats(ctx);
  if(stats.live_meshes||stats.live_targets!=(mobile?19:10)||stats.live_textures!=(mobile?23:14)) throw std::runtime_error("exposure resource cleanup failed: "+std::to_string(stats.live_targets)+","+std::to_string(stats.live_textures));
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin auto exposure regression",.width=640,.height=480,.target_fps=120};cfg.audio=false;cfg.resizable=true;cfg.clear_bg_color={0,0,0,1};cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strcmp(argv[i],"--mobile")==0) mobile=true;else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    capture=(std::filesystem::path(argv[0]).parent_path()/"captures"/("exposure-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(int repeat=0;repeat<2;++repeat) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="auto exposure regression",.setup=setup});njin::run(*ctx);}
    return 0;
  } catch(const std::exception &e) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR,"NjinExposure","%s",e.what());
#endif
    std::fprintf(stderr,"%s\n",e.what());return 1;
  }
}
