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
unsigned frame=0,mode=0;bool mobile=false;
std::string capture;
njin::render_texture_handle offscreen;
njin::model_handle plane;
struct Probe {std::string file;njin::rect viewport;njin::vec2 logical;};
std::vector<Probe> probes;
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  njin::window_set_bar_color(ctx,{1,0,1,1});
  offscreen=njin::render_texture_load(ctx,8,8);
  const njin::vec3 p[]{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}},n[]{{0,0,1},{0,0,1},{0,0,1},{0,0,1}};const njin::u32 indices[]{0,1,2,0,2,3};
  plane=njin::model_create(ctx,{.positions=p,.normals=n,.vertex_count=4,.indices=indices,.index_count=6});
  auto material=njin::model_material_get(ctx,plane,0);material.double_sided=true;material.surface.unlit=true;material.surface.cast_shadows=false;njin::model_material_set(ctx,plane,0,material);
}
void update(njin::context &ctx) {
  if(frame==2&&!mobile) njin::window_set_size(ctx,{400,300});
  if(frame==4) njin::window_set_virtual_size(ctx,{80,60},mode<2);
  if(frame==6&&!mobile) njin::window_set_virtual_size(ctx,{},false);
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  const auto logical=frame>=6&&!mobile?njin::window_size(ctx):frame>=4?njin::vec2{80,60}:njin::vec2{100,60};
  if(njin::screen_size(ctx)!=logical) throw std::runtime_error("virtual screen logical size mismatch");
  // HDR must resolve into the virtual image, with the same projection aspect.
  auto light=njin::light3d_get(ctx);light.hdr=true;light.shadows=false;light.tonemap=njin::tonemap3d_linear;njin::light3d_set(ctx,light);
  njin::post3d post;post.aa=frame%2?njin::aa3d_fxaa:njin::aa3d_none;njin::post3d_set(ctx,post);
  njin::camera3d camera;camera.position={0,0,5};camera.target={};camera.projection=njin::camera3d_orthographic;camera.ortho_height=6;camera.entities=false;
  njin::begin_3d(ctx,camera);njin::draw_model(ctx,plane,{}, {0,1,0,1});njin::end_3d(ctx);
  njin::render_texture_begin(ctx,offscreen,{0,0,1,1});njin::render_texture_end(ctx);
  njin::render_texture_draw(ctx,offscreen,{20,2},{1,1,1,1});
  njin::clip_begin(ctx,{{2,2},{4,4}});njin::draw_rect(ctx,{{0,0},{10,10}},{1,0,0,1});njin::clip_end(ctx);
}
void ui(njin::context &ctx) {
  njin::draw_rect(ctx,{{2,20},{8,8}},{1,1,0,1});
  const auto file=capture+"-"+std::to_string(mode)+"-"+std::to_string(frame)+".png";
  probes.push_back({file,njin::window_viewport(ctx),njin::screen_size(ctx)});njin::screenshot(ctx,file.c_str());
  if(++frame==8) njin::quit(ctx);
}
void check(const njin::RenderImageAsset &image,unsigned x,unsigned y,std::array<int,3> expected) {
  const auto i=(size_t(y)*image.width+x)*4;
  for(unsigned c=0;c<3;++c) if(std::abs(int(image.pixels.at(i+c))-expected[c])>3)
    throw std::runtime_error("virtual screen pixel mismatch at "+std::to_string(x)+","+std::to_string(y)+": "+std::to_string(image.pixels[i])+","+std::to_string(image.pixels[i+1])+","+std::to_string(image.pixels[i+2]));
}
void shutdown(njin::context &ctx) {
  for(const auto &probe:probes) {
    const auto image=njin::renderer_load_image(probe.file.c_str());
    const auto pixel=[&](float x,float y,std::array<int,3> color) {check(image,unsigned(probe.viewport.pos.x+x*probe.viewport.size.x/probe.logical.x),unsigned(probe.viewport.pos.y+y*probe.viewport.size.y/probe.logical.y),color);};
    pixel(4,4,{255,0,0});pixel(1,1,{0,0,0});pixel(24,6,{0,0,255});pixel(6,24,{255,255,0});pixel(probe.logical.x/2,probe.logical.y/2,{0,255,0});
    if(probe.viewport.pos.y>1) check(image,0,0,{255,0,255});
  }
  njin::model_unload(ctx,plane);njin::render_texture_unload(ctx,offscreen);
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_update,update);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_post_render,ui);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin portable virtual screen regression",.width=320,.height=240,.target_fps=120};cfg.audio=false;cfg.resizable=true;cfg.clear_bg_color={0,0,0,1};cfg.virtual_size={100,60};
    for(int i=1;i<argc;++i) if(std::strcmp(argv[i],"--mobile")==0) mobile=true;else if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    capture=(std::filesystem::path(argv[0]).parent_path()/"captures"/("virtual-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(mode=0;mode<4;++mode) {frame=0;probes.clear();cfg.integer_scale=mode<2;cfg.render_scale=mode%2?2:1;cfg.smooth_ui=mode%2!=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="portable virtual screen",.setup=setup});njin::run(*ctx);}return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
