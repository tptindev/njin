#include <njin.h>
#include <algorithm>
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
unsigned frame=0;std::string capture;
njin::texture_handle checker,height,lightmap,under,under_normal,green_normal,environment;
njin::model_handle panel,glass,coat,obj,packed,unpacked,brushed;njin::instance_buffer_handle instances;
njin::model_material glass_material;
njin::model_material coat_materials[2];
void require(bool b,const char *message) {if(!b) throw std::runtime_error(message);}
void startup(njin::context &ctx) {
#if defined(_WIN32)
  ShowWindow(static_cast<HWND>(njin::window_native_handle(ctx)),SW_RESTORE);
#endif
  unsigned char pixels[16*16*4],heights[16*16*4];
  for(unsigned y=0;y<16;++y) for(unsigned x=0;x<16;++x) {
    const unsigned p=(y*16+x)*4;const unsigned char v=((x/2+y/2)%2)?240:40;
    pixels[p]=v;pixels[p+1]=v;pixels[p+2]=v;pixels[p+3]=255;
    heights[p]=heights[p+1]=heights[p+2]=x<8?0:255;heights[p+3]=255;
  }
  checker=njin::texture_from_pixels(ctx,pixels,16,16);height=njin::texture_from_pixels(ctx,heights,16,16);
  const unsigned char blue[]{30,60,180,255},red[]{160,30,20,255},normal[]{255,128,180,255};
  lightmap=njin::texture_from_pixels(ctx,red,1,1);under=njin::texture_from_pixels(ctx,blue,1,1);under_normal=njin::texture_from_pixels(ctx,normal,1,1);
  const unsigned char up[]{128,255,180,255};green_normal=njin::texture_from_pixels(ctx,up,1,1);
  const unsigned char sky[]{0,255,70,255,0,255,70,255};environment=njin::texture_from_pixels(ctx,sky,2,1);
  const njin::vec3 positions[]{{-.7f,-.7f,0},{.7f,-.7f,0},{.7f,.7f,0},{-.7f,.7f,0}};
  const njin::vec2 uv[]{{0,1},{1,1},{1,0},{0,0}};const njin::u32 indices[]{0,1,2,0,2,3};
  panel=njin::model_create(ctx,{.positions=positions,.texcoords=uv,.vertex_count=4,.indices=indices,.index_count=6});
  require(panel.id,"advanced material panel creation failed");
  const auto glass_path=(std::filesystem::path(capture).parent_path().parent_path()/"model-test-assets"/"gltf-volume-maps.gltf").string();
  glass=njin::model_load(ctx,{.path=glass_path.c_str(),.pbr=true});
  require(glass.id,"required glTF volume extensions failed to load");
  glass_material=njin::model_material_get(ctx,glass,1);
  require(std::abs(glass_material.surface.ior-1.33f)<.001f&&glass_material.surface.thickness==1&&
          glass_material.surface.transmission==1&&glass_material.surface.attenuation_distance==.5f,"glTF volume scalars missing");
  const auto assets=std::filesystem::path(capture).parent_path().parent_path()/"model-test-assets";
  const auto coat_path=(assets/"gltf-clearcoat-maps.gltf").string();
  coat=njin::model_load(ctx,{.path=coat_path.c_str(),.pbr=true});
  require(coat.id,"required glTF clearcoat maps failed to load");
  for(int i=0;i<2;++i) coat_materials[i]=njin::model_material_get(ctx,coat,i+1);
  require(coat_materials[0].surface.clearcoat==1&&coat_materials[0].surface.clearcoat_roughness==1&&
          coat_materials[1].surface.clearcoat==1&&coat_materials[1].surface.clearcoat_roughness==1&&coat_materials[0].surface.ior==1,"glTF clearcoat factors missing");
  bool refused=false;
  try {
    auto glass_coat=coat_materials[0];glass_coat.surface.transmission=1;
    njin::model_material_set(ctx,coat,1,glass_coat);
  } catch(const std::invalid_argument &) {refused=true;}
  require(refused,"transmission was accepted on a material with clearcoat maps");
  const auto conflict_path=(assets/"gltf-clearcoat-conflict.gltf").string();
  refused=false;
  try {refused=!njin::model_load(ctx,{.path=conflict_path.c_str(),.pbr=true}).id;} catch(const std::runtime_error &) {refused=true;}
  require(refused,"required clearcoat maps with transmission were not refused");
  const auto obj_path=(assets/"obj-materials.obj").string();
  obj=njin::model_load(ctx,{.path=obj_path.c_str()});
  require(obj.id,"OBJ model failed to load");
  // The legacy layout: the mtllib's materials from index 0, colours stored as 8-bit.
  const auto obj_red=njin::model_material_get(ctx,obj,0),obj_textured=njin::model_material_get(ctx,obj,1);
  require(njin::model_material_find(ctx,obj,"red")==0&&njin::model_material_find(ctx,obj,"textured")==1,"OBJ material names/order");
  require(std::abs(obj_red.color.r-204/255.f)<1e-4f&&std::abs(obj_red.color.g-25/255.f)<1e-4f&&obj_red.color.a==1,"OBJ Kd colour");
  require(std::abs(obj_red.emission_color.r-51/255.f)<1e-4f&&std::abs(obj_red.emission_color.b-102/255.f)<1e-4f,"OBJ Ke emission colour");
  require(obj_textured.color.r==1&&obj_textured.color.g==1&&obj_textured.color.b==1,"OBJ map_Kd material should keep a white colour");
  const auto packed_path=(assets/"gltf-meshopt.gltf").string(),unpacked_path=(assets/"gltf-meshopt-plain.gltf").string();
  packed=njin::model_load(ctx,{.path=packed_path.c_str(),.pbr=true});unpacked=njin::model_load(ctx,{.path=unpacked_path.c_str(),.pbr=true});
  require(packed.id&&unpacked.id,"meshopt-compressed or plain glTF failed to load");
  // KHR_materials_anisotropy factors as the legacy loader (degrees), dropped without tangents.
  const auto brushed_path=(assets/"gltf-anisotropy.gltf").string();
  brushed=njin::model_load(ctx,{.path=brushed_path.c_str(),.pbr=true});
  require(brushed.id,"anisotropy glTF failed to load");
  const auto with_tangents=njin::model_material_get(ctx,brushed,1),without=njin::model_material_get(ctx,brushed,2);
  require(std::abs(with_tangents.surface.anisotropy-.5f)<1e-5f&&std::abs(with_tangents.surface.anisotropy_rotation-.3f*57.2957795f)<1e-3f,"glTF anisotropy factors missing");
  require(without.surface.anisotropy==0,"glTF anisotropy kept on a primitive without tangents");
  instances=njin::instance_buffer_create(ctx,8);const float data[]{1.5f,0,0,1,1,1,1,1};njin::instance_buffer_upload(ctx,instances,data,1);
  njin::post3d post;post.oit=true;njin::post3d_set(ctx,post);
}
void render(njin::context &ctx) {
  const unsigned stage=frame/4;
  njin::light3d light;light.direction={-.8f,0,-1};light.color={1,1,1,1};light.ambient={.15f,.15f,.15f,1};light.hdr=true;light.tonemap=njin::tonemap3d_linear;
  if(stage==5||stage==6) {light.color={0,0,0,1};light.ambient={0,0,0,1};}
  if(stage>=12&&stage<=13) light.direction={0,-1,0};
  if(stage>=14) light.direction={-.2f,-1,-.7f};
  if(stage>=18) light.color={0,0,0,1};
  if(stage>=20) {light.ambient={0,0,0,1};light.environment=environment;light.environment_visible=false;}
  if(stage>=22) {
    light.environment={};light.ambient={.12f,.12f,.12f,1};light.color={1,1,1,1};
    light.direction={-.55f,-1,-.4f};light.shadows=stage==26;light.shadow_size=512;light.shadow_range=10;
  }
  // Strong head-on sun on black IOR-1 bases: only the coat reflects. A rough coat's GGX
  // lobe is flat, a smooth one misses the off-axis eye, and a tilted coat normal lowers N.L.
  if(stage>=31) {light.direction={0,0,-1};light.color={1,1,1,60};light.ambient={0,0,0,1};light.shadows=false;}
  // OBJ: full ambient and no sun, so a surface shows its own colour.
  if(stage>=33) {light.color={0,0,0,1};light.ambient={1,1,1,1};}
  // meshopt: a head-on sun, so decoded (octahedral-filtered) normals affect the result.
  if(stage>=34) {light.color={1,1,1,1};light.ambient={.2f,.2f,.2f,1};}
  if(stage>=35) light.fog_density=0;
  njin::light3d_set(ctx,light);
  njin::material3d surface;surface.unlit=stage<4;surface.texture=checker;surface.specular=0;
  if(stage==1) {surface.height=height;surface.height_scale=.4f;}
  if(stage>=2&&stage<=4) surface.world_uv=.7f;
  if(stage==3||stage==4) {surface.under=under;surface.under_amount=1;}
  if(stage==4) surface.under_normal=under_normal;
  if(stage==5||stage==6) {surface.texture={};surface.lightmap=lightmap;surface.lightmap_intensity=stage==5?1:2;}
  if(stage>=7&&stage<=10) {surface.texture={};surface.pbr=true;surface.roughness=stage==10?.9f:.05f;surface.transmission=stage==7?0:1;surface.thickness=stage>=9?1:0;surface.attenuation_distance=stage>=9?1:0;surface.attenuation_color={1,.1f,.05f,1};}
  if(stage==11) {surface.world_uv=.7f;surface.under=under;surface.under_amount=1;}
  if(stage>=12&&stage<=13) {surface.world_uv=.7f;if(stage==12) surface.normal=green_normal;}
  if(stage>=14) {
    surface.texture={};surface.pbr=true;surface.metallic=0;surface.roughness=.75f;
    if(stage==15) {surface.snow=1;surface.snow_color={.1f,.3f,1,1};}
    if(stage>=16&&stage<=19) surface.glitter=4;
    surface.glitter_metal=stage==17;surface.glitter_lamps=stage==19;
    if(stage==21) {surface.clearcoat=1;surface.clearcoat_roughness=.1f;}
  }
  auto material=njin::model_material_get(ctx,panel,0);material.surface=surface;material.albedo=surface.texture;material.double_sided=true;njin::model_material_set(ctx,panel,0,material);
  njin::camera3d camera;camera.position={2,1,5};camera.target={0,0,0};camera.projection=njin::camera3d_orthographic;camera.ortho_height=4;camera.entities=false;
  if(stage>=22) camera.position={0,2.7f,5};
  if(stage>=28) camera.position={0,0,5};
  njin::begin_3d(ctx,camera);
  if(stage>=35) {
    // draw_sky3d: clear day (no clouds/stars, compared with sky3d_color), overcast day and
    // night driving the light of a cube drawn after it.
    njin::camera3d look;look.position={0,1,0};look.target={0,1,10};look.fovy=60;look.entities=false;
    njin::end_3d(ctx);njin::begin_3d(ctx,look);
    njin::sky3d sky;sky.hour=stage==37?0.f:10.f;sky.weather.clouds=stage==36?1.f:0.f;sky.weather.cloud_darkness=stage==36?.5f:0.f;
    sky.stars=stage==37;sky.drive_light=stage!=35;
    if(stage==38) {sky.hour=10;sky.weather.clouds=1;sky.weather.cloud_darkness=.5f;sky.weather.rain=1;sky.weather.snow=1;}
    if(stage==37&&frame%4==0) {
      // Rain and wetness are ported: the bgfx path accepts them (sky_parity checks their look).
      bool accepted=true;auto wet=sky;wet.weather.rain=.5f;wet.weather.wetness=.5f;
      try {njin::draw_sky3d(ctx,wet);} catch(const std::runtime_error &) {accepted=false;}
      require(accepted,"rain/wetness from draw_sky3d was refused");
    }
    njin::draw_sky3d(ctx,sky);
    if(stage>=36) {njin::material3d plain;njin::material3d_set(ctx,plain);njin::draw_cube3d(ctx,{0,0.2f,4},{1.2f,1.2f,1.2f},{.8f,.8f,.8f,1});}
    njin::end_3d(ctx);
    if(frame%4==2) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
    if(++frame==156) njin::quit(ctx);
    return;
  }
  if(stage>=34) {
    njin::draw_model(ctx,packed,{.position={-1,0,0}});njin::draw_model(ctx,unpacked,{.position={1,0,0}});
    njin::draw_model(ctx,brushed,{.position={0,-6,0}}); // off screen: imported anisotropy must draw
    njin::end_3d(ctx);
    if(frame%4==2) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
    ++frame;
    return;
  }
  if(stage>=33) {
    njin::draw_model(ctx,obj,{});
    njin::end_3d(ctx);
    if(frame%4==2) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
    ++frame;
    return;
  }
  if(stage>=31) {
    for(int i=0;i<2;++i) {
      auto m=coat_materials[i];
      if(stage==32) m.surface.clearcoat=0;
      njin::model_material_set(ctx,coat,i+1,m);
    }
    njin::draw_model(ctx,coat,{});
    njin::end_3d(ctx);
    if(frame%4==2) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
    ++frame;
    return;
  }
  if(stage>=28) {
    njin::material3d background;background.unlit=true;
    njin::material3d_set(ctx,background);
    njin::draw_cube3d(ctx,{0,0,-2},{5,4,.1f},{.9f,.14f,.03f,1});
    auto volume=glass_material;
    if(stage>=29) volume.surface.thickness=0;
    if(stage==30) volume.surface.transmission=0;
    njin::model_material_set(ctx,glass,1,volume);
    njin::draw_model(ctx,glass,{});
    njin::end_3d(ctx);
    if(frame%4==2) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
    ++frame;
    return;
  }
  if(stage>=22) {
    surface.clay=stage==23?1.f:0.f;
    surface.clay_detail=5;
    njin::material3d_set(ctx,surface);
    if(stage<=23) {
      njin::draw_shape3d(ctx,{.kind=njin::shape3d_box,.position={-1.5f,0,0},.rotation={10,20,0},.rounding=.2f},{.9f,.15f,.1f,1});
      njin::draw_shape3d(ctx,{.kind=njin::shape3d_torus,.rotation={20,0,0},.thickness=.18f},{.15f,.9f,.15f,1});
      njin::draw_shape3d(ctx,{.kind=njin::shape3d_cylinder,.position={1.5f,0,0},.rounding=.12f},{.15f,.3f,.9f,1});
    } else {
      const njin::sdf_part parts[]{
        {.a={-.9f,-.1f,0},.b={0,0,0},.ra=.3f,.rb=.39f},
        {.a={0,0,0},.b={.65f,.6f,0},.ra=.39f,.rb=.25f},
      };
      njin::draw_sdf_blend(ctx,parts,2,stage==24?0.f:.35f,{.9f,.45f,.18f,1});
      njin::draw_shape3d(ctx,{.kind=njin::shape3d_capsule,.position={-1.5f,0,0},.radius=.24f,.height=1.2f},{.3f,.7f,.9f,1});
      if(stage>=26) njin::draw_plane3d(ctx,{0,-.75f,0},{5,4},{.7f,.7f,.7f,1});
    }
    njin::end_3d(ctx);
    if(frame%4==2) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
    ++frame;
    return;
  }
  if(stage==18||stage==19) {
    njin::light3d_source lamp;lamp.position={1,3,3};lamp.radius=12;lamp.intensity=2;
    njin::light3d_add(ctx,lamp);
  }
  if(stage>=7&&stage<=10) {njin::material3d background;background.unlit=true;background.texture=checker;njin::material3d_set(ctx,background);njin::draw_cube3d(ctx,{0,0,-2},{8,4,.1f},{1,1,1,1});}
  njin::material3d_set(ctx,surface);
  njin::draw_model(ctx,panel,{.position={-1.5f,0,0}});njin::draw_cube3d(ctx,{0,0,0},{1,1,1},{1,1,1,1});njin::draw_instanced3d(ctx,panel,instances,0,1);
  if(stage>=14) njin::draw_plane3d(ctx,{0,-.6f,0},{4,4},{.2f,.2f,.2f,1});
  bool retained=false;
  if(stage==5) {try {njin::texture_unload(ctx,lightmap);} catch(const std::logic_error &) {retained=true;}require(retained,"queued lightmap was released");}
  njin::end_3d(ctx);
  if(frame%4==2) njin::screenshot(ctx,(capture+"."+std::to_string(stage)+".png").c_str());
  ++frame;
}
void shutdown(njin::context &ctx) {
  const auto im=[](unsigned i){return njin::renderer_load_image((capture+"."+std::to_string(i)+".png").c_str());};
  const auto changed=[](const auto &a,const auto &b){unsigned count=0;for(size_t p=0;p<a.pixels.size();p+=4) count+=std::abs(int(a.pixels[p])-int(b.pixels[p]))>5||std::abs(int(a.pixels[p+1])-int(b.pixels[p+1]))>5||std::abs(int(a.pixels[p+2])-int(b.pixels[p+2]))>5;return count;};
  require(changed(im(0),im(1))>300,"parallax map had no visible effect");
  require(changed(im(0),im(2))>500,"world UV projection had no visible effect");
  const auto exposed=im(3);unsigned blue=0;for(size_t p=0;p<exposed.pixels.size();p+=4) blue+=exposed.pixels[p+2]>exposed.pixels[p]*2&&exposed.pixels[p+2]>80;
  require(blue>1000,"under layer missing on model/primitive/instance");
  require(changed(im(3),im(4))>300,"lit under normal stage had no visible effect");
  const auto baked=im(5);unsigned red=0;for(size_t p=0;p<baked.pixels.size();p+=4) red+=baked.pixels[p]>baked.pixels[p+1]*2&&baked.pixels[p]>80;
  require(red>1000&&changed(baked,im(6))>500,"baked lighting or intensity missing");
  require(changed(im(7),im(8))>1000,"transmission did not reveal the opaque scene");
  require(changed(im(8),im(9))>500,"volume attenuation/refraction missing");
  require(changed(im(9),im(10))>100,"rough transmission filtering missing");
  require(changed(im(4),im(11))>100,"under-layer normal map did not affect lighting");
  const auto up=im(12),flat=im(13);unsigned brighter=0;
  for(size_t p=0;p<up.pixels.size();p+=4) brighter+=int(up.pixels[p+1])>int(flat.pixels[p+1])+15;
  require(brighter>200,"world-projected normal green axis does not face upward");
  require(changed(im(14),im(15))>500,"mesh snow coverage missing");
  require(changed(im(14),im(16))>5,"sun did not illuminate glitter");
  require(im(14).pixels==im(17).pixels,"metal-only glitter changed a nonmetallic surface");
  require(changed(im(18),im(19))>5,"local light did not illuminate glitter");
  require(changed(im(20),im(21))>300,"clearcoat did not reflect the environment with direct lights disabled");
  const auto shapes=im(22);
  unsigned red_shape=0,green_shape=0,blue_shape=0;
  for(size_t i=0;i<shapes.pixels.size();i+=4) {
    const auto *p=shapes.pixels.data()+i;
    red_shape+=p[0]>p[1]+20&&p[0]>p[2]+20;
    green_shape+=p[1]>p[0]+20&&p[1]>p[2]+20;
    blue_shape+=p[2]>p[0]+20&&p[2]>p[1]+20;
  }
  require(red_shape>100&&green_shape>100&&blue_shape>100,"rounded box, torus or cylinder missing");
  require(changed(shapes,im(23))>150,"clay had no visible effect");
  require(changed(im(24),im(25))>100,"smooth-min did not join the SDF parts");
  require(changed(im(26),im(27))>30,"SDF meshes did not cast shadows");
  const auto volume=im(28),thin=im(29),opaque=im(30);
  const auto sample=[](const auto &image,float x,float y,int channel=0) {
    float w=float(image.width),h=float(image.height),left=0,top=0;
#if defined(__ANDROID__)
    const float fit=std::floor(std::min(w/320,h/240));
    left=(w-320*fit)*.5f;top=(h-240*fit)*.5f;w=320*fit;h=240*fit;
#endif
    const auto p=(size_t(top+y*h)*image.width+size_t(left+x*w))*4;
    return int(image.pixels[p+size_t(channel)]);
  };
  require(sample(thin,.43f,.4f)>sample(opaque,.43f,.4f)+35,"glTF transmission R or transformed UV1 missing");
  require(std::abs(sample(thin,.57f,.4f)-sample(opaque,.57f,.4f))<10,"glTF transmission mask leaked into its zero region");
  require(std::abs(sample(volume,.43f,.4f)-sample(thin,.43f,.4f))<10&&
          sample(volume,.43f,.6f)+35<sample(thin,.43f,.6f),"glTF volume thickness G or attenuation missing");
  // Quads at x=-1 and x=1 (0.8 scale); quarter centres in the 320x240 orthographic view.
  const auto coated=im(31),bare=im(32);
  const float q0l=.2375f,q0r=.3875f,q1l=.6125f,q1r=.7625f,top=.4f,bottom=.6f;
  require(sample(coated,q0l,bottom)>sample(bare,q0l,bottom)+60,"glTF clearcoat R or rough roughness G missing");
  require(sample(coated,q0l,bottom)>sample(coated,q0l,top)+60,"glTF clearcoat roughness G had no effect");
  require(std::abs(sample(coated,q0r,top)-sample(bare,q0r,top))<10&&std::abs(sample(coated,q0r,bottom)-sample(bare,q0r,bottom))<10,
          "glTF clearcoat R mask leaked into its zero region");
  require(sample(coated,q1l,bottom)>sample(bare,q1l,bottom)+60,"glTF clearcoat transformed R or second binding missing");
  require(std::abs(sample(coated,q1l,top)-sample(bare,q1l,top))<10,"glTF clearcoat texture transform ignored");
  require(sample(coated,q1l,bottom)>sample(coated,q1r,bottom)+40&&sample(coated,q1r,bottom)>sample(bare,q1r,bottom)+5,
          "glTF clearcoat normal map did not tilt the coat");
  // OBJ quads: Kd red at x=-1, map_Kd (green top row, blue bottom row after the V flip)
  // at x=1, and an unknown usemtl drawn with material 0 above the gap.
  const auto objs=im(33);
  const auto dominant=[&](float x,float y,int c) {
    const int v=sample(objs,x,y,c);
    for(int k=0;k<3;++k) if(k!=c&&v<sample(objs,x,y,k)+60) return false;
    return true;
  };
  require(dominant(.3125f,.5f,0),"OBJ Kd material not red");
  require(dominant(.6875f,.4f,1)&&dominant(.6875f,.6f,2),"OBJ map_Kd texture missing or not V-flipped");
  require(dominant(.5f,.15f,0),"OBJ face with an unknown usemtl did not use material 0");
  // meshopt-decoded quad (left) matches the uncompressed copy (right).
  const auto mo=im(34);
  for(float y:{.35f,.5f,.65f}) for(int c=0;c<3;++c)
    require(std::abs(sample(mo,.3125f,y,c)-sample(mo,.6875f,y,c))<=3,"meshopt-decoded glTF differs from the uncompressed copy");
  require(sample(mo,.3125f,.5f,0)>sample(mo,.3125f,.5f,2)+60&&sample(mo,.5f,.5f,0)<10,"meshopt quad missing or misplaced");
  njin::model_unload(ctx,packed);njin::model_unload(ctx,unpacked);njin::model_unload(ctx,brushed);
  // Sky pixels match the CPU sky3d_color() reference (same formula, no clouds/stars).
  const auto clear=im(35),overcast=im(36),night=im(37);
  njin::sky3d day;day.hour=10;day.weather.clouds=0;day.stars=false;
  const float t=std::tan(30.f*3.14159265f/180.f),aspect=320.f/240.f;
  for(const auto &at:std::initializer_list<std::array<float,2>>{{.5f,.45f},{.5f,.1f},{.15f,.2f},{.85f,.3f}}) {
    const njin::vec3 dir{(at[0]*2-1)*t*aspect*-1,(1-at[1]*2)*t,1};
    const auto expected=njin::sky3d_color(day,dir);
    const float want[]{expected.r,expected.g,expected.b};
    for(int c=0;c<3;++c) require(std::abs(sample(clear,at[0],at[1],c)-int(want[c]*255+.5f))<=8,"draw_sky3d colour differs from sky3d_color");
  }
  require(std::abs(sample(overcast,.5f,.1f,0)-sample(clear,.5f,.1f,0))+std::abs(sample(overcast,.5f,.1f,2)-sample(clear,.5f,.1f,2))>30,"overcast clouds had no effect");
  require(sample(night,.5f,.2f,2)<60,"night sky is not dark");
  require(sample(overcast,.5f,.62f,0)>sample(night,.5f,.62f,0)+40,"draw_sky3d drive_light did not light the cube");
  // Stage 38: stage 36 (overcast, lit cube) with rain and snow (cover_auto map on).
  const auto stormy=im(38);
  unsigned streaks=0;
  // Only the ground below the horizon (no drifting clouds), beside the cube.
  for(unsigned y=stormy.height*3/4;y<stormy.height;++y) for(unsigned x=0;x<stormy.width;++x) {
    if(x>stormy.width*2/5&&x<stormy.width*3/5) continue;
    const size_t p=(size_t(y)*stormy.width+x)*4;
    streaks+=std::abs(int(stormy.pixels[p])-int(overcast.pixels[p]))>20;
  }
  require(streaks>100,"rain/snow from draw_sky3d was not drawn");
  njin::instance_buffer_destroy(ctx,instances);njin::model_unload(ctx,panel);njin::model_unload(ctx,glass);njin::model_unload(ctx,coat);njin::model_unload(ctx,obj);
  for(auto t:{checker,height,lightmap,under,under_normal,green_normal,environment}) njin::texture_unload(ctx,t);
}
void setup(njin::context &ctx) {njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin advanced material maps",.width=320,.height=240,.target_fps=120};cfg.audio=false;cfg.clear_bg_color={0,0,0,1};cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
#if defined(__ANDROID__)
    cfg.virtual_size={320,240};
#endif
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) require(njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend),"invalid renderer");else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
    capture=(std::filesystem::absolute(argv[0]).parent_path()/"captures"/("advanced-materials-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();std::filesystem::create_directories(std::filesystem::path(capture).parent_path());
    for(unsigned repeat=0;repeat<2;++repeat) {frame=0;std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);njin::mod_register(*ctx,{.name="advanced material regression",.setup=setup});njin::run(*ctx);}return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
