#include <njin.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
namespace {
using namespace njin;
namespace fs=std::filesystem;
fs::path root,assets;
std::string backend;
int frame=0,stage=0,at=0;
unsigned attempt=0;
stream_config limits;
stream_handle mip,lod,static_lod;
texture_handle reference,virtual_image;
model_handle reference_model,reference_static,patch,wrapping;
terrain3d_handle virtual_ground,reference_ground;
bool sync=false,saw_mip=false,saw_lod=false,saw_static=false;
usize fine_mip_bytes=0,fine_lod_bytes=0,peak_upload=0;
usize coarse_mip_bytes=0,coarse_lod_bytes=0;
u32 max_difference=0;
void require(bool ok,const char *what) {if(!ok) throw std::runtime_error(what);}
void image_file(const fs::path &path) {
  constexpr unsigned size=512,row=size*3,total=54+size*row;
  std::ofstream file(path,std::ios::binary);unsigned char header[54]{};
  const auto put=[&](unsigned at,unsigned n){for(unsigned i=0;i<4;++i) header[at+i]=u8(n>>(i*8));};
  header[0]='B';header[1]='M';put(2,total);put(10,54);put(14,40);put(18,size);put(22,size);header[26]=1;header[28]=24;
  file.write(reinterpret_cast<char*>(header),54);
  for(unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x) {
    const unsigned char pixel[]{u8((x/8+y/8)%2?220:40),u8(y*255/(size-1)),u8(x*255/(size-1))};
    file.write(reinterpret_cast<const char*>(pixel),3);
  }
}
void next() {++stage;at=frame;}
bool ready(context &c,stream_handle h) {return streaming_info(c,h).state==stream_state::ready;}
void startup(context &c) {
  frame=stage=at=0;sync=saw_mip=saw_lod=saw_static=false;peak_upload=0;
  limits={};limits.upload_bytes_per_frame=2*1024*1024;limits.upload_units_per_frame=1;limits.test_delay_ms=20;limits.idle_frames=10000;
  streaming_configure(c,limits);image_file(root/"source.bmp");
  virtual_texture_cook((root/"source.bmp").string().c_str(),(root/"source.nvt").string().c_str(),32);
  mip=texture_request_mips(c,(root/"source.bmp").string().c_str(),50);streaming_pin(c,mip);
  lod=model_request_lods(c,(assets/"lod-animated-grid.gltf").string().c_str(),{.levels=2},40);streaming_pin(c,lod);
  {
    std::ofstream obj(root/"grid.obj");constexpr int n=16;
    for(int y=0;y<n;++y) for(int x=0;x<n;++x) obj<<"v "<<x*2.f/(n-1)-1<<" "<<y*2.f/(n-1)-1<<" 0"<<std::endl;
    for(int y=0;y<n;++y) for(int x=0;x<n;++x) obj<<"vt "<<x/float(n-1)<<" "<<y/float(n-1)<<std::endl;
    obj<<"vn 0 0 1"<<std::endl;
    for(int y=0;y<n-1;++y) for(int x=0;x<n-1;++x) {obj<<"f";for(int v:{y*n+x+1,y*n+x+2,(y+1)*n+x+2,(y+1)*n+x+1}) obj<<" "<<v<<"/"<<v<<"/1";obj<<std::endl;}
  }
  static_lod=model_request_lods(c,(root/"grid.obj").string().c_str(),{.levels=2},40);streaming_pin(c,static_lod);
  reference_static=model_load(c,(root/"grid.obj").string().c_str());
  virtual_image=virtual_texture_load(c,(root/"source.nvt").string().c_str(),{.cache_side=4,.idle_frames=2});
  reference=texture_load(c,(root/"source.bmp").string().c_str());texture_set_filter(c,reference,filter_mipmap);
  reference_model=model_load(c,(assets/"lod-animated-grid.gltf").string().c_str());
  wrapping=model_load(c,(assets/"virtual-wrap.gltf").string().c_str());require(wrapping.id,"virtual wrap fixture failed to load");
  const vec3 positions[]{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};
  const vec2 uv[]{{.03125f,.09375f},{.09375f,.09375f},{.09375f,.03125f},{.03125f,.03125f}};
  const u32 indices[]{0,1,2,0,2,3};
  patch=model_create(c,{.positions=positions,.texcoords=uv,.vertex_count=4,.indices=indices,.index_count=6});
  std::array<float,9*9> height{};terrain3d_desc terrain;terrain.origin={.5f,-1,.5f};terrain.size=2;terrain.resolution=9;terrain.heights=height.data();terrain.auto_splat=false;terrain.collision=false;
  terrain.layers[0].albedo=virtual_image;terrain.layers[0].tile=32;terrain.cast_shadows=false;virtual_ground=terrain3d_create(c,terrain);
  terrain.layers[0].albedo=reference;reference_ground=terrain3d_create(c,terrain);
}
void update(context &c) {
  const auto m=streaming_info(c,mip),l=streaming_info(c,lod),s=streaming_info(c,static_lod);
  const auto stats=streaming_stats(c);peak_upload=std::max(peak_upload,stats.uploaded_bytes);
  require(stats.uploaded_bytes<=limits.upload_bytes_per_frame,"residency upload cap exceeded");
  if(m.state==stream_state::failed) throw std::runtime_error(m.error);
  if(l.state==stream_state::failed) throw std::runtime_error(l.error);
  if(s.state==stream_state::failed) throw std::runtime_error(s.error);
  saw_mip|=m.renderable&&m.resident_level>0&&m.resident_level!=~u32(0);
  saw_lod|=l.renderable&&l.resident_level>0&&l.resident_level!=~u32(0);
  saw_static|=s.renderable&&s.resident_level>0&&s.resident_level!=~u32(0);
  if(frame>1800) throw std::runtime_error("residency timeout stage="+std::to_string(stage)+" mip="+m.error+" lod="+l.error+" vt="+virtual_texture_get(c,virtual_image).error);
  if(stage<7) virtual_texture_request(c,virtual_image,{0,0},{.125f,.125f},0,20);
  const auto vt=virtual_texture_get(c,virtual_image);require(vt.error.empty(),vt.error.c_str());
  if(stage==0&&ready(c,mip)&&ready(c,lod)&&ready(c,static_lod)&&vt.tail_ready&&!vt.pending) {
    require(saw_mip,"coarse mip was never published");require(saw_lod,"coarse animated LOD was never published");
    if(!saw_static) {
      streaming_detail(c,static_lod,~u32(0));
      throw std::runtime_error("coarse static LOD was never published; stream_max="+std::to_string(streaming_info(c,static_lod).target_level)+" synchronous_levels="+std::to_string(model_lod_build(c,reference_static,{.levels=2})));
    }
    require(m.resident_level==0&&l.resident_level==0&&s.resident_level==0,"fine level not resident");
    require(model_anim_count(c,streaming_model(c,lod))==model_anim_count(c,reference_model),"LOD stream lost animation clips");
    fine_mip_bytes=m.gpu_bytes;fine_lod_bytes=l.gpu_bytes;next();
  } else if(stage==1&&frame>at+3) {sync=true;next();}
  else if(stage==2&&frame>at+3) {
    const auto a=renderer_load_image((root/(backend+"-resident.png")).string().c_str());
    const auto b=renderer_load_image((root/(backend+"-reference.png")).string().c_str());
    max_difference=0;for(size_t i=0;i<a.pixels.size();++i) max_difference=std::max(max_difference,u32(std::abs(int(a.pixels[i])-int(b.pixels[i]))));
    require(max_difference<=2,"virtual terrain/model or mip/LOD differs from reference");
    sync=false;streaming_detail(c,mip,4);streaming_detail(c,lod,2);streaming_detail(c,static_lod,2);next();
  } else if(stage==3&&ready(c,mip)&&ready(c,lod)&&ready(c,static_lod)) {
    coarse_mip_bytes=m.gpu_bytes;coarse_lod_bytes=l.gpu_bytes;
    require(m.gpu_bytes<fine_mip_bytes&&l.gpu_bytes<fine_lod_bytes,"coarsening failed to release physical resources");
    limits.upload_bytes_per_frame=16;streaming_configure(c,limits);streaming_detail(c,mip,0);streaming_detail(c,lod,0);streaming_detail(c,static_lod,0);next();
  } else if(stage==4&&frame>at+4) {
    require(m.renderable&&l.renderable&&s.renderable&&m.resident_level>0&&l.resident_level>0&&s.resident_level>0,"budget dropped resident fallback");
    require(!m.error.empty()&&!l.error.empty(),"detail budget wait not observable");
    limits.upload_bytes_per_frame=2*1024*1024;streaming_configure(c,limits);next();
  } else if(stage==5&&ready(c,mip)&&ready(c,lod)&&ready(c,static_lod)) {
    streaming_cancel(c,mip);streaming_cancel(c,lod);streaming_cancel(c,static_lod);next();
  } else if(stage==6) {
    require(!m.renderable&&!l.renderable&&!s.renderable,"cancel retained published residency");
    streaming_retry(c,mip);streaming_retry(c,lod);streaming_retry(c,static_lod);next();
  } else if(stage==7&&ready(c,mip)&&ready(c,lod)&&ready(c,static_lod)) {next();}
  else if(stage==8) {
    const unsigned tile=unsigned((frame-at)/14)%16;
    streaming_detail(c,lod,u32((frame-at)/20)%3);
    streaming_detail(c,static_lod,u32((frame-at)/20)%3);
    virtual_texture_request(c,virtual_image,{tile/16.f,.25f},{(tile+1)/16.f,.3125f},0,30);
    if(frame>at+240) {require(vt.evictions>0,"virtual page cache never evicted");next();}
  } else if(stage==9) {
    virtual_texture_request(c,virtual_image,{0,0},{.125f,.125f},0,40);
    if(vt.pending==0&&frame>at+50) {
      // Shutdown with range reads pending and verify unload invalidates their weak owner.
      auto abandoned=virtual_texture_load(c,(root/"source.nvt").string().c_str());texture_unload(c,abandoned);
      quit(c);
    }
  }
}
void apply(context &c,model_handle model,texture_handle image) {
  for(int i=0;i<model_material_count(c,model);++i) {auto m=model_material_get(c,model,i);m.albedo=image;m.double_sided=true;m.surface.unlit=true;model_material_set(c,model,i,m);}
}
void render(context &c) {
  light3d light;light.hdr=false;light.shadows=stage>=8;light.shadow_size=256;light.tonemap=tonemap3d_none;light.ambient={1,1,1,1};light.color=stage>=8?rgba{1,1,1,1}:rgba{0,0,0,1};light3d_set(c,light);
  graphics_quality quality;quality.lod_scale=stage>=8?1.f:0.f;graphics_quality_set(c,quality);
  camera3d camera;camera.position={0,3,7};camera.target={0,0,0};camera.entities=false;camera.fovy=45;
  if(stage==8) {camera.position.x=std::sin(frame*.15f)*4;camera.position.z=5+std::abs(std::sin(frame*.1f))*16;}
  const auto model=sync?reference_model:streaming_model(c,lod);
  if(model.id) apply(c,model,sync?reference:streaming_texture(c,mip));
  apply(c,patch,sync?reference:virtual_image);
  apply(c,wrapping,sync?reference:virtual_image);
  const auto static_model=sync?reference_static:streaming_model(c,static_lod);apply(c,static_model,sync?reference:streaming_texture(c,mip));
  begin_3d(c,camera);draw_model(c,model,{.position={-2,0,0},.scale={.7f,.7f,.7f}});
  draw_model(c,patch,{.position={0,1,0},.scale={.6f,.6f,.6f}});
  draw_model(c,wrapping,{.position={1.6f,1,0},.scale={.5f,.5f,.5f}});
  draw_model(c,static_model,{.position={-2,-1.6f,0},.scale={.7f,.7f,.7f}});
  draw_terrain3d(c,sync?reference_ground:virtual_ground);end_3d(c);
  if(stage==1&&frame==at+1) screenshot(c,(root/(backend+"-resident.png")).string().c_str());
  if(stage==2&&frame==at+1) screenshot(c,(root/(backend+"-reference.png")).string().c_str());
  ++frame;
}
void shutdown(context &c) {
  const auto vt=virtual_texture_get(c,virtual_image);
  std::printf("RESIDENCY PASS backend=%s frames=%d mip_partial=%d lod_partial=%d upload_peak=%zu mip_fine_bytes=%zu lod_fine_bytes=%zu virtual_bytes=%zu pages=%u hits=%llu evictions=%llu max_image_difference=%u\n",backend.c_str(),frame,saw_mip,saw_lod,peak_upload,fine_mip_bytes,fine_lod_bytes,vt.gpu_bytes,vt.resident,(unsigned long long)vt.hits,(unsigned long long)vt.evictions,max_difference);
  std::ofstream metrics(root/(backend+"-metrics.txt"),std::ios::app);
  metrics<<"run="<<attempt<<" frames="<<frame<<" mip_fine="<<fine_mip_bytes<<" mip_coarse="<<coarse_mip_bytes
    <<" lod_fine="<<fine_lod_bytes<<" lod_coarse="<<coarse_lod_bytes<<" virtual_allocated="<<vt.gpu_bytes
    <<" upload_peak="<<peak_upload<<" pages="<<vt.resident<<" evictions="<<vt.evictions<<" max_difference="<<max_difference<<std::endl;
  terrain3d_destroy(c,virtual_ground);terrain3d_destroy(c,reference_ground);model_unload(c,patch);model_unload(c,wrapping);model_unload(c,reference_model);model_unload(c,reference_static);texture_unload(c,reference);texture_unload(c,virtual_image);
  streaming_release(c,mip);streaming_release(c,lod);streaming_release(c,static_lod);
}
void setup(context &c) {ecs_register(c,phase_startup,startup);ecs_register(c,phase_update,update);ecs_register(c,phase_render,render);ecs_register(c,phase_shutdown,shutdown);}
}
int main(int argc,char **argv) {
  try {
    config config{.title="Njin mip LOD virtual residency",.width=640,.height=480,.target_fps=240};config.audio=false;config.graphics_backend=GraphicsBackend::Vulkan;config.clear_bg_color={.02f,.03f,.04f,1};
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) require(parse_graphics_backend(argv[i]+11,config.graphics_backend),"invalid renderer");else if(std::strcmp(argv[i],"--gpu-validation")==0) config.graphics_validation=true;
    backend=graphics_backend_name(config.graphics_backend);const auto bin=fs::absolute(argv[0]).parent_path();assets=bin/"model-test-assets";root=bin/"residency-fixtures";fs::create_directories(root);
    fs::remove(root/(backend+"-metrics.txt"));
    for(attempt=0;attempt<2;++attempt) {std::unique_ptr<context,void(*)(context*)> c(create(config),destroy);mod_register(*c,{.name="residency regression",.setup=setup});run(*c);}
    return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"RESIDENCY FAIL: %s\n",e.what());return 1;}
}
