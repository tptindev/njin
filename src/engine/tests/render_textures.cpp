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
int frame=0;std::string capture;
njin::render_texture_handle a,b;
njin::model_handle plane;
njin::RenderTextureHandle mutable_image;
bool feedback=false,active_read=false,active_unload=false;
void check(const njin::RenderImageAsset &im,unsigned x,unsigned y,std::array<int,3> expected) {
  const size_t at=(size_t(y)*im.width+x)*4;
  for(unsigned c=0;c<3;++c) if(std::abs(int(im.pixels[at+c])-expected[c])>2) throw std::runtime_error("render texture pixel mismatch at "+std::to_string(x)+","+std::to_string(y)+": "+std::to_string(im.pixels[at])+","+std::to_string(im.pixels[at+1])+","+std::to_string(im.pixels[at+2]));
}
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  a=njin::render_texture_load(ctx,64,64);b=njin::render_texture_load(ctx,16,16);
  if(!a.id||!b.id||njin::render_texture_size(ctx,a)!=njin::vec2{64,64}||njin::render_texture_load(ctx,0,0).id) throw std::runtime_error("render texture allocation/size contract failed");
  const njin::vec3 p[]{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}},n[]{{0,0,1},{0,0,1},{0,0,1},{0,0,1}};const njin::u32 indices[]{0,1,2,0,2,3};
  plane=njin::model_create(ctx,{.positions=p,.normals=n,.vertex_count=4,.indices=indices,.index_count=6});
  auto material=njin::model_material_get(ctx,plane,0);material.double_sided=true;material.surface.unlit=true;material.surface.cast_shadows=false;njin::model_material_set(ctx,plane,0,material);
  const unsigned char red[]{255,0,0,255};mutable_image=njin::renderer_create_texture(ctx,{1,1,red});
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  // Content on the back buffer before a synchronous readback must survive it.
  njin::draw_rect(ctx,{{100,0},{16,16}},{1,1,0,1});
  njin::render_texture_begin(ctx,a,{0,0,0,0});
  njin::draw_rect(ctx,{{0,0},{32,32}},{1,0,0,1});
  njin::draw_rect(ctx,{{32,0},{32,32}},{0,1,0,1});
  njin::draw_rect(ctx,{{0,32},{32,32}},{0,0,1,1});
  njin::draw_rect(ctx,{{32,32},{32,32}},{1,1,1,1});
  try {njin::render_texture_draw(ctx,a,{},{1,1,1,1});} catch(const std::logic_error &) {feedback=true;}
  try {njin::render_texture_save(ctx,a,(capture+".invalid.png").c_str());} catch(const std::logic_error &) {active_read=true;}
  try {njin::render_texture_unload(ctx,a);} catch(const std::logic_error &) {active_unload=true;}
  njin::render_texture_begin(ctx,b,{1,0,1,1});njin::draw_rect(ctx,{{0,0},{8,8}},{0,1,1,1});njin::render_texture_end(ctx);
  njin::render_texture_draw(ctx,b,{24,24},{1,1,1,1});njin::render_texture_end(ctx);
  if(frame==0&&!njin::render_texture_save(ctx,a,(capture+".target.png").c_str())) throw std::runtime_error("synchronous target save failed");
  njin::render_texture_draw(ctx,a,{},{1,1,1,1});
  // Resume without clearing must preserve all four quadrants.
  njin::render_texture_begin(ctx,a);njin::draw_rect(ctx,{{48,48},{8,8}},{0,0,0,1});njin::render_texture_end(ctx);
  // Public 3D target overload closes its target automatically and tests depth.
  auto light=njin::light3d_get(ctx);light.hdr=false;light.shadows=false;light.tonemap=njin::tonemap3d_none;njin::light3d_set(ctx,light);njin::post3d_set(ctx,{});
  njin::camera3d camera;camera.position={0,0,5};camera.target={0,0,0};camera.projection=njin::camera3d_orthographic;camera.ortho_height=4;camera.entities=false;
  njin::begin_3d(ctx,camera,b,{0,0,0,1});
  njin::draw_model(ctx,plane,{.position={0,0,.5f}},{0,1,0,1});njin::draw_model(ctx,plane,{}, {1,0,0,1});njin::end_3d(ctx);
  njin::render_texture_draw(ctx,b,{72,24},{1,1,1,1});
  // HDR resolve and both spatial AA paths must return to their explicit parent.
  light.hdr=true;light.tonemap=njin::tonemap3d_linear;njin::light3d_set(ctx,light);
  njin::post3d post;post.aa=frame==1?njin::aa3d_fxaa:frame==2?njin::aa3d_smaa:njin::aa3d_none;njin::post3d_set(ctx,post);
  njin::render_texture_begin(ctx,a); // A compatibility target nested under the HDR target overload.
  njin::begin_3d(ctx,camera,b,{0,0,0,1});
  njin::draw_model(ctx,plane,{.position={0,0,.5f}},{0,1,0,1});njin::draw_model(ctx,plane,{}, {1,0,0,1});njin::end_3d(ctx);
  njin::draw_rect(ctx,{{56,56},{8,8}},{1,0,1,1}); // Must draw into A after the HDR overload closes B.
  njin::render_texture_end(ctx);
  if(!njin::render_texture_save(ctx,b,(capture+".hdr"+std::to_string(frame)+".png").c_str())) throw std::runtime_error("HDR target save failed");
  njin::draw_rect(ctx,{{200,0},{8,8}},{0,1,1,1}); // Must now draw into the back buffer.
  if(frame==0) {
    if(!njin::render_texture_save(ctx,b,(capture+".depth.png").c_str())) throw std::runtime_error("depth target save failed");
    // An image already referenced by a draw can be replaced without changing that draw.
    const njin::RenderVertex left[]{{120,0,0,0,0},{128,0,0,1,0},{128,8,0,1,1},{120,0,0,0,0},{128,8,0,1,1},{120,8,0,0,1}};
    njin::MeshDraw draw;draw.vertices=left;draw.texture=mutable_image;draw.depth_test=false;const auto size=njin::window_size(ctx);draw.transform={2.f/size.x,0,0,0,0,-2.f/size.y,0,0,0,0,1,0,-1,1,0,1};njin::renderer_draw_mesh(ctx,draw);
    const unsigned char green[]{0,255,0,255};njin::renderer_update_texture(ctx,mutable_image,{1,1,green});draw.transform[12]+=16.f/size.x;njin::renderer_draw_mesh(ctx,draw);
    njin::screenshot(ctx,(capture+".png").c_str());
  }
  if(++frame==4) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  const auto saved=njin::renderer_load_image((capture+".target.png").c_str());
  check(saved,8,8,{255,0,0});check(saved,48,8,{0,255,0});check(saved,8,48,{0,0,255});check(saved,48,48,{255,255,255});check(saved,26,26,{0,255,255});check(saved,38,38,{255,0,255});
  const auto depth=njin::renderer_load_image((capture+".depth.png").c_str());check(depth,8,8,{0,255,0});
  const auto screen=njin::renderer_load_image((capture+".png").c_str());check(screen,108,8,{255,255,0});check(screen,8,8,{255,0,0});check(screen,80,32,{0,255,0});check(screen,124,4,{255,0,0});check(screen,132,4,{0,255,0});check(screen,204,4,{0,255,255});
  for(unsigned i=0;i<4;++i) {const auto hdr=njin::renderer_load_image((capture+".hdr"+std::to_string(i)+".png").c_str());check(hdr,8,8,{0,255,0});check(hdr,0,0,{0,0,0});}
  if(!feedback||!active_read||!active_unload) throw std::runtime_error("render texture validation was bypassed");
  if(!njin::render_texture_save(ctx,a,(capture+".outside.png").c_str())) throw std::runtime_error("target save outside a frame failed");
  const auto outside=njin::renderer_load_image((capture+".outside.png").c_str());check(outside,50,50,{0,0,0});check(outside,8,8,{255,0,0});check(outside,60,60,{255,0,255});
  njin::render_texture_unload(ctx,a);njin::render_texture_unload(ctx,a);njin::render_texture_unload(ctx,b);njin::model_unload(ctx,plane);njin::renderer_destroy_texture(ctx,mutable_image);
  const auto stats=njin::renderer_resource_stats(ctx);if(stats.live_targets!=4||stats.live_meshes||stats.live_textures!=8) throw std::runtime_error("render texture resource leak: "+std::to_string(stats.live_targets)+","+std::to_string(stats.live_textures));
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin legacy render-target API regression",.width=320,.height=240,.target_fps=120};cfg.audio=false;cfg.clear_bg_color={0,0,0,1};
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    capture=(std::filesystem::path(argv[0]).parent_path()/"captures"/("render-target-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(unsigned i=0;i<2;++i) {frame=0;feedback=active_read=active_unload=false;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="legacy render-target API",.setup=setup});njin::run(*ctx);}return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
