#include <njin.h>
#include <algorithm>
#include <array>
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
int frame=0;
bool mobile=false;
std::string capture;
njin::camera3d camera;
njin::RenderTargetHandle extra;
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  camera.position={4,3,6};camera.target={0,.5f,0};camera.fovy=60;camera.near_plane=.1f;camera.far_plane=50;camera.entities=false;
  extra=njin::renderer_create_target(ctx,16,16);const auto depth=njin::renderer_target_depth_texture(ctx,extra);
  bool rejected=false;try {njin::renderer_destroy_texture(ctx,depth);} catch(const std::logic_error &) {rejected=true;}
  if(!rejected||njin::renderer_target_depth_texture(ctx,extra).id!=depth.id) throw std::runtime_error("borrowed depth ownership/cache failed");
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  const int stage=frame/6;
  if(!mobile&&frame==48) njin::window_set_size(ctx,{800,600});
  if(!mobile&&frame==54) njin::window_set_size(ctx,{640,480});
  auto light=njin::light3d_get(ctx);light.color={0,0,0,1};light.ambient={1,1,1,1};light.shadows=stage==9||stage==11;light.shadow_cascades=stage>=10?2:1;light.shadow_size=stage>=10?512:256;light.shadow_range=30;light.hdr=stage>=9;light.tonemap=njin::tonemap3d_linear;njin::light3d_set(ctx,light);
  if(stage>=10) {light.direction={-.5f,-1,-.5f};light.color={1,1,1,1};light.ambient={.1f,.1f,.1f,1};njin::light3d_set(ctx,light);}
  njin::post3d post;post.ssao=stage==1||stage==2||stage==5||stage==7||stage==8||stage==9?1.f:0.f;post.ssao_radius=.8f;post.ssao_samples=24;post.ssao_half=stage!=2;
  if(stage==9) {post.bloom=.05f;post.aa=njin::aa3d_fxaa;}
  njin::post3d_set(ctx,post);
  camera.projection=stage==6||stage==7?njin::camera3d_orthographic:njin::camera3d_perspective;camera.ortho_height=8;
  njin::begin_3d(ctx,camera);
  njin::material3d surface;surface.specular=0;njin::material3d_set(ctx,surface);
  njin::draw_plane3d(ctx,{0,0,0},{10,10},{1,1,1,1});
  if(stage!=4&&stage!=5&&stage<10) njin::draw_cube3d(ctx,{0,1,0},{2,2,2},{1,1,1,1});
  njin::end_3d(ctx);
  if(frame%6==4) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
  if(++frame==72) njin::quit(ctx);
}
njin::RenderImageAsset image(int stage) {return njin::renderer_load_image((capture+"."+std::to_string(stage)+".png").c_str());}
void shutdown(njin::context &ctx) {
  const auto off=image(0),half=image(1),full=image(2),reset=image(3),flat=image(4),flat_ao=image(5),ortho=image(6),ortho_ao=image(7),resized=image(8);
  const auto darkened=[](const auto &before,const auto &after) {
    if(before.width!=after.width||before.height!=after.height) throw std::runtime_error("SSAO control dimensions differ");
    unsigned count=0,unchanged=0;
    // Count only opaque white interiors; silhouette/background pixels cannot
    // make a blanket darkening or a broken projection satisfy this control.
    for(unsigned y=2;y+2<before.height;++y) for(unsigned x=2;x+2<before.width;++x) {
      const size_t at=(size_t(y)*before.width+x)*4;if(before.pixels[at]<254) continue;
      bool interior=true;for(auto d:{-2,2}) interior&=before.pixels[(size_t(int(y)+d)*before.width+x)*4]>253&&before.pixels[(size_t(y)*before.width+unsigned(int(x)+d))*4]>253;
      if(!interior) continue;count+=after.pixels[at]<235;unchanged+=after.pixels[at]>=252;
    }
    return std::array<unsigned,2>{count,unchanged};
  };
  for(const auto *ao:{&half,&full}) {const auto counts=darkened(off,*ao);if(counts[0]<100||counts[1]<1000) throw std::runtime_error("SSAO contact/far-surface control failed: "+std::to_string(counts[0])+","+std::to_string(counts[1]));}
  for(const auto *ao:{&half,&full}) {
    double sum=0;unsigned samples=0;
    for(unsigned y=180;y<210;++y) for(unsigned x=260;x<375;++x) {
      const unsigned px=unsigned(ao->width*.5f+(float(x)-320)*ao->height/480.f),py=unsigned(float(y)*ao->height/480.f);
      sum+=ao->pixels[(size_t(py)*ao->width+px)*4];++samples;
    }
    if(sum/samples<250) throw std::runtime_error("planar cube face self-occluded during depth reconstruction");
  }
  if(darkened(ortho,ortho_ao)[0]<100) throw std::runtime_error("orthographic SSAO reconstruction failed");
  if(darkened(flat,flat_ao)[0]>20) throw std::runtime_error("flat isolated plane self-occluded");
  if(off.pixels!=reset.pixels) throw std::runtime_error("SSAO state leaked after disable");
  if(!mobile&&(resized.width!=800||resized.height!=600)) throw std::runtime_error("SSAO resize failed");
  const auto unshadowed=image(10),shadowed=image(11);unsigned interior=0,artifacts=0;
  for(unsigned y=2;y+2<unshadowed.height;++y) for(unsigned x=2;x+2<unshadowed.width;++x) {
    const size_t at=(size_t(y)*unshadowed.width+x)*4;
    if(unshadowed.pixels[at]<100||unshadowed.pixels[(size_t(y-2)*unshadowed.width+x)*4]<100||unshadowed.pixels[(size_t(y+2)*unshadowed.width+x)*4]<100) continue;
    ++interior;artifacts+=std::abs(int(unshadowed.pixels[at])-int(shadowed.pixels[at]))>3;
  }
  if(!interior||artifacts>interior/1000+10) throw std::runtime_error("sloped planar receiver self-shadowed: "+std::to_string(artifacts)+"/"+std::to_string(interior));
  njin::renderer_destroy_target(ctx,extra);
  const auto stats=njin::renderer_resource_stats(ctx);
  // Primitive/white caches + scene/depth view, 3 AO targets, 6 bloom levels, AA.
  if(stats.live_meshes!=2||stats.live_targets!=12||stats.live_textures!=14) throw std::runtime_error("SSAO depth/target cleanup failed: "+std::to_string(stats.live_targets)+","+std::to_string(stats.live_textures));
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin SSAO regression",.width=640,.height=480,.target_fps=60};cfg.audio=false;cfg.resizable=true;cfg.clear_bg_color={0,0,0,1};cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;else if(std::strcmp(argv[i],"--mobile")==0) mobile=true;
    capture=(std::filesystem::path(argv[0]).parent_path()/"captures"/("ssao-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(int repeat=0;repeat<2;++repeat) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="SSAO regression",.setup=setup});njin::run(*ctx);}
    return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
