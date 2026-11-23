#include <njin.h>
#include <algorithm>
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
int frame=0;
bool mobile=false;
std::string capture;
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  const int stage=frame/6;
  if(!mobile&&frame==48) njin::window_set_size(ctx,{800,600});
  if(!mobile&&frame==54) njin::window_set_size(ctx,{640,480});
  auto light=njin::light3d_get(ctx);light.color={0,0,0,1};light.ambient={1,1,1,1};light.hdr=true;light.tonemap=njin::tonemap3d_linear;light.shadows=stage==9;light.shadow_size=256;light.fog_color={.1f,.2f,.3f,1};njin::light3d_set(ctx,light);
  njin::post3d post;post.ssr=stage==0||stage==3||stage==6?0.f:1.f;post.ssr_steps=96;post.ssr_thickness=.15f;post.ssr_sky=stage==10?1.f:0.f;
  if(stage==9) {post.ssao=.5f;post.bloom=.04f;post.aa=njin::aa3d_fxaa;}
  njin::post3d_set(ctx,post);
  njin::camera3d camera;camera.position={4,3,6};camera.target={0,.5f,0};camera.fovy=60;camera.near_plane=.1f;camera.far_plane=50;camera.entities=false;
  camera.projection=stage==6||stage==7?njin::camera3d_orthographic:njin::camera3d_perspective;camera.ortho_height=8;
  njin::begin_3d(ctx,camera);
  njin::material3d surface;surface.specular=0;surface.reflect=stage==4?0.f:1.f;surface.roughness=stage==2?1.f:.05f;njin::material3d_set(ctx,surface);
  njin::draw_plane3d(ctx,{0,0,0},{10,10},{.25f,.25f,.25f,1});
  surface.reflect=0;surface.roughness=.5f;njin::material3d_set(ctx,surface);
  if(stage!=5&&stage!=10) {
    njin::draw_cube3d(ctx,{-1,1,0},{1,2,1},{1,.04f,.04f,1});
    njin::draw_cube3d(ctx,{1,.6f,.3f},{1,1.2f,1},{.04f,.1f,1,1});
  }
  njin::end_3d(ctx);
  if(frame%6==4) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
  if(++frame==66) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  const auto image=[](int stage){return njin::renderer_load_image((capture+"."+std::to_string(stage)+".png").c_str());};
  const auto off=image(0),on=image(1),rough=image(2),reset=image(3),nonreflective=image(4),miss=image(5),ortho=image(6),orthoOn=image(7),resized=image(8),sky=image(10);
  const auto reflected=[](const auto &a,const auto &b) {
    unsigned result=0;
    for(size_t i=0;i<a.pixels.size();i+=4) {
      // Only originally neutral floor pixels. Visible cubes cannot satisfy this.
      if(a.pixels[i]<40||a.pixels[i]>90||std::abs(int(a.pixels[i])-int(a.pixels[i+1]))>2||std::abs(int(a.pixels[i])-int(a.pixels[i+2]))>2) continue;
      const int r=b.pixels[i],g=b.pixels[i+1],blue=b.pixels[i+2];
      result+=r>g+30||blue>g+30;
    }
    return result;
  };
  const auto count=reflected(off,on),orthographic=reflected(ortho,orthoOn);
  std::printf("SSR reflected floor pixels: perspective=%u orthographic=%u\n",count,orthographic);
  if(count<100||orthographic<100) throw std::runtime_error("SSR did not reflect colored geometry on the neutral floor");
  if(off.pixels!=reset.pixels||off.pixels!=rough.pixels||off.pixels!=nonreflective.pixels) throw std::runtime_error("SSR disable/roughness/material mask control failed");
  for(size_t i=0;i<off.pixels.size();i+=4) {
    const int r=off.pixels[i],g=off.pixels[i+1],b=off.pixels[i+2];
    // A float16 round-trip may round a silhouette sample by one 8-bit level.
    if((r>g+50||b>g+50)&&(std::abs(int(off.pixels[i])-int(on.pixels[i]))>1||std::abs(int(off.pixels[i+1])-int(on.pixels[i+1]))>1||std::abs(int(off.pixels[i+2])-int(on.pixels[i+2]))>1))
      throw std::runtime_error("SSR changed a nonreflective occluder");
  }
  unsigned missPixels=0,skyPixels=0;
  for(size_t i=0;i<miss.pixels.size();i+=4) if(miss.pixels[i]>40&&miss.pixels[i]<90) {++missPixels;skyPixels+=sky.pixels[i+2]>sky.pixels[i]+10;}
  if(missPixels<1000||skyPixels<1000) throw std::runtime_error("SSR miss/sky fallback control failed");
  if(!mobile&&(resized.width!=800||resized.height!=600)) throw std::runtime_error("SSR resize failed");
  const auto stats=njin::renderer_resource_stats(ctx);
  // Primitive caches, scene, two SSR targets + borrowed mask depth, cached AO/bloom/AA.
  if(stats.live_meshes!=2||stats.live_targets!=14||stats.live_textures!=17) throw std::runtime_error("SSR resource accounting failed: "+std::to_string(stats.live_targets)+","+std::to_string(stats.live_textures));
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin SSR regression",.width=640,.height=480,.target_fps=60};cfg.audio=false;cfg.resizable=true;cfg.clear_bg_color={0,0,0,1};cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;else if(std::strcmp(argv[i],"--mobile")==0) mobile=true;
    capture=(std::filesystem::path(argv[0]).parent_path()/"captures"/("ssr-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(int repeat=0;repeat<2;++repeat) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="SSR regression",.setup=setup});njin::run(*ctx);}
    return 0;
  } catch(const std::exception &e) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR,"NjinSSR","%s",e.what());
#endif
    std::fprintf(stderr,"%s\n",e.what());return 1;
  }
}
