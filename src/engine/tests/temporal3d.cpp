#include <njin.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
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
int frame=0;bool mobile=false;std::string capture;
njin::model_handle quad,animated;
njin::instance_buffer_handle occluder;
njin::RenderTargetHandle offscreen;
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  offscreen=njin::renderer_create_target(ctx,8,8);
  const njin::vec3 p[]{{-.5f,-.5f,0},{.5f,-.5f,0},{.5f,.5f,0},{-.5f,.5f,0}},n[]{{0,0,1},{0,0,1},{0,0,1},{0,0,1}};
  const njin::u32 indices[]{0,1,2,0,2,3};quad=njin::model_create(ctx,{.positions=p,.normals=n,.vertex_count=4,.indices=indices,.index_count=6});
  auto material=njin::model_material_get(ctx,quad,0);material.double_sided=true;material.surface.unlit=true;material.surface.cast_shadows=false;njin::model_material_set(ctx,quad,0,material);
  const auto path=(std::filesystem::path(capture).parent_path().parent_path()/"model-test-assets"/"animated-model.gltf").string();animated=njin::model_load(ctx,{.path=path.c_str()});
  if(!animated.id) throw std::runtime_error("temporal animation fixture failed to load");
  for(unsigned i=0;i<njin::model_material_count(ctx,animated);++i) {material=njin::model_material_get(ctx,animated,i);material.surface.unlit=true;material.double_sided=true;material.surface.cast_shadows=false;njin::model_material_set(ctx,animated,i,material);}
  occluder=njin::instance_buffer_create(ctx,8);const float data[]{0,0,.5f,2.5f,0,1,0,1};njin::instance_buffer_upload(ctx,occluder,data,1);
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));
  if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  const int stage=frame/20,step=frame%20;
  if(!mobile&&frame==220) njin::window_set_size(ctx,{800,600});
  if(!mobile&&frame==240) njin::window_set_size(ctx,{640,480});
  auto light=njin::light3d_get(ctx);light.hdr=true;light.tonemap=njin::tonemap3d_linear;light.shadows=false;njin::light3d_set(ctx,light);
  njin::post3d post;post.taa=stage==1||stage==7||stage==8||stage==9||stage==10||stage==11;post.taa_sharpen=0;
  post.motion_blur=stage==2||stage==4||stage==6||stage==10||stage==14||stage==16||stage==17?1.f:0.f;post.motion_blur_samples=16;
  if(stage==10) post.aa=njin::aa3d_smaa;njin::post3d_set(ctx,post);
  njin::camera3d camera;camera.position={0,0,5};camera.target={0,0,0};camera.projection=njin::camera3d_orthographic;camera.ortho_height=8;camera.entities=false;
  if(stage==5||stage==6) {camera.position.x=camera.target.x=.1f*step;}
  if(stage==1) {
    auto plain=light;plain.hdr=false;njin::light3d_set(ctx,plain);njin::post3d_set(ctx,{});
    njin::renderer_begin_target(ctx,offscreen);njin::begin_3d(ctx,camera);njin::end_3d(ctx);njin::renderer_end_target(ctx);
    njin::light3d_set(ctx,light);njin::post3d_set(ctx,post);
  } else if(stage==4) {
    bool rejected=false;njin::renderer_begin_target(ctx,offscreen);
    try {njin::begin_3d(ctx,camera);} catch(const std::runtime_error &) {rejected=true;}
    if(!rejected) njin::end_3d(ctx);njin::renderer_end_target(ctx);
    if(!rejected) throw std::runtime_error("nested temporal HDR did not fail explicitly");
  }
  const auto before=njin::renderer_resource_stats(ctx);njin::begin_3d(ctx,camera);
  if(stage>=13&&stage<=16) {
    const njin::model_pose pose{.anim=stage>=15?3:0,.time=.05f*step};njin::draw3d_motion_id(ctx,99);njin::draw_model_anim(ctx,animated,{},pose);
  } else {
    njin::transform3d transform;transform.position.x=(stage==3||stage==4||stage==7||stage==10||stage==17)?-.8f+.1f*step:.6f;transform.rotation.z=17;
    if(stage!=8||step<10) {if(stage==7||stage==10) njin::draw3d_motion_id(ctx,123);njin::draw_model(ctx,quad,transform);}
    if(stage==9) {
      const auto red=[&]{njin::draw3d_motion_id(ctx,1);njin::draw_model(ctx,quad,{.position={-2,.05f*step,0}},{1,0,0,1});};
      const auto blue=[&]{njin::draw3d_motion_id(ctx,2);njin::draw_model(ctx,quad,{.position={2,-.05f*step,0}},{0,0,1,1});};
      if(step&1) {red();blue();} else {blue();red();}
    }
  }
  if(stage==17||stage==18) njin::draw_instanced3d(ctx,quad,occluder,0,1);
  njin::end_3d(ctx);
  if(stage>=13&&stage<=16&&njin::renderer_resource_stats(ctx).deformed_mesh_uploads-before.deformed_mesh_uploads!=1) throw std::runtime_error("velocity pass duplicated the current animated pose upload");
  njin::draw_rect(ctx,{{4,4},{12,12}},{1,0,0,1});
  if(step==14) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
  if(++frame==380) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  const auto image=[&](int stage){return njin::renderer_load_image((capture+"."+std::to_string(stage)+".png").c_str());};
  const auto intermediate=[&](int stage){const auto im=image(stage);size_t n=0;for(size_t i=0;i<im.pixels.size();i+=4) if(im.pixels[i]>4&&im.pixels[i]<250) ++n;return n;};
  if(image(0).pixels!=image(2).pixels||image(0).pixels!=image(12).pixels) throw std::runtime_error("static camera/object was motion blurred");
  if(image(17).pixels!=image(18).pixels) throw std::runtime_error("hidden geometry contaminated the velocity buffer through a camera-only instanced occluder");
  if(intermediate(1)<=intermediate(0)+10) throw std::runtime_error("TAA did not resolve subpixel coverage");
  for(const auto pair:{std::pair{3,4},std::pair{5,6},std::pair{13,14},std::pair{15,16}})
    if(intermediate(pair.second)<=intermediate(pair.first)+10) throw std::runtime_error("motion blur missing for control "+std::to_string(pair.second));
  const auto hidden=image(8);for(unsigned y=40;y<hidden.height;++y) for(unsigned x=0;x<hidden.width;++x) if(hidden.pixels[(size_t(y)*hidden.width+x)*4]>2) throw std::runtime_error("TAA retained a disappeared object");
  const auto im=image(10);const auto ui=(size_t(8)*im.width+8)*4;if(im.pixels[ui]!=255||im.pixels[ui+1]) throw std::runtime_error("temporal effects filtered later UI");
  if(!mobile&&image(11).width!=800) throw std::runtime_error("temporal resize failed");
  njin::model_unload(ctx,quad);njin::model_unload(ctx,animated);
  njin::instance_buffer_destroy(ctx,occluder);njin::renderer_destroy_target(ctx,offscreen);
  // Cached target colors + scene depth + SMAA lookups + distinct model/UI whites.
  const auto stats=njin::renderer_resource_stats(ctx);if(stats.live_targets!=(mobile?10:3)||stats.live_textures!=(mobile?15:8)) throw std::runtime_error("temporal resource cleanup failed: "+std::to_string(stats.live_targets)+","+std::to_string(stats.live_textures));
  if(stats.live_meshes) throw std::runtime_error("temporal models leaked");
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin temporal renderer regression",.width=640,.height=480,.target_fps=120};cfg.audio=false;cfg.resizable=true;cfg.clear_bg_color={0,0,0,1};cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;else if(std::strcmp(argv[i],"--mobile")==0) mobile=true;
    capture=(std::filesystem::path(argv[0]).parent_path()/"captures"/("temporal-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(unsigned i=0;i<2;++i) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="temporal regression",.setup=setup});njin::run(*ctx);}
    return 0;
  } catch(const std::exception &e) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR,"NjinTemporal","%s",e.what());
#endif
    std::fprintf(stderr,"%s\n",e.what());return 1;
  }
}
