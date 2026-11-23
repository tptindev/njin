#include <njin.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
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
unsigned frame=0;std::string capture;
entt::entity lamp,panel,wall;std::vector<njin::texture_handle> textures;
void require(bool b,const char *s) {if(!b) throw std::runtime_error(s);}
njin::texture_handle solid(njin::context &ctx,unsigned w,unsigned h,unsigned char r,unsigned char g,unsigned char b,bool hole=false) {
  std::vector<unsigned char> pixels(w*h*4);
  for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) {const size_t i=(size_t(y)*w+x)*4;pixels[i]=r;pixels[i+1]=g;pixels[i+2]=b;pixels[i+3]=hole&&x>=w/4&&x<w*3/4&&y>=h/3&&y<h*2/3?0:255;}
  auto t=njin::texture_from_pixels(ctx,pixels.data(),w,h);textures.push_back(t);return t;
}
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  textures.clear();auto floor_tex=solid(ctx,160,120,180,180,180),panel_tex=solid(ctx,24,24,180,180,180);
  auto wall_tex=solid(ctx,24,60,255,255,255,true);solid(ctx,24,24,255,128,180);solid(ctx,24,24,220,40,128);solid(ctx,24,24,30,20,255);solid(ctx,24,60,255,255,255);
  auto &r=njin::world(ctx);const auto floor=r.create();r.emplace<njin::transform>(floor);auto &base=r.emplace<njin::sprite>(floor);base.texture=floor_tex;base.origin={0,0};base.layer=-1;
  panel=r.create();r.emplace<njin::transform>(panel).pos={35,25};r.emplace<njin::sprite>(panel).texture=panel_tex;
  wall=r.create();r.emplace<njin::transform>(wall).pos={80,60};r.emplace<njin::sprite>(wall).texture=wall_tex;
  lamp=r.create();r.emplace<njin::transform>(lamp).pos={30,60};auto &l=r.emplace<njin::light_2d>(lamp);l.radius=220;l.height=50;l.size=0;l.falloff=njin::falloff_none;
  njin::lighting_desc d;d.enabled=true;d.ambient={.03f,.03f,.03f,1};d.scale=1;d.occluder_margin=32;d.occluder_lod=0;d.shadow_columns=512;njin::lighting_set(ctx,d);
}
void update(njin::context &ctx) {
  const unsigned stage=frame/3;auto &r=njin::world(ctx);auto &s=r.get<njin::sprite>(panel);s.normal=stage==1?textures[3]:njin::texture_handle{};s.material=stage==2?textures[4]:njin::texture_handle{};s.emissive=stage==3?textures[5]:njin::texture_handle{};s.emissive_power=3;
  auto &l=r.get<njin::light_2d>(lamp);l.intensity=stage==3?0:1;
  l.cast_shadows=stage>=4&&stage!=7&&stage!=10&&stage!=14;
  const bool sun=stage==8||stage>=13;
  l.kind=sun?njin::light_directional:stage==9?njin::light_spot:njin::light_point;
  l.angle=sun?180:0;l.cone=100;l.size=stage==12?12:0;
  r.remove<njin::light_occluder_pixels>(wall);r.remove<njin::light_occluder_sprite>(wall);
  if(stage==4) r.emplace<njin::light_occluder_pixels>(wall).mask=textures[6];
  if(stage==5||(stage>=7&&stage<10)) r.emplace<njin::light_occluder_pixels>(wall);
  if(stage==6) {auto &o=r.emplace<njin::light_occluder_sprite>(wall);o.simplify=0;}
  if(frame==30) {
    for(int i=0;i<80;++i) {
      const auto caster=r.create();
      r.emplace<njin::transform>(caster).pos={65.f+4.f*(i%8),6.f+11.f*(i/8)};
      r.emplace<njin::light_occluder>(caster,njin::light_occluder_box({2,7}));
    }
  }
}
void render(njin::context &ctx) {
  if(frame%3==1) njin::screenshot(ctx,(capture+"."+std::to_string(frame/3)+".png").c_str());
  if(++frame==45) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  const auto im=[](unsigned i){return njin::renderer_load_image((capture+"."+std::to_string(i)+".png").c_str());};
  const auto changed=[](const auto &a,const auto &b){unsigned n=0;for(size_t p=0;p<a.pixels.size();p+=4) n+=std::abs(int(a.pixels[p])-int(b.pixels[p]))>5||std::abs(int(a.pixels[p+1])-int(b.pixels[p+1]))>5||std::abs(int(a.pixels[p+2])-int(b.pixels[p+2]))>5;return n;};
  require(changed(im(0),im(1))>100,"sprite normal G-buffer missing");
  require(changed(im(0),im(2))>100,"sprite MRA G-buffer missing");
  const auto glow=im(3);unsigned blue=0;for(size_t p=0;p<glow.pixels.size();p+=4) blue+=glow.pixels[p+2]>150&&glow.pixels[p+2]>glow.pixels[p]*2;
  require(blue>100,"sprite emissive G-buffer missing");
  require(changed(im(4),im(5))>300,"pixel shadow alpha hole did not transmit light");
  require(changed(im(4),im(7))>500,"pixel shadows did not darken the receiver");
  require(changed(im(6),im(7))>100,"sprite silhouette shadows missing");
  require(changed(im(10),im(11))>300,"point light did not shadow a dense edge set");
  require(changed(im(11),im(12))>150,"soft point shadows missing in edge buckets");
  require(changed(im(13),im(14))>300,"directional strips did not shadow a dense edge set");
  for(auto t:textures) njin::texture_unload(ctx,t);
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_update,update);njin::ecs_register(ctx,njin::phase_post_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin deferred 2D lighting",.width=160,.height=120,.target_fps=120};cfg.audio=false;cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
#if defined(__ANDROID__)
    cfg.virtual_size={160,120};
#endif
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) require(njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend),"invalid renderer");else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    capture=(std::filesystem::absolute(argv[0]).parent_path()/"captures"/("light2d-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(unsigned i=0;i<2;++i) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="deferred 2D lighting",.setup=setup});njin::run(*ctx);}return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
