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
unsigned frame=0,port=0;bool legacy=false;std::string output;
entt::entity actor;njin::model_handle plane;njin::RenderTargetHandle target;njin::render_texture_handle legacy_target;
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  const njin::vec3 p[]{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};const njin::u32 indices[]{0,1,2,0,2,3};
  plane=njin::model_create(ctx,{.positions=p,.vertex_count=4,.indices=indices,.index_count=6});
  auto material=njin::model_material_get(ctx,plane,0);material.double_sided=true;material.surface.unlit=true;njin::model_material_set(ctx,plane,0,material);
  actor=njin::world(ctx).create();njin::world(ctx).emplace<njin::transform3d>(actor);
  auto &model=njin::world(ctx).emplace<njin::model3d>(actor);model.model=plane;model.tint={0,1,0,1};
  if(legacy) legacy_target=njin::render_texture_load(ctx,32,32);
  else target=njin::renderer_create_hdr_target(ctx,32,32);
  const auto directory=(std::filesystem::path(output)/"recordings").string();
  njin::debug_doc_source(ctx,"renderer",[]{return std::vector<njin::debug_doc_entry>{{"pipeline","Pipeline"}};},[](const std::string&){return std::string("SDL3/bgfx");});
  if(!njin::debug_server_start(ctx,{.port=njin::u16(port),.snapshot_hz=120,.recording_directory=directory})) throw std::runtime_error("inspector server did not listen");
}
void update(njin::context &ctx) {
  njin::debug_watch(ctx,"counter",frame);njin::debug_watch(ctx,"dt",njin::delta(ctx));
  njin::world(ctx).get<njin::transform3d>(actor).position.x=std::sin(njin::elapsed(ctx)*3)*.25f;
  if(std::filesystem::exists(std::filesystem::path(output)/"complete")||frame>600) njin::quit(ctx);
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  auto light=njin::light3d_get(ctx);light.hdr=true;light.shadows=false;light.tonemap=njin::tonemap3d_linear;njin::light3d_set(ctx,light);
  njin::camera3d camera;camera.position={0,0,5};camera.target={};camera.projection=njin::camera3d_orthographic;camera.ortho_height=4;
  njin::begin_3d(ctx,camera);njin::end_3d(ctx);++frame;
}
void shutdown(njin::context &ctx) {njin::debug_server_stop(ctx);njin::world(ctx).destroy(actor);njin::model_unload(ctx,plane);if(target) njin::renderer_destroy_target(ctx,target);if(legacy_target.id) njin::render_texture_unload(ctx,legacy_target);}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_update,update);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin portable inspector protocol regression",.width=160,.height=120,.target_fps=60};cfg.audio=false;cfg.clear_bg_color={0,0,0,1};
    for(int i=1;i<argc;++i) {
      if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");}
      else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
      else if(std::strncmp(argv[i],"--port=",7)==0) port=unsigned(std::stoul(argv[i]+7));
      else if(std::strncmp(argv[i],"--output=",9)==0) output=argv[i]+9;
    }
    if(!port||port>65535||output.empty()) throw std::invalid_argument("inspector regression requires an output directory and local port");
    legacy=cfg.graphics_backend==njin::GraphicsBackend::OpenGL;
    std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="portable inspector",.setup=setup});njin::run(*ctx);return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
