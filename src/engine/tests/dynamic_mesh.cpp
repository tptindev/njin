#include <njin.h>
#include "../runtime/njin_model.h"
#include "../runtime/njin_ctx_get.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <numeric>
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
unsigned frame=0;std::string capture;
njin::model_handle split,single;
std::vector<njin::vec3> vertices,normals;
bool pending_rejected=false,submitted_rejected=false;
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  vertices.assign(65538,{1000,1000,0});normals.assign(vertices.size(),{0,0,1});
  std::vector<njin::u32> indices(vertices.size());std::iota(indices.begin(),indices.end(),0);
  const njin::vec3 triangle[]{{-.5f,-.5f,0},{.5f,-.5f,0},{0,.5f,0}};
  for(unsigned i=0;i<3;++i) {vertices[i]=triangle[i]+njin::vec3{-2,0,0};vertices[65535+i]=triangle[i]+njin::vec3{2,0,0};}
  auto &store=njin::ctx_ref<njin::model_store>(ctx);
  split=njin::model_store_create_split(store,vertices.data(),normals.data(),unsigned(vertices.size()),indices.data(),unsigned(indices.size()));
  single=njin::model_create(ctx,{.positions=triangle,.normals=normals.data(),.vertex_count=3});
  if(!split.id||!single.id||store->slots.at(split.id-1).portable_meshes.size()!=2) throw std::runtime_error("split mesh did not cross the 16-bit index boundary");
  for(auto handle:{split,single}) {auto material=njin::model_material_get(ctx,handle,0);material.double_sided=true;material.surface.unlit=true;njin::model_material_set(ctx,handle,0,material);}
  auto invalid=vertices;invalid.back().x=std::numeric_limits<float>::infinity();const auto before=njin::renderer_resource_stats(ctx);
  bool rejected=false;try {njin::model_store_update_vertices(store,split,invalid.data(),normals.data(),unsigned(vertices.size()));} catch(const std::invalid_argument &) {rejected=true;}
  if(!rejected||njin::renderer_resource_stats(ctx).live_meshes!=before.live_meshes) throw std::runtime_error("invalid split update was not atomic");
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  auto &store=njin::ctx_ref<njin::model_store>(ctx);
  const float y=frame%2?1.f:0.f;
  const njin::vec3 triangle[]{{-.5f,-.5f+y,0},{.5f,-.5f+y,0},{0,.5f+y,0}};
  for(unsigned i=0;i<3;++i) {vertices[i]=triangle[i]+njin::vec3{-2,0,0};vertices[65535+i]=triangle[i]+njin::vec3{2,0,0};}
  njin::model_store_update_vertices(store,split,vertices.data(),normals.data(),unsigned(vertices.size()));
  njin::model_store_update_vertices(store,single,triangle,normals.data(),3);
  njin::vec3 lo,hi;if(!njin::model_bounds(ctx,single,&lo,&hi)||std::abs(lo.y-(-.5f+y))>1e-5f||std::abs(hi.y-(.5f+y))>1e-5f) throw std::runtime_error("dynamic CPU bounds were not updated");
  auto light=njin::light3d_get(ctx);light.hdr=false;light.shadows=false;light.tonemap=njin::tonemap3d_none;njin::light3d_set(ctx,light);
  njin::camera3d camera;camera.position={0,0,5};camera.target={};camera.projection=njin::camera3d_orthographic;camera.ortho_height=6;camera.entities=false;
  njin::begin_3d(ctx,camera);njin::draw_model(ctx,split,{}, {0,1,0,1});njin::draw_model(ctx,single,{}, {1,0,0,1});
  try {njin::model_store_update_vertices(store,split,vertices.data(),normals.data(),unsigned(vertices.size()));} catch(const std::logic_error &) {pending_rejected=true;}
  njin::end_3d(ctx);
  try {njin::model_store_update_vertices(store,single,triangle,normals.data(),3);} catch(const std::logic_error &) {submitted_rejected=true;}
  njin::screenshot(ctx,(capture+"."+std::to_string(frame)+".png").c_str());if(++frame==4) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  for(unsigned f=0;f<4;++f) {
    const auto image=njin::renderer_load_image((capture+"."+std::to_string(f)+".png").c_str());
    const unsigned y=f%2?80:120;
    for(unsigned x:{80u,160u,240u}) {const unsigned px=unsigned(image.width*.5f+(float(x)-160)*image.height/240.f),py=unsigned(float(y)*image.height/240.f);const auto i=(size_t(py)*image.width+px)*4;if(image.pixels[i]!=(x==160?255:0)||image.pixels[i+1]!=(x==160?0:255)) throw std::runtime_error("dynamic/split mesh pixel mismatch");}
    const auto old=(size_t(unsigned(float(f%2?120:80)*image.height/240.f))*image.width+image.width/2)*4;if(image.pixels[old]||image.pixels[old+1]) throw std::runtime_error("old dynamic vertices remained visible");
  }
  if(!pending_rejected||!submitted_rejected) throw std::runtime_error("dynamic mesh update bypassed pending draw protection");
  njin::model_unload(ctx,split);njin::model_unload(ctx,single);if(njin::renderer_resource_stats(ctx).live_meshes) throw std::runtime_error("dynamic mesh resources leaked");
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin portable dynamic/split mesh regression",.width=320,.height=240,.target_fps=120};cfg.audio=false;cfg.clear_bg_color={0,0,0,1};
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    capture=(std::filesystem::path(argv[0]).parent_path()/"captures"/("dynamic-mesh-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(unsigned i=0;i<2;++i) {frame=0;pending_rejected=submitted_rejected=false;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="portable dynamic mesh",.setup=setup});njin::run(*ctx);}return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
