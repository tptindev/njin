#include <njin.h>
#include "../../tools/lightmap/fixture.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace {
void require(bool b,const char *message) {if(!b) throw std::runtime_error(message);}
template<class F> void refused(F fn) {bool caught=false;try {fn();}catch(const std::exception &) {caught=true;}require(caught,"invalid lightmap operation accepted");}
std::string path,capture;njin::LightmapAsset baked;njin::LightmapScene scene;
njin::LightmapBinding pages[2];njin::RenderTargetHandle target,hdr;unsigned frame=0;
float texel(const njin::LightmapAsset &a,unsigned page,unsigned x,unsigned y) {
  const auto &image=a.pages[page].image;
  x=x*image.width/a.settings.size;y=y*image.height/a.settings.size;
  float value;std::memcpy(&value,image.pixels.data()+(size_t(y)*image.width+x)*16,4);return value;
}
void cpu() {
  scene=njin::lightmap_fixture::scene();njin::LightmapSettings settings;
  auto start=std::chrono::steady_clock::now();baked=njin::lightmap_bake(scene,settings);
  std::printf("CPU bake %.3f ms\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
  require(texel(baked,0,32,32)==0&&texel(baked,1,32,32)>1.8f,"visibility or instance placement failed");
  require(texel(baked,0,16,16)>1.8f,"unoccluded receiver not lit");
  auto again=njin::lightmap_bake(scene,settings);require(again.pages[0].image.pixels==baked.pages[0].image.pixels,"seed repeatability failed");
  auto clear=njin::lightmap_bake(njin::lightmap_fixture::scene(false),settings);require(texel(clear,0,32,32)>1.8f,"blocker A/B failed");
  njin::lightmap_save(path.c_str(),baked);njin::lightmap_save(path.c_str(),baked);
  auto loaded=njin::lightmap_load(path.c_str(),baked.fingerprint);require(loaded.pages[0].image.pixels==baked.pages[0].image.pixels,"roundtrip failed");
  refused([&]{njin::lightmap_load(path.c_str(),baked.fingerprint+1);});
  refused([&]{njin::lightmap_load((path+".missing").c_str(),baked.fingerprint);});
  refused([&]{njin::lightmap_bake(scene,settings,[](float p){return p<.5f;});});
  require(njin::lightmap_load(path.c_str(),baked.fingerprint).pages.size()==2,"cancel damaged asset");
  {std::ifstream in(path,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});bytes[bytes.size()-1]^=1;std::ofstream out(path+".corrupt",std::ios::binary);out.write(bytes.data(),bytes.size());}
  refused([&]{njin::lightmap_load((path+".corrupt").c_str(),baked.fingerprint);});
  auto bad=scene;bad.instances[0].dynamic=true;refused([&]{njin::lightmap_bake(bad);});
  bad=scene;bad.direction={1e30f,0,0};refused([&]{njin::lightmap_bake(bad);});
  auto geometry=std::make_shared<njin::RenderGeometryData>(*scene.instances[0].geometry);bad=scene;bad.instances[0].geometry=geometry;
  geometry->vertices[0].lightmap_uv={-.1f,0};refused([&]{njin::lightmap_bake(bad);});
  *geometry=*scene.instances[0].geometry;geometry->indices.insert(geometry->indices.end(),{0,1,2});refused([&]{njin::lightmap_bake(bad);});
  bad=scene;bad.instances[0].placement.transform[0]=-2;bad.instances[0].placement.transform[5]=.5f;
  auto reflected=njin::lightmap_bake(bad);require(texel(reflected,1,32,32)>1.8f,"negative/nonuniform scale failed");
  bad=scene;bad.illuminance[0]+=1;require(njin::lightmap_fingerprint(bad,settings)!=baked.fingerprint,"light invalidation failed");
  bad=scene;bad.instances[0].placement.transform[12]+=.1f;require(njin::lightmap_fingerprint(bad,settings)!=baked.fingerprint,"transform invalidation failed");
  njin::LightmapScene charts;charts.identity="public-multi-chart";charts.direction={-1,-2,-3};charts.illuminance={6,3,1.5f};
  charts.instances.push_back({17,njin::lightmap_fixture::cube()});charts.instances.push_back({18,charts.instances[0].geometry});charts.instances[1].placement.transform[12]=2;
  auto unwrap=settings;unwrap.generate_uv=true;
  const auto atlas=njin::lightmap_bake(charts,unwrap);
  require(atlas.pages.size()==12,"xatlas did not split cube charts into independent mip-safe pages");
  for(const auto &page:atlas.pages) for(const auto &v:page.geometry->vertices) {
    bool found=false;
    for(const auto &source:charts.instances[0].geometry->vertices) if(v.x==source.x&&v.y==source.y&&v.z==source.z&&v.normal==source.normal&&v.u==source.u&&v.v==source.v&&v.uv1==source.uv1) found=true;
    require(found,"unwrap damaged material UV0/UV1 or source attributes");
  }
  njin::lightmap_save((path+".charts").c_str(),atlas);
  auto chart_copy=njin::lightmap_load((path+".charts").c_str(),njin::lightmap_fingerprint(charts,unwrap));
  require(chart_copy.pages.size()==12&&chart_copy.pages[0].geometry->vertices[0].uv1==atlas.pages[0].geometry->vertices[0].uv1,"chart geometry asset roundtrip failed");
  auto repeat_atlas=njin::lightmap_bake(charts,unwrap);require(repeat_atlas.pages[0].image.pixels==atlas.pages[0].image.pixels,"unwrap/bake is not deterministic");
  auto existing=charts;auto supplied=std::make_shared<njin::RenderGeometryData>(*charts.instances[0].geometry);
  for(unsigned vertex=0;vertex<supplied->vertices.size();++vertex) {
    const unsigned chart=vertex/4;auto &v=supplied->vertices[vertex];
    v.lightmap_uv={(float(chart%3)+.1f+v.u*.8f)/3,(float(chart/3)+.1f+v.v*.8f)/2};
  }
  existing.instances[0].geometry=existing.instances[1].geometry=supplied;
  const auto supplied_atlas=njin::lightmap_bake(existing,settings);
  require(supplied_atlas.pages.size()==12,"valid supplied multi-chart UVs were not packed into isolated pages");
  std::printf("asset %llu bytes, CPU base texels %zu bytes per context; sampling: one trilinear lightmap lookup/fragment\n",(unsigned long long)std::filesystem::file_size(path),baked.pages[0].image.pixels.size()*2);
}
void startup(njin::context &ctx) {
  auto asset=njin::lightmap_load(path.c_str(),njin::lightmap_fingerprint(scene,{}));
  for(unsigned i=0;i<2;++i) pages[i]=njin::lightmap_upload(ctx,asset,i+1);
  target=njin::renderer_create_target(ctx,320,160);
  hdr=njin::renderer_create_hdr_target(ctx,320,160);
  njin::MeshDraw stale;stale.geometry=scene.instances[0].geometry;stale.world=pages[0].placement.transform;stale.world[12]+=1;
  refused([&]{njin::lightmap_bind(stale,pages[0]);});
}
void render(njin::context &ctx) {
  const unsigned stage=frame/3;
  njin::renderer_begin_target(ctx,stage==4?hdr:target,{0,0,0,1});
  for(unsigned i=0;i<2;++i) {
    njin::MeshDraw d;d.geometry=scene.instances[i].geometry;d.vertices=d.geometry->vertices;d.indices=d.geometry->indices;
    static const uint8_t white[]{255,255,255,255};d.image={1,1,white};
    d.world=scene.instances[i].placement.transform;
    d.transform[0]=.42f;d.transform[5]=.8f;d.transform[12]=i?.52f:-.52f;
    if(stage==5) {d.transform[0]=.04f;d.transform[5]=.08f;}
    d.lighting.enabled=true;d.lighting.color=stage==1?std::array<float,3>{100,100,100}:std::array<float,3>{0,0,0};
    d.lighting.ambient={0,0,0};d.lighting.specular=0;d.blend=njin::RenderBlend::Opaque;
    d.tonemap=njin::RenderToneMap::ACES;d.exposure=stage==2?.25f:1;
    if(stage==4) {d.tonemap=njin::RenderToneMap::None;d.linear_output=true;}
    if(stage!=3) njin::lightmap_bind(d,pages[i]);
    if(frame==0) {auto secondary=d;secondary.color_uv.set=1;njin::renderer_draw_mesh(ctx,secondary);}
    njin::renderer_draw_mesh(ctx,d);
  }
  njin::renderer_end_target(ctx);
  const njin::RenderVertex fullscreen[]{{-1,-1,0,0,1},{1,-1,0,1,1},{1,1,0,1,0},{-1,-1,0,0,1},{1,1,0,1,0},{-1,1,0,0,0}};
  if(stage==4) {
    const auto linear=njin::renderer_read_target(ctx,hdr);uint16_t red;std::memcpy(&red,linear.pixels.data()+(80*320+243)*8,2);
    require(linear.format==njin::RenderImageFormat::RGBA16F&&red>0x3c00,"baked radiance clipped before HDR resolve");
    njin::renderer_begin_target(ctx,target,{0,0,0,1});
    njin::MeshDraw resolve;resolve.vertices=fullscreen;resolve.texture=njin::renderer_target_texture(ctx,hdr);resolve.resolve=true;resolve.tonemap=njin::RenderToneMap::ACES;resolve.depth_test=false;
    njin::renderer_draw_mesh(ctx,resolve);njin::renderer_end_target(ctx);
  }
  njin::MeshDraw present;present.vertices=fullscreen;present.texture=njin::renderer_target_texture(ctx,target);present.depth_test=false;
  njin::renderer_draw_mesh(ctx,present);
  if(frame%3==2) {
    auto image=njin::renderer_read_target(ctx,target);
    auto red=[&](unsigned x,unsigned y){return image.pixels[(size_t(y)*320+x)*4];};
    if(stage<3) require(red(243,80)>100&&red(77,80)<10,"baked visibility missing in renderer");
    if(stage==3) require(red(243,80)<10,"unbound direct-off control not black");
    if(stage==5) require(red(243,80)>100&&red(77,80)<red(243,80),"minified page lost light/visibility or leaked between instances");
    static std::vector<uint8_t> reference;
    if(stage==0) reference=image.pixels;
    if(stage==1) require(reference==image.pixels,"static direct counted realtime light twice");
    if(stage==2) require(image.pixels!=reference,"exposure did not affect lightmap");
    njin::screenshot(ctx,(capture+"-"+std::to_string(stage)+".png").c_str());
  }
  if(++frame==18) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  njin::renderer_destroy_target(ctx,target);njin::renderer_destroy_target(ctx,hdr);for(auto &p:pages) njin::renderer_destroy_texture(ctx,p.map.texture);
  require(njin::renderer_resource_stats(ctx).live_textures==0,"lightmap resource leak");
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin baked static direct",.width=320,.height=160,.target_fps=120};cfg.audio=false;cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
    bool cpu_only=false,legacy_refusal=false;
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) require(njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend),"renderer invalid");else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;else if(std::strcmp(argv[i],"--cpu-only")==0) cpu_only=true;else if(std::strcmp(argv[i],"--legacy-refusal")==0) legacy_refusal=true;
    auto root=std::filesystem::absolute(argv[0]).parent_path();path=(root/"lightmap-fixture.nlm").string();capture=(root/"captures"/("lightmap-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());cpu();if(cpu_only) return 0;
    if(legacy_refusal) {
      cfg.graphics_backend=njin::GraphicsBackend::OpenGL;
      std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);
      auto binding=njin::lightmap_upload(*ctx,baked,1);njin::MeshDraw d;d.geometry=scene.instances[0].geometry;d.world=scene.instances[0].placement.transform;
      njin::lightmap_bind(d,binding);bool caught=false;
      try {njin::renderer_draw_mesh(*ctx,d);} catch(const std::invalid_argument &e) {caught=std::strstr(e.what(),"requires SDL3/bgfx")!=nullptr;}
      njin::renderer_destroy_texture(*ctx,binding.map.texture);require(caught,"legacy renderer silently accepted static direct lightmap");return 0;
    }
    for(unsigned repeat=0;repeat<2;++repeat) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="lightmap validation",.setup=setup});njin::run(*ctx);}
    return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
