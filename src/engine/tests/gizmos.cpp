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
njin::model_handle plane;entt::entity collider;
struct Probe {std::string file;njin::rect viewport;njin::vec2 logical;};
std::vector<Probe> probes;
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  const njin::vec3 p[]{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};const njin::u32 indices[]{0,1,2,0,2,3};
  plane=njin::model_create(ctx,{.positions=p,.vertex_count=4,.indices=indices,.index_count=6});
  auto material=njin::model_material_get(ctx,plane,0);material.double_sided=true;material.surface.unlit=true;njin::model_material_set(ctx,plane,0,material);
  collider=njin::world(ctx).create();njin::world(ctx).emplace<njin::transform>(collider).pos={60,50};njin::world(ctx).emplace<njin::collider>(collider).size={16,16};njin::collision_set_debug(ctx,true);
}
void update(njin::context &ctx) {
  njin::gizmos_set_visible(ctx,frame!=2);
  if(frame==0) {
    // Persistent controls must survive a cold software-driver shader compilation.
    njin::gizmo_rect(ctx,{{10,10},{20,20}},{1,1,0,1},1000);
    njin::gizmo_arrow(ctx,{80,20},{100,20},{1,0,1,1},1000);njin::gizmo_circle(ctx,{40,20},5,{0,1,1,1},1000);
    njin::gizmo_text(ctx,{10,70},"GPU",{1,1,1,1},1000);
    njin::gizmo_line3d(ctx,{-1,0,-1},{1,0,-1},{1,0,0,1},1000);
    njin::gizmo_line3d(ctx,{-1.5f,-1,10},{-1.5f,1,0},{0,0,1,1},1000); // Crosses the camera's near plane.
    njin::gizmo_line3d(ctx,{-1,-.5f,10},{1,-.5f,10},{0,1,0,1},1000); // Fully behind the camera.
    njin::gizmo_point3d(ctx,{2,1,0},{1,1,1,1},1000);
    njin::gizmo_box3d(ctx,{-2,1,0},{.25f,.25f,.25f},{0,1,1,1},1000);
    njin::gizmo_sphere3d(ctx,{2,-1,0},.2f,{1,0,1,1},1000);
    njin::gizmo_arrow3d(ctx,{-2,-1,0},{-1.5f,-1,0},{1,1,0,1},1000);
    njin::gizmo_axes3d(ctx,{0,2,0},.25f,1000);
    njin::gizmo_line(ctx,{10,40},{30,40},{1,1,1,1}); // Must expire next frame.
  }
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  auto light=njin::light3d_get(ctx);light.hdr=frame%2!=0;light.shadows=false;light.tonemap=njin::tonemap3d_linear;njin::light3d_set(ctx,light);
  njin::camera3d camera;camera.position={0,0,5};camera.target={};camera.projection=njin::camera3d_orthographic;camera.ortho_height=6;camera.entities=false;
  njin::begin_3d(ctx,camera);njin::draw_model(ctx,plane,{}, {.25f,.25f,.25f,1});njin::end_3d(ctx);
}
void capture_frame(njin::context &ctx) {
  const auto file=capture+"."+std::to_string(frame)+".png";probes.push_back({file,njin::window_viewport(ctx),njin::screen_size(ctx)});njin::screenshot(ctx,file.c_str());if(++frame==4) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  for(unsigned f=0;f<probes.size();++f) {
    const auto &probe=probes[f];const auto image=njin::renderer_load_image(probe.file.c_str());
    const auto find=[&](float x,float y,unsigned channel,bool world) {
      if(world) {x=probe.logical.x*.5f+(x-160)*probe.logical.y/240;y=y*probe.logical.y/240;}
      const int px=int(probe.viewport.pos.x+x*probe.viewport.size.x/probe.logical.x),py=int(probe.viewport.pos.y+y*probe.viewport.size.y/probe.logical.y);
      for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) {
        const int xx=px+dx,yy=py+dy;if(xx<0||yy<0||xx>=int(image.width)||yy>=int(image.height)) continue;
        const auto i=(size_t(yy)*image.width+xx)*4;
        if(channel==3) {if(image.pixels[i]>180&&image.pixels[i+1]>180&&image.pixels[i+2]>180) return true;}
        else if(image.pixels[i+channel]>150&&image.pixels[i+(channel+1)%3]<100&&image.pixels[i+(channel+2)%3]<100) return true;
      }
      return false;
    };
    if(find(180,120,0,true)!=(f!=2)||find(100,100,2,true)!=(f!=2)) throw std::runtime_error("3D gizmo depth/near-plane/visibility control failed");
    if(find(160,140,1,true)) throw std::runtime_error("gizmo behind the camera was not clipped");
    if(find(20,40,3,false)!=(f==0)) throw std::runtime_error("zero-duration gizmo did not expire");
    if(!find(60,42,1,false)) throw std::runtime_error("portable collider debug outline missing");
  }
  njin::world(ctx).destroy(collider);njin::model_unload(ctx,plane);
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_update,update);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_post_render,capture_frame);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin portable gizmo/debug regression",.width=320,.height=240,.target_fps=120};cfg.audio=false;cfg.clear_bg_color={0,0,0,1};
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    const auto root=std::filesystem::path(argv[0]).parent_path();capture=(root/"captures"/("gizmos-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(unsigned repeat=0;repeat<2;++repeat) {frame=0;probes.clear();cfg.virtual_size=repeat?njin::vec2{160,120}:njin::vec2{};cfg.smooth_ui=repeat!=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="portable gizmo/debug",.setup=setup});njin::run(*ctx);}return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
