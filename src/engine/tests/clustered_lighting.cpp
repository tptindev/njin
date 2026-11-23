#include <njin.h>
#include "../runtime/njin_ctx_get.h"
#include "../runtime/njin_model.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#if defined(__ANDROID__)
#include <android/log.h>
#endif
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
unsigned frame=0;std::string capture;
void require(bool b,const char *m) {if(!b) throw std::runtime_error(m);}
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
}
void render(njin::context &ctx) {
  const unsigned stage=frame/3;njin::light3d sun;sun.hdr=true;sun.color={0,0,0,1};sun.ambient={0,0,0,1};sun.tonemap=njin::tonemap3d_linear;sun.lights_max=stage==0?16:1024;sun.source_shadows=4;sun.source_shadow_size=128;
  if(stage>=8&&stage<=10) {sun.volume_density=.15f;sun.volume_lamps=stage!=10;sun.volume_distance=12;sun.volume_steps=8;sun.volume_sun=0;sun.volume_ambient=0;}
  if(stage==9) sun.lights_max=16;
  njin::light3d_set(ctx,sun);
  njin::camera3d camera;camera.position={0,4,7};camera.target={0,0,0};camera.entities=false;camera.projection=stage==6?njin::camera3d_perspective:njin::camera3d_orthographic;camera.ortho_height=8;camera.near_plane=.1f;camera.far_plane=50;
  njin::begin_3d(ctx,camera);njin::material3d material;material.pbr=true;material.roughness=.8f;njin::material3d_set(ctx,material);
  njin::draw_plane3d(ctx,{0,0,0},{10,10},{1,1,1,1});njin::draw_cube3d(ctx,{0,1,0},{1,2,1},{.8f,.8f,.8f,1});
  const unsigned count=stage<2||stage==9?16:128;
  for(unsigned i=0;i<count;++i) {
    njin::light3d_source l;l.position={float(int(i%4)-2)*1.5f,2,float(int(i/4)-2)*1.5f};l.radius=4;l.intensity=.15f;l.color={.5f,.7f,1,1};
    if(stage>=3) {l.position={1000+float(i),0,0};l.radius=1;if(i==(stage==9?15u:127u)) {l.position={2,3,1};l.radius=10;l.intensity=2;l.color={1,.3f,.1f,1};l.shadows=stage==4;l.kind=stage==7?njin::light3d_spot:njin::light3d_point;l.direction={-2,-3,-1};l.cone=100;}}
    njin::light3d_add(ctx,l);
  }
  njin::end_3d(ctx);
  const njin::model_store &store=njin::ctx_ref<njin::model_store>(ctx);
  if(stage==11) {
    static constexpr njin::RenderVertex quad[]{{-1,1,0,0,0},{1,1,0,1,0},{1,-1,0,1,1},{-1,1,0,0,0},{1,-1,0,1,1},{-1,-1,0,0,1}};
    njin::MeshDraw depth;depth.vertices=quad;depth.texture=njin::renderer_target_depth_texture(ctx,store.portable_hdr);depth.depth_test=false;depth.blend=njin::RenderBlend::Opaque;depth.opaque_alpha=true;
    njin::renderer_draw_mesh(ctx,depth);
  }
  if(stage>0&&stage!=9) {
    require(bool(store.portable_clusters),"extended lights did not create a cluster packet");
    const auto &grid=store.portable_cluster_grid;
    require(grid.counts.size()==16*9*24&&std::any_of(grid.counts.begin(),grid.counts.end(),[](int n){return n>0;}),"cluster lists are empty");
    if(stage>=3) require(*std::max_element(grid.counts.begin(),grid.counts.end())<=1,"offscreen lights were not culled from cluster lists");
  }
  if(frame%3==1) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
  if(++frame==36) njin::quit(ctx);
}
void shutdown(njin::context &) {
  const auto im=[](unsigned s){return njin::renderer_load_image((capture+"."+std::to_string(s)+".png").c_str());};
  const auto changed=[](const auto &a,const auto &b,int threshold=5){unsigned count=0;for(size_t p=0;p<a.pixels.size();p+=4) count+=std::abs(int(a.pixels[p])-int(b.pixels[p]))>threshold||std::abs(int(a.pixels[p+1])-int(b.pixels[p+1]))>threshold||std::abs(int(a.pixels[p+2])-int(b.pixels[p+2]))>threshold;return count;};
  require(changed(im(0),im(1),2)<30,"clustered 16-light output differs from the uniform path");
  require(changed(im(1),im(2))>500,"lights beyond the first sixteen had no effect");
  const auto late=im(3);unsigned red=0;for(size_t p=0;p<late.pixels.size();p+=4) red+=late.pixels[p]>40&&late.pixels[p]>late.pixels[p+1]*1.2;
  require(red>1000,"light index127 was dropped");
  require(changed(im(3),im(4))>100,"shadowed clustered source beyond index16 missing");
  const auto base=im(5),volume=im(8),uniform_volume=im(9),extinction=im(10);
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO,"NjinAndroid","Volume changed pixels: clustered=%u uniform=%u extinction=%u",changed(base,volume),changed(base,uniform_volume),changed(base,extinction));
#endif
  require(changed(base,volume)>100,"clustered volumetric local lights missing");
  require(changed(base,uniform_volume)>100,"uniform volumetric local lights missing");
  require(changed(base,extinction)>100,"volumetric extinction missing");
  require(changed(volume,extinction)>100,"clustered volume has extinction without light scattering");
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin shared clustered lighting",.width=320,.height=240,.target_fps=120};cfg.audio=false;cfg.clear_bg_color={0,0,0,1};cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
#if defined(__ANDROID__)
    cfg.virtual_size={320,240};
#endif
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) require(njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend),"invalid renderer");else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    capture=(std::filesystem::absolute(argv[0]).parent_path()/"captures"/("clustered-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(unsigned i=0;i<2;++i) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="clustered lighting",.setup=setup});njin::run(*ctx);}return 0;
  } catch(const std::exception &e) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR,"NjinAndroid","Clustered regression: %s",e.what());
#endif
    std::fprintf(stderr,"%s\n",e.what());return 1;}
}
