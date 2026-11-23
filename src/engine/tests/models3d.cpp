#include <njin.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
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
int frame=0,limit=16;
bool portable=false,resize_captured=false,mobile=false,lifecycle=false,lifecycle_captured=false;
bool input_smoke=false,audio_smoke=false,touch_pressed=false,touch_moved=false,touch_released=false;
njin::sound_handle silent_sound;
njin::vec2 baseline_window{},hdr_window{},resized_window{},lifecycle_window{};
int resize_ready_frame=-1;
njin::model_handle asset,procedural;
njin::texture_handle override_texture;
entt::entity model_entity,shape_entity;
std::string capture;
njin::camera3d camera;
void startup(njin::context &ctx) {
#if defined(_WIN32)
  // A command/test runner can supply STARTUPINFO's minimized show command;
  // GLFW honors it. This pixel regression explicitly needs a drawable window.
  // Restrict native window preparation to the test harness, not engine gameplay.
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  resize_captured=false; resize_ready_frame=-1; lifecycle_captured=false;
  touch_pressed=touch_moved=touch_released=false;
  if(audio_smoke) {
    const std::array<float,2205> silence{};
    silent_sound=njin::sound_load_samples(ctx,silence.data(),int(silence.size()),22050);
    if(!silent_sound.id) throw std::runtime_error("audio device/sample upload failed");
    njin::sound_play_loop(ctx,silent_sound);
  }
  // DXT1 8x8: red, green / blue, white blocks, decoded to RGBA8 on the CPU (bimg).
  const auto compressed=(std::filesystem::path(capture).parent_path().parent_path()/"model-test-assets/compressed-albedo.dds").string();
  const auto decoded=njin::renderer_load_image(compressed.c_str());
  const auto texel=[&](unsigned x,unsigned y){const auto *p=&decoded.pixels[(size_t(y)*decoded.width+x)*4];return std::array<int,4>{p[0],p[1],p[2],p[3]};};
  if(decoded.width!=8||decoded.height!=8||texel(1,1)!=std::array<int,4>{255,0,0,255}||texel(6,1)!=std::array<int,4>{0,255,0,255}||
     texel(1,6)!=std::array<int,4>{0,0,255,255}||texel(6,6)!=std::array<int,4>{255,255,255,255})
    throw std::runtime_error("DXT1 image was not decoded to the expected RGBA8 blocks");
  const char *only[]{"panel"};
  asset=njin::model_load(ctx,{.path="model-test-assets/static-model.gltf",.only_nodes=only,.only_count=1});
  if(!asset.id) throw std::runtime_error("static glTF model did not load");
  const int material=njin::model_material_find(ctx,asset,"checker");
  if(material<0) throw std::runtime_error("glTF material names were lost");
  auto m=njin::model_material_get(ctx,asset,material); m.surface.unlit=true;
  njin::model_material_set(ctx,asset,material,m);
  njin::vec3 lo,hi;
  if(!njin::model_bounds(ctx,asset,&lo,&hi)||std::abs(lo.x)>0.001f||std::abs(hi.x-2)>0.001f)
    throw std::runtime_error("glTF node hierarchy/filter bounds mismatch");
  const auto hit=njin::ray3d_model(ctx,{{1,0,5},{0,0,-1}},asset,{});
  if(!hit.hit||std::abs(hit.distance-5)>0.001f) throw std::runtime_error("CPU model ray picking failed");
  const njin::vec3 p[]{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};
  const njin::rgba c[]{{0,0,1,1},{0,0,1,1},{0,0,1,1},{0,0,1,1}};
  const njin::u32 indices[]{0,1,2,0,2,3};
  procedural=njin::model_create(ctx,{.positions=p,.colors=c,.vertex_count=4,.indices=indices,.index_count=6,.material="generated"});
  if(!procedural.id) throw std::runtime_error("procedural model did not create");
  m=njin::model_material_get(ctx,procedural,0); m.surface.unlit=true;
  if(portable) {
    m.double_sided=true; m.surface.unlit=false; m.surface.specular=0; m.surface.emission={0,1,0,1};
    njin::light3d light; light.direction={0,0,-1}; light.ambient={0,0,0,1};
    njin::light3d_set(ctx,light);
    if(njin::light3d_get(ctx).direction.z!=-1) throw std::runtime_error("portable light state mismatch");
    light.fog_density=1;njin::light3d_set(ctx,light);
    if(njin::light3d_get(ctx).fog_density!=1) throw std::runtime_error("portable fog state mismatch");
    auto invalid=light;invalid.fog_density=-1;bool rejected=false;
    try { njin::light3d_set(ctx,invalid); } catch(const std::invalid_argument &) { rejected=true; }
    if(!rejected || njin::light3d_get(ctx).fog_density!=1) throw std::runtime_error("invalid fog mutated lighting state");
    light.fog_density=0;njin::light3d_set(ctx,light);
  }
  njin::model_material_set(ctx,procedural,0,m);
  const unsigned char white[]{255,255,255,255};
  override_texture=njin::texture_from_pixels(ctx,white,1,1);
  auto &registry=njin::world(ctx);
  model_entity=registry.create(); registry.emplace<njin::transform3d>(model_entity,njin::transform3d{.position={1,1,0}});
  registry.emplace<njin::model3d>(model_entity,njin::model3d{.model=asset});
  shape_entity=registry.create(); registry.emplace<njin::transform3d>(shape_entity,njin::transform3d{.position={2,-2,0}});
  auto &shape=registry.emplace<njin::shape3d_render>(shape_entity);
  shape.shape.kind=njin::shape3d_box; shape.material.unlit=true; shape.color={1,0,0,1};
  camera.position={0,0,5}; camera.target={0,0,0}; camera.projection=njin::camera3d_orthographic; camera.ortho_height=8;
  if(njin::model_load(ctx,"njin-missing-static-model.gltf").id) throw std::runtime_error("missing model was accepted");
  if(portable) {
    const auto initial=njin::renderer_resource_stats(ctx);
    const auto pbr=njin::model_load(ctx,{.path="model-test-assets/pbr-model.gltf",.pbr=true});
    if(!pbr.id) throw std::runtime_error("PBR glTF did not load");
    const auto imported=njin::model_material_get(ctx,pbr,njin::model_material_find(ctx,pbr,"checker"));
    if(!imported.surface.pbr || imported.surface.unlit || std::abs(imported.surface.metallic-0.6f)>0.001f ||
       std::abs(imported.surface.roughness-0.8f)>0.001f || std::abs(imported.color.r-0.5371f)>0.001f || std::abs(imported.color.g-0.7354f)>0.001f)
      throw std::runtime_error("PBR glTF material/color import mismatch");
    njin::model_unload(ctx,pbr);
    const auto released=njin::renderer_resource_stats(ctx);
    if(initial.live_meshes!=released.live_meshes||initial.live_textures!=released.live_textures) throw std::runtime_error("PBR import resources leaked");
    const auto before=njin::renderer_resource_stats(ctx);
    const auto with_dds=njin::model_load(ctx,"model-test-assets/compressed-model-image.gltf");
    if(!with_dds.id) throw std::runtime_error("glTF with a DXT1 albedo did not load");
    njin::model_unload(ctx,with_dds);
    const auto unloaded=njin::renderer_resource_stats(ctx);
    if(before.live_textures!=unloaded.live_textures||before.live_meshes!=unloaded.live_meshes) throw std::runtime_error("DXT1 model resources leaked");
    for(const char *path : {"model-test-assets/missing-model-image.gltf"}) {
      bool rejected=false;
      try { (void)njin::model_load(ctx,path); }
      catch(const std::runtime_error &e) { rejected=std::string(e.what()).find("albedo image")!=std::string::npos; }
      const auto after=njin::renderer_resource_stats(ctx);
      if(!rejected||before.live_textures!=after.live_textures||before.live_meshes!=after.live_meshes)
        throw std::runtime_error("failed model load leaked resources or hid an unsupported image");
    }
  }
}
void update(njin::context &ctx) {
  if(input_smoke) {
    const auto size=njin::window_size(ctx);
    if(frame>=20 && size.y>size.x) std::ofstream("input-ready.txt") << int(size.x) << " " << int(size.y) << "\n";
    for(int i=0;i<njin::touch_count(ctx);++i) {
      const auto touch=njin::touch_get(ctx,i);
      if(!std::isfinite(touch.pos.x)||!std::isfinite(touch.pos.y)||touch.pos.x<0||touch.pos.y<0||touch.pos.x>size.x||touch.pos.y>size.y)
        throw std::runtime_error("Android touch is outside logical window coordinates");
      touch_pressed|=touch.pressed; touch_released|=touch.released;
      touch_moved|=std::abs(touch.delta.x)+std::abs(touch.delta.y)>1;
    }
  }
  if(portable && frame==12) {
    auto light=njin::light3d_get(ctx); light.hdr=true; light.tonemap=njin::tonemap3d_aces; njin::light3d_set(ctx,light);
    auto material=njin::model_material_get(ctx,procedural,0); material.surface.emission={0,1,0,4}; material.surface.pbr=true; material.surface.metallic=0; material.surface.roughness=1;
    camera.position={-2,-2,5}; camera.target={-2,-2,0};
    njin::model_material_set(ctx,procedural,0,material);
  }
  if(portable && !mobile && frame==14) njin::window_set_size(ctx,{800,600});
  if(frame==8) {
    const int index=njin::model_material_find(ctx,asset,"checker");
    auto m=njin::model_material_get(ctx,asset,index); m.albedo=override_texture;
    njin::model_material_set(ctx,asset,index,m);
  }
}
void render(njin::context &ctx) {
  njin::begin_3d(ctx,camera);
  njin::material3d_set(ctx,{.unlit=true});
  njin::draw_model(ctx,asset,{.position={-3,1,0}});
  njin::draw_model(ctx,asset,{.position={1,1,0},.scale={-1,1,1}});
  njin::draw_model(ctx,procedural,{.position={-2,-2,1}});
  if(portable) njin::draw_model(ctx,procedural,{.position={-4.5f,-2,0},.rotation={0,180,0},.scale={0.3f,0.5f,2}});
  njin::draw_model(ctx,asset,{.position={-3,-2,-1}}); // far draw cannot replace the near blue mesh
  njin::material3d cube_material; cube_material.unlit=!portable; cube_material.specular=0;
  njin::material3d_set(ctx,cube_material);
  njin::draw_cube3d(ctx,{0,-2,0},{1,1,1},{0,1,1,1});
  njin::material3d_set(ctx,{.unlit=true});
  njin::draw_sphere3d(ctx,{4,-2,0},0.5f,{1,0,1,1});
  if(portable && frame==0) {
    bool rejected=false;
    try { njin::model_unload(ctx,asset); } catch(const std::logic_error &) { rejected=true; }
    if(!rejected||njin::model_material_count(ctx,asset)==0) throw std::runtime_error("model unload during a frame was not atomic");
  }
  if(portable && frame==9) {
    bool rejected=false;
    try { njin::texture_unload(ctx,override_texture); } catch(const std::logic_error &) { rejected=true; }
    if(!rejected||njin::texture_size(ctx,override_texture).x!=1) throw std::runtime_error("queued material texture was released");
  }
  njin::end_3d(ctx);
  njin::draw_rect(ctx,{{2,2},{4,4}},{1,1,1,1});
  njin::texture_draw_ex(ctx,override_texture,{.pos={30,10},.scale={10,10},.tint={0,1,0,1}});
  njin::texture_draw_ex(ctx,override_texture,{.pos={50,10},.scale={10,10},.tint={0,1,0,0.5f}});
  if(portable && frame==4) {
    const auto saved_light=njin::light3d_get(ctx);
    auto saved_material=njin::model_material_get(ctx,procedural,0);
    auto material=saved_material; material.surface.emission={0,0,0,0}; njin::model_material_set(ctx,procedural,0,material);
    auto light=saved_light; light.color={0,0,0,1}; light.ambient={0,0,0,1}; njin::light3d_set(ctx,light);
    auto &registry=njin::world(ctx); registry.get<njin::model3d>(model_entity).visible=false; registry.get<njin::shape3d_render>(shape_entity).visible=false;
    const auto lamp=registry.create(); registry.emplace<njin::transform3d>(lamp,njin::transform3d{.position={2,0,2}});
    auto &spot=registry.emplace<njin::light3d_source>(lamp); spot.kind=njin::light3d_spot; spot.position={1000,1000,1000};
    spot.direction={0,0,-1}; spot.radius=4; spot.intensity=4; spot.cone=30;
    njin::begin_3d(ctx,camera);
    for(float x:{-2.0f,0.0f,2.0f}) njin::draw_model(ctx,procedural,{.position={x,0,0}});
    njin::light3d_source point; point.position={-2,0,2}; point.radius=4; point.intensity=4;
    auto invalid=point; invalid.shadows=true; invalid.kind=njin::light3d_spot; invalid.cone=180;
    bool rejected=false; try { njin::light3d_add(ctx,invalid); } catch(const std::invalid_argument &) { rejected=true; }
    if(!rejected) throw std::runtime_error("invalid shadowed spot cone was accepted");
    njin::light3d_add(ctx,point); // deliberately AFTER all manual draw calls
    njin::end_3d(ctx); njin::screenshot(ctx,(capture+".local.png").c_str());
    registry.destroy(lamp); registry.get<njin::model3d>(model_entity).visible=true; registry.get<njin::shape3d_render>(shape_entity).visible=true;
    njin::light3d_set(ctx,saved_light); njin::model_material_set(ctx,procedural,0,saved_material);
  }
  if(portable && frame>=12) njin::draw_rect(ctx,{{10,10},{10,10}},{1,0,0,1});
  if(portable && frame==13) { hdr_window=njin::window_size(ctx); njin::screenshot(ctx,(capture+".hdr.png").c_str()); }
  if(portable && frame>=15 && !resize_captured) {
    const auto size=njin::window_size(ctx);
    if(mobile || (size.x==800 && size.y==600)) {
      if(resize_ready_frame<0) resize_ready_frame=frame;
      if(frame>resize_ready_frame) { resized_window=size; njin::screenshot(ctx,(capture+(mobile?".hdr-native.png":".hdr-resize.png")).c_str()); resize_captured=true; }
    }
    if(frame>120) throw std::runtime_error("HDR resize acknowledgement timed out");
  }
  if(frame==6) { baseline_window=njin::window_size(ctx); njin::screenshot(ctx,capture.c_str()); }
  if(frame==10) njin::screenshot(ctx,(capture+".override.png").c_str());
  if(mobile && frame==20) std::ofstream("frame-ready.txt") << "READY\n";
  if(mobile && lifecycle && frame==limit-2) { lifecycle_window=njin::window_size(ctx); njin::screenshot(ctx,(capture+".lifecycle.png").c_str()); lifecycle_captured=true; }
  if(++frame>=limit && (!portable || resize_captured)) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  if(frame<limit || (portable&&!resize_captured) || (mobile&&lifecycle&&!lifecycle_captured)) throw std::runtime_error("model test interrupted before its capture frames");
  if(input_smoke && (!touch_pressed||!touch_moved||!touch_released)) throw std::runtime_error("Android touch press/motion/release was not received through Njin input");
  if(audio_smoke) { njin::sound_stop(ctx,silent_sound); njin::sound_unload(ctx,silent_sound); }
  const auto image=njin::renderer_load_image(capture.c_str());
  const auto changed=njin::renderer_load_image((capture+".override.png").c_str());
  const auto check=[](const njin::RenderImageAsset &im,unsigned x,unsigned y,std::array<unsigned,3> want) {
    if(!im.width || !im.height || (!mobile && size_t(im.width)*480!=size_t(im.height)*640))
      throw std::runtime_error("model capture aspect ratio mismatch");
    if(mobile) x=unsigned(int(im.width/2)+(int(x)-320)*int(im.height)/480);
    else x=unsigned(size_t(x)*im.width/640);
    y=unsigned(size_t(y)*im.height/480);
    if(x>=im.width||y>=im.height) throw std::runtime_error("model control is outside drawable viewport");
    const auto i=(size_t(y)*im.width+x)*4;
    for(unsigned c=0;c<3;++c) if(std::abs(int(im.pixels[i+c])-int(want[c]))>1)
      throw std::runtime_error("model capture mismatch at "+std::to_string(x)+","+std::to_string(y)+
        ": got "+std::to_string(im.pixels[i])+","+std::to_string(im.pixels[i+1])+","+std::to_string(im.pixels[i+2]));
  };
  const auto check_ui=[](const njin::RenderImageAsset &im,njin::vec2 logical,unsigned x,unsigned y,std::array<unsigned,3> want) {
    if(logical.x<=0||logical.y<=0) throw std::runtime_error("invalid captured window metrics");
    const auto px=unsigned(x*im.width/logical.x),py=unsigned(y*im.height/logical.y);
    if(px>=im.width||py>=im.height) throw std::runtime_error("UI control is outside capture");
    const auto offset=(size_t(py)*im.width+px)*4;
    for(unsigned c=0;c<3;++c) if(std::abs(int(im.pixels[offset+c])-int(want[c]))>1)
      throw std::runtime_error("UI readback mismatch at "+std::to_string(px)+","+std::to_string(py));
  };
  check_ui(image,baseline_window,35,15,{0,255,0}); check_ui(image,baseline_window,55,15,{0,128,0});
  check_ui(image,baseline_window,3,3,{255,255,255}); // distinguish screen readback from a 3D shading failure
  check(image,170,150,{128,0,0}); check(image,230,150,{0,255,0});
  check(image,170,210,{0,0,255}); check(image,230,210,{0,0,0}); // alpha MASK
  check(image,350,150,{128,0,0}); check(image,290,210,{0,0,0}); // mirrored culling
  check(image,410,150,{128,0,0}); // ECS instance
  check(image,170,330,portable ? std::array<unsigned,3>{0,255,255}:std::array<unsigned,3>{0,0,255}); // depth
  if(portable) check(image,50,360,{0,255,0}); // reversed normal: emission survives without sunlight
  check(image,320,360,{0,255,255}); check(image,440,360,{255,0,0}); check(image,560,360,{255,0,255});
  check(changed,170,150,{128,255,255}); check(changed,230,210,{128,255,255}); // borrowed albedo override
  if(portable) {
    const auto local=njin::renderer_load_image((capture+".local.png").c_str());
    check(local,200,240,{0,0,255}); check(local,320,240,{0,0,62}); check(local,440,240,{0,0,255}); check(local,400,240,{0,0,0});
    const auto hdr=njin::renderer_load_image((capture+".hdr.png").c_str());
    const auto resized=njin::renderer_load_image((capture+(mobile?".hdr-native.png":".hdr-resize.png")).c_str());
    for(const auto *im:{&hdr,&resized}) { check(*im,320,240,{230,250,216}); check(*im,170,240,{231,250,196}); check_ui(*im,im==&hdr?hdr_window:resized_window,15,15,{255,0,0}); }
    if(mobile && lifecycle) {
      const auto last=njin::renderer_load_image((capture+".lifecycle.png").c_str());
      check(last,320,240,{230,250,216}); check_ui(last,lifecycle_window,15,15,{255,0,0});
      check_ui(last,lifecycle_window,35,15,{0,255,0}); check_ui(last,lifecycle_window,55,15,{0,128,0});
    }
    if(!mobile && (resized.width<=hdr.width || resized.height<=hdr.height)) throw std::runtime_error("HDR resize did not increase drawable size");
  }
  njin::world(ctx).destroy(model_entity); njin::world(ctx).destroy(shape_entity);
  njin::model_unload(ctx,asset); njin::model_unload(ctx,procedural);
  if(njin::model_material_count(ctx,asset)!=0||njin::texture_size(ctx,override_texture).x!=1)
    throw std::runtime_error("model unload released an application texture or left a live slot");
  njin::texture_unload(ctx,override_texture);
  if(portable) {
    const auto stats=njin::renderer_resource_stats(ctx);
    if(stats.live_meshes!=2||stats.live_textures!=3||stats.live_targets!=1) throw std::runtime_error("model resources leaked after unload: meshes="+std::to_string(stats.live_meshes)+", textures="+std::to_string(stats.live_textures)+", targets="+std::to_string(stats.live_targets));
  }
}
void setup(njin::context &ctx) {
  njin::ecs_register(ctx,njin::phase_startup,startup);
  njin::ecs_register(ctx,njin::phase_update,update);
  njin::ecs_register(ctx,njin::phase_render,render);
  njin::ecs_register(ctx,njin::phase_shutdown,shutdown);
}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin static model regression",.width=640,.height=480,.target_fps=60};
    cfg.audio=false; cfg.resizable=true; cfg.clear_bg_color={0,0,0,1};
    for(int i=1;i<argc;++i) {
      if(std::strncmp(argv[i],"--renderer=",11)==0) {
        if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");
      } else if(std::strcmp(argv[i],"--mobile")==0) mobile=true;
      else if(std::strcmp(argv[i],"--lifecycle")==0) lifecycle=true;
      else if(std::strcmp(argv[i],"--input-smoke")==0) input_smoke=true;
      else if(std::strcmp(argv[i],"--audio-smoke")==0) { audio_smoke=true; cfg.audio=true; }
      else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
      else if(std::strncmp(argv[i],"--frames=",9)==0) limit=std::max(16,std::stoi(argv[i]+9));
    }
    portable=cfg.graphics_backend!=njin::GraphicsBackend::OpenGL;
    capture=(std::filesystem::path(argv[0]).parent_path()/"captures"/("model-"+std::string(njin::graphics_backend_name(cfg.graphics_backend))+".png")).string();
    std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(int attempt=0;attempt<(lifecycle?1:2);++attempt) {
      frame=0;
      std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);
      njin::mod_register(*ctx,{.name="model regression",.setup=setup}); njin::run(*ctx);
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
