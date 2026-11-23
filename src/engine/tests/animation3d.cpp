#include <njin.h>
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
int frame=0;
std::string capture;
njin::model_handle model,root_model,bind_model,weighted_model,node_morph_model,receiver,procedural;
njin::camera3d camera;
njin::instance_buffer_handle instances;
entt::entity actor;
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  const auto directory=std::filesystem::path(capture).parent_path().parent_path()/"model-test-assets";
  const auto path=(directory/"animated-model.gltf").string(),root=(directory/"animated-root-model.gltf").string();
  model=njin::model_load(ctx,{.path=path.c_str()});root_model=njin::model_load(ctx,{.path=root.c_str()});
  if(!model.id||!root_model.id||njin::model_anim_count(ctx,model)!=6||njin::model_bone_count(ctx,model)!=2||njin::model_anim_find(ctx,model,"morph")!=3||njin::model_morph_find(ctx,model,"up")!=0||njin::model_anim_duration(ctx,model,0)!=1)
    throw std::runtime_error("portable animation import/metadata failed");
  const auto bind_path=(directory/"animated-bind-model.gltf").string(),weighted_path=(directory/"animated-weighted-model.gltf").string(),bad_path=(directory/"animated-invalid-model.gltf").string();
  bind_model=njin::model_load(ctx,{.path=bind_path.c_str()});weighted_model=njin::model_load(ctx,{.path=weighted_path.c_str()});
  const auto node_path=(directory/"animated-node-morphs.gltf").string();node_morph_model=njin::model_load(ctx,{.path=node_path.c_str()});
  const njin::vec3 custom[]{{-.5f,-.5f,0},{.5f,-.5f,0},{.5f,.5f,0},{-.5f,-.5f,0},{.5f,.5f,0},{-.5f,.5f,0}};
  const njin::u32 custom_indices[]{0,1,2,3,4,5},ends[]{3,6};const njin::u8 joints[24]{};float weights[24]{};for(unsigned i=0;i<6;++i) weights[i*4]=2; // Normalized per vertex by the importer.
  procedural=njin::model_create_skinned(ctx,{.positions=custom,.vertex_count=6,.joints=joints,.weights=weights,.indices=custom_indices,.index_count=6,.part_ends=ends,.part_count=2,.skeleton=model});
  if(!procedural.id||njin::model_anim_count(ctx,procedural)!=6||njin::model_bone_count(ctx,procedural)!=2||njin::model_morph_count(ctx,procedural)!=0||njin::model_morph_weights(ctx,procedural,{},nullptr,0)!=0) throw std::runtime_error("procedural skeleton/borrowed clip metadata failed");
  for(int i=0;i<2;++i) {auto m=njin::model_material_get(ctx,procedural,i);m.surface.unlit=true;m.double_sided=true;njin::model_material_set(ctx,procedural,i,m);}
  const auto before=njin::renderer_resource_stats(ctx);bool rejected=false;
  try {(void)njin::model_load(ctx,{.path=bad_path.c_str()});} catch(const std::runtime_error &e) {rejected=std::string(e.what()).find("invalid skin influence")!=std::string::npos;}
  const auto after=njin::renderer_resource_stats(ctx);
  if(!rejected||before.live_meshes!=after.live_meshes||before.live_textures!=after.live_textures) throw std::runtime_error("invalid skin did not roll back");
  const auto step=njin::model_bone_pose(ctx,model,{.anim=4,.time=.75f},0),last=njin::model_bone_pose(ctx,model,{.anim=4,.time=1,.loop=false},0);
  if(step.position.x!=0||last.position.x!=2) throw std::runtime_error("STEP interpolation or held endpoint failed");
  if(njin::model_bone_find(ctx,model,"root")!=0||njin::model_bone_parent(ctx,model,1)!=0) throw std::runtime_error("skeleton names/hierarchy failed");
  const njin::ray3d ray{{1,0,5},{0,0,-1}};
  if(!njin::ray3d_model(ctx,ray,model,{},njin::model_pose{.anim=0,.time=.5f}).hit||njin::ray3d_model(ctx,ray,model,{}).hit) throw std::runtime_error("animated CPU picking did not follow the rendered pose");
  const auto bone=njin::model_bone_pose(ctx,model,{.anim=0,.time=.5f},0);
  if(std::abs(bone.position.x-1)>1e-5f) throw std::runtime_error("animated non-joint ancestor did not move its child joint");
  const auto rotation=njin::model_bone_pose(ctx,model,{.anim=2,.time=1,.loop=false},0);
  if(std::abs(rotation.x_axis.y-1)>1e-5f||std::abs(rotation.y_axis.x+1)>1e-5f) throw std::runtime_error("quaternion animation endpoint failed");
  float weight=0;njin::model_morph_weights(ctx,model,{.anim=3,.time=1.f},&weight,1);
  if(std::abs(weight-.75f)>1e-5f) throw std::runtime_error("cubic spline tangent/duration interpolation failed");
  const njin::vec3 positions[]{{-.5f,-.5f,0},{.5f,-.5f,0},{.5f,.5f,0},{-.5f,.5f,0}},normals[]{{0,0,1},{0,0,1},{0,0,1},{0,0,1}};
  const njin::u32 indices[]{0,1,2,0,2,3};receiver=njin::model_create(ctx,{.positions=positions,.normals=normals,.vertex_count=4,.indices=indices,.index_count=6});
  auto material=njin::model_material_get(ctx,receiver,0);material.double_sided=true;material.surface.specular=0;njin::model_material_set(ctx,receiver,0,material);
  instances=njin::instance_buffer_create(ctx,8);const float data[]{-2,0,0,1,1,0,0,1, 2,0,0,-1,0,1,0,1};njin::instance_buffer_upload(ctx,instances,data,2);
  camera.position={0,0,10};camera.target={0,0,0};camera.projection=njin::camera3d_orthographic;camera.ortho_height=8;camera.entities=false;
  auto &world=njin::world(ctx);actor=world.create();world.emplace<njin::transform3d>(actor);auto &entity=world.emplace<njin::model3d>(actor);entity.model=model;entity.tint={1,0,0,1};entity.visible=false;
}
void update(njin::context &ctx) {
  if(frame==108) {njin::model_unload(ctx,model);model={};if(njin::model_anim_count(ctx,procedural)!=0) throw std::runtime_error("released animation library stayed visible");}
}
void render(njin::context &ctx) {
#if defined(_WIN32)
  const auto window=static_cast<HWND>(njin::window_native_handle(ctx));if(IsIconic(window)) ShowWindow(window,SW_RESTORE);
#endif
  const int stage=frame/6;
  auto light=njin::light3d_get(ctx);light.color={1,1,1,1};light.ambient={.1f,.1f,.1f,1};light.direction={1,0,-1};light.shadows=stage==10;light.shadow_cascades=1;light.shadow_size=256;light.shadow_range=30;light.tonemap=njin::tonemap3d_linear;njin::light3d_set(ctx,light);
  camera.entities=stage==9;auto &entity=njin::world(ctx).get<njin::model3d>(actor);entity.visible=stage==9;entity.pose={.anim=0,.time=.5f};
  njin::model_pose pose;
  const float morph[]{1};std::array<njin::bone_pose3d,2> bones{};for(auto &b:bones) b.position={-2,0,0};
  if(stage==0||stage==8||stage==10) pose={.anim=0,.time=.5f};
  else if(stage==1) pose={.anim=0,.time=1.25f};
  else if(stage==2||stage==12) pose={.anim=0,.time=2,.loop=false};
  else if(stage==3) {pose.anim=0;pose.time=1;pose.loop=false;pose.blend_anim=1;pose.blend_time=1;pose.blend_loop=false;pose.blend=.5f;}
  else if(stage==4) pose={.anim=2,.time=1,.loop=false};
  else if(stage==5) pose={.anim=3,.time=1.f};
  else if(stage==13) pose={.anim=0,.time=.5f};
  else if(stage==15) pose={.anim=0,.time=1};
  else if(stage==14) pose={.anim=5,.time=1,.loop=false};
  else if(stage==6) {pose.morph_weights=morph;pose.morph_count=1;}
  else if(stage==7) pose.bones=bones.data();
  const auto before=njin::renderer_resource_stats(ctx);
  njin::begin_3d(ctx,camera);
  if(stage==17||stage==18) {
    const njin::model_recolor palette[]{{0,{0,1,0,1}},{1,{0,0,1,1}}};
    njin::draw_model_anim(ctx,procedural,{},njin::model_pose{.anim=0,.time=.5f},{1,1,1,1},palette,2);
  }
  else if(stage==16) {
    const njin::model_recolor one{1,{1,1,0,0}};
    njin::model_recolor palette[]{{0,{1,0,0,1}},{1,{0,1,0,0}},{1,{0,0,1,1}}};
    njin::draw_model_anim(ctx,model,{.position={-2,0,0}},pose,{1,1,1,1},one);
    njin::draw_model_anim(ctx,model,{},pose,{1,1,1,1},palette,3);
    palette[1].color={1,0,0,1}; // Submission snapshots must not borrow caller memory.
    njin::draw_model_anim(ctx,model,{.position={2,0,0}},pose,{1,1,1,1},nullptr,9);
    const auto shared=njin::model_material_get(ctx,model,1).color;
    if(shared.r!=1||shared.g!=1||shared.b!=1||shared.a!=1) throw std::runtime_error("recolor mutated shared material");
  }
  else if(stage==8) njin::draw_instanced3d(ctx,model,instances,0,2,pose);
  else if(stage==10) {njin::draw_model(ctx,receiver,{.scale={10,8,1}});njin::draw_model_anim(ctx,model,{.position={0,0,1}},pose,{1,0,0,1});}
  else if(stage==11) njin::draw_model(ctx,model,{}, {1,0,0,1});
  else if(stage!=9) njin::draw_model_anim(ctx,stage==12?root_model:stage==13?bind_model:stage==14?weighted_model:stage==15?node_morph_model:model,{.position=stage==13?njin::vec3{-2,0,0}:njin::vec3{}},pose,{1,0,0,1});
  njin::end_3d(ctx);
  const auto after=njin::renderer_resource_stats(ctx);if(after.deformed_mesh_uploads-before.deformed_mesh_uploads!=unsigned(stage==16?3:stage==15||stage>=17?2:1)) throw std::runtime_error("pose geometry was uploaded more than once across shadow/instance groups");
  if(frame%6==4) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
  if(++frame==114) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  const auto pixel=[&](int stage,unsigned x,unsigned y) {const auto im=njin::renderer_load_image((capture+"."+std::to_string(stage)+".png").c_str());x=unsigned(im.width*.5f+(float(x)-320)*im.height/480.f);y=unsigned(float(y)*im.height/480.f);const auto at=(size_t(y)*im.width+x)*4;return std::array<int,3>{im.pixels[at],im.pixels[at+1],im.pixels[at+2]};};
  const auto red=[&](int stage,unsigned x,unsigned y) {if(pixel(stage,x,y)!=std::array<int,3>{255,0,0}) throw std::runtime_error("animated pixel mismatch at stage "+std::to_string(stage));};
  red(0,380,240);red(1,350,240);red(2,440,240);red(3,420,240);red(4,320,240);red(5,320,200);red(6,320,200);red(7,200,240);red(8,260,240);red(9,380,240);red(11,320,240);red(12,440,240);red(13,380,240);red(14,380,240);red(15,440,183);red(15,320,200);
  if(pixel(0,320,240)!=std::array<int,3>{0,0,0}||pixel(2,320,240)!=std::array<int,3>{0,0,0}||pixel(8,380,240)!=std::array<int,3>{0,255,0}) throw std::runtime_error("pose snapshot/range did not reach geometry");
  if(pixel(10,440,240)[0]>=pixel(10,200,240)[0]-100) throw std::runtime_error("deformed shadow caster did not follow its animated pose");
  if(pixel(16,200,240)!=std::array<int,3>{255,255,0}||pixel(16,320,240)!=std::array<int,3>{0,255,0}||pixel(16,440,240)!=std::array<int,3>{255,255,255}) throw std::runtime_error("animated recolor snapshot/first-match/material-alpha preservation failed");
  for(int stage:{17,18}) {
    const unsigned center=stage==17?380:320;
    if(pixel(stage,center-12,228)!=std::array<int,3>{0,0,255}||pixel(stage,center+12,252)!=std::array<int,3>{0,255,0}) throw std::runtime_error("procedural skin/borrowed library lifetime pixel mismatch at stage "+std::to_string(stage));
  }
  njin::model_unload(ctx,procedural);
  njin::world(ctx).destroy(actor);njin::model_unload(ctx,model);njin::model_unload(ctx,root_model);njin::model_unload(ctx,bind_model);njin::model_unload(ctx,weighted_model);njin::model_unload(ctx,node_morph_model);njin::model_unload(ctx,receiver);njin::instance_buffer_destroy(ctx,instances);
  const auto stats=njin::renderer_resource_stats(ctx);if(stats.live_meshes||stats.live_targets!=1||stats.live_textures!=2) throw std::runtime_error("animation resources leaked");
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_update,update);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin portable animation regression",.width=640,.height=480,.target_fps=60};cfg.audio=false;cfg.clear_bg_color={0,0,0,1};cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) {if(!njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend)) throw std::invalid_argument("invalid renderer");} else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    capture=(std::filesystem::path(argv[0]).parent_path()/"captures"/("animation-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(int repeat=0;repeat<2;++repeat) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="portable animation regression",.setup=setup});njin::run(*ctx);}
    return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
