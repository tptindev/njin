// Optional real-asset regression. The asset is supplied at runtime and is
// neither vendored nor required by the default build/CI tests.
#include <njin.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
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
std::string asset,capture;
njin::model_handle person;
njin::instance_buffer_handle crowd;
njin::camera3d camera;
std::vector<std::string> names;
std::vector<int> selected;bool quick=false,ssr=false;
int frame=0,clip_count=0,idle=-1,moving=-1;
std::string filename(int clip,int sample) {return capture+"."+std::to_string(clip)+"."+std::to_string(sample)+".png";}
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  person=njin::model_load(ctx,{.path=asset.c_str(),.pbr=true});
  if(!person.id) throw std::runtime_error("real animation asset did not load");
  clip_count=njin::model_anim_count(ctx,person);
  if(clip_count<=0||clip_count>1024||njin::model_bone_count(ctx,person)<=0) throw std::runtime_error("asset has no usable animated skeleton");
  names.clear();for(int i=0;i<clip_count;++i) {names.emplace_back(njin::model_anim_name(ctx,person,i));std::printf("clip %d: %s duration %.6f\n",i,names.back().c_str(),njin::model_anim_duration(ctx,person,i));}
  idle=njin::model_anim_find(ctx,person,"Idle_Loop");if(idle<0) idle=0;
  moving=njin::model_anim_find(ctx,person,"Jog_Fwd_Loop");if(moving<0) moving=clip_count-1;
  selected.clear();
  if(quick) {
    for(const char *name:{"A_TPose","Idle_Loop","Jog_Fwd_Loop","Dance_Loop","Death01","Sprint_Loop"}) {
      const int clip=njin::model_anim_find(ctx,person,name);if(clip>=0&&std::find(selected.begin(),selected.end(),clip)==selected.end()) selected.push_back(clip);
    }
    if(std::find(selected.begin(),selected.end(),moving)==selected.end()) selected.push_back(moving);
  } else for(int clip=0;clip<clip_count;++clip) selected.push_back(clip);
  njin::vec3 lo,hi;if(!njin::model_bounds(ctx,person,&lo,&hi)||hi.y-lo.y<=0) throw std::runtime_error("asset has invalid bounds");
  std::printf("asset: %d bones, %d clips, bounds %.3f %.3f %.3f / %.3f %.3f %.3f\n",njin::model_bone_count(ctx,person),clip_count,lo.x,lo.y,lo.z,hi.x,hi.y,hi.z);
  const float height=hi.y-lo.y;
  camera.position={height*2,height*1.8f,height*3.2f};camera.target={0,height*.5f,0};camera.fovy=45;camera.near_plane=.05f;camera.far_plane=100;camera.entities=false;
  crowd=njin::instance_buffer_create(ctx,8);
  const float data[]{-2.2f,0,-2,1,1,1,1,1, 0,0,-2,1,1,1,1,1, 2.2f,0,-2,1,1,1,1,1};njin::instance_buffer_upload(ctx,crowd,data,3);
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  const int clip=selected.at(frame/4),sample=frame%4;
  auto light=njin::light3d_get(ctx);light.direction={-.5f,-1,-.5f};light.color={1,1,1,2};light.ambient={.3f,.3f,.3f,1};light.shadows=true;light.shadow_cascades=2;light.shadow_size=quick?512:2048;light.shadow_range=25;light.hdr=true;light.tonemap=njin::tonemap3d_aces;njin::light3d_set(ctx,light);
  njin::post3d post;post.ssr=ssr?.8f:0.f;post.ssr_sky=0;post.ssao=.7f;post.ssao_radius=.25f;post.ssao_samples=12;post.bloom=.03f;post.aa=njin::aa3d_fxaa;njin::post3d_set(ctx,post);
  njin::model_pose pose;pose.anim=clip;pose.time=njin::model_anim_duration(ctx,person,clip)*float(sample)/3;pose.loop=false;
  auto blend=pose;blend.blend_anim=idle;blend.blend_time=.3f;blend.blend_loop=true;blend.blend=.5f;
  const auto before=njin::renderer_resource_stats(ctx);
  njin::begin_3d(ctx,camera);
  njin::material3d ground;ground.pbr=true;ground.roughness=ssr?.1f:1.f;ground.reflect=ssr?1.f:0.f;njin::material3d_set(ctx,ground);njin::draw_plane3d(ctx,{0,0,0},{12,12},{.45f,.48f,.52f,1});
  njin::draw_model_anim(ctx,person,{.position={-1.2f,0,0}},pose);
  njin::draw_model_anim(ctx,person,{.position={1.2f,0,0}},blend);
  njin::draw_instanced3d(ctx,person,crowd,0,3,pose);
  njin::end_3d(ctx);
  const auto after=njin::renderer_resource_stats(ctx);
  // This particular supplied asset has two material meshes. Each of the three
  // prepared poses must upload only once per mesh, despite shadow/crowd passes.
  if(after.deformed_mesh_uploads-before.deformed_mesh_uploads!=6) throw std::runtime_error("real asset geometry upload reuse failed");
  if(sample==3||(clip==moving&&sample==1)) njin::screenshot(ctx,filename(clip,sample).c_str());
  if(++frame>=int(selected.size())*4) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  for(int clip:selected) {
    const auto image=njin::renderer_load_image(filename(clip,3).c_str());unsigned colored=0;
    for(size_t p=0;p<image.pixels.size();p+=4) {const int r=image.pixels[p],g=image.pixels[p+1],b=image.pixels[p+2];colored+=(r>b*1.2f&&r>g*1.1f&&r>35)||(b>r*1.2f&&b>g*1.2f&&b>35);}
    if(colored<100) throw std::runtime_error("real asset not visible in clip "+names[clip]);
  }
  const auto first=njin::renderer_load_image(filename(moving,1).c_str()),last=njin::renderer_load_image(filename(moving,3).c_str());
  unsigned changed=0;for(size_t p=0;p<first.pixels.size();p+=4) changed+=std::abs(int(first.pixels[p])-int(last.pixels[p]))>8;
  if(changed<100) throw std::runtime_error("real motion clip produced no visible pose change");
  njin::model_unload(ctx,person);njin::instance_buffer_destroy(ctx,crowd);
  const auto stats=njin::renderer_resource_stats(ctx);
  if(stats.live_meshes!=1||stats.live_targets!=(ssr?14:12)||stats.live_textures!=(ssr?17:14)) throw std::runtime_error("real-asset scene leaked model resources");
  std::printf("PASS: %zu/%d clips, cross-fade, crowd instancing, animated shadows, SSAO/HDR/bloom/FXAA, resource cleanup\n",selected.size(),clip_count);
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin supplied animation asset",.width=960,.height=640,.target_fps=60};cfg.audio=false;cfg.clear_bg_color={.035f,.045f,.065f,1};cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strncmp(argv[i],"--model=",8)==0) asset=argv[i]+8;else if(std::strcmp(argv[i],"--quick")==0) quick=true;else if(std::strcmp(argv[i],"--ssr")==0) ssr=true;else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    if(asset.empty()) throw std::invalid_argument("usage: njin_animation_asset_test --model=<GLB path> --renderer=vulkan");
    std::printf("Real-asset SSR: %s\n",ssr?"enabled":"disabled");
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO,"NjinAsset","Real-asset SSR: %s",ssr?"enabled":"disabled");
#endif
    capture=(std::filesystem::path(argv[0]).parent_path()/"captures"/("person-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(int repeat=0;repeat<2;++repeat) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="supplied animation asset regression",.setup=setup});njin::run(*ctx);}
    return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
