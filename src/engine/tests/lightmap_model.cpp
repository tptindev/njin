#include <njin.h>
#include "../../tools/lightmap/fixture.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <set>

namespace {
void require(bool b,const char *s) {if(!b) throw std::runtime_error(s);}
template<class F> void refused(F fn) {bool caught=false;try {fn();}catch(const std::exception &) {caught=true;}require(caught,"invalid static lightmap operation accepted");}
std::filesystem::path root;std::string capture;unsigned frame=0;
njin::model_handle plane,cube,imported;njin::lightmap_handle direct_asset,gi_asset,emission_asset,cube_asset,import_asset;
njin::transform3d placements[3]{{.rotation={-90,0,0}},{.position={1,1,0},.rotation={0,-90,0}},{.position={0,1,-1}}};
njin::transform3d cube_places[2]{{.position={-.8f,.5f,0}},{.position={.8f,.5f,0}}},import_place{.position={20,0,0}};
entt::entity cube_entities[2];size_t loaded_pages=0;
njin::model_handle create(njin::context &ctx,const njin::RenderGeometryData &g) {
  std::vector<njin::vec3> positions,normals;std::vector<njin::vec2> uv0,uv1;std::vector<uint32_t> indices;
  for(auto &v:g.vertices) {positions.push_back({v.x,v.y,v.z});normals.push_back({v.normal[0],v.normal[1],v.normal[2]});uv0.push_back({v.u,v.v});uv1.push_back({v.uv1[0],v.uv1[1]});}
  for(auto i:g.indices) indices.push_back(i);
  return njin::model_create(ctx,{.positions=positions.data(),.normals=normals.data(),.texcoords=uv0.data(),.texcoords2=uv1.data(),.vertex_count=uint32_t(positions.size()),.indices=indices.data(),.index_count=uint32_t(indices.size())});
}
double sum(const njin::LightmapAsset &a,uint64_t object,unsigned channel) {
  double result=0;size_t count=0;
  for(const auto &page:a.pages) if(page.source_instance==object) for(size_t p=channel*4;p<page.image.pixels.size();p+=16) {float v;std::memcpy(&v,page.image.pixels.data()+p,4);result+=v;++count;}
  return count?result/count:0;
}
njin::lightmap_handle persist(njin::context &ctx,const njin::LightmapScene &scene,const njin::LightmapSettings &settings,const char *name,njin::LightmapAsset *result=nullptr) {
  auto start=std::chrono::steady_clock::now();auto asset=njin::lightmap_bake(scene,settings);const auto file=(root/name).string();njin::lightmap_save(file.c_str(),asset);
  std::printf("%s: %.2f ms, %zu pages, %llu bytes\n",name,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count(),asset.pages.size(),(unsigned long long)std::filesystem::file_size(file));
  loaded_pages+=asset.pages.size();if(result) *result=asset;
  return njin::lightmap_load(ctx,file.c_str(),njin::lightmap_fingerprint(scene,settings));
}
void startup(njin::context &ctx) {
  plane=create(ctx,*njin::lightmap_fixture::quad());cube=create(ctx,*njin::lightmap_fixture::cube());require(plane.id&&cube.id,"procedural models failed");
  // Three separate model handles allow independent material and bake-instance data.
  auto wall=create(ctx,*njin::lightmap_fixture::quad());auto back=create(ctx,*njin::lightmap_fixture::quad());
  auto red=njin::model_material_get(ctx,wall,0);red.color={1,.08f,.08f,1};red.surface.specular=0;njin::model_material_set(ctx,wall,0,red);
  // Keep wall/back handles in ECS too; normal model unload remains explicit below.
  auto &registry=njin::world(ctx);
  const auto wall_entity=registry.create();registry.emplace<njin::model3d>(wall_entity).model=wall;registry.get<njin::model3d>(wall_entity).visible=false;
  const auto back_entity=registry.create();registry.emplace<njin::model3d>(back_entity).model=back;registry.get<njin::model3d>(back_entity).visible=false;
  njin::LightmapScene scene;scene.identity="public-model-color-bleeding";scene.direction={1,0,0};scene.illuminance={6,6,6};
  njin::lightmap_add_model(ctx,scene,plane,placements[0],1);njin::lightmap_add_model(ctx,scene,wall,placements[1],2);njin::lightmap_add_model(ctx,scene,back,placements[2],3);
  njin::LightmapSettings settings;settings.size=32;settings.samples=1;settings.generate_uv=true;settings.indirect_samples=256;
  njin::LightmapAsset direct,gi;direct_asset=persist(ctx,scene,settings,"lightmap-model-direct.nlm",&direct);
  settings.indirect_bounces=2;gi_asset=persist(ctx,scene,settings,"lightmap-model-gi.nlm",&gi);
  require(sum(direct,1,0)<1e-5&&sum(gi,1,0)>.01&&sum(gi,1,0)>sum(gi,1,1)*5,"traced GI did not produce red color bleeding on unlit floor");
  auto one=settings;one.indirect_bounces=1;const auto single=njin::lightmap_bake(scene,one);
  require(sum(gi,1,0)>sum(single,1,0)+.001,"second diffuse bounce made no contribution");
  const auto repeat=njin::lightmap_bake(scene,settings);require(repeat.pages[0].image.pixels==gi.pages[0].image.pixels,"GI seed is not reproducible");
  auto changed=scene;changed.instances[1].albedo={.1f,.8f,.1f};require(njin::lightmap_fingerprint(changed,settings)!=gi.fingerprint,"bounce material did not invalidate GI");
  refused([&]{njin::lightmap_load(ctx,(root/"lightmap-model-gi.nlm").string().c_str(),njin::lightmap_fingerprint(changed,settings));});
  auto image=std::make_shared<njin::RenderImageAsset>();image->width=2;image->height=1;image->pixels={255,0,0,255,0,255,0,255};
  changed=scene;changed.instances[1].albedo={1,1,1};changed.instances[1].albedo_texture.image=image;
  changed.instances[1].albedo_texture.uv.set=1;changed.instances[1].albedo_texture.uv.values={0,0,0,0,.75f,.5f};
  const auto green=njin::lightmap_bake(changed,settings);require(sum(green,1,1)>sum(green,1,0)*5,"secondary-UV albedo texture did not affect bounced lighting");
  require(njin::lightmap_fingerprint(changed,settings)!=gi.fingerprint,"material texture/sampler did not invalidate GI");
  refused([&]{njin::lightmap_bake(scene,settings,[](float p){return p<.4f;});});
  scene.illuminance={0,0,0};scene.instances[1].emission={2,0,0};njin::LightmapAsset emission;
  emission_asset=persist(ctx,scene,settings,"lightmap-model-emission.nlm",&emission);require(sum(emission,1,0)>.01&&sum(emission,1,1)<1e-5,"emissive transport missing");
  for(unsigned object=0;object<2;++object) {
    const auto entity=registry.create();cube_entities[object]=entity;
    registry.emplace<njin::transform3d>(entity,cube_places[object]);registry.emplace<njin::model3d>(entity).model=cube;
    registry.emplace<njin::lightmap3d>(entity,njin::lightmap3d{{},44+object});
  }
  njin::light3d source_light;source_light.direction={-1,-2,-3};source_light.color={1,1,1,4};source_light.ambient={0,0,0,1};njin::light3d_set(ctx,source_light);
  const auto boxes=njin::lightmap_scene_snapshot(ctx,"public-cube-chart-model");
  settings.indirect_bounces=0;cube_asset=persist(ctx,boxes,settings,"lightmap-model-cube.nlm");
  for(auto entity:cube_entities) registry.get<njin::lightmap3d>(entity).asset=cube_asset;
  registry.get<njin::transform3d>(cube_entities[0]).position.x+=.1f;
  require(njin::lightmap_fingerprint(njin::lightmap_scene_snapshot(ctx,boxes.identity),settings)!=njin::lightmap_fingerprint(boxes,settings),"ECS transform did not invalidate bake");
  registry.get<njin::transform3d>(cube_entities[0])=cube_places[0];
  const auto file=(root/"model-test-assets/gltf-meshopt-plain.gltf").string();imported=njin::model_load(ctx,{.path=file.c_str(),.pbr=true});require(imported.id,"public glTF fixture missing");
  njin::LightmapScene model;model.identity="public-imported-lightmap";model.direction={-1,-2,-3};model.illuminance={4,4,4};njin::lightmap_add_model(ctx,model,imported,import_place,55);
  import_asset=persist(ctx,model,settings,"lightmap-model-import.nlm");
  std::printf("floor red direct %.6f, bounce1 %.6f, bounce2 %.6f, green %.6f, emission %.6f\n",sum(direct,1,0),sum(single,1,0),sum(gi,1,0),sum(gi,1,1),sum(emission,1,0));
}
void render(njin::context &ctx) {
  const unsigned stage=frame/4;auto &registry=njin::world(ctx);
  for(auto entity:cube_entities) registry.get<njin::model3d>(entity).visible=stage==3||stage==4;
  njin::light3d light;light.hdr=true;light.tonemap=njin::tonemap3d_aces;light.color={0,0,0,1};light.ambient={0,0,0,1};njin::light3d_set(ctx,light);
  njin::camera3d camera;camera.position={-3,2.5f,4};camera.target={0,.6f,0};camera.projection=njin::camera3d_orthographic;camera.ortho_height=3.5f;camera.entities=true;
  if(stage==4) camera.ortho_height=14;
  if(stage==5) {camera.position={23,2.5f,4};camera.target={20,0,0};camera.ortho_height=3;}
  njin::begin_3d(ctx,camera);
  if(stage<3) {
    const auto asset=stage==0?direct_asset:stage==1?gi_asset:emission_asset;
    njin::draw_lightmapped_model(ctx,plane,placements[0],asset,1);
    if(frame%4==0) refused([&]{njin::lightmap_unload(ctx,asset);});
    for(auto [e,m]:registry.view<njin::model3d>().each()) if(m.model.id!=cube.id) {
      // Source identities are tied to handles, independent of registry iteration order.
      const bool red=njin::model_material_get(ctx,m.model,0).color.g<.5f;const unsigned object=red?2:3;
      if(stage==2&&red) {auto material=njin::model_material_get(ctx,m.model,0);material.surface.emission={1,0,0,2};njin::model_material_set(ctx,m.model,0,material);}
      njin::draw_lightmapped_model(ctx,m.model,placements[object-1],asset,object);
    }
  }
  njin::draw_lightmapped_model(ctx,imported,import_place,import_asset,55);
  njin::end_3d(ctx);
  if(frame%4==2) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
  if(++frame==24) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  const auto off=njin::renderer_load_image((capture+".0.png").c_str()),on=njin::renderer_load_image((capture+".1.png").c_str());
  require(off.width==on.width&&off.height==on.height&&off.format==njin::RenderImageFormat::RGBA8,"capture readback failed");
  size_t bleeding=0;
  for(size_t pixel=0;pixel<on.pixels.size();pixel+=4) if(on.pixels[pixel]>off.pixels[pixel]+3&&on.pixels[pixel]>2*on.pixels[pixel+1]) ++bleeding;
  require(bleeding>size_t(on.width)*on.height/200,"baked GI/color bleeding did not reach the normal model renderer");
  const auto before=njin::renderer_resource_stats(ctx).live_textures;
  for(auto handle:{direct_asset,gi_asset,emission_asset,cube_asset,import_asset}) njin::lightmap_unload(ctx,handle);
  require(before-njin::renderer_resource_stats(ctx).live_textures==loaded_pages,"asset page unload leaked textures");
  std::set<uint32_t> unloaded;
  for(auto [e,m]:njin::world(ctx).view<njin::model3d>().each()) if(unloaded.insert(m.model.id).second) njin::model_unload(ctx,m.model);
  njin::model_unload(ctx,plane);njin::model_unload(ctx,imported);njin::world(ctx).clear();
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin model lightmaps and traced GI",.width=320,.height=240,.target_fps=120};cfg.audio=false;cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
#if defined(__ANDROID__)
    cfg.virtual_size={320,240};
#endif
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) require(njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend),"invalid renderer");else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    root=std::filesystem::absolute(argv[0]).parent_path();capture=(root/"captures"/("lightmap-model-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(root/"captures");
    for(unsigned repeat=0;repeat<2;++repeat) {frame=0;loaded_pages=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="lightmap model and GI regression",.setup=setup});njin::run(*ctx);}
    return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
