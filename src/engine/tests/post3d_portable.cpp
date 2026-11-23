#include <njin.h>
#include <array>
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
int frame=0;bool post_mobile=false;
#if defined(NJIN_TEST_SMAA)
constexpr bool smaa_mode=true;
#else
constexpr bool smaa_mode=false;
#endif
std::string capture;
njin::model_handle square,diagonal;
njin::camera3d camera;
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  const njin::vec3 positions[]{{-.5f,-.5f,0},{.5f,-.5f,0},{.5f,.5f,0},{-.5f,.5f,0}};
  const njin::vec3 normals[]{{0,0,1},{0,0,1},{0,0,1},{0,0,1}};
  const njin::u32 indices[]{0,1,2,0,2,3};
  square=njin::model_create(ctx,{.positions=positions,.normals=normals,.vertex_count=4,.indices=indices,.index_count=6});
  const njin::vec3 triangle[]{{-3,-2,0},{3,-2,0},{-1.793f,2.013f,0}};
  diagonal=njin::model_create(ctx,{.positions=triangle,.normals=normals,.vertex_count=3});
  auto m=njin::model_material_get(ctx,square,0);m.double_sided=true;m.surface.specular=0;m.surface.emission={1,1,1,.25f};njin::model_material_set(ctx,square,0,m);
  m=njin::model_material_get(ctx,diagonal,0);m.double_sided=true;m.surface.unlit=true;njin::model_material_set(ctx,diagonal,0,m);
  camera.position={0,0,10};camera.target={0,0,0};camera.projection=njin::camera3d_orthographic;camera.ortho_height=8;camera.entities=false;
  auto invalid=njin::post3d{};invalid.motion_blur=2;bool rejected=false;
  try {njin::post3d_set(ctx,invalid);} catch(const std::invalid_argument &) {rejected=true;}
  if(!rejected||njin::post3d_get(ctx).motion_blur!=0) throw std::runtime_error("invalid post feature changed settings");
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  const int stage=frame/6;
  if(!post_mobile&&frame==36) njin::window_set_size(ctx,{800,600});
  if(!post_mobile&&frame==42) njin::window_set_size(ctx,{640,480});
  auto light=njin::light3d_get(ctx);light.color={0,0,0,1};light.ambient={0,0,0,1};light.shadows=false;light.hdr=stage<8;light.tonemap=njin::tonemap3d_linear;
  njin::light3d_set(ctx,light);
  njin::post3d post;post.bloom=(stage==1||stage==3||stage==6||stage==7)?.5f:0;post.aa=(stage==5||stage==6||stage==7)?(smaa_mode?njin::aa3d_smaa:njin::aa3d_fxaa):njin::aa3d_none;
  njin::post3d_set(ctx,post);
  njin::begin_3d(ctx,camera);
  if(stage<4||stage==6||stage==7) {
    auto m=njin::model_material_get(ctx,square,0);m.surface.emission.a=stage<2?.25f:8;njin::model_material_set(ctx,square,0,m);
    njin::draw_model(ctx,square,{.scale=stage<2?njin::vec3{100,100,1}:njin::vec3{.2f,.2f,1}});
  } else if(stage<6) njin::draw_model(ctx,diagonal,{});
  njin::end_3d(ctx);
  njin::draw_rect(ctx,{{4,4},{12,12}},{1,0,0,1}); // UI after post stays sharp
  if(frame%6==4) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
  if(++frame==54) njin::quit(ctx);
}
njin::RenderImageAsset image(int stage) {return njin::renderer_load_image((capture+"."+std::to_string(stage)+".png").c_str());}
int red(const njin::RenderImageAsset &im,unsigned x,unsigned y) {x=unsigned(im.width*.5f+(float(x)-320)*im.height/480.f);y=unsigned(float(y)*im.height/480.f);return im.pixels[(size_t(y)*im.width+x)*4];}
void shutdown(njin::context &ctx) {
  const auto flat=image(0),bloom=image(1),point=image(2),glow=image(3),raw=image(4),aa=image(5),wide=image(6),normal=image(7),off=image(8);
  if(std::abs(red(flat,320,240)-137)>2||std::abs(red(bloom,320,240)-137)>2) throw std::runtime_error("bloom changed constant radiance energy");
  if(red(point,335,240)!=0||red(glow,335,240)<5||red(glow,320,240)<180) throw std::runtime_error("HDR emissive bloom halo is missing");
  unsigned changed=0,softened=0;
  for(size_t i=0;i<raw.pixels.size();i+=4) {changed+=raw.pixels[i]!=aa.pixels[i];softened+=aa.pixels[i]>8&&aa.pixels[i]<247;}
  if(changed<50||softened<50) throw std::runtime_error("anti-aliasing did not filter the diagonal edge");
  if(!post_mobile&&(wide.width!=800||wide.height!=600||normal.width!=640||normal.height!=480)) throw std::runtime_error("post targets did not follow resize");
  if(red(off,320,240)!=0) throw std::runtime_error("post state leaked after disable");
  for(const auto *im:{&bloom,&glow,&aa,&wide,&normal,&off}) {
    const auto at=(size_t(8)*im->width+8)*4;
    if(im->pixels[at]!=255||im->pixels[at+1]||im->pixels[at+2]) throw std::runtime_error("post-processing modified later UI");
  }
  njin::model_unload(ctx,square);njin::model_unload(ctx,diagonal);
  const auto stats=njin::renderer_resource_stats(ctx);
  if(stats.live_meshes||stats.live_targets!=(smaa_mode?10:8)||stats.live_textures!=(smaa_mode?14:10)) throw std::runtime_error("post target resize leaked resources: "+std::to_string(stats.live_targets)+","+std::to_string(stats.live_textures));
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin portable post regression",.width=640,.height=480,.target_fps=60};cfg.audio=false;cfg.resizable=true;cfg.clear_bg_color={0,0,0,1};cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strcmp(argv[i],"--mobile")==0) post_mobile=true;else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    capture=(std::filesystem::path(argv[0]).parent_path()/"captures"/((smaa_mode?"smaa-":"post-")+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(int repeat=0;repeat<2;++repeat) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="portable post regression",.setup=setup});njin::run(*ctx);}
    return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
