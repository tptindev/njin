#include <njin.h>
#include <algorithm>
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
unsigned frame=0;std::string capture;entt::entity lamp,wall;
void require(bool v,const char *m) {if(!v) throw std::runtime_error(m);}
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  lamp=njin::world(ctx).create();njin::world(ctx).emplace<njin::transform>(lamp).pos={80,120};
  auto &light=njin::world(ctx).emplace<njin::light_2d>(lamp);light.enabled=false;light.falloff=njin::falloff_linear;light.radius=300;light.height=70;light.size=4;
  wall=njin::world(ctx).create();njin::world(ctx).emplace<njin::transform>(wall).pos={160,120};
  njin::world(ctx).emplace<njin::light_occluder>(wall,njin::light_occluder_box({8,90}));
  auto invalid=njin::post_fx{};invalid.blur=std::nanf("");bool rejected=false;
  try {njin::post_fx_set(ctx,invalid);} catch(const std::invalid_argument &) {rejected=true;}
  require(rejected&&njin::post_fx_get(ctx).blur==0,"invalid post settings mutated state");
  njin::ui_canvas_set(ctx,{.height=120});
}
void update(njin::context &ctx) {
  const unsigned stage=frame/4;
  njin::post_fx post;
  if(stage==1) post.saturation=0;
  if(stage==2) post.blur=8;
  if(stage==3) {post=njin::post::crt();post.grain=.05f;post.pixelate=3;post.sepia=.3f;}
  if(stage==20) {post.dof=10;post.dof_focus=1;post.dof_range=.1f;post.dof_falloff=1;}
  if(stage==21) {post.bloom=1;post.bloom_threshold=.3f;}
  njin::post_fx_set(ctx,post);
  njin::lighting_desc lighting;lighting.enabled=stage==4||stage==5||stage==22;lighting.ambient={.2f,.2f,.2f,1};njin::lighting_set(ctx,lighting);
  auto &light=njin::world(ctx).get<njin::light_2d>(lamp);light.enabled=stage==5||stage==22;light.cast_shadows=stage!=22;
  if(frame%4==0) {
    njin::particles3d_clear(ctx);
    if(stage>=14&&stage<=18) {
      njin::particle_emitter em;em.life={10,10};em.speed={0,0};em.color_start=em.color_end={1,1,1,1};em.size_start=em.size_end=1.8f;em.shape=njin::particle_square;
      njin::particles3d_desc desc;desc.scale=1;desc.style=njin::particle3d_style(stage-14);
      njin::particles3d_spawn(ctx,em,{0,0,0},1,desc);require(njin::particles3d_count(ctx)==1,"particle simulation store missing");
    }
  }
}
void render(njin::context &ctx) {
  const unsigned stage=frame/4;
  if(stage<6||stage==22) {
    njin::draw_rect(ctx,{{40,40},{240,160}},{1,0,0,1});
    for(unsigned x=40;x<280;x+=20) njin::draw_rect(ctx,{{float(x),80},{10,80}},{0,1,0,1});
  } else {
    njin::light3d light;light.direction={-.4f,-.3f,-1};light.ambient={.1f,.1f,.1f,1};light.tonemap=njin::tonemap3d_linear;
    light.fog_density=stage==7?1.f:0;light.fog_color={0,0,1,1};light.toon_bands=stage==9?3:0;
    light.volume_density=stage==13?.3f:0;light.volume_distance=10;light.volume_steps=16;light.volume_ambient=1;
    njin::light3d_set(ctx,light);
    njin::camera3d camera;camera.position={0,0,5};camera.target={0,0,0};camera.projection=njin::camera3d_orthographic;camera.ortho_height=6;camera.entities=false;
    njin::begin_3d(ctx,camera);
    njin::material3d material;material.unlit=stage==6||stage==7||stage==20||stage==21;material.pbr=stage==10||stage==11||stage==12;
    material.clearcoat=stage==11?1.f:0;material.clearcoat_roughness=.3f;material.rim=stage==12?njin::rgba{1,0,0,1}:njin::rgba{};
    njin::material3d_set(ctx,material);
    if(stage>=8&&stage<=12) njin::draw_sphere3d(ctx,{0,0,0},1,{.4f,.4f,.4f,1});
    else if(stage==6||stage==7||stage==20||stage==21) njin::draw_cube3d(ctx,{0,0,0},{2,2,.1f},{1,1,1,1});
    if(stage==19) {
      const njin::ribbon3d_point points[]{{{-1,-.4f,0},.4f,{1,0,0,1}},{{0,.4f,0},.4f,{1,1,0,1}},{{1,-.4f,0},.4f,{1,0,0,1}}};
      njin::draw_ribbon3d(ctx,points,3);
    }
    njin::end_3d(ctx);
  }
  if(frame%4==2) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
  if(++frame==92) njin::quit(ctx);
}
void overlay(njin::context &ctx) {
  njin::draw_rect(ctx,{{4,4},{12,12}},{1,0,0,1});
  njin::ui_canvas_begin(ctx);njin::clip_begin(ctx,{{148,8},{10,10}});
  njin::draw_rect(ctx,{{150,10},{5,5}},{0,1,0,1});njin::clip_end(ctx);njin::ui_canvas_end(ctx);
}
void shutdown(njin::context &ctx) {
  const auto image=[](unsigned i){return njin::renderer_load_image((capture+"."+std::to_string(i)+".png").c_str());};
  const auto changed=[](const auto &a,const auto &b) {unsigned n=0;for(size_t p=0;p<a.pixels.size();p+=4) n+=std::abs(int(a.pixels[p])-int(b.pixels[p]))>4||std::abs(int(a.pixels[p+1])-int(b.pixels[p+1]))>4||std::abs(int(a.pixels[p+2])-int(b.pixels[p+2]))>4;return n;};
  const auto nonblack=[](const auto &a) {unsigned n=0;for(size_t p=0;p<a.pixels.size();p+=4) n+=a.pixels[p]>10||a.pixels[p+1]>10||a.pixels[p+2]>10;return n;};
  const auto base=image(0),gray=image(1),blur=image(2),crt=image(3),ambient=image(4),lit=image(5);
  require(changed(base,gray)>1000&&changed(base,blur)>100&&changed(base,crt)>1000,"post chain produced no visible change");
  require(changed(base,ambient)>1000&&changed(ambient,lit)>1000,"2D light passes produced no visible change");
  for(unsigned i=0;i<23;++i) {
    const auto a=image(i);const auto viewport=njin::window_viewport(ctx);
    const auto at=[&](float x,float y) {return (size_t(unsigned(viewport.pos.y+y*viewport.size.y/240))*a.width+unsigned(viewport.pos.x+x*viewport.size.x/320))*4;};
    const auto p=at(8,8);
    require(a.pixels[p]>250&&a.pixels[p+1]<3,"world post contaminated UI");
    const auto scale=njin::ui_canvas_scale(ctx);const auto q=at(152*scale,12*scale);
    require(a.pixels[q+1]>250&&a.pixels[q]<3,"post world target broke UI canvas or scissor");
  }
  require(changed(lit,image(22))>100,"2D polygon shadow missing");
  require(changed(image(6),image(7))>1000,"distance fog missing");
  require(changed(image(8),image(9))>50,"toon shading missing");
  require(changed(image(10),image(11))>20&&changed(image(10),image(12))>20,"material extension missing");
  require(nonblack(image(13))>1000,"volumetric scattering missing in empty space");
  for(unsigned i=14;i<=19;++i) require(nonblack(image(i))>200,"particle/ribbon style missing");
  require(changed(image(6),image(20))>100,"DOF missing");
  require(changed(image(6),image(21))>100,"legacy bloom missing");
  njin::particles3d_clear(ctx);require(njin::particles3d_count(ctx)==0,"particle cleanup failed");njin::world(ctx).clear();
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_update,update);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_post_render,overlay);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin renderer batch",.width=320,.height=240,.target_fps=120};cfg.audio=false;cfg.clear_bg_color={0,0,0,1};
#if defined(__ANDROID__)
    cfg.virtual_size={320,240};
#endif
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) require(njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend),"invalid renderer");else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    capture=(std::filesystem::absolute(argv[0]).parent_path()/"captures"/("renderer-batch-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(unsigned i=0;i<2;++i) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="renderer batch",.setup=setup});njin::run(*ctx);}return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
