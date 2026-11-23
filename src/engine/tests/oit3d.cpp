#include <njin.h>
#include <algorithm>
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
#if defined(__ANDROID__)
#include <android/log.h>
#endif
namespace {
int frame=0;bool mobile=false,rejected=false;
std::string capture,assets;
njin::model_handle plane;
std::array<njin::model_handle,3> models;
njin::instance_buffer_handle instances;
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  const njin::vec3 p[]{{-.5f,-.5f,0},{.5f,-.5f,0},{.5f,.5f,0},{-.5f,.5f,0}},n[]{{0,0,1},{0,0,1},{0,0,1},{0,0,1}};
  const njin::u32 indices[]{0,1,2,0,2,3};plane=njin::model_create(ctx,{.positions=p,.normals=n,.vertex_count=4,.indices=indices,.index_count=6});
  auto m=njin::model_material_get(ctx,plane,0);m.double_sided=true;m.surface.unlit=true;m.surface.cast_shadows=false;njin::model_material_set(ctx,plane,0,m);
  unsigned i=0;for(auto mode:{"blend","opaque","mask"}) {const auto path=(std::filesystem::path(assets)/(std::string("oit-")+mode+".gltf")).string();models[i++]=njin::model_load(ctx,{.path=path.c_str()});if(!models[i-1].id) throw std::runtime_error("glTF alpha-mode fixture failed to load");}
  instances=njin::instance_buffer_create(ctx,8);
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  const int stage=frame/6;
  if(!mobile&&frame==72) njin::window_set_size(ctx,{800,600});
  if(!mobile&&frame==78) njin::window_set_size(ctx,{640,480});
  auto light=njin::light3d_get(ctx);light.hdr=true;light.tonemap=stage==16?njin::tonemap3d_aces:njin::tonemap3d_linear;light.shadows=stage==11;light.shadow_size=256;light.shadow_cascades=1;light.shadow_range=30;njin::light3d_set(ctx,light);
  njin::post3d post;post.oit=stage!=6&&stage!=7&&stage!=10&&stage!=14;if(stage==11) {post.ssao=.3f;post.ssr=.5f;post.outline=.8f;post.bloom=.04f;post.aa=njin::aa3d_smaa;}njin::post3d_set(ctx,post);
  njin::camera3d camera;camera.position={0,0,5};camera.target={0,0,0};camera.projection=njin::camera3d_orthographic;camera.ortho_height=8;camera.entities=false;
  const auto before=njin::renderer_resource_stats(ctx);
  auto material=njin::model_material_get(ctx,plane,0);material.surface.emission={1,1,1,stage==16?100.f:0.f};njin::model_material_set(ctx,plane,0,material);
  njin::begin_3d(ctx,camera);
  const auto layer=[&](bool blue,float alpha=-1.f){njin::draw_model(ctx,plane,{.position={0,0,blue?-.1f:.1f},.scale={2,2,1}},blue?njin::rgba{0,0,1,alpha<0?.4f:alpha}:njin::rgba{1,0,0,alpha<0?.5f:alpha});};
  if(stage==2||stage==3||stage==11||stage==14) njin::draw_model(ctx,plane,{.position={0,0,stage==2?.4f:-.4f},.scale={3,3,1}},{0,1,0,1});
  if(stage==5||stage==6||stage==7||stage==15) njin::draw_model(ctx,models[stage==7?2:stage==6||stage==15?1:0],{});
  else if(stage==8||stage==9) {
    float data[]{0,0,.1f,2,1,0,0,.5f, 0,0,-.1f,2,0,0,1,.4f};
    if(stage==9) for(unsigned i=0;i<8;++i) std::swap(data[i],data[i+8]);
    njin::instance_buffer_upload(ctx,instances,data,2);njin::draw_instanced3d(ctx,plane,instances,0,2);
  } else if(stage==10) {try {layer(false);} catch(const std::runtime_error &) {rejected=true;}}
  else if(stage!=14) {if(stage==1) {layer(true);layer(false);} else {layer(false,stage==4?0.f:-1.f);layer(true);}}
  njin::end_3d(ctx);
  if(stage==8||stage==9) {const auto after=njin::renderer_resource_stats(ctx);if(after.instance_uploads-before.instance_uploads!=1||after.instances_submitted-before.instances_submitted!=4) throw std::runtime_error("transparent instance upload/pass reuse failed");}
  njin::draw_rect(ctx,{{4,4},{12,12}},{1,0,0,1});
  if(frame%6==4) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
  if(++frame==102) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  const auto image=[](int i){return njin::renderer_load_image((capture+"."+std::to_string(i)+".png").c_str());};
  const auto pixel=[&](int stage,float x=0){auto im=image(stage);const auto at=(size_t(im.height/2)*im.width+unsigned(im.width*.5f+x*im.height/8))*4;return std::array<int,3>{im.pixels[at],im.pixels[at+1],im.pixels[at+2]};};
  const auto forward=image(0),reverse=image(1);
  for(size_t i=0;i<forward.pixels.size();++i) if(std::abs(int(forward.pixels[i])-int(reverse.pixels[i]))>2) throw std::runtime_error("OIT depends on draw order");
  auto p=pixel(0);if(std::abs(p[0]-167)>3||p[1]||std::abs(p[2]-151)>3) throw std::runtime_error("weighted transparency/revealage failed: "+std::to_string(p[0])+","+std::to_string(p[2]));
  p=pixel(2);if(p!=std::array<int,3>{0,255,0}) throw std::runtime_error("transparency drew through an opaque occluder");
  p=pixel(3);if(std::abs(p[0]-167)>3||std::abs(p[1]-149)>3||std::abs(p[2]-151)>3) throw std::runtime_error("opaque background contribution failed");
  p=pixel(4);if(p[0]||p[1]||std::abs(p[2]-170)>3) throw std::runtime_error("zero-alpha layer affected the result");
  if(std::abs(pixel(5,-.5f)[0]-137)>3||std::abs(pixel(5,.5f)[2]-225)>3) throw std::runtime_error("glTF BLEND texture alpha was not preserved");
  if(pixel(6,-.5f)[0]!=255||pixel(6,.5f)[2]!=255||pixel(7,-.5f)[0]!=0||pixel(7,.5f)[2]!=255) throw std::runtime_error("glTF OPAQUE/MASK alpha semantics regressed");
  for(int stage:{8,9}) {p=pixel(stage);if(std::abs(p[0]-167)>3||std::abs(p[2]-151)>3) throw std::runtime_error("instanced transparency failed");}
  p=pixel(16);if(p[0]<240||p[1]<240||p[2]<240) throw std::runtime_error("HDR transparency overflowed or failed to tone map");
  // Without post3d.oit a transparent draw is alpha blended (as on OpenGL), not refused.
  p=pixel(10);
  if(rejected||p[0]<40||p[1]||p[2]) throw std::runtime_error("transparency without OIT was not alpha blended: "+std::to_string(p[0])+","+std::to_string(p[1])+","+std::to_string(p[2]));
  if(pixel(14)!=std::array<int,3>{0,255,0}||image(6).pixels!=image(15).pixels) throw std::runtime_error("OIT opt-in/disable/opaque control failed");
  if(!mobile) {auto im=image(12);if(im.width!=800||im.height!=600) throw std::runtime_error("OIT resize failed");}
  auto im=image(11);const auto ui=(size_t(8)*im.width+8)*4;if(im.pixels[ui]!=255||im.pixels[ui+1]) throw std::runtime_error("OIT filtered later UI");
  for(auto model:models) njin::model_unload(ctx,model);njin::model_unload(ctx,plane);njin::instance_buffer_destroy(ctx,instances);
  const auto stats=njin::renderer_resource_stats(ctx);
  if(stats.live_meshes||stats.live_targets!=(mobile?20:6)||stats.live_textures!=(mobile?26:11)) throw std::runtime_error("OIT resource cleanup failed: "+std::to_string(stats.live_targets)+","+std::to_string(stats.live_textures));
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin weighted transparency regression",.width=640,.height=480,.target_fps=120};cfg.audio=false;cfg.resizable=true;cfg.clear_bg_color={0,0,0,1};cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strcmp(argv[i],"--mobile")==0) mobile=true;else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    const auto root=std::filesystem::path(argv[0]).parent_path();assets=(root/"model-test-assets").string();capture=(root/"captures"/("oit-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(int repeat=0;repeat<2;++repeat) {frame=0;rejected=false;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="weighted transparency regression",.setup=setup});njin::run(*ctx);}
    return 0;
  } catch(const std::exception &e) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR,"NjinOIT","%s",e.what());
#endif
    std::fprintf(stderr,"%s\n",e.what());return 1;
  }
}
