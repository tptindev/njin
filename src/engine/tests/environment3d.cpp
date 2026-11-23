#include <njin.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
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
std::string prefix;
njin::texture_handle uniform_env,directional_env,hdr_env;
njin::camera3d camera;
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  const unsigned char gray[]{188,188,188,255,188,188,188,255};
  uniform_env=njin::texture_from_pixels(ctx,gray,2,1);
  std::vector<unsigned char> pixels(64*32*4);
  for(unsigned y=0;y<32;++y) for(unsigned x=0;x<64;++x) {
    const auto at=(y*64+x)*4;const bool blue=x>=42&&x<55;pixels[at]=blue?0:255;pixels[at+2]=blue?255:0;pixels[at+3]=255;
  }
  directional_env=njin::texture_from_pixels(ctx,pixels.data(),64,32);
  const auto path=prefix+".hdr";
  {std::ofstream file(path,std::ios::binary);file<<"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n";
    const unsigned char rgbe[]{128,64,32,131};for(int i=0;i<8;++i) file.write(reinterpret_cast<const char*>(rgbe),4);}
  hdr_env=njin::texture_load(ctx,path.c_str());if(!hdr_env.id) throw std::runtime_error("HDR environment did not load");
  camera.position={0,0,10};camera.target={0,0,0};camera.projection=njin::camera3d_orthographic;camera.ortho_height=6;camera.entities=false;
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  const int stage=frame/6;
  auto light=njin::light3d_get(ctx);light.color={0,0,0,1};light.ambient={0,0,0,1};light.shadows=false;
  light.environment=stage==0?uniform_env:stage<4?directional_env:stage==4?hdr_env:njin::texture_handle{};
  light.environment_visible=true;light.environment_rotation=stage==2?180.f:0.f;
  light.environment_intensity=1;light.tonemap=njin::tonemap3d_linear;light.exposure=stage==4?.125f:1.f;light.hdr=stage==4;
  njin::light3d_set(ctx,light);
  njin::begin_3d(ctx,camera);
  if(stage<4) {
    njin::material3d material;material.pbr=true;material.metallic=stage==0?0:1;material.roughness=stage==3?1:.05f;
    njin::material3d_set(ctx,material);njin::draw_cube3d(ctx,{0,0,0},{2,2,2},{1,1,1,1});
  }
  njin::end_3d(ctx);
  if(frame%6==4) njin::screenshot(ctx,(prefix+"."+std::to_string(stage)+".png").c_str());
  if(++frame==36) njin::quit(ctx);
}
std::array<int,3> pixel(int stage,unsigned x,unsigned y) {
  const auto im=njin::renderer_load_image((prefix+"."+std::to_string(stage)+".png").c_str());x=unsigned(float(x)*im.width/640.f);y=unsigned(float(y)*im.height/480.f);const auto at=(size_t(y)*im.width+x)*4;
  return {im.pixels[at],im.pixels[at+1],im.pixels[at+2]};
}
void shutdown(njin::context &ctx) {
  const auto check=[](std::array<int,3> got,std::array<int,3> expected,int tolerance) {
    for(unsigned i=0;i<3;++i) if(std::abs(got[i]-expected[i])>tolerance) throw std::runtime_error("IBL pixel mismatch: "+std::to_string(got[0])+","+std::to_string(got[1])+","+std::to_string(got[2]));
  };
  check(pixel(0,20,20),{188,188,188},2);
  // Constant environment integrates to the same irradiance regardless of normal.
  // Diffuse + analytic split-sum specular for white dielectric, N.V approximately 1.
  check(pixel(0,320,240),{188,188,188},5);
  const auto blue=pixel(1,320,240),red=pixel(2,320,240),rough=pixel(3,320,240);
  if(blue[2]<240||blue[0]>30||red[0]<240||red[2]>30) throw std::runtime_error("IBL rotation or reflection orientation failed");
  if(rough[0]<40||rough[2]<rough[0]||rough[2]>=blue[2]-20) throw std::runtime_error("GGX roughness did not broaden the environment reflection");
  check(pixel(4,20,20),{188,137,99},3); // radiance (4,2,1), exposure 1/8
  check(pixel(5,20,20),{0,0,0},2); // no sky/IBL state leaked after disable
  njin::texture_unload(ctx,uniform_env);njin::texture_unload(ctx,directional_env);njin::texture_unload(ctx,hdr_env);
  const auto stats=njin::renderer_resource_stats(ctx);
  // One cached primitive, its white texture, and one retained HDR target/color.
  if(stats.live_meshes!=1||stats.live_targets!=1||stats.live_textures!=2) throw std::runtime_error("IBL cache replacement leaked resources");
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin environment regression",.width=640,.height=480,.target_fps=60};cfg.audio=false;cfg.clear_bg_color={0,0,0,1};cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    prefix=(std::filesystem::path(argv[0]).parent_path()/"captures"/("environment-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(prefix).parent_path());
    for(int repeat=0;repeat<2;++repeat) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="IBL regression",.setup=setup});njin::run(*ctx);}
    return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
