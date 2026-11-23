#include <njin.h>
#include <algorithm>
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
unsigned frame=0;std::string capture;
njin::model_handle model;entt::entity entity,shape_entity;
void require(bool value,const char *message) {if(!value) throw std::runtime_error(message);}
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  const njin::vec3 p[]{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};
  const njin::u32 indices[]{0,1,2,0,2,3};
  model=njin::model_create(ctx,{.positions=p,.vertex_count=4,.indices=indices,.index_count=6});
  auto material=njin::model_material_get(ctx,model,0);material.double_sided=true;material.surface.unlit=true;njin::model_material_set(ctx,model,0,material);
  entity=njin::world(ctx).create();njin::world(ctx).emplace<njin::transform3d>(entity);
  auto &m=njin::world(ctx).emplace<njin::model3d>(entity);m.model=model;m.visible=false;
  shape_entity=njin::world(ctx).create();njin::world(ctx).emplace<njin::transform3d>(shape_entity);
  auto &shape=njin::world(ctx).emplace<njin::shape3d_render>(shape_entity);shape.visible=false;
  shape.shape.kind=njin::shape3d_box;shape.shape.size={2,2,.1f};shape.material.unlit=true;shape.fx.flash={0,0,1,1};
}
void render(njin::context &ctx) {
  const unsigned stage=frame/4;
  auto light=njin::light3d_get(ctx);light.hdr=stage==8;light.tonemap=njin::tonemap3d_linear;
  light.shadows=stage>=12;light.direction={1,-2,1};light.shadow_size=512;light.shadow_cascades=1;light.shadow_range=20;
  njin::light3d_set(ctx,light);
  njin::post3d post;if(stage==9) {post.taa=true;post.outline=1;post.motion_blur=1;}if(stage==11) post.oit=true;njin::post3d_set(ctx,post);
  njin::camera3d camera;camera.position={0,0,5};camera.target={0,0,0};camera.projection=njin::camera3d_orthographic;camera.ortho_height=6;camera.entities=stage==7;
  if(stage>=12) {camera.position={4,3,6};camera.target={0,.5f,0};}
  camera.entities=stage==7||stage==10;
  auto &e=njin::world(ctx).get<njin::model3d>(entity);e.visible=stage==7;e.fx.flash={0,1,0,1};
  njin::world(ctx).get<njin::shape3d_render>(shape_entity).visible=stage==10;
  njin::begin_3d(ctx,camera);
  njin::fx3d invalid;invalid.grain=0;bool rejected=false;
  try {njin::fx3d_set(ctx,invalid);} catch(const std::invalid_argument &) {rejected=true;}
  require(rejected,"zero grain was accepted");
  invalid={};invalid.seed=std::numeric_limits<float>::quiet_NaN();rejected=false;
  try {njin::fx3d_set(ctx,invalid);} catch(const std::invalid_argument &) {rejected=true;}
  require(rejected,"non-finite FX was accepted");
  njin::fx3d fx;
  if(stage==1||stage==8) fx.flash={1,0,0,1};
  if(stage==2||stage==3||stage==4||stage==9) fx.dissolve=stage==2||stage==4?.5f:1.f;
  if(stage==4) {fx.dissolve_from={-1,0,0};fx.dissolve_to={1,0,0};fx.dissolve_sweep=1;}
  if(stage==5) {fx.mask_center={0,0,-1};fx.mask_radius=1;}
  njin::fx3d_set(ctx,fx);
  if(stage<10&&stage!=7) njin::draw_model(ctx,model,{});
  if(stage==11) {fx.flash={1,0,0,1};njin::fx3d_set(ctx,fx);njin::draw_model(ctx,model,{}, {1,1,1,.5f});}
  if(stage>=12) {
    njin::material3d surface;surface.specular=0;njin::material3d_set(ctx,surface);
    njin::fx3d_set(ctx,{});njin::draw_plane3d(ctx,{0,-1,0},{8,8},{1,1,1,1});
    if(stage!=14) {fx={};fx.dissolve=stage==13?1.f:0.f;njin::fx3d_set(ctx,fx);njin::draw_cube3d(ctx,{0,0,0},{1.5f,2,1.5f},{1,1,1,1});}
  }
  // Per-draw snapshot: resetting must not erase queued effects.
  njin::fx3d_set(ctx,{});
  njin::end_3d(ctx);
  if(frame%4==2) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
  if(++frame==60) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  const auto image=[](unsigned i){return njin::renderer_load_image((capture+"."+std::to_string(i)+".png").c_str());};
  const auto base=image(0);
  const auto count=[](const auto &im,unsigned channel) {unsigned n=0;for(size_t p=0;p<im.pixels.size();p+=4) n+=im.pixels[p+channel]>200;return n;};
  const unsigned full=count(base,0);require(full>1000,"baseline mesh missing");
  const auto flash=image(1),partial=image(2),gone=image(3),sweep=image(4),mask=image(5),reset=image(6),ecs=image(7),hdr=image(8),temporal=image(9);
  require(count(flash,0)==full&&count(flash,1)==0,"flash or draw snapshot failed");
  require(count(partial,0)>full/5&&count(partial,0)<full*4/5,"world noise dissolve coverage failed");
  require(count(gone,0)==0&&count(temporal,0)==0,"dissolve endpoint/normal/temporal coverage failed");
  require(std::abs(int(count(sweep,0))-int(full/2))<int(full/20),"directional sweep coverage failed");
  unsigned wrong=0;for(unsigned y=0;y<sweep.height;++y) for(unsigned x=0;x<sweep.width/2;++x) wrong+=sweep.pixels[(size_t(y)*sweep.width+x)*4]>200;
  require(wrong==0,"directional sweep removed the wrong half");
  require(count(mask,0)>full/5&&count(mask,0)<full*4/5,"sphere masking coverage failed");
  require(base.pixels==reset.pixels,"FX leaked across passes");
  require(count(ecs,1)==full&&count(ecs,0)==0,"ECS model FX failed");
  require(count(hdr,0)>=full*9/10&&count(hdr,1)==0,"HDR flash color failed");
  const auto shape=image(10),oit=image(11),caster=image(12),dissolved=image(13),absent=image(14);
  require(count(shape,2)>=full*9/10&&count(shape,0)==0,"ECS shape FX failed");
  unsigned blended=0;for(size_t p=0;p<oit.pixels.size();p+=4) blended+=oit.pixels[p]>100&&oit.pixels[p]<200&&oit.pixels[p+1]==0&&oit.pixels[p+2]==0;
  require(blended>=full*9/10,"OIT flash/alpha composition failed");
  require(dissolved.pixels==absent.pixels,"dissolved caster left color or shadow coverage");
  require(caster.pixels!=absent.pixels,"shadow control has no visible caster");
  njin::world(ctx).clear();njin::model_unload(ctx,model);
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin world FX regression",.width=400,.height=300,.target_fps=120};cfg.audio=false;cfg.clear_bg_color={0,0,0,1};
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) require(njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend),"invalid renderer");else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    capture=(std::filesystem::absolute(argv[0]).parent_path()/"captures"/("world-fx-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(unsigned repeat=0;repeat<2;++repeat) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="world FX",.setup=setup});njin::run(*ctx);}return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
