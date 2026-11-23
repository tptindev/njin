#include <njin.h>
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
int frame=0;std::string capture,assets,mutable_path;
njin::atlas_handle atlas;
njin::texture_handle quad,cyan,yellow,magenta,large,reloadable,mipped;
std::string path(const char *name) {return (std::filesystem::path(assets)/(std::string("atlas-")+name+".png")).string();}
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  atlas=njin::atlas_create(ctx,{.size=16,.padding=1,.filter=njin::filter_linear});
  quad=njin::atlas_load(ctx,atlas,path("quad").c_str());cyan=njin::atlas_load(ctx,atlas,path("cyan").c_str());yellow=njin::atlas_load(ctx,atlas,path("yellow").c_str());large=njin::atlas_load(ctx,atlas,path("large").c_str());
  if(!quad.id||!cyan.id||!yellow.id||!large.id||njin::texture_size(ctx,quad)!=njin::vec2{4,4}||njin::texture_size(ctx,large)!=njin::vec2{20,20}||njin::atlas_load(ctx,atlas,"missing-atlas-image.png").id) throw std::runtime_error("atlas load/size/fallback contract failed");
  njin::texture_set_filter(ctx,quad,njin::filter_nearest);
  mipped=njin::texture_load(ctx,path("quad").c_str());njin::texture_set_filter(ctx,mipped,njin::filter_mipmap);
  mutable_path=capture+".mutable.png";std::filesystem::copy_file(path("red"),mutable_path,std::filesystem::copy_options::overwrite_existing);
  reloadable=njin::texture_load(ctx,mutable_path.c_str());njin::hot_reload_enable(ctx,true,60);
  auto &registry=njin::world(ctx);auto e=registry.create();registry.emplace<njin::transform>(e,njin::transform{.pos={100,40},.scale=4});auto &sprite=registry.emplace<njin::sprite>(e);sprite.texture=quad;sprite.origin={0,0};
  e=registry.create();registry.emplace<njin::transform>(e,njin::transform{.pos={120,50}});auto &map=registry.emplace<njin::tilemap>(e);map.tileset=quad;map.tile_size={2,2};njin::tilemap_set(map,0,0,3);njin::tilemap_set(map,1,0,2);
  e=registry.create();registry.emplace<njin::transform>(e);auto &em=registry.emplace<njin::particle_emitter>(e);em.texture=quad;em.size_start=em.size_end=16;em.color_start=em.color_end={1,1,1,1};em.emitting=false;em.destroy_when_done=false;em.particles.push_back({.pos={160,40},.life=1000});
}
void render(njin::context &ctx) {
  njin::texture_draw_ex(ctx,quad,{.pos={0,0},.scale={4,4}});
  njin::texture_draw_ex(ctx,quad,{.pos={24,0},.scale={4,4},.flip_x=true});
  njin::texture_draw_ex(ctx,quad,{.pos={48,0},.source={{2,0},{2,4}},.scale={4,4}});
  njin::texture_draw_ex(ctx,cyan,{.pos={64,0},.scale={4,4}});
  njin::texture_draw(ctx,yellow,{0,32},{1,1,1,1});njin::texture_draw(ctx,large,{24,32},{1,1,1,1});
  njin::draw_triangle_textured(ctx,cyan,{72,32},{88,32},{72,48},{0,0},{1,0},{0,1},{1,1,1,1});
  if(frame==0) {
    // Add to an already sampled page, then sample the newly packed image.
    magenta=njin::atlas_load(ctx,atlas,path("magenta").c_str());if(!magenta.id) throw std::runtime_error("runtime atlas update failed");
    njin::texture_draw_ex(ctx,magenta,{.pos={88,0},.scale={4,4}});
    bool rejected=false;try {njin::atlas_destroy(ctx,atlas);} catch(const std::logic_error &) {rejected=true;}
    if(!rejected||njin::texture_size(ctx,cyan)!=njin::vec2{4,4}) throw std::runtime_error("atlas destroy was not atomic for in-use pages");
    njin::texture_draw(ctx,reloadable,{180,40},{1,1,1,1});
    const auto stamp=std::filesystem::last_write_time(mutable_path);
    std::filesystem::copy_file(path("green"),mutable_path,std::filesystem::copy_options::overwrite_existing);std::filesystem::last_write_time(mutable_path,stamp+std::chrono::seconds(2));
    if(njin::hot_reload_now(ctx)!=1) throw std::runtime_error("portable texture hot reload failed");
    njin::texture_draw(ctx,reloadable,{188,40},{1,1,1,1});
    std::ofstream(mutable_path,std::ios::binary|std::ios::trunc)<<"invalid PNG";std::filesystem::last_write_time(mutable_path,stamp+std::chrono::seconds(4));
    if(njin::hot_reload_now(ctx)!=0) throw std::runtime_error("invalid reload did not retain old texture");
    njin::texture_draw(ctx,reloadable,{196,40},{1,1,1,1});
    njin::texture_draw_ex(ctx,mipped,{.pos={210,40},.scale={.25f,.25f}});
    njin::screenshot(ctx,(capture+".png").c_str());
  }
  if(++frame==4) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  const auto image=njin::renderer_load_image((capture+".png").c_str());
  const auto pixel=[&](unsigned x,unsigned y,std::array<int,3> expected) {const auto at=(size_t(y)*image.width+x)*4;for(unsigned c=0;c<3;++c) if(std::abs(int(image.pixels[at+c])-expected[c])>3) throw std::runtime_error("atlas pixel mismatch at "+std::to_string(x)+","+std::to_string(y)+": "+std::to_string(image.pixels[at])+","+std::to_string(image.pixels[at+1])+","+std::to_string(image.pixels[at+2]));};
  pixel(2,2,{255,0,0});pixel(12,2,{0,255,0});pixel(2,12,{0,0,255});pixel(12,12,{255,255,255});pixel(26,2,{0,255,0});pixel(50,2,{0,255,0});pixel(66,2,{0,255,255});pixel(2,34,{255,255,0});pixel(26,34,{255,128,0});pixel(74,34,{0,255,255});pixel(90,2,{255,0,255});
  pixel(102,42,{255,0,0});pixel(112,42,{0,255,0});pixel(120,50,{255,255,255});pixel(122,50,{0,0,255});pixel(154,34,{255,0,0});pixel(164,34,{0,255,0});pixel(182,42,{255,0,0});pixel(190,42,{0,255,0});pixel(198,42,{0,255,0});pixel(210,40,{188,188,188});
  const auto before=njin::renderer_resource_stats(ctx);njin::texture_unload(ctx,quad);
  if(njin::renderer_resource_stats(ctx).live_textures!=before.live_textures||njin::texture_size(ctx,cyan)!=njin::vec2{4,4}) throw std::runtime_error("packed image owned its atlas page");
  njin::atlas_destroy(ctx,atlas);njin::atlas_destroy(ctx,atlas);
  if(njin::texture_size(ctx,cyan)!=njin::vec2{}||njin::texture_size(ctx,large)!=njin::vec2{20,20}) throw std::runtime_error("atlas invalidation/fallback ownership failed");
  njin::texture_unload(ctx,large);njin::texture_unload(ctx,reloadable);njin::texture_unload(ctx,mipped);
  const auto stats=njin::renderer_resource_stats(ctx);if(stats.live_targets||stats.live_textures) throw std::runtime_error("atlas resource leak: "+std::to_string(stats.live_textures));
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin portable atlas regression",.width=320,.height=240,.target_fps=120};cfg.audio=false;cfg.clear_bg_color={0,0,0,1};
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    const auto root=std::filesystem::path(argv[0]).parent_path();assets=(root/"model-test-assets").string();capture=(root/"captures"/("atlas-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(unsigned i=0;i<2;++i) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="portable atlas",.setup=setup});njin::run(*ctx);}return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
