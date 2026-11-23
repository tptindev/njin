#include <njin.h>
#include "../runtime/njin_image_mips.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#if defined(__ANDROID__)
#include <android/log.h>
#endif
namespace {
int frame=0;std::string capture;bool mobile=false;
std::array<njin::RenderTextureHandle,4> textures;
njin::model_handle model;
void cpu_checks() {
  const unsigned char odd[]{255,0,0,255,0,0,0,255,0,0,0,255};
  const njin::rhi::ImageMipChain npot({3,1,odd,njin::RenderImageFormat::RGBA8,njin::RenderMipMode::Linear});
  if(npot.levels!=2||npot.pixels.size()!=16||npot.pixels[12]!=85) throw std::runtime_error("NPOT mip chain lost an edge");
  const unsigned char normal[]{255,128,128,255,0,128,128,255};
  const njin::rhi::ImageMipChain normals({2,1,normal,njin::RenderImageFormat::RGBA8,njin::RenderMipMode::Normal});
  const auto &p=normals.pixels;float length=0;for(unsigned i=8;i<11;++i) length+=std::pow(p[i]/127.5f-1,2);
  if(std::abs(length-1)>.025f) throw std::runtime_error("normal mip was not normalized");
}
void startup(njin::context &ctx) {
  const auto asset=(std::filesystem::path(capture).parent_path().parent_path()/"model-test-assets"/"mipmaps.gltf").string();
  model=njin::model_load(ctx,{.path=asset.c_str()});if(!model.id) throw std::runtime_error("mipmap glTF failed to load");
  std::vector<unsigned char> pixels(256*256*4);
  for(unsigned y=0;y<256;++y) for(unsigned x=0;x<256;++x) for(unsigned c=0;c<4;++c) pixels[(y*256+x)*4+c]=c==3?255:((x+y)&1)*255;
  textures[0]=njin::renderer_create_texture(ctx,{256,256,pixels,njin::RenderImageFormat::RGBA8,njin::RenderMipMode::SRGB});
  textures[1]=njin::renderer_create_texture(ctx,{256,256,pixels,njin::RenderImageFormat::RGBA8,njin::RenderMipMode::Linear});
  textures[2]=njin::renderer_create_texture(ctx,{256,256,pixels});
  const unsigned char odd[]{255,0,0,255,0,0,0,255,0,0,0,255};
  textures[3]=njin::renderer_create_texture(ctx,{3,1,odd,njin::RenderImageFormat::RGBA8,njin::RenderMipMode::Linear});
  bool invalid=false;try {njin::renderer_create_texture(ctx,{1,1,std::span<const unsigned char>(odd,4),njin::RenderImageFormat::RGBA8,static_cast<njin::RenderMipMode>(99)});} catch(const std::invalid_argument &) {invalid=true;}
  if(!invalid) throw std::runtime_error("invalid mip mode accepted");
}
void render(njin::context &ctx) {
  const auto size=njin::window_size(ctx);
  for(unsigned i=0;i<4;++i) {
    // One pixel per 16 source texels. Odd texture repeats to select its 1x1 tail.
    const float left=-.75f+i*.5f,half=8.f/size.x,hy=8.f/size.y,uv=i==3?64.f:1.f;
    const njin::RenderVertex v[]{{left-half,-hy,0,0,0},{left+half,-hy,0,uv,0},{left+half,hy,0,uv,uv},
      {left-half,-hy,0,0,0},{left+half,hy,0,uv,uv},{left-half,hy,0,0,uv}};
    njin::MeshDraw draw;draw.vertices=v;draw.texture=textures[i];draw.depth_test=false;
    draw.linear_sampling=false;draw.color_min_filter=njin::RenderMinFilter::NearestMipNearest;
    njin::renderer_draw_mesh(ctx,draw);
    if(i==0) {draw.color_min_filter=njin::RenderMinFilter::Nearest;draw.transform[13]=-.5f;njin::renderer_draw_mesh(ctx,draw);}
  }
  auto light=njin::light3d_get(ctx);light.hdr=false;light.shadows=false;light.tonemap=njin::tonemap3d_none;njin::light3d_set(ctx,light);
  njin::camera3d camera;camera.position={0,0,5};camera.target={0,0,0};camera.projection=njin::camera3d_orthographic;camera.ortho_height=8;camera.entities=false;
  njin::begin_3d(ctx,camera);njin::draw_model(ctx,model,{.position={0,-2,0},.scale={.0625f,.0625f,1}});njin::end_3d(ctx);
  if(frame==4) njin::screenshot(ctx,(capture+".png").c_str());
  if(++frame==7) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  const auto image=njin::renderer_load_image((capture+".png").c_str());
  for(unsigned i=0;i<4;++i) {
    const size_t at=(size_t(image.height/2)*image.width+unsigned(image.width*(.125f+i*.25f)))*4;
    const int expected[]{188,128,0,85};
    if(i==2?(image.pixels[at]>2&&image.pixels[at]<253):std::abs(int(image.pixels[at])-expected[i])>2) throw std::runtime_error("GPU mip filter failed at "+std::to_string(i)+": "+std::to_string(image.pixels[at]));
  }
  for(auto texture:textures) njin::renderer_destroy_texture(ctx,texture);
  const size_t base_at=(size_t(image.height*3/4)*image.width+image.width/8)*4;
  if(image.pixels[base_at]>2&&image.pixels[base_at]<253) throw std::runtime_error("non-mip min filter sampled the mip chain");
  const size_t gltf_at=(size_t(image.height*3/4)*image.width+image.width/2)*4;
  if(std::abs(int(image.pixels[gltf_at])-188)>3) throw std::runtime_error("glTF sRGB trilinear minification failed");
  njin::model_unload(ctx,model);
  if(njin::renderer_resource_stats(ctx).live_textures) throw std::runtime_error("mipmap resources leaked");
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    cpu_checks();njin::config cfg{.title="Njin material mipmap regression",.width=640,.height=480,.target_fps=120};cfg.audio=false;cfg.clear_bg_color={0,0,0,1};cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;else if(std::strcmp(argv[i],"--mobile")==0) mobile=true;
    capture=(std::filesystem::path(argv[0]).parent_path()/"captures"/("mipmaps-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(unsigned i=0;i<2;++i) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="mipmap regression",.setup=setup});njin::run(*ctx);}
    return 0;
  } catch(const std::exception &e) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR,"NjinMipmap","%s",e.what());
#endif
    std::fprintf(stderr,"%s\n",e.what());return 1;
  }
}
