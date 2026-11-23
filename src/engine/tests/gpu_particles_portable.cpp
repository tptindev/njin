#include <njin.h>
#include "../runtime/modules/particles_gpu.h"
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
#define Rectangle Win32Rectangle
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#include <windows.h>
#undef Rectangle
#undef CloseWindow
#undef ShowCursor
#endif
namespace {
unsigned frame=0;std::string capture;entt::entity source,icon;njin::texture_handle texture;
void require(bool b,const char *m) {if(!b) throw std::runtime_error(m);}
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  require(njin::particles_gpu_available(ctx),"bgfx GPU analytic particles unavailable");njin::time_set_scale(ctx,0);
  auto &r=njin::world(ctx);source=r.create();r.emplace<njin::transform>(source).pos={80,40};
  auto &em=r.emplace<njin::particle_emitter>(source);em.emitting=false;em.gpu=true;em.gravity={0,20};em.size_start=12;em.size_end=12;em.color_start={1,0,0,1};em.color_end={0,1,0,1};
  em.particles.push_back({.pos={40,40},.velocity={40,0},.life=2,.spin=90,.size=1});r.emplace<njin::particle_gpu_buffer>(source).next_compact=100;
  const unsigned char cyan[]{0,255,255,255,0,255,255,255};texture=njin::texture_from_pixels(ctx,cyan,2,1);
  icon=r.create();r.emplace<njin::transform>(icon);auto &textured=r.emplace<njin::particle_emitter>(icon);textured.emitting=false;textured.gpu=true;textured.texture=texture;textured.size_start=20;textured.size_end=20;textured.color_start=textured.color_end={1,1,1,1};
  textured.particles.push_back({.pos={120,20},.life=2,.size=1});r.emplace<njin::particle_gpu_buffer>(icon).next_compact=100;
}
void update(njin::context &ctx) {
  const unsigned stage=frame/3;auto &r=njin::world(ctx);auto &em=r.get<njin::particle_emitter>(source);
  const float clock=stage==0?0:stage==1?.5f:stage==6?2.1f:1;
  r.get<njin::particle_gpu_buffer>(source).clock=clock;r.get<njin::particle_gpu_buffer>(icon).clock=clock;
  em.drag=stage>=3?2:0;em.local_space=stage==4;
  if(stage==4) r.get<njin::transform>(source).rot=90;else r.get<njin::transform>(source).rot=0;
  em.shape=stage==1?njin::particle_square:stage==2?njin::particle_soft:njin::particle_circle;
  em.blend=stage==5?njin::blend_additive:njin::blend_alpha;
  if(stage==5) njin::particles_set_backend(ctx,njin::particle_backend_cpu);
  require(em.gpu,"backend switch moved live GPU particles to CPU");
}
void render(njin::context &ctx) {
  const unsigned stage=frame/3;
  require(njin::world(ctx).get<njin::particle_emitter>(source).particles.front().pos.x==40,"GPU trajectory changed CPU spawn positions");
  if(frame%3==1) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
  if(++frame==21) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  const auto im=[](unsigned i){return njin::renderer_load_image((capture+"."+std::to_string(i)+".png").c_str());};
  const auto sample_near=[&](unsigned stage,int x,int y) {
    const auto image=im(stage);float scale=1,ox=0,oy=0;
#if defined(__ANDROID__)
    const float fit=std::min(image.width/160.f,image.height/120.f);scale=fit>=1?std::floor(fit):fit;
    ox=(image.width-160*scale)*.5f;oy=(image.height-120*scale)*.5f;
#endif
    unsigned lit=0;for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) {
      const int px=int(ox+(x+dx+.5f)*scale),py=int(oy+(y+dy+.5f)*scale);
      require(px>=0&&py>=0&&px<int(image.width)&&py<int(image.height),"GPU particle sample outside viewport");
      const size_t p=(size_t(py)*image.width+px)*4;lit+=image.pixels[p]+image.pixels[p+1]+image.pixels[p+2]>50;
    }return lit;
  };
  require(sample_near(0,40,40)>10&&sample_near(1,60,42)>10&&sample_near(2,80,50)>5,"GPU gravity/velocity trajectory is wrong");
  require(sample_near(3,57,45)>10&&sample_near(4,34,97)>10,"GPU drag or local-space transform is wrong");
  require(sample_near(0,120,20)>10,"textured GPU particles missing");
  const auto dead=im(6);unsigned colored=0;for(size_t p=0;p<dead.pixels.size();p+=4) colored+=dead.pixels[p]+dead.pixels[p+1]+dead.pixels[p+2]>40;
  require(colored==0,"expired GPU particles remained visible");
  const auto stats=njin::renderer_resource_stats(ctx);
  require(stats.instanced_draws>=30&&stats.instance_uploads==2,"GPU spawn buffers were uploaded again on clock-only frames");
  njin::world(ctx).destroy(source);njin::world(ctx).destroy(icon);njin::texture_unload(ctx,texture);
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_update,update);njin::ecs_register(ctx,njin::phase_post_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin GPU analytic particles",.width=160,.height=120,.target_fps=120};cfg.audio=false;cfg.clear_bg_color={0,0,0,1};cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
#if defined(__ANDROID__)
    cfg.virtual_size={160,120};
#endif
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) require(njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend),"invalid renderer");else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    capture=(std::filesystem::absolute(argv[0]).parent_path()/"captures"/("gpu-particles-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(unsigned i=0;i<2;++i) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="GPU analytic particles",.setup=setup});njin::run(*ctx);}return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
