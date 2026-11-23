#include <njin.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace {
int frame=0;
std::string capture;
njin::model_handle panel;
njin::render_texture_handle target;
njin::camera3d camera;
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  if(!njin::instancing_available(ctx)) throw std::runtime_error("GPU instancing is unavailable");
  target=njin::render_texture_load(ctx,640,480);
  const njin::vec3 positions[]{{-.5f,-.5f,0},{.5f,-.5f,0},{.5f,.5f,0},{-.5f,.5f,0}};
  const njin::vec3 normals[]{{0,0,1},{0,0,1},{0,0,1},{0,0,1}};
  const njin::u32 indices[]{0,1,2,0,2,3};
  panel=njin::model_create(ctx,{.positions=positions,.normals=normals,.vertex_count=4,.indices=indices,.index_count=6});
  auto material=njin::model_material_get(ctx,panel,0);material.surface.unlit=true;material.double_sided=false;njin::model_material_set(ctx,panel,0,material);
  camera.position={0,0,10};camera.target={0,0,0};camera.projection=njin::camera3d_orthographic;camera.ortho_height=8;camera.entities=false;
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  std::vector<float> data(1002*16);
  for(unsigned i=0;i<1002;++i) {
    auto *p=data.data()+i*16;p[0]=i==1?-2.f:i==2?2.f:i==0?0.f:100.f;p[3]=1;
    p[4]=i==1?1.f:0.f;p[5]=i==2?1.f:0.f;p[6]=i==0?1.f:0.f;p[7]=1;
    if(i==2) {p[10]=90;p[12]=frame<6?-2.f:2.f;p[13]=1;p[14]=1;}
  }
  const auto buffer=njin::instance_buffer_create(ctx,16);njin::instance_buffer_upload(ctx,buffer,data.data(),1002);
  // Fixed-size controls also work on Android's fullscreen drawable.
  njin::begin_3d(ctx,camera,target,{0,0,0,1});
  const auto before=njin::renderer_resource_stats(ctx);
  njin::draw_instanced3d(ctx,panel,buffer,1,1001);
  // Portable submission owns a snapshot; releasing the public CPU buffer cannot
  // invalidate queued forward/shadow commands or alias another upload.
  njin::instance_buffer_destroy(ctx,buffer);
  njin::end_3d(ctx);
  const auto after=njin::renderer_resource_stats(ctx);const unsigned groups=frame<6?2:1;
  if(after.instanced_draws-before.instanced_draws!=groups||after.instance_uploads-before.instance_uploads!=groups||after.instances_submitted-before.instances_submitted!=1001)
    throw std::runtime_error("N instances were not submitted in one GPU draw per orientation group");
  if(frame==4||frame==10) if(!njin::render_texture_save(ctx,target,(capture+"."+std::to_string(frame)+".png").c_str())) throw std::runtime_error("instancing capture failed");
  njin::render_texture_draw(ctx,target,{},{1,1,1,1});
  if(++frame>=14) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  for(int stage:{4,10}) {
    const auto image=njin::renderer_load_image((capture+"."+std::to_string(stage)+".png").c_str());
    for(auto sample:{std::array<unsigned,4>{200,240,255,0},std::array<unsigned,4>{440,240,0,255},std::array<unsigned,4>{320,240,0,0}}) {
      const auto at=(size_t(sample[1])*image.width+sample[0])*4;
      if(image.pixels[at]!=sample[2]||image.pixels[at+1]!=sample[3]||image.pixels[at+2]) throw std::runtime_error("instanced range/rotation/color/mirrored-culling pixel mismatch");
    }
  }
  njin::render_texture_unload(ctx,target);
  njin::model_unload(ctx,panel);const auto stats=njin::renderer_resource_stats(ctx);
  if(stats.live_meshes||stats.live_targets||stats.live_textures!=1) throw std::runtime_error("instancing resources leaked");
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin GPU instancing regression",.width=640,.height=480,.target_fps=60};cfg.audio=false;cfg.clear_bg_color={0,0,0,1};cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    capture=(std::filesystem::path(argv[0]).parent_path()/"captures"/("instancing-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(int repeat=0;repeat<2;++repeat) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="GPU instancing regression",.setup=setup});njin::run(*ctx);}
    return 0;
  } catch(const std::exception &e) {if(!capture.empty()) std::ofstream(capture+".error.txt")<<e.what()<<'\n';std::fprintf(stderr,"%s\n",e.what());return 1;}
}
