#include <njin.h>
#include "../runtime/njin_model.h"
#include "../runtime/njin_ctx_get.h"
#include "../runtime/njin_frame_impl.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define Rectangle Win32Rectangle
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#include <windows.h>
#undef Rectangle
#undef CloseWindow
#undef ShowCursor
#endif
namespace {
unsigned frame=0;std::string capture,assets;
njin::model_handle grid,animated,loaded,combined,coarse,merged;
std::vector<njin::vec3> vertices,normals;
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  const auto path=(std::filesystem::path(assets)/"lod-animated-grid.gltf").string();animated=njin::model_load(ctx,path.c_str());
  auto &store=njin::ctx_ref<njin::model_store>(ctx);const auto &source=njin::ctx_ref<njin::window_guard>(ctx).renderer_resources.meshes.at(store->slots.at(animated.id-1).portable_meshes.at(0).id);
  for(const auto &v:source.cpu) {vertices.push_back({v.x,v.y,v.z});normals.push_back({v.normal[0],v.normal[1],v.normal[2]});}
  std::vector<njin::u32> indices(source.indices.begin(),source.indices.end());
  grid=njin::model_create(ctx,{.positions=vertices.data(),.normals=normals.data(),.vertex_count=unsigned(vertices.size()),.indices=indices.data(),.index_count=unsigned(indices.size())});
  const njin::vec3 quad[]{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};const njin::u32 ix[]{0,1,2,0,2,3};
  loaded=njin::model_create(ctx,{.positions=quad,.vertex_count=4,.indices=ix,.index_count=6});
  for(auto handle:{grid,animated,loaded}) {auto material=njin::model_material_get(ctx,handle,0);material.double_sided=true;material.surface.unlit=true;njin::model_material_set(ctx,handle,0,material);}
  for(auto handle:{grid,animated}) {
    if(njin::model_lod_build(ctx,handle,{.levels=2,.screen=2})!=2) throw std::runtime_error("static/skinned/morph meshoptimizer LOD did not simplify");
    const auto *slot=njin::model_slot_of(store,handle);
    const auto &resources=njin::ctx_ref<njin::window_guard>(ctx).renderer_resources;
    const auto a=resources.meshes.at(slot->portable_lods[0][0].id).indices.size(),b=resources.meshes.at(slot->portable_lods[1][0].id).indices.size();
    if(a>=source.indices.size()||b>=a) throw std::runtime_error("LOD triangle counts did not decrease");
  }
  const auto level=(std::filesystem::path(assets)/"lod-triangle.gltf").string();const char *paths[]{level.c_str()};
  if(njin::model_lod_load(ctx,loaded,paths,1,2)!=1) throw std::runtime_error("authored LOD file did not attach");
  const njin::model_place places[]{{grid,{.position={-2,0,0},.scale={1,.5f,1}}},{grid,{.position={2,0,0},.scale={-1,.5f,1}}}};
  combined=njin::model_combine(ctx,places,2,2);coarse=njin::model_combine(ctx,places,2,2,1);
  for(auto handle:{combined,coarse}) {
    const auto *slot=njin::model_slot_of(store,handle);
    if(!slot||slot->portable_meshes.size()!=1||slot->portable_lods.empty()) throw std::runtime_error("combined material groups/LOD levels failed");
    auto material=njin::model_material_get(ctx,handle,0);material.color=handle.id==combined.id?njin::rgba{1,0,0,1}:njin::rgba{0,0,1,1};material.double_sided=false;njin::model_material_set(ctx,handle,0,material);
  }
  const auto &resources=njin::ctx_ref<njin::window_guard>(ctx).renderer_resources;
  if(resources.meshes.at(njin::model_slot_of(store,coarse)->portable_meshes[0].id).indices.size()>2*resources.meshes.at(njin::model_slot_of(store,grid)->portable_lods[0][0].id).indices.size()) throw std::runtime_error("combine from_level retained full-detail triangles");
  const auto merge_path=(std::filesystem::path(assets)/"lod-merge.gltf").string();merged=njin::model_load(ctx,{.path=merge_path.c_str(),.merge=true});
  if(!merged.id||njin::model_slot_of(store,merged)->portable_meshes.size()!=1) throw std::runtime_error("static glTF same-material primitives did not merge");
  bool rejected=false;const auto before=njin::renderer_resource_stats(ctx);
  try {(void)njin::model_load(ctx,{.path=path.c_str(),.merge=true});} catch(const std::runtime_error &e) {rejected=std::strstr(e.what(),"merging animated")!=nullptr;}
  const auto after=njin::renderer_resource_stats(ctx);
  if(!rejected||before.live_meshes!=after.live_meshes||before.live_textures!=after.live_textures) throw std::runtime_error("unsupported animated merge did not roll back");
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  auto &store=njin::ctx_ref<njin::model_store>(ctx);
  njin::graphics_quality quality;quality.lod_scale=frame==0?0:frame>=10?.16f:1;njin::graphics_quality_set(ctx,quality);
  if(frame==6) {
    for(auto &p:vertices) p.x+=.5f;
    njin::model_store_update_vertices(store,grid,vertices.data(),normals.data(),unsigned(vertices.size()));
    if(!store->slots.at(grid.id-1).portable_lods.empty()) throw std::runtime_error("dynamic update retained stale LOD geometry");
  }
  auto light=njin::light3d_get(ctx);light.hdr=false;light.shadows=false;light.tonemap=njin::tonemap3d_none;njin::light3d_set(ctx,light);
  njin::camera3d camera;camera.position={0,0,5};camera.target={};camera.projection=njin::camera3d_orthographic;camera.ortho_height=6;camera.entities=false;
  njin::begin_3d(ctx,camera);
  if(frame>=10) {
    const float records[]{-1,0,0,1, 1,0,0,.25f, 40,0,0,1};
    const auto buffer=njin::instance_buffer_create(ctx,4);
    njin::instance_buffer_upload(ctx,buffer,records,3);
    const njin::model_pose pose{.anim=0,.time=.5f,.loop=false};
    const bool posed=frame==11;
    const auto model=posed?animated:loaded;
    const auto &slot=store->slots.at(model.id-1);
    const auto before=njin::renderer_resource_stats(ctx);
    const auto culled_before=njin::render_info_get(ctx).instances3d_culled;
    njin::draw_instanced3d(ctx,model,buffer,{.radius=1,.pose=posed?&pose:nullptr});
    njin::instance_buffer_destroy(ctx,buffer);
    const auto &resources=njin::ctx_ref<njin::window_guard>(ctx).renderer_resources;
    bool base=false,coarse=false,offscreen=false;
    for(const auto &draw:store->portable_queue) {
      if(!draw.instances||draw.instances->size()!=1) throw std::runtime_error("instance selection did not split the three records");
      const auto index_count=[&](auto mesh){return resources.meshes.at(mesh.id).indices.size();};
      const auto selected=[&](auto mesh){return posed?(draw.geometry&&draw.geometry->indices.size()==index_count(mesh)):draw.mesh.id==mesh.id;};
      if(draw.shadow_only) offscreen=selected(slot.portable_meshes[0]);
      else {
        base|=selected(slot.portable_meshes[0]);
        coarse|=selected(slot.portable_lods.back()[0]);
      }
    }
    const auto culled=njin::render_info_get(ctx).instances3d_culled-culled_before;
    if(!base||!coarse||!offscreen||culled!=1)
      throw std::runtime_error("instance selection failed at frame "+std::to_string(frame)+
        " (base="+std::to_string(base)+", coarse="+std::to_string(coarse)+
        ", shadow="+std::to_string(offscreen)+", culled="+std::to_string(culled)+")");
    njin::end_3d(ctx);
    if(njin::renderer_resource_stats(ctx).instances_submitted-before.instances_submitted!=2)
      throw std::runtime_error("off-camera instance entered the camera draw");
    if(++frame==12) njin::quit(ctx);
    return;
  }
  if(frame>=2&&frame<=4) {
    const njin::model_pose pose{.anim=frame==4?1:0,.time=frame==4?1.f:.5f,.loop=false};njin::draw_model_anim(ctx,animated,{},pose,{0,1,0,1});
    const auto &packet=store->portable_queue.back();
    const auto &resources=njin::ctx_ref<njin::window_guard>(ctx).renderer_resources;
    if(!packet.geometry||packet.geometry->indices.size()!=resources.meshes.at(store->slots.at(animated.id-1).portable_lods[1][0].id).indices.size()) throw std::runtime_error("animated LOD did not reach the renderer submission");
  } else njin::draw_model(ctx,frame==5?loaded:frame==7?combined:frame==8?coarse:frame==9?merged:grid,{},frame>=7?njin::rgba{1,1,1,1}:njin::rgba{0,1,0,1});
  njin::end_3d(ctx);
  if(frame<2) {
    const auto &slot=store->slots.at(grid.id-1);const auto expected=frame?slot.portable_lods[1][0]:slot.portable_meshes[0];
    if(!njin::ctx_ref<njin::window_guard>(ctx).graphics->mesh_in_use(expected)) throw std::runtime_error("quality/projected-size LOD selection did not reach the graphics device");
  }
  njin::screenshot(ctx,(capture+"."+std::to_string(frame)+".png").c_str());++frame;
}
void shutdown(njin::context &ctx) {
  for(unsigned f=0;f<10;++f) {
    const auto image=njin::renderer_load_image((capture+"."+std::to_string(f)+".png").c_str());
    const auto green=[&](unsigned x,unsigned y) {const unsigned px=unsigned(image.width*.5f+(float(x)-160)*image.height/240.f),py=unsigned(float(y)*image.height/240.f);const auto i=(size_t(py)*image.width+px)*4;return image.pixels[i]==0&&image.pixels[i+1]==255&&image.pixels[i+2]==0;};
    if(f>=7) {
      for(unsigned x:{80u,240u}) {const unsigned px=unsigned(image.width*.5f+(float(x)-160)*image.height/240.f);const auto i=(size_t(image.height/2)*image.width+px)*4;const unsigned channel=f==7?0:f==8?2:1;if(image.pixels[i+channel]!=255||image.pixels[i+(channel+1)%3]||image.pixels[i+(channel+2)%3]) throw std::runtime_error("combined/mirrored/merged textured draw mismatch at stage "+std::to_string(f));}
      continue;
    }
    if(!green(f==2||f==3?200:f==6?180:160,f==4?70:120)) throw std::runtime_error("LOD raster/skinning/morph pixel mismatch at stage "+std::to_string(f));
    if(f==5&&green(125,90)) throw std::runtime_error("authored triangle LOD still rendered the base quad");
    if(f==4&&green(160,150)) throw std::runtime_error("morph LOD retained the rest silhouette");
  }
  for(auto handle:{combined,coarse,merged,grid,animated,loaded}) njin::model_unload(ctx,handle);
  if(njin::renderer_resource_stats(ctx).live_meshes||njin::renderer_resource_stats(ctx).live_textures!=1) throw std::runtime_error("LOD resources leaked");
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin portable mesh LOD regression",.width=320,.height=240,.target_fps=120};cfg.audio=false;cfg.clear_bg_color={0,0,0,1};
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    const auto root=std::filesystem::path(argv[0]).parent_path();capture=(root/"captures"/("lod-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();assets=(root/"model-test-assets").string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(unsigned i=0;i<2;++i) {frame=0;vertices.clear();normals.clear();std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="portable mesh LOD",.setup=setup});njin::run(*ctx);}return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
