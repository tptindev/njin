// Qua API gameplay: relief phải đổi silhouette, fade về mặt gốc và giữ height query.
#include <njin.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
namespace {
unsigned frame=0;
bool legacy=false;
std::string capture;
std::array<njin::terrain3d_handle,8> terrains;
njin::terrain3d_handle no_cast;
std::uint64_t near_meshes=0;
njin::texture_handle height_map,bump_map;
njin::render_texture_handle target;
void require(bool value,const char *message) {if(!value) throw std::runtime_error(message);}
void startup(njin::context &ctx) {
  target=njin::render_texture_load(ctx,320,240);require(target.id!=0,"relief target creation failed");
  std::array<njin::u8,32*32*4> pixels;
  for(size_t i=0;i<pixels.size();i+=4) {pixels[i]=pixels[i+1]=pixels[i+2]=255;pixels[i+3]=255;}
  height_map=njin::texture_from_pixels(ctx,pixels.data(),32,32);
  for(unsigned y=0;y<32;++y) for(unsigned x=0;x<32;++x) {
    const auto p=(y*32+x)*4;
    pixels[p]=pixels[p+1]=pixels[p+2]=njin::u8(127.5f+120.f*std::sin(x*.785398f)*std::sin(y*.785398f));
  }
  bump_map=njin::texture_from_pixels(ctx,pixels.data(),32,32);
  std::array<float,17*17> heights{};
  njin::terrain3d_desc desc;desc.size=8;desc.resolution=17;desc.heights=heights.data();desc.origin={-4,0,-4};
  desc.chunk_quads=8;desc.auto_splat=false;desc.collision=false;desc.relief_detail=4;
  desc.layers[0].color={.45f,.7f,.25f,1};desc.layers[0].height=height_map;desc.layers[0].tile=2;
  for(unsigned i=0;i<terrains.size();++i) {
    desc.layers[0].relief=i==0||i==7?0:2;
    desc.layers[0].height=i>=6?bump_map:height_map;
    desc.relief_distance=i==2?1:40;
    desc.relief_detail=i==3?1:4;
    desc.layer_count=i==4||i==5?2:1;
    desc.layers[1]=desc.layers[0];desc.layers[1].relief=0;
    terrains[i]=njin::terrain3d_create(ctx,desc);require(terrains[i].id!=0,"terrain creation failed");
  }
  njin::terrain3d_paint(ctx,terrains[4],{0,0,0},10000,1,1);
  njin::terrain3d_paint(ctx,terrains[5],{0,0,0},2,1,1);
  if(!legacy) {
    desc.layer_count=1;desc.cast_shadows=false;desc.layers[0].height=height_map;desc.layers[0].relief=2;
    no_cast=njin::terrain3d_create(ctx,desc);
  }
}
void render(njin::context &ctx) {
  const unsigned stage=frame/5;
  if(stage==6&&frame%5==1) njin::terrain3d_edit(ctx,terrains[stage],{.center={0,0,0},.radius=1,.strength=.25f});
  njin::light3d light;light.direction={-.4f,-1,-.3f};light.ambient={.25f,.25f,.25f,1};light.fog_density=0;
  light.shadows=true;light.shadow_size=512;
  njin::light3d_set(ctx,light);
  njin::camera3d camera;camera.position={7,5,10};camera.target={0,0,0};camera.entities=false;camera.fovy=45;
  njin::begin_3d(ctx,camera,target,{1,0,1,1});njin::draw_terrain3d(ctx,terrains[stage]);
  if(no_cast.id&&frame==35) njin::draw_terrain3d(ctx,no_cast);
  if(!legacy&&stage==1&&frame%5==0) {
    bool refused=false;
    try {njin::terrain3d_destroy(ctx,terrains[stage]);} catch(const std::logic_error &) {refused=true;}
    require(refused,"submitted relief resources were destroyed");
  }
  njin::end_3d(ctx);
  if(no_cast.id&&frame==35) near_meshes=njin::renderer_resource_stats(ctx).live_meshes;
  if(no_cast.id&&frame==36) {
    // Rời cả tầm relief lẫn frustum: lưới mịn không được mắc lại khi tắt caster.
    auto far=camera;far.position={500,20,500};far.target={500,0,490};
    njin::begin_3d(ctx,far,target,{1,0,1,1});njin::draw_terrain3d(ctx,no_cast);njin::end_3d(ctx);
    require(njin::renderer_resource_stats(ctx).live_meshes<near_meshes,"off-camera non-caster retained dense relief meshes");
  }
  njin::render_texture_draw(ctx,target,{},{1,1,1,1});
  const float smooth_height=stage==6&&frame%5>=1?.25f:0;
  require(std::abs(njin::terrain3d_height(ctx,terrains[stage],0,0)-smooth_height)<.0001f,"relief changed terrain height query or edit did not rebuild");
  if(frame%5==3) require(njin::render_texture_save(ctx,target,(capture+"-"+std::to_string(stage)+".png").c_str()),"relief target capture failed");
  if(++frame==terrains.size()*5) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  std::array<njin::RenderImageAsset,8> images;
  for(unsigned i=0;i<images.size();++i) images[i]=njin::renderer_load_image((capture+"-"+std::to_string(i)+".png").c_str());
  const auto change=[&](unsigned a,unsigned b) {
    require(images[a].pixels.size()==images[b].pixels.size()&&!images[a].pixels.empty(),"missing relief capture");
    size_t changed=0;
    for(size_t i=0;i<images[a].pixels.size();i+=4)
      if(std::abs(int(images[a].pixels[i])-int(images[b].pixels[i]))+
         std::abs(int(images[a].pixels[i+1])-int(images[b].pixels[i+1]))+
         std::abs(int(images[a].pixels[i+2])-int(images[b].pixels[i+2]))>25) ++changed;
    return changed;
  };
  const auto displaced=change(0,1),faded=change(0,2),coarse=change(1,3),painted=change(0,4),mixed=change(1,5);
  std::printf("relief: displaced=%zu faded=%zu coarse=%zu painted=%zu mixed=%zu\n",displaced,faded,coarse,painted,mixed);
  require(displaced>500,"relief did not change projected geometry");
  require(faded<100,"relief did not fade outside reach");
  require(coarse<150,"constant displacement depends on grid density");
  require(painted<150,"height map ignored zero splat weight");
  require(mixed>100,"painted relief blend did not affect geometry");
  require(change(6,7)>500,"nonuniform height map did not affect terrain");
  const auto variation=[&](unsigned image) {
    double value=0;
    for(unsigned y=112;y<135;++y) for(unsigned x=110;x<175;++x) {
      const auto p=(size_t(y)*images[image].width+x)*4;
      value+=std::abs(int(images[image].pixels[p+1])-int(images[image].pixels[p+5]));
    }
    return value/(23*65);
  };
  std::printf("relief normals: flat=%.3f bumped=%.3f\n",variation(0),variation(6));
  require(variation(6)>variation(0)+.2,"relief normal did not affect lighting");
  for(auto h:terrains) njin::terrain3d_destroy(ctx,h);
  if(no_cast.id) njin::terrain3d_destroy(ctx,no_cast);
  njin::texture_unload(ctx,height_map);
  njin::texture_unload(ctx,bump_map);
  njin::render_texture_unload(ctx,target);
}
void setup(njin::context &ctx) {
  njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);
}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin terrain relief",.width=320,.height=240,.target_fps=60};cfg.audio=false;cfg.clear_bg_color={1,0,1,1};
    cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
    for(int i=1;i<argc;++i) {
      if(std::strncmp(argv[i],"--renderer=",11)==0) require(njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend),"invalid renderer");
      else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    }
    const auto dir=std::filesystem::absolute(argv[0]).parent_path()/"captures";std::filesystem::create_directories(dir);
    legacy=cfg.graphics_backend==njin::GraphicsBackend::OpenGL;
    capture=(dir/("terrain-relief-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();
    std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);
    njin::mod_register(*ctx,{.name="terrain relief",.setup=setup});njin::run(*ctx);return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
