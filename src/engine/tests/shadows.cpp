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
#if defined(__ANDROID__)
#include <android/log.h>
#endif
namespace {
int shadow_frame=0,shadow_limit=93;
bool shadow_portable=true,shadow_mobile=false,shadow_lifecycle=false;
bool shadow_gpu_culling=false;
njin::model_handle panel,caster;
njin::instance_buffer_handle shadow_instances;
njin::camera3d shadow_camera;
std::string shadow_capture;
njin::RenderTargetHandle explicit_target;
njin::texture_handle shadow_mask;
void shadow_start(njin::context &ctx) {
  if(shadow_gpu_culling) {njin::render3d_options options;options.occlusion=true;njin::render3d_options_set(ctx,options);}
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  const njin::vec3 positions[]{{-.5f,-.5f,0},{.5f,-.5f,0},{.5f,.5f,0},{-.5f,.5f,0}};
  const njin::vec3 normals[]{{0,0,1},{0,0,1},{0,0,1},{0,0,1}};
  const njin::vec2 uv[]{{0,1},{1,1},{1,0},{0,0}};
  const njin::u32 indices[]{0,1,2,0,2,3};
  panel=njin::model_create(ctx,{.positions=positions,.normals=normals,.texcoords=uv,.vertex_count=4,.indices=indices,.index_count=6});
  caster=njin::model_create(ctx,{.positions=positions,.normals=normals,.texcoords=uv,.vertex_count=4,.indices=indices,.index_count=6});
  if(!panel.id || !caster.id) throw std::runtime_error("shadow panel creation failed");
  auto m=njin::model_material_get(ctx,panel,0); m.double_sided=true;
  m.surface.specular=0; njin::model_material_set(ctx,panel,0,m); njin::model_material_set(ctx,caster,0,m);
  shadow_camera.position={0,0,5}; shadow_camera.target={0,0,0}; shadow_camera.up={0,1,0};
  shadow_camera.projection=njin::camera3d_orthographic; shadow_camera.ortho_height=8; shadow_camera.entities=false;
  explicit_target=njin::renderer_create_target(ctx,512,512);
  const njin::u8 mask[]{255,255,255,0,255,255,255,255};
  shadow_mask=njin::texture_from_pixels(ctx,mask,2,1);
  if(shadow_portable) shadow_instances=njin::instance_buffer_create(ctx,16);
  if(shadow_portable) {
    bool rejected=false;
    try { njin::renderer_resume_target(ctx,explicit_target); } catch(const std::logic_error &) { rejected=true; }
    if(!rejected) throw std::runtime_error("uninitialized target resume was accepted");
    const auto saved=njin::light3d_get(ctx);
    auto invalid=saved; invalid.shadow_cascades=5; rejected=false;
    try { njin::light3d_set(ctx,invalid); } catch(const std::invalid_argument &) { rejected=true; }
    if(!rejected || njin::light3d_get(ctx).shadow_cascades!=saved.shadow_cascades)
      throw std::runtime_error("invalid shadow configuration was not rejected atomically");
  }
}
void shadow_render(njin::context &ctx) {
#if defined(_WIN32)
  // This is a window pixel test: recover if the runner/desktop minimized its
  // window while driver shader compilation was in progress.
  const auto test_window=static_cast<HWND>(njin::window_native_handle(ctx));
  if(IsIconic(test_window)) ShowWindow(test_window,SW_RESTORE);
#endif
  const int step=std::min(shadow_frame/3,29);
  const bool off_camera_caster=step==29&&shadow_gpu_culling;
  const bool cube=step>=14&&step<20;
  const njin::vec3 rotations[]{{0,0,0},{0,180,0},{0,90,0},{0,-90,0},{90,0,0},{-90,0,0}};
  const auto rotation=cube?rotations[step-14]:njin::vec3{};
  const auto rotate=[&](njin::vec3 p) {
    const float x=rotation.x*3.14159265359f/180,y=rotation.y*3.14159265359f/180;
    const njin::vec3 v{p.x,p.y*std::cos(x)-p.z*std::sin(x),p.y*std::sin(x)+p.z*std::cos(x)};
    return njin::vec3{v.x*std::cos(y)+v.z*std::sin(y),v.y,-v.x*std::sin(y)+v.z*std::cos(y)};
  };
  auto camera=shadow_camera; camera.position=rotate(camera.position); camera.up=rotate(camera.up);
  const bool split_test=step>=20&&step<=23;
  if(split_test) { const float distances[]{.2f,.6f,1.5f,5}; camera.position.z=distances[step-20]; }
  if(step==28) { camera.position.x+=0.00001f; camera.target.x+=0.00001f; }
  auto light=njin::light3d_get(ctx);
  light.direction={1,0,-1}; light.color={1,1,1,1}; light.ambient={.1f,.1f,.1f,1};
  light.shadows=true; light.shadow_range=6; light.shadow_size=256; light.source_shadow_size=128;
  light.shadow_softness=step==13?1.5f:0; light.shadow_cascades=(step==6||split_test||step==24||step==25||step==27||step==28)?4:1;
  light.hdr=step==7; light.tonemap=light.hdr?njin::tonemap3d_aces:njin::tonemap3d_none;
  if(step==4 || step==5 || cube) { light.shadows=false; light.color={0,0,0,1}; }
  njin::light3d_set(ctx,light);
  auto m=njin::model_material_get(ctx,panel,0);
  m.surface.cast_shadows=false; m.surface.receive_shadows=step!=2; m.surface.unlit=false;
  njin::model_material_set(ctx,panel,0,m);
  if(step==8 && shadow_portable) {
    njin::renderer_begin_target(ctx,explicit_target,{1,0,1,1});
    njin::draw_rect(ctx,{{20,20},{20,20}},{0,0,1,1});
    const njin::u8 white[]{255,255,255,255};
    const njin::RenderVertex front[]{{.4f,.6f,-1,0,0},{.6f,.6f,-1,0,0},{.6f,.4f,-1,0,0},
      {.4f,.6f,-1,0,0},{.6f,.4f,-1,0,0},{.4f,.4f,-1,0,0}};
    njin::MeshDraw depth{front,{1,1,white}}; depth.tint={0,0,1,1}; njin::renderer_draw_mesh(ctx,depth);
  }
  njin::begin_3d(ctx,camera);
  njin::draw_model(ctx,panel,{.rotation=rotation,.scale={step==8?6.0f:8.0f,step==8?6.0f:8.0f,1}});
  m.surface.cast_shadows=step!=1; m.surface.receive_shadows=true;
  m.albedo=step==10?shadow_mask:njin::texture_handle{}; m.surface.alpha_cutoff=step==10?0.5f:0.0f; m.shadow_only=step==12; m.double_sided=step!=26;
  njin::model_material_set(ctx,caster,0,m);
  if(shadow_portable && (shadow_frame%3==2||off_camera_caster)) {
    // Outside even the resized camera, but rays along (1,0,-1) project it
    // onto the visible receiver at the origin. Camera rejection must not leak.
    const auto p=off_camera_caster?njin::vec3{-8,0,8}:rotate(cube?njin::vec3{-.5f,0,1.5f}:njin::vec3{step==11?0.0f:-1.0f,0,1});
    const float data[]{p.x,p.y,p.z,1, 1,0,0,1, rotation.x,rotation.y,rotation.z,0, step==26?-1.f:1.f,1,1,0};
    njin::instance_buffer_upload(ctx,shadow_instances,data,1);
    if(off_camera_caster) njin::draw_instanced3d(ctx,caster,shadow_instances,njin::instanced3d_desc{.cull=true,.lod=false,.radius=1});
    else njin::draw_instanced3d(ctx,caster,shadow_instances,0,1);
  } else {
  njin::draw_model(ctx,caster,{.position=rotate(cube?njin::vec3{-.5f,0,1.5f}:njin::vec3{step==11?0.0f:-1.0f,0,1}),.rotation=rotation,.scale={step==26?-1.0f:1.0f,1,1}},{1,0,0,1});
  }
  if(step==4 || step==5 || cube || step==24 || step==25) {
    njin::light3d_source source; source.position=rotate(cube?njin::vec3{-1,0,3}:njin::vec3{-2,0,2}); source.direction={1,0,-1};
    source.radius=8; source.intensity=(step==24||step==25)?0.0f:4.0f; source.shadows=true;
    source.kind=(step==5||step==25)?njin::light3d_spot:njin::light3d_point; source.cone=100;
    njin::light3d_add(ctx,source);
    if(step==24 || step==25) for(int i=1;i<4;++i) {
      source.position={-2,float(i)*3,2}; source.intensity=0;
      source.kind=step==25?njin::light3d_spot:njin::light3d_point;
      njin::light3d_add(ctx,source);
    }
  }
  njin::end_3d(ctx);
  if(off_camera_caster&&shadow_frame==shadow_limit-2) {
    const auto camera_count=njin::renderer_occlusion_readback(ctx),shadow_count=njin::renderer_shadow_culling_readback(ctx);
    std::printf("Off-camera caster GPU camera %llu/%llu shadow %llu/%llu\n",(unsigned long long)camera_count[1],(unsigned long long)camera_count[0],(unsigned long long)shadow_count[1],(unsigned long long)shadow_count[0]);
    if(camera_count[0]!=2||camera_count[1]!=1||shadow_count[0]!=1||shadow_count[1]!=1)
      throw std::runtime_error("off-camera caster did not remain independently visible in the light view");
  }
  if(step==8 && shadow_portable) {
    njin::renderer_end_target(ctx);
    const njin::RenderVertex quad[]{{-1,1,0,0,0},{1,1,0,1,0},{1,-1,0,1,1},{-1,1,0,0,0},{1,-1,0,1,1},{-1,-1,0,0,1}};
    njin::MeshDraw draw{quad,{}}; draw.texture=njin::renderer_target_texture(ctx,explicit_target); draw.depth_test=false;
    njin::renderer_draw_mesh(ctx,draw);
  }
  // A UI control verifies the shadow pass restored the caller's target/state.
  njin::draw_rect(ctx,{{4,4},{8,8}},{1,0,0,1});
  static const char *names[]{"sun","no-caster","no-receiver","sun-repeat","point","spot","cascades","hdr","target","resize","mask","moved","shadow-only","pcf","face-nz","face-pz","face-nx","face-px","face-py","face-ny","csm-near","csm-mid1","csm-mid2","csm-far","atlas-points","atlas-spots","mirrored","csm-stable","csm-subtexel","final"};
  if(shadow_frame%3==2 && step<29) njin::screenshot(ctx,(shadow_capture+"."+names[step]+".png").c_str());
  if(shadow_mobile && shadow_frame==87) std::ofstream("frame-ready.txt") << "READY\n";
  if(!shadow_mobile && shadow_frame==27) njin::window_set_size(ctx,{768,576});
  if(shadow_frame==shadow_limit-2) njin::screenshot(ctx,(shadow_capture+".lifecycle.png").c_str());
  if(++shadow_frame>=shadow_limit) njin::quit(ctx);
}
void shadow_stop(njin::context &ctx) {
  const auto probe=[](const njin::RenderImageAsset &im,float x,float y) {
    // Orthographic world coordinates, also correct after portrait/resize.
    const int px=int(im.width/2+x*im.height/8),py=int(im.height/2-y*im.height/8);
    if(px<0||py<0||px>=int(im.width)||py>=int(im.height)) throw std::runtime_error("shadow probe outside image");
    return int(im.pixels[(size_t(py)*im.width+px)*4]);
  };
  for(const char *name:{"sun","sun-repeat","point","spot","cascades","target","resize","shadow-only","pcf","face-nz","face-pz","face-nx","face-px","face-py","face-ny","csm-near","csm-mid1","csm-mid2","csm-far","atlas-points","atlas-spots","mirrored","csm-stable","csm-subtexel","lifecycle"}) {
    const auto im=njin::renderer_load_image((shadow_capture+"."+name+".png").c_str());
    const int shaded=probe(im,0,0),lit=probe(im,1.5f,0);
    if(shaded>50 || lit<100 || lit-shaded<60)
      throw std::runtime_error(std::string("shadow control failed ")+name+": shaded="+std::to_string(shaded)+", lit="+std::to_string(lit));
  }
  for(const char *name:{"no-caster","no-receiver"}) {
    const auto im=njin::renderer_load_image((shadow_capture+"."+name+".png").c_str());
    if(probe(im,0,0)<150) throw std::runtime_error(std::string("shadow flag failed ")+name);
  }
  const auto mask=njin::renderer_load_image((shadow_capture+".mask.png").c_str());
  if(probe(mask,-.25f,0)<150||probe(mask,.25f,0)>50) throw std::runtime_error("alpha MASK shadow did not preserve the cutout");
  const auto moved=njin::renderer_load_image((shadow_capture+".moved.png").c_str());
  if(probe(moved,-.25f,0)<150||probe(moved,1,0)>50) throw std::runtime_error("moving caster did not move its shadow");
  if(shadow_portable) {
    const auto stable=njin::renderer_load_image((shadow_capture+".csm-stable.png").c_str());
    const auto moved=njin::renderer_load_image((shadow_capture+".csm-subtexel.png").c_str());
    if(stable.pixels!=moved.pixels) throw std::runtime_error("CSM changed under sub-texel camera translation");
    const auto soft=njin::renderer_load_image((shadow_capture+".pcf.png").c_str());
    bool filtered=false;
    for(int x=30;x<=70;++x) { const int value=probe(soft,x*.01f,0); filtered|=value>50&&value<170; }
    if(!filtered) throw std::runtime_error("PCF edge had no partially shadowed samples");
  }
  if(shadow_portable) {
    const auto target=njin::renderer_load_image((shadow_capture+".target.png").c_str());
    for(auto uv:{njin::vec2{30.0f/512,30.0f/512},njin::vec2{.75f,.25f}}) {
      const auto offset=(size_t(uv.y*target.height)*target.width+unsigned(uv.x*target.width))*4;
      if(target.pixels[offset]>1||target.pixels[offset+1]>1||target.pixels[offset+2]<254)
        throw std::runtime_error("shadow pass erased the caller target's color/depth contents");
    }
  }
  const auto hdr=njin::renderer_load_image((shadow_capture+".hdr.png").c_str());
  if(probe(hdr,0,0)>=probe(hdr,1.5f,0)-40) throw std::runtime_error("HDR sun shadow did not survive resolve");
  if(shadow_portable) njin::instance_buffer_destroy(ctx,shadow_instances);
  njin::model_unload(ctx,panel); njin::model_unload(ctx,caster); njin::texture_unload(ctx,shadow_mask); njin::renderer_destroy_target(ctx,explicit_target);
  if(shadow_portable) {
    const auto stats=njin::renderer_resource_stats(ctx);
    if(stats.live_meshes!=0||(!shadow_gpu_culling&&(stats.live_targets!=2||stats.live_textures!=4))||
       (shadow_gpu_culling&&(stats.live_targets>6||stats.live_textures!=stats.live_targets+2)))
      throw std::runtime_error("shadow fixture resource leak after releasing scene assets: meshes="+std::to_string(stats.live_meshes)+", targets="+std::to_string(stats.live_targets)+", textures="+std::to_string(stats.live_textures));
  }
}
void shadow_setup(njin::context &ctx) {
  njin::ecs_register(ctx,njin::phase_startup,shadow_start);
  njin::ecs_register(ctx,njin::phase_render,shadow_render);
  njin::ecs_register(ctx,njin::phase_shutdown,shadow_stop);
}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin shadow regression",.width=512,.height=512,.target_fps=60};
    cfg.audio=false; cfg.resizable=true; cfg.graphics_backend=njin::GraphicsBackend::Vulkan; cfg.clear_bg_color={0,0,0,1};
    for(int i=1;i<argc;++i) {
      if(std::strncmp(argv[i],"--renderer=",11)==0) { if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer"); }
      else if(std::strcmp(argv[i],"--mobile")==0) shadow_mobile=true;
      else if(std::strcmp(argv[i],"--lifecycle")==0) shadow_lifecycle=true;
      else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
      else if(std::strcmp(argv[i],"--gpu-culling")==0) shadow_gpu_culling=true;
      else if(std::strncmp(argv[i],"--frames=",9)==0) shadow_limit=std::max(93,std::stoi(argv[i]+9));
    }
    shadow_portable=cfg.graphics_backend!=njin::GraphicsBackend::OpenGL;
    shadow_capture=(std::filesystem::path(argv[0]).parent_path()/"captures"/("shadows-"+std::string(njin::graphics_backend_name(cfg.graphics_backend))+(shadow_gpu_culling?"-gpu":"")+".png")).string();
    std::filesystem::create_directories(std::filesystem::path(shadow_capture).parent_path());
    for(int attempt=0;attempt<(shadow_lifecycle?1:2);++attempt) {
      shadow_frame=0;
      std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);
      njin::mod_register(*ctx,{.name="shadow regression",.setup=shadow_setup}); njin::run(*ctx);
    }
    return 0;
  } catch(const std::exception &e) {
    std::fprintf(stderr,"%s\n",e.what());
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR,"NjinAndroid","%s",e.what());
#endif
    return 1;
  }
}
