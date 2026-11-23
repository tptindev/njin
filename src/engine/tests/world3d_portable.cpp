#include <njin.h>
#include "../runtime/njin_ctx_get.h"
#include "../runtime/njin_frame_impl.h"
#include "../runtime/njin_model.h"
#include "../runtime/njin_world3d_impl.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <raymath.h>
#if defined(__ANDROID__)
#include <android/log.h>
#endif
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
static_assert(requires { njin::RenderWaterEffects{std::array<std::array<float,4>,13>{}}; });
constexpr unsigned stage_count=63;
unsigned frame=0;std::string capture,assets;
njin::terrain3d_handle terrain;njin::grass3d_handle grass;njin::water3d_handle water,optical_water;njin::scatter3d_handle scatter;
njin::texture_handle red,green,water_environment;njin::model_handle rock;
njin::texture_handle tile_pattern;
std::array<njin::terrain3d_handle,4> finish_terrain;
std::vector<size_t> triangles;float water_a=0,water_b=0,initial_height=0;bool guarded=false;
std::array<float,2> foreground_uv{};
std::array<float,2> round_corner_uv{};
void require(bool value,const char *message) {if(!value) throw std::runtime_error(message);}
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  red=njin::texture_load(ctx,(assets+"/atlas-red.png").c_str());green=njin::texture_load(ctx,(assets+"/atlas-green.png").c_str());
  require(red.id&&green.id,"terrain fixture textures missing");
  const unsigned char sky[]{188,0,0,255,188,0,0,255};
  water_environment=njin::texture_from_pixels(ctx,sky,2,1);
  std::vector<float> heights(33*33);for(unsigned z=0;z<33;++z) for(unsigned x=0;x<33;++x) heights[z*33+x]=.3f*std::sin(x*.2f)*std::cos(z*.23f);
  njin::terrain3d_desc t;t.origin={-4,0,-4};t.size=8;t.resolution=33;t.heights=heights.data();t.chunk_quads=8;t.lod_levels=3;t.lod_distance=10;t.collision=false;t.layer_count=2;
  t.layers[0].albedo=red;t.layers[0].roughness=red;t.layers[0].tile=1;
  t.layers[1].albedo=green;t.layers[1].min_height=.12f;t.layers[1].blend=.04f;t.layers[1].tile=1;
  terrain=njin::terrain3d_create(ctx,t);require(terrain.id,"terrain CPU state was not installed");initial_height=njin::terrain3d_height(ctx,terrain,0,0);
  std::array<unsigned char,8*8*4> pattern;
  for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) {
    const auto p=(y*8+x)*4;
    pattern[p]=pattern[p+1]=pattern[p+2]=x<2||y<2?50:180;pattern[p+3]=255;
  }
  tile_pattern=njin::texture_from_pixels(ctx,pattern.data(),8,8);
  auto finish=t;
  finish.heights=nullptr;finish.layer_count=1;finish.resolution=17;
  finish.layers[0].albedo=tile_pattern;finish.layers[0].roughness={};finish.layers[0].tile=.9f;
  for(unsigned i=0;i<finish_terrain.size();++i) {
    finish.layers[0].detile=i==1||i==2?1.f:0.f;
    finish.layers[0].detile_scale=i==2?.8f:0.f;
    finish.layers[0].glitter=i==3?4.f:0.f;
    finish_terrain[i]=njin::terrain3d_create(ctx,finish);
    require(finish_terrain[i].id,"terrain layer finish setup failed");
  }
  njin::grass3d_desc g;g.terrain=terrain;g.layer=-1;g.density=20;g.height=.8f;g.width=.06f;g.patchiness=0;g.cast_shadows=true;g.ground_color=.3f;g.curve=.3f;g.draw_distance=30;g.fade=5;
  grass=njin::grass3d_create(ctx,g);require(grass.id,"grass CPU state missing");
  njin::water3d_desc w;w.size={7,7};w.level=.35f;w.terrain=terrain;w.detail=.5f;
  water=njin::water3d_create(ctx,w);require(water.id,"water CPU state missing");
  w.terrain={};w.level=1.5f;w.bed=1;w.foam_width=0;w.crest_foam=0;w.ripples=1;
  optical_water=njin::water3d_create(ctx,w);
  const njin::vec3 points[]{{-.25f,0,0},{.25f,0,0},{0,.7f,0}};
  rock=njin::model_create(ctx,{.positions=points,.vertex_count=3});auto material=njin::model_material_get(ctx,rock,0);material.surface.unlit=true;material.double_sided=true;material.color={.2f,.3f,.8f,1};njin::model_material_set(ctx,rock,0,material);
  njin::scatter3d_desc sc;sc.terrain=terrain;sc.model=rock;sc.density=1;sc.spacing=.6f;sc.color_jitter=.1f;sc.max_slope=70;
  scatter=njin::scatter3d_create(ctx,sc);require(scatter.id&&njin::scatter3d_count(ctx,scatter)>0,"scatter placement missing");
  njin::post3d p;p.oit=true;njin::post3d_set(ctx,p);
  triangles.assign(stage_count,0);guarded=false;
}
void update(njin::context &ctx) {
  const unsigned stage=frame/4;
  if(stage==1&&frame%4==0) njin::terrain3d_paint(ctx,terrain,{0,0,0},2,1,1);
  if(stage==2&&frame%4==0) {njin::terrain3d_edit(ctx,terrain,{.center={0,0,0},.radius=2,.strength=1});require(njin::terrain3d_height(ctx,terrain,0,0)>initial_height+.9f,"terrain edit/height query diverged");}
  auto quality=njin::graphics_quality_get(ctx);quality.lod_scale=stage==10?.01f:1;njin::graphics_quality_set(ctx,quality);
  njin::ctx_ref<njin::world3d_store>(ctx)->time=stage==5||stage==8?2.4f:0.f;
  if(stage>=16&&stage<=26) {
    auto w=njin::water3d_get(ctx,water);w.wave_count=stage>=22&&stage<=24?3:0;w.ripples=0;w.foam_width=0;w.crest_foam=0;
    w.terrain={};w.bed=1;w.level=1.5f;w.shallow_color=w.deep_color={.5f,.7f,.8f,.7f};
    w.ice=stage==22||stage==23?1.f:stage==24?.4f:0.f;w.ice_snow=stage==23?1.f:0.f;
    w.pour=stage==17||stage==18?1.f:0.f;w.pour_reach=4;w.pour_at={-1.7f,1.5f};
    njin::water3d_set(ctx,water,w);
    auto &world=njin::ctx_ref<njin::world3d_store>(ctx);
    if(stage==19&&frame%4==0) njin::water3d_splash(ctx,water,{-1.7f,1.5f},1,1.5f);
    world->time=stage==18?.6f:stage==19?.3f:stage==20?1.3f:stage>=21?50.f:0.f;
    if(stage==22||stage==23) require(njin::water3d_height(ctx,water,1,1)==w.level,"frozen water query is not flat");
  }
  if(stage>=27&&stage<=31) {
    auto g=njin::grass3d_get(ctx,grass);g.far_cover=stage==27||stage==31?0.f:1.f;
    g.draw_distance=3;g.fade=2;g.layer=stage==29?0:-1;g.min_height=stage==30?100.f:-1e9f;
    njin::grass3d_set(ctx,grass,g);
  }
  if(stage>=32&&stage<49) {
    auto w=njin::water3d_get(ctx,optical_water);w.refract=((stage>=33&&stage<=35)||(stage>=39&&stage<=41)||stage==45)?1.f:0.f;
    w.round=stage==45||stage==46;
    const bool reflection_control=(stage>=36&&stage<=38)||stage==43||stage==44||stage>=47;
    w.wave_count=reflection_control?0:3;w.ripples=reflection_control?0.f:1.f;
    w.reflect=stage==37||stage==38||stage==44||stage>=47?1.f:0.f;w.caustics=stage==39||stage==40?1.f:0.f;
    w.absorb=stage==34?njin::rgba{.3f,.8f,.95f,1}:njin::rgba{1,1,1,1};
    njin::water3d_set(ctx,optical_water,w);njin::ctx_ref<njin::world3d_store>(ctx)->time=stage==35?1.3f:0.f;
  }
  if(stage>=49&&stage<59) {
    // Cross the 16-bit timestamp wrap while an imprint is still fading.
    constexpr float times[]{654,654,654,655.5f,657,658,658,659,660,661};
    njin::ctx_ref<njin::time_state>(ctx).elapsed=times[stage-49];
    if(frame%4==0) {
      njin::terrain3d_snow_set(ctx,terrain,stage>=50&&stage<=53?1.f:0.f);
      njin::terrain3d_imprint_desc desc{.texel=.1f,.size=128,.depth=.15f,.rim=.15f,.lifetime=2.5f,.fade=2,.tint={.3f,.4f,1,1}};
      if(stage==51||stage==58) {
        if(stage==58) {desc.size=64;desc.tint.a=0;desc.depth=0;}
        njin::terrain3d_imprint_set(ctx,terrain,desc);
      }
      if(stage==51||stage==54||stage==56||stage==58) {
        njin::terrain3d_imprint_follow(ctx,terrain,{0,0,0});
        njin::terrain3d_imprint_stamp(ctx,terrain,{.position={-1,0,1},.half_size={1.1f,1.2f}});
      }
      if(stage==55) njin::terrain3d_imprint_clear(ctx,terrain);
      if(stage==57) {
        njin::terrain3d_imprint_follow(ctx,terrain,{40,0,0});
        njin::terrain3d_imprint_follow(ctx,terrain,{0,0,0});
      }
    }
  }
  njin::wind3d_set(ctx,stage>=4?njin::vec2{8,2}:njin::vec2{});
#if !defined(__ANDROID__)
  if(stage==12&&frame%4==0) njin::window_set_size(ctx,{640,480});
#endif
}
void render(njin::context &ctx) {
  const unsigned stage=frame/4;
  njin::light3d light;light.hdr=true;light.tonemap=njin::tonemap3d_linear;light.direction={.5f,-1,.3f};light.color={1,1,1,1};light.ambient={.25f,.25f,.25f,1};light.shadows=stage==15;light.shadow_size=512;light.shadow_range=20;njin::light3d_set(ctx,light);
  if(stage==40||stage==41) {light.color={0,0,0,1};njin::light3d_set(ctx,light);}
  if(stage>=47&&stage<49) {light.color={0,0,0,1};light.ambient={0,0,0,1};light.environment=water_environment;light.environment_visible=false;light.exposure=stage==47?.25f:1.f;njin::light3d_set(ctx,light);}
  njin::camera3d camera;camera.position={5,5,7};camera.target={0,0,0};camera.projection=njin::camera3d_orthographic;camera.ortho_height=10;camera.entities=false;camera.far_plane=100;
  if(stage==38) camera.projection=njin::camera3d_perspective;
  if(stage==43||stage==44) camera.near_plane=2;
  njin::begin_3d(ctx,camera);njin::material3d material;material.unlit=stage<14;material.pbr=true;njin::material3d_set(ctx,material);
  if(stage>=59) njin::draw_terrain3d(ctx,finish_terrain[stage-59]);
  else if(stage<32||stage>=49) njin::draw_terrain3d(ctx,terrain);
  else {
    if(stage<47) {
    njin::draw_plane3d(ctx,{0,-.5f,0},{8,8},{.1f,.2f,.8f,1});
    for(int x=-3;x<=3;++x) njin::draw_cube3d(ctx,{float(x),-.4f,0},{.35f,.1f,7},{.8f,.8f,.2f,1});
    njin::draw_cube3d(ctx,{0,5,-3},{12,6,.3f},{1,.1f,.05f,1});
    njin::draw_cube3d(ctx,{1.5f,2,1.5f},{1,2,1},{.1f,.8f,.1f,1});
    }
    njin::draw_water3d(ctx,optical_water);
  }
  if(stage>=3&&stage<=5) {njin::draw_grass3d(ctx,grass);require(njin::grass3d_drawn(ctx,grass)>100,"visible grass cells generated no blades");}
  if((stage>=7&&stage<=9)||(stage>=16&&stage<=26)) {
    if(stage==9) {
      // rain_rings is ported: drawn instead of the plain water, with no draw_sky3d rain this
      // frame it must be accepted and look exactly like the plain water.
      auto rings=njin::water3d_get(ctx,water);const auto saved=rings;rings.rain_rings=.5f;njin::water3d_set(ctx,water,rings);bool accepted=true;
      try {njin::draw_water3d(ctx,water);} catch(const std::runtime_error &) {accepted=false;}
      njin::water3d_set(ctx,water,saved);require(accepted,"water rain rings were refused");
    } else njin::draw_water3d(ctx,water);
    if(stage==25) {
      const auto saved=njin::water3d_get(ctx,water);auto bad=saved;bad.ice=-.1f;njin::water3d_set(ctx,water,bad);
      bool rejected=false;try {njin::draw_water3d(ctx,water);} catch(const std::invalid_argument &) {rejected=true;}
      njin::water3d_set(ctx,water,saved);require(rejected,"negative ice was submitted to GPU");
    }
    if(stage==7) water_a=njin::water3d_height(ctx,water,2,1);
    if(stage==8) water_b=njin::water3d_height(ctx,water,2,1);
  }
  if(stage==11) njin::draw_scatter3d(ctx,scatter);
  if(stage>=14&&stage<27) njin::draw_cube3d(ctx,{0,1.5f,0},{1,2,1},{1,1,1,1});
  if(stage==13) {
    try {njin::terrain3d_destroy(ctx,terrain);} catch(const std::logic_error &) {guarded=true;}
    require(guarded&&njin::terrain3d_contains(ctx,terrain,0,0),"terrain destruction was not atomic while queued");
  }
  const njin::model_store &store=njin::ctx_ref<njin::model_store>(ctx);
  if(stage==32) {
    const auto m=MatrixToFloatV(store.portable_view_projection);
    const float w=m.v[3]*1.5f+m.v[7]*2.4f+m.v[11]*2+m.v[15];
    foreground_uv={(m.v[0]*1.5f+m.v[4]*2.4f+m.v[8]*2+m.v[12])/w*.5f+.5f,
      .5f-(m.v[1]*1.5f+m.v[5]*2.4f+m.v[9]*2+m.v[13])/w*.5f};
    const float c=m.v[3]*3.3f+m.v[7]*1.5f+m.v[11]*3.3f+m.v[15];
    round_corner_uv={(m.v[0]*3.3f+m.v[4]*1.5f+m.v[8]*3.3f+m.v[12])/c*.5f+.5f,
      .5f-(m.v[1]*3.3f+m.v[5]*1.5f+m.v[9]*3.3f+m.v[13])/c*.5f};
  }
  if(stage==22||stage==23) for(const auto &packet:store.portable_queue) if(packet.world_style[0][0]==3) {
    const float level=njin::water3d_get(ctx,water).level;
    require(packet.water&&packet.geometry&&std::all_of(packet.geometry->vertices.begin(),packet.geometry->vertices.end(),[&](const auto &v){return v.y==level;}),"ice mesh disagrees with flat physics/query surface");
  }
  for(const auto &packet:store.portable_queue) if(packet.world_style[0][0]==1&&packet.world_style[0][1]==0)
    triangles[stage]+=njin::ctx_ref<njin::window_guard>(ctx).renderer_resources.meshes.at(packet.mesh.id).indices.size()/3;
  njin::end_3d(ctx);
  if(frame%4==2) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
  if(++frame==stage_count*4) njin::quit(ctx);
}
void shutdown(njin::context &ctx) {
  const auto im=[](unsigned i){return njin::renderer_load_image((capture+"."+std::to_string(i)+".png").c_str());};
  const auto changed=[](const auto &a,const auto &b){require(a.width==b.width&&a.height==b.height,"unexpected capture size");unsigned count=0;for(size_t p=0;p<a.pixels.size();p+=4) count+=std::abs(int(a.pixels[p])-int(b.pixels[p]))>4||std::abs(int(a.pixels[p+1])-int(b.pixels[p+1]))>4||std::abs(int(a.pixels[p+2])-int(b.pixels[p+2]))>4;return count;};
  const auto base=im(0);unsigned red_count=0,green_count=0;
  for(size_t p=0;p<base.pixels.size();p+=4) {red_count+=base.pixels[p]>100&&base.pixels[p]>base.pixels[p+1]*2;green_count+=base.pixels[p+1]>100&&base.pixels[p+1]>base.pixels[p]*2;}
  require(red_count>500&&green_count>500,"terrain layered textures missing");
  require(changed(base,im(1))>100&&changed(im(1),im(2))>100,"terrain paint/edit did not rebuild GPU chunks");
  require(im(2).pixels==im(6).pixels,"terrain cache changed without an edit");
  require(changed(im(2),im(3))>300&&changed(im(4),im(5))>100,"instanced grass/wind animation missing");
  require(changed(im(6),im(7))>300&&changed(im(7),im(8))>100&&std::abs(water_a-water_b)>.001f,"Gerstner water render/query animation missing");
  require(im(7).pixels==im(9).pixels,"rain rings without rain changed the water");
  require(triangles[10]<triangles[2]/2,"terrain LOD did not reduce submitted triangles");
  require(changed(im(2),im(11))>100,"scatter GPU instances missing");
  require(changed(im(14),im(15))>20,"terrain/caster shadows missing");
  require(changed(im(16),im(17))>20&&changed(im(17),im(18))>20,"pour rings/animation missing");
  require(changed(im(16),im(19))>20&&changed(im(19),im(20))>20,"splash propagation missing");
  require(im(16).pixels==im(21).pixels&&im(16).pixels==im(26).pixels,"expired splash/effects leaked into later water draws");
  require(changed(im(16),im(22))>300&&changed(im(22),im(23))>100&&changed(im(22),im(24))>100,"ice/snow coverage missing");
  require(changed(im(27),im(28))>300&&changed(im(28),im(29))>100,"grass far cover/layer mask missing");
  require(im(27).pixels==im(30).pixels&&im(27).pixels==im(31).pixels,"grass far-cover height/reset leaked");
  require(changed(im(32),im(33))>100&&changed(im(33),im(34))>100,"water refraction/absorption missing");
  require(changed(im(33),im(35))>100,"refracted water animation missing");
  require(changed(im(36),im(37))>100,"water scene reflection missing");
  const auto mirror=im(37),no_mirror=im(36);unsigned reflected_red=0,baseline_red=0;
  for(unsigned y=mirror.height/2;y<mirror.height;++y) for(unsigned x=0;x<mirror.width;++x) {
    const auto p=(size_t(y)*mirror.width+x)*4;
    reflected_red+=mirror.pixels[p]>35&&mirror.pixels[p]>mirror.pixels[p+1]*1.3f;
    baseline_red+=no_mirror.pixels[p]>35&&no_mirror.pixels[p]>no_mirror.pixels[p+1]*1.3f;
  }
  require(reflected_red>baseline_red+100,"reflection did not return the above-water red wall in the water region");
  require(changed(im(33),im(39))>20,"water caustics missing");
  require(im(40).pixels==im(41).pixels,"caustics added light without sunlight");
  require(im(32).pixels==im(42).pixels,"water optics leaked after disabling effects");
  require(changed(im(43),im(44))>100,"orthographic reflection with near plane beyond one metre missing");
  const auto before=im(32),after=im(33);
  float width=float(before.width),height=float(before.height),ox=0,oy=0;
#if defined(__ANDROID__)
  const float fit=std::floor(std::min(width/320,height/240));ox=(width-320*fit)*.5f;oy=(height-240*fit)*.5f;width=320*fit;height=240*fit;
#endif
  const unsigned px=unsigned(ox+foreground_uv[0]*width),py=unsigned(oy+foreground_uv[1]*height);
  require(px<before.width&&py<before.height,"foreground control outside capture");
  const auto offset=(size_t(py)*before.width+px)*4;
  require(before.pixels[offset+1]>before.pixels[offset]*2&&before.pixels[offset+1]>before.pixels[offset+2]*2,"foreground marker is not visible");
  for(unsigned c=0;c<3;++c) require(std::abs(int(before.pixels[offset+c])-int(after.pixels[offset+c]))<=1,"water refracted an opaque object above the surface");
  const auto rounded=im(45),round_base=im(46);require(changed(rounded,round_base)>100,"rounded lake refraction missing");
  const unsigned rx=unsigned(ox+round_corner_uv[0]*width),ry=unsigned(oy+round_corner_uv[1]*height);
  require(rx<before.width&&ry<before.height,"rounded corner control outside capture");
  const auto corner=(size_t(ry)*rounded.width+rx)*4;
  for(unsigned c=0;c<3;++c) require(std::abs(int(rounded.pixels[corner+c])-int(round_base.pixels[corner+c]))<=1,"refraction escaped the rounded lake mask");
  const auto energy=[](const auto &image) {double sum=0;for(size_t p=0;p<image.pixels.size();p+=4) {const double x=image.pixels[p]/255.;sum+=x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4);}return sum;};
  const double dim=energy(im(47)),bright=energy(im(48));
  require(dim>1&&std::abs(bright/dim-4)<.4,"water IBL fallback exposure differs from the HDR scene");
  const auto bare_terrain=im(49),snow=im(50);
  require(changed(bare_terrain,snow)>500,"terrain snow coverage missing");
  require(changed(snow,im(51))>150&&changed(im(51),im(52))>100,"snow imprint or fade across timestamp wrap missing");
  require(snow.pixels==im(53).pixels,"expired imprint changed snow");
  require(changed(bare_terrain,im(54))>100&&changed(bare_terrain,im(56))>100,"bare ground imprint missing");
  require(bare_terrain.pixels==im(55).pixels&&bare_terrain.pixels==im(57).pixels,"clear or moving the imprint window left stale marks");
  require(changed(bare_terrain,im(58))>50,"imprint rim did not change lighting with tint and depression disabled");
  require(changed(im(59),im(60))>300,"terrain detiling did not alter the repeated texture");
  require(changed(im(60),im(61))>300,"terrain detile scale spread did not alter the texture");
  require(changed(im(59),im(62))>5,"terrain glitter had no visible effect");
  require(njin::renderer_resource_stats(ctx).instances_submitted>0,"grass/scatter were not submitted as GPU instances");
  njin::scatter3d_destroy(ctx,scatter);njin::grass3d_destroy(ctx,grass);njin::water3d_destroy(ctx,water);njin::water3d_destroy(ctx,optical_water);njin::terrain3d_destroy(ctx,terrain);
  njin::model_unload(ctx,rock);njin::texture_unload(ctx,red);njin::texture_unload(ctx,green);njin::texture_unload(ctx,water_environment);
  for(auto handle:finish_terrain) njin::terrain3d_destroy(ctx,handle);
  njin::texture_unload(ctx,tile_pattern);
  require(!njin::terrain3d_contains(ctx,terrain,0,0),"destroyed terrain remained queryable");
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_update,update);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin portable world regression",.width=320,.height=240,.target_fps=120};cfg.audio=false;cfg.clear_bg_color={0,0,0,1};
#if defined(__ANDROID__)
    cfg.virtual_size={320,240};
#endif
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) require(njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend),"invalid renderer");else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    const auto root=std::filesystem::absolute(argv[0]).parent_path();assets=(root/"model-test-assets").string();capture=(root/"captures"/("world3d-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(unsigned i=0;i<2;++i) {frame=0;cfg.width=320;cfg.height=240;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="portable world",.setup=setup});njin::run(*ctx);}return 0;
  } catch(const std::exception &e) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_ERROR,"NjinAndroid","World regression: %s",e.what());
#endif
    std::fprintf(stderr,"%s\n",e.what());return 1;}
}
