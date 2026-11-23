#include <njin.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace {
unsigned frame=0;bool legacy=false;std::string capture,assets;njin::texture_handle white,packed;njin::atlas_handle atlas;
entt::entity flash,combined,fade,atlas_flash,vanish,reverse,expired,destroyed;
struct Probe {std::string path;njin::rect viewport;njin::vec2 logical;};std::vector<Probe> probes;
void require(bool value,const char *why) {if(!value) throw std::runtime_error(why);}
entt::entity sprite(njin::context &ctx,njin::vec2 at) {auto &r=njin::world(ctx);auto e=r.create();r.emplace<njin::transform>(e,njin::transform{.pos=at});auto &s=r.emplace<njin::sprite>(e);s.texture=white;s.origin={0,0};return e;}
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  njin::time_set_scale(ctx,0);std::array<njin::u8,32*32*4> pixels;pixels.fill(255);white=njin::texture_from_pixels(ctx,pixels.data(),32,32);njin::texture_set_filter(ctx,white,njin::filter_nearest);
  atlas=njin::atlas_create(ctx,{.size=16,.padding=1,.filter=njin::filter_nearest});packed=njin::atlas_load(ctx,atlas,(std::filesystem::path(assets)/"atlas-quad.png").string().c_str());require(packed.id!=0,"missing atlas FX fixture");
  flash=sprite(ctx,{10,10});combined=sprite(ctx,{50,10});fade=sprite(ctx,{90,10});atlas_flash=sprite(ctx,{130,10});
  auto &s=njin::world(ctx).get<njin::sprite>(atlas_flash);s.texture=packed;s.source={{2,2},{2,2}};s.flip_x=true;njin::world(ctx).get<njin::transform>(atlas_flash).scale=16;
  vanish=sprite(ctx,{50,70});reverse=sprite(ctx,{90,70});expired=sprite(ctx,{130,70});destroyed=sprite(ctx,{170,70});
}
void update(njin::context &ctx) {
  auto &r=njin::world(ctx);const float progress=frame==0?0:frame==1?.4f:frame==2?.8f:1;
  njin::sprite_flash(ctx,flash,{1,0,0,1},1);r.get<njin::flash_fx>(flash).fade=false;
  njin::sprite_flash(ctx,fade,{1,1,1,1},1);r.get<njin::sprite>(fade).tint={.2f,.4f,.6f,1};r.get<njin::flash_fx>(fade).time=.5f;
  njin::sprite_flash(ctx,atlas_flash,{0,0,1,1},1);r.get<njin::flash_fx>(atlas_flash).fade=false;
  njin::sprite_dissolve(ctx,combined,1,{0,0,0,0});r.get<njin::dissolve_fx>(combined).time=progress;r.get<njin::dissolve_fx>(combined).edge_width=0;r.get<njin::dissolve_fx>(combined).grain=2;
  njin::sprite_flash(ctx,combined,{1,0,0,1},1);r.get<njin::flash_fx>(combined).fade=false;
  njin::sprite_dissolve(ctx,vanish,1,{0,0,0,0});r.get<njin::dissolve_fx>(vanish).time=progress;r.get<njin::dissolve_fx>(vanish).edge_width=0;
  r.emplace_or_replace<njin::dissolve_fx>(reverse,njin::dissolve_fx{.edge_color={0,0,0,0},.edge_width=0,.duration=1,.time=progress,.reverse=true});
  njin::sprite_flash(ctx,expired,{1,0,0,1},0);
  if(r.valid(destroyed)) r.emplace_or_replace<njin::dissolve_fx>(destroyed,njin::dissolve_fx{.duration=1,.time=progress,.destroy_when_done=true});
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  require(!njin::world(ctx).all_of<njin::flash_fx>(expired),"expired flash component was retained");
  if(frame==3) {require(!njin::world(ctx).valid(destroyed),"finished dissolve did not destroy entity");require(!njin::world(ctx).all_of<njin::dissolve_fx>(reverse),"reverse dissolve component did not expire");}
  if(!legacy) {bool invalid=false;try {njin::draw_dissolve_begin(ctx,std::numeric_limits<float>::quiet_NaN());}catch(const std::invalid_argument &) {invalid=true;}require(invalid,"non-finite dissolve accepted");}
  const float progress=frame==0?0:frame==1?.4f:frame==2?.8f:1;
  njin::draw_dissolve_begin(ctx,progress,{.edge_color={0,0,0,0},.edge_width=0,.grain=2,.seed=3});
  if(!legacy) {bool nested=false;try {njin::draw_dissolve_begin(ctx,0);}catch(const std::logic_error &) {nested=true;}require(nested,"nested dissolve scope accepted");}
  njin::texture_draw_ex(ctx,white,{.pos={10,130},.scale={2,1}});njin::texture_draw_ex(ctx,white,{.pos={90,130},.scale={2,1},.flip_x=true});njin::draw_dissolve_end(ctx);
  njin::texture_draw(ctx,white,{170,130},{1,1,1,1});
}
void post(njin::context &ctx) {const auto file=capture+"."+std::to_string(frame)+".png";probes.push_back({file,njin::window_viewport(ctx),njin::screen_size(ctx)});njin::screenshot(ctx,file.c_str());if(++frame==4) njin::quit(ctx);}
void shutdown(njin::context &ctx) {
  unsigned previous=2049;
  for(unsigned f=0;f<probes.size();++f) {
    const auto &p=probes[f];const auto image=njin::renderer_load_image(p.path.c_str());
    const auto rgb=[&](unsigned x,unsigned y) {const auto px=unsigned(p.viewport.pos.x+(x+.5f)*p.viewport.size.x/p.logical.x),py=unsigned(p.viewport.pos.y+(y+.5f)*p.viewport.size.y/p.logical.y);const auto i=(size_t(py)*image.width+px)*4;return std::array<int,3>{image.pixels[i],image.pixels[i+1],image.pixels[i+2]};};
    const auto check=[&](unsigned x,unsigned y,std::array<int,3> expected) {const auto actual=rgb(x,y);for(unsigned c=0;c<3;++c) if(std::abs(actual[c]-expected[c])>3) throw std::runtime_error("sprite FX color mismatch at "+std::to_string(x)+","+std::to_string(y)+" frame "+std::to_string(f)+": "+std::to_string(actual[0])+","+std::to_string(actual[1])+","+std::to_string(actual[2]));};
    check(25,25,{255,0,0});check(105,25,{153,179,204});check(145,25,{0,0,255});check(145,85,{255,255,255});check(185,145,{255,255,255});
    unsigned visible=0,flipped=0,sprite_visible=0,reversed=0,red=0;
    for(unsigned y=0;y<32;++y) for(unsigned x=0;x<64;++x) {const auto a=rgb(10+x,130+y),b=rgb(90+63-x,130+y);visible+=a[0]>200;flipped+=b[0]>200;require((a[0]>200)==(b[0]>200),"dissolve pattern did not follow flipped UVs");}
    for(unsigned y=0;y<32;++y) for(unsigned x=0;x<32;++x) {sprite_visible+=rgb(50+x,70+y)[0]>200;reversed+=rgb(90+x,70+y)[0]>200;const auto c=rgb(50+x,10+y);red+=c[0]>200&&c[1]<3&&c[2]<3;}
    require(visible==flipped&&visible<=previous,"dissolve coverage was not monotonic");previous=visible;
    if(f==0) {if(!(visible==2048&&sprite_visible==1024&&reversed==0&&red==1024)) throw std::runtime_error("dissolve endpoints at zero failed: "+std::to_string(visible)+","+std::to_string(sprite_visible)+","+std::to_string(reversed)+","+std::to_string(red));}
    else if(f==3) require(visible==0&&sprite_visible==0&&reversed==1024&&red==0,"dissolve endpoints at one failed");
    else if(!(visible>50&&visible<1950&&sprite_visible>0&&sprite_visible<1024&&red>0&&red<1024)) throw std::runtime_error("partial dissolve/flash combination did not render frame "+std::to_string(f)+": "+std::to_string(visible)+","+std::to_string(sprite_visible)+","+std::to_string(reversed)+","+std::to_string(red));
  }
  njin::world(ctx).clear();njin::texture_unload(ctx,white);njin::atlas_destroy(ctx,atlas);
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_update,update);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_post_render,post);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin sprite flash/dissolve regression",.width=320,.height=240,.target_fps=120};cfg.audio=false;cfg.clear_bg_color={0,0,0,1};
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) require(njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend),"invalid renderer");else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    legacy=cfg.graphics_backend==njin::GraphicsBackend::OpenGL;
    const auto root=std::filesystem::absolute(argv[0]).parent_path();assets=(root/"model-test-assets").string();capture=(root/"captures"/("sprite-fx-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(unsigned repeat=0;repeat<2;++repeat) {frame=0;probes.clear();cfg.virtual_size=repeat?njin::vec2{320,240}:njin::vec2{};cfg.render_scale=repeat?2:1;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="portable sprite FX",.setup=setup});njin::run(*ctx);}return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
