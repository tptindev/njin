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
namespace {
bool portable=true;
int frame=0;
njin::model_handle mapped;
njin::texture_handle flat_override;
std::array<njin::RenderTextureHandle,5> textures;
std::string capture;
njin::camera3d camera;
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  const unsigned char white[]{255,255,255,255},normal[]{255,128,128,255,128,128,255,255};
  const unsigned char mr[]{0,255,0,255},ao[]{128,0,0,255},emission[]{0,0,255,255},flat[]{128,128,255,255};
  textures[0]=njin::renderer_create_texture(ctx,{1,1,white});
  textures[1]=njin::renderer_create_texture(ctx,{2,1,normal});
  textures[2]=njin::renderer_create_texture(ctx,{1,1,mr});
  textures[3]=njin::renderer_create_texture(ctx,{1,1,ao});
  textures[4]=njin::renderer_create_texture(ctx,{1,1,emission});
  if(portable) {
    const auto dir=std::filesystem::path(capture).parent_path().parent_path()/"model-test-assets";
    const auto path=(dir/"pbr-maps.gltf").string();
    mapped=njin::model_load(ctx,{.path=path.c_str(),.pbr=true});
    if(!mapped.id) throw std::runtime_error("mapped glTF did not load");
    const auto before=njin::renderer_resource_stats(ctx);
    bool rejected=false; const auto missing=(dir/"pbr-maps-missing-uv.gltf").string();
    try { (void)njin::model_load(ctx,{.path=missing.c_str(),.pbr=true}); }
    catch(const std::runtime_error &e) { rejected=std::string(e.what()).find("UV set is missing")!=std::string::npos; }
    const auto after=njin::renderer_resource_stats(ctx);
    if(!rejected||before.live_meshes!=after.live_meshes||before.live_textures!=after.live_textures)
      throw std::runtime_error("missing map UV did not roll back all imported resources");
    flat_override=njin::texture_from_pixels(ctx,flat,1,1);
    camera.position={0,0,100}; camera.target={0,0,0}; camera.projection=njin::camera3d_orthographic;
    camera.ortho_height=8; camera.far_plane=1000; camera.entities=false;
  }
}
void low_quad(njin::context &ctx,float x,float y,njin::RenderMaterialMaps maps,njin::RenderLighting light,njin::RenderToneMap tone=njin::RenderToneMap::None) {
  const float a=x/320-1,b=(x+80)/320-1,c=1-y/240,d=1-(y+40)/240;
  njin::RenderVertex v[]{{a,c,0,.25f,.5f},{b,c,0,.25f,.5f},{b,d,0,.25f,.5f},{a,c,0,.25f,.5f},{b,d,0,.25f,.5f},{a,d,0,.25f,.5f}};
  const float uvs[6][2]{{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};
  for(unsigned i=0;i<6;++i) v[i].uv1={uvs[i][0],uvs[i][1]};
  njin::MeshDraw draw{v,{}}; draw.texture=textures[0]; draw.depth_test=false; draw.opaque_alpha=true;
  draw.maps=maps; draw.lighting=light; draw.tonemap=tone;
  njin::renderer_draw_mesh(ctx,draw);
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx)); if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  if(portable) {
    auto light=njin::light3d_get(ctx); light.direction={0,0,-1}; light.color=(frame<6||frame>=12)?njin::rgba{1,1,1,1}:njin::rgba{0,0,0,1};
    light.ambient=(frame<6||frame>=12)?njin::rgba{0,0,0,1}:njin::rgba{.25f,.25f,.25f,1}; light.tonemap=njin::tonemap3d_linear;
    njin::light3d_set(ctx,light);
    auto m=njin::model_material_get(ctx,mapped,1); m.surface.specular=0;
    if(frame>=12) m.normal=flat_override;
    njin::model_material_set(ctx,mapped,1,m);
    njin::begin_3d(ctx,camera);
    njin::draw_model(ctx,mapped,{.position={-1.5f,0,0},.scale={2,2,1}});
    if(frame==12) {
      bool rejected=false;
      try { njin::texture_unload(ctx,flat_override); } catch(const std::logic_error &) { rejected=true; }
      if(!rejected||njin::texture_size(ctx,flat_override).x!=1) throw std::runtime_error("queued normal map was released");
    }
    njin::end_3d(ctx);
  }
  njin::RenderLighting light; light.enabled=true; light.ambient={0,0,0}; light.specular=0; light.camera={0,0,10000};
  njin::RenderMaterialMaps maps; maps.normal.texture=textures[1]; maps.normal.linear=false; maps.normal.uv.set=1;
  maps.normal.uv.values={-1,0,0,1,1,0};
  low_quad(ctx,400,20,maps,light);
  maps={}; maps.metal_rough.texture=textures[2]; light.pbr=true; light.metallic=light.roughness=1;
  low_quad(ctx,400,80,maps,light,njin::RenderToneMap::Linear);
  maps={}; maps.occlusion.texture=textures[3]; maps.occlusion_strength=.5f;
  light.pbr=false; light.color={0,0,0}; light.ambient={.5f,.5f,.5f};
  low_quad(ctx,400,140,maps,light);
  maps={}; maps.emission.texture=textures[4]; maps.emission_factor={0,0,.5f,2};
  light.ambient={0,0,0}; low_quad(ctx,400,200,maps,light,njin::RenderToneMap::Linear);
  maps={}; light.color={1,1,1}; low_quad(ctx,400,260,maps,light); // no sampler/uniform leakage
  if(frame==4) njin::screenshot(ctx,(capture+".direct.png").c_str());
  if(frame==10) njin::screenshot(ctx,(capture+".ambient.png").c_str());
  if(frame==16) njin::screenshot(ctx,(capture+".override.png").c_str());
  if(++frame>=20) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  const auto check=[](const njin::RenderImageAsset &im,unsigned x,unsigned y,std::array<int,3> color,int tolerance=2,bool world=false) {
    x=unsigned(world?im.width*.5f+(float(x)-320)*im.height/480.f:float(x)*im.width/640.f);y=unsigned(float(y)*im.height/480.f);
    const auto i=(size_t(y)*im.width+x)*4;
    for(unsigned c=0;c<3;++c) if(std::abs(int(im.pixels[i+c])-color[c])>tolerance)
      throw std::runtime_error("material pixel mismatch at "+std::to_string(x)+","+std::to_string(y)+": got "+std::to_string(im.pixels[i])+","+std::to_string(im.pixels[i+1])+","+std::to_string(im.pixels[i+2]));
  };
  const auto direct=njin::renderer_load_image((capture+".direct.png").c_str());
  check(direct,420,40,{255,255,255}); check(direct,460,40,{1,1,1});
  check(direct,440,100,{151,151,151}); check(direct,440,160,{96,96,96});
  check(direct,440,220,{0,0,255}); check(direct,440,280,{255,255,255});
  if(portable) {
    check(direct,200,240,{151,151,255},3,true);
    const auto grazing=(size_t(direct.height/2)*direct.width+unsigned(direct.width*.5f-60*direct.height/480.f))*4;
    if(direct.pixels[grazing]>32||direct.pixels[grazing+1]>32||direct.pixels[grazing+2]<254)
      throw std::runtime_error("UV1 transformed normal or linear emissive factor was not applied");
    const auto ambient=njin::renderer_load_image((capture+".ambient.png").c_str());
    check(ambient,200,240,{55,55,255},2,true); check(ambient,260,240,{55,55,255},2,true);
    const auto override_image=njin::renderer_load_image((capture+".override.png").c_str());
    check(override_image,200,240,{151,151,255},2,true); check(override_image,260,240,{151,151,255},2,true);
    njin::model_unload(ctx,mapped); njin::texture_unload(ctx,flat_override);
  }
  for(auto texture:textures) njin::renderer_destroy_texture(ctx,texture);
  const auto stats=njin::renderer_resource_stats(ctx);
  if(stats.live_meshes||stats.live_targets||stats.live_textures) throw std::runtime_error("material fixture leaked owned resources");
}
void setup(njin::context &ctx) {
  njin::ecs_register(ctx,njin::phase_startup,startup); njin::ecs_register(ctx,njin::phase_render,render); njin::ecs_register(ctx,njin::phase_shutdown,shutdown);
}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin material regression",.width=640,.height=480,.target_fps=60}; cfg.audio=false; cfg.resizable=true; cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {
      if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");
    } else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    portable=cfg.graphics_backend!=njin::GraphicsBackend::OpenGL;
    capture=(std::filesystem::path(argv[0]).parent_path()/"captures"/("materials-"+std::string(njin::graphics_backend_name(cfg.graphics_backend))+".png")).string();
    std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(int attempt=0;attempt<2;++attempt) {
      frame=0; std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);
      njin::mod_register(*ctx,{.name="material regression",.setup=setup}); njin::run(*ctx);
    }
    return 0;
  } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
