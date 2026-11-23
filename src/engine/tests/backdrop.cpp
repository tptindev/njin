#include <njin.h>
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
namespace {
unsigned frame=0;std::string capture;
void require(bool b,const char *m) {if(!b) throw std::runtime_error(m);}
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
}
void update(njin::context &ctx) {
  if(frame==16) njin::window_set_virtual_size(ctx,{160,120});
#if !defined(__ANDROID__)
  if(frame==20) njin::window_set_size(ctx,{640,480});
#endif
}
void render(njin::context &ctx) {
  const unsigned stage=frame/4;const auto screen=njin::screen_size(ctx);
  for(int x=0;x<int(screen.x);x+=4) njin::draw_rect(ctx,{{float(x),0},{4,screen.y}},x%8?njin::rgba{1,0,0,1}:njin::rgba{0,0,1,1});
  const njin::rect area{{40,40},{120,100}};
  if(stage==1||stage>=4) njin::draw_backdrop_blur(ctx,area,12,20,{1,1,1,1});
  if(stage==2) {njin::draw_backdrop_blur(ctx,area,12,20,{1,1,1,1});njin::draw_backdrop_blur(ctx,{{80,60},{120,100}},8,12,{.3f,1,.3f,.5f});}
  if(stage==3) {njin::clip_begin(ctx,{{0,0},{100,240}});njin::draw_backdrop_blur(ctx,area,12,20,{1,1,1,1});njin::clip_end(ctx);}
  if(stage>=4) {njin::draw_offset_push(ctx,{5,5},.5f);njin::draw_backdrop_blur(ctx,{{20,20},{80,40}},6,5,{1,1,1,.8f});njin::draw_offset_pop(ctx);}
  if(frame%4==2) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
  if(++frame==24) njin::quit(ctx);
}
void shutdown(njin::context &) {
  const auto im=[](unsigned s){return njin::renderer_load_image((capture+"."+std::to_string(s)+".png").c_str());};
  const auto base=im(0),blur=im(1),overlap=im(2),clipped=im(3);
  unsigned changed=0,mixed=0,over=0;const unsigned scale=base.width/320;
  // Pixel coordinates are window pixels; Android may letterbox the logical image.
  for(size_t p=0;p<base.pixels.size();p+=4) {
    changed+=std::abs(int(base.pixels[p])-int(blur.pixels[p]))>10;
    mixed+=blur.pixels[p]>60&&blur.pixels[p+2]>60;
    over+=std::abs(int(overlap.pixels[p])-int(blur.pixels[p]))>10;
  }
  require(changed>1000&&mixed>1000&&over>500,"GPU backdrop blur/tint/overlap missing");
#if !defined(__ANDROID__)
  const auto at=[&](const auto &image,unsigned x,unsigned y){return (size_t(y)*image.width+x)*4;};
  require(base.pixels[at(base,42,42)]==blur.pixels[at(blur,42,42)],"rounded backdrop corner was filled");
  require(base.pixels[at(base,130,80)]==clipped.pixels[at(clipped,130,80)],"backdrop scissor leaked");
  require(im(5).width==640&&im(5).height==480,"backdrop resize capture missing");
#endif
  require(!im(4).pixels.empty()&&!im(5).pixels.empty(),"virtual/offset backdrop captures missing");
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_update,update);njin::ecs_register(ctx,njin::phase_post_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin GPU backdrop",.width=320,.height=240,.target_fps=120};cfg.audio=false;cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
#if defined(__ANDROID__)
    cfg.virtual_size={320,240};
#endif
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) require(njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend),"invalid renderer");else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    capture=(std::filesystem::absolute(argv[0]).parent_path()/"captures"/("backdrop-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(unsigned i=0;i<2;++i) {frame=0;cfg.width=320;cfg.height=240;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="backdrop regression",.setup=setup});njin::run(*ctx);}return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
