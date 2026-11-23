// Đối chứng hình ảnh với culling tắt; số visible đọc từ GPU, không ước tính CPU.
#include <njin.h>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(NJIN_OCCLUSION_TIMING)
#include <bgfx/bgfx.h>
#endif
namespace {
unsigned frame=0;
bool benchmark=false;
std::uint64_t warm_allocations=0;
std::string capture;
std::string assets;
void report(const char *format,...) {
  char text[4096];va_list args;va_start(args,format);
  const int size=std::vsnprintf(text,sizeof(text),format,args);va_end(args);
  if(size<0||size>=int(sizeof(text))) throw std::runtime_error("occlusion diagnostic overflow");
  std::fputs(text,stdout);
  std::ofstream(capture+"-checks.txt",std::ios::app)<<text;
}
njin::model_handle wall,masked,animated,authored;
njin::instance_buffer_handle instances;
std::array<njin::render_texture_handle,2> target;
void require(bool value,const char *message) {if(!value) throw std::runtime_error(message);}
struct Measurement {double submit=0,cpu=0,gpu=0;unsigned samples=0,gpu_samples=0;std::uint64_t allocations=0,dispatches=0;};
std::array<Measurement,6> measurements;
unsigned previous_gpu_frame=~0u;
void startup(njin::context &ctx) {
  frame=0;warm_allocations=0;measurements={};previous_gpu_frame=~0u;
  const auto diagnostics=njin::renderer_gpu_occlusion_diagnostics(ctx);
  report("%s\n",diagnostics.c_str());
  if(!njin::renderer_gpu_occlusion_available(ctx)) throw std::runtime_error(diagnostics);
  const njin::vec3 vertices[]{{-3,-3,0},{3,-3,0},{3,3,0},{-3,3,0}};
  const njin::u32 indices[]{0,1,2,0,2,3};
  wall=njin::model_create(ctx,{.positions=vertices,.vertex_count=4,.indices=indices,.index_count=6});
  auto material=njin::model_material_get(ctx,wall,0);material.surface.unlit=true;material.double_sided=true;
  njin::model_material_set(ctx,wall,0,material);
  instances=njin::instance_buffer_create(ctx,16);
  target[0]=njin::render_texture_load(ctx,320,240);target[1]=njin::render_texture_load(ctx,384,256);
  masked=njin::model_load(ctx,(std::filesystem::path(assets)/"oit-mask.gltf").string().c_str());
  animated=njin::model_load(ctx,(std::filesystem::path(assets)/"lod-animated-grid.gltf").string().c_str());
  auto am=njin::model_material_get(ctx,animated,0);am.surface.unlit=true;am.double_sided=true;njin::model_material_set(ctx,animated,0,am);
  require(njin::model_lod_build(ctx,animated,{.levels=2,.screen=.25f})==2,"animated LOD fixture did not simplify");
  const njin::vec3 quad[]{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};
  authored=njin::model_create(ctx,{.positions=quad,.vertex_count=4,.indices=indices,.index_count=6});
  auto sm=njin::model_material_get(ctx,authored,0);sm.surface.unlit=true;sm.double_sided=true;njin::model_material_set(ctx,authored,0,sm);
  const auto triangle=(std::filesystem::path(assets)/"lod-triangle.gltf").string();const char *paths[]{triangle.c_str()};
  require(njin::model_lod_load(ctx,authored,paths,1,.25f)==1,"authored silhouette LOD fixture did not load");
}
void measure(njin::context &ctx) {
  const auto started=std::chrono::steady_clock::now();const unsigned block=frame/240,step=frame%240;const bool enabled=block%2;
  std::vector<float> records(4096*16);
  for(unsigned i=0;i<4096;++i) {auto *p=records.data()+i*16;p[0]=(int(i%64)-32)*.08f;p[1]=(int(i/64)-32)*.08f;p[2]=-3;p[3]=.025f;p[4]=1;p[7]=1;}
  njin::instance_buffer_upload(ctx,instances,records.data(),4096);
  njin::render3d_options options;options.occlusion=enabled;njin::render3d_options_set(ctx,options);
  njin::light3d light;light.shadows=true;light.shadow_size=128;light.fog_density=0;njin::light3d_set(ctx,light);
  njin::camera3d camera;camera.position={0,0,10};camera.target={0,0,0};camera.projection=njin::camera3d_orthographic;camera.ortho_height=10;camera.entities=false;
  const auto before=njin::renderer_resource_stats(ctx);
  njin::begin_3d(ctx,camera,target[0],{1,0,1,1});njin::draw_model(ctx,wall,{});
  njin::draw_instanced3d(ctx,njin::mesh3d_cube,instances,njin::instanced3d_desc{.cull=true,.lod=false,.radius=.9f});njin::end_3d(ctx);
  njin::render_texture_draw(ctx,target[0],{},{1,1,1,1});
  const auto after=njin::renderer_resource_stats(ctx);
  if(step>=80&&step<239) {
    auto &m=measurements[block];++m.samples;m.submit+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    m.allocations+=after.occlusion_buffer_allocations-before.occlusion_buffer_allocations;m.dispatches+=after.occlusion_dispatches-before.occlusion_dispatches;
#if defined(NJIN_OCCLUSION_TIMING)
    const auto *stats=bgfx::getStats();if(stats->cpuTimerFreq>0) m.cpu+=1000.0*stats->cpuTimeFrame/stats->cpuTimerFreq;
    if(stats->gpuTimerFreq>0&&stats->gpuTimeEnd>stats->gpuTimeBegin&&stats->gpuFrameNum!=previous_gpu_frame) {
      previous_gpu_frame=stats->gpuFrameNum;m.gpu+=1000.0*(stats->gpuTimeEnd-stats->gpuTimeBegin)/stats->gpuTimerFreq;++m.gpu_samples;
    }
#endif
  }
  if(step==239) {
    const auto &m=measurements[block];
    report("BENCH block %u culling %u records 4096 samples %u submit_ms %.6f cpu_frame_ms %.6f gpu_ms %.6f gpu_samples %u cull_allocations %llu cull_dispatches_per_frame %.2f\n",block,enabled,m.samples,m.submit/m.samples,m.cpu/m.samples,m.gpu_samples?m.gpu/m.gpu_samples:-1,m.gpu_samples,(unsigned long long)m.allocations,double(m.dispatches)/m.samples);
    if(enabled) {const auto counts=njin::renderer_occlusion_readback(ctx);report("BENCH GPU input %llu visible %llu\n",(unsigned long long)counts[0],(unsigned long long)counts[1]);}
  }
  if(++frame==1440) njin::quit(ctx);
}
void advanced(njin::context &ctx) {
  const unsigned stage=frame/5;const bool occlusion=stage%2!=0;
  njin::render3d_options options;options.occlusion=occlusion;njin::render3d_options_set(ctx,options);
  njin::light3d light;light.shadows=true;light.shadow_size=128;light.shadow_range=100;light.fog_density=0;light.direction={1,0,-1};njin::light3d_set(ctx,light);
  const bool silhouette=stage==16||stage==17;
  auto wall_material=njin::model_material_get(ctx,wall,0);wall_material.surface.unlit=!silhouette;wall_material.surface.cast_shadows=!silhouette;njin::model_material_set(ctx,wall,0,wall_material);
  const unsigned views=stage==18||stage==19?2:1;
  const auto before=njin::renderer_resource_stats(ctx);
  for(unsigned view=0;view<views;++view) {
    njin::camera3d camera;camera.position=stage>=16?njin::vec3{1,1,6}:njin::vec3{0,0,10};camera.target={0,0,0};
    camera.entities=false;camera.projection=stage>=16?njin::camera3d_perspective:njin::camera3d_orthographic;camera.ortho_height=10;
    if(view) {camera.position={4,0,10};camera.target={1,0,0};}
    std::array<float,48> records{};
    for(unsigned i=0;i<3;++i) {auto *p=records.data()+i*16;p[0]=float(i)*2-2;p[2]=-3;p[3]=i==0?.8f:i==1?.3f:.05f;p[4+i]=1;p[7]=1;}
    njin::instance_buffer_upload(ctx,instances,records.data(),3);
    if(stage>=20) {
      camera.position={0,0,10};camera.projection=njin::camera3d_orthographic;
      std::vector<float> large(129*16);
      for(unsigned i=0;i<129;++i) {auto *p=large.data()+i*16;p[0]=(int(i%43)-21)*.2f;p[1]=float(i/43)*.4f-.4f;p[2]=-3;p[3]=.05f;p[4]=1;p[7]=1;}
      njin::instance_buffer_upload(ctx,instances,large.data(),129);
    }
    njin::begin_3d(ctx,camera,target[view],{1,0,1,1});
    if(silhouette) njin::draw_model(ctx,wall,{.position={0,0,-4}}, {.8f,.8f,.8f,1});
    if(stage==10||stage==11) njin::draw_model(ctx,masked,{.scale={3,3,3}});
    if(stage==12||stage==13) {
      njin::fx3d_set(ctx,{.dissolve=.6f});njin::draw_model(ctx,wall,{});njin::fx3d_set(ctx,{});
    }
    if(stage>=20) {
      njin::draw_model(ctx,wall,{});
      njin::draw_instanced3d(ctx,njin::mesh3d_cube,instances,njin::instanced3d_desc{.cull=true,.lod=false,.radius=.9f});
    } else if(stage>=14) {
      const njin::model_pose pose{.anim=0,.time=.5f,.loop=false};
      njin::draw_instanced3d(ctx,silhouette?authored:animated,instances,njin::instanced3d_desc{.cull=true,.lod=true,.radius=3,.pose=silhouette?nullptr:&pose});
    } else njin::draw_instanced3d(ctx,njin::mesh3d_cube,instances,njin::instanced3d_desc{.cull=true,.lod=false,.radius=.9f});
    njin::end_3d(ctx);
  }
  const auto after=njin::renderer_resource_stats(ctx);
  if(occlusion&&frame%5==1) warm_allocations=after.occlusion_buffer_allocations;
  if(occlusion&&frame%5>=2) require(after.occlusion_buffer_allocations==warm_allocations,"advanced steady scene reallocated culling buffers");
  require(after.occlusion_pool_slots<=16,"multiple views grew the culling pool without bound");
  if(occlusion&&frame%5==3) {
    const auto count=njin::renderer_occlusion_readback(ctx);
    report("Advanced stage %u GPU input %llu visible %llu dispatches %llu allocations %llu pool bytes %llu\n",stage,(unsigned long long)count[0],(unsigned long long)count[1],(unsigned long long)(after.occlusion_dispatches-before.occlusion_dispatches),(unsigned long long)(after.occlusion_buffer_allocations-before.occlusion_buffer_allocations),(unsigned long long)after.occlusion_pool_bytes);
    require(count[1]>0&&count[1]<=count[0],"advanced GPU compaction has no visible objects");
    if(stage>=15&&stage<20) require(count[1]==3*views+(silhouette?1:0),"GPU LOD did not emit each visible instance and receiver once");
    if(stage>=20) require(count[0]==130&&count[1]>1&&count[1]<130,"multi-workgroup compaction lost exposed instances or retained all hidden instances");
  }
  if(frame%5==3) for(unsigned view=0;view<views;++view)
    require(njin::render_texture_save(ctx,target[view],(capture+"-"+std::to_string(stage)+(view?"-view1":"")+".png").c_str()),"advanced capture failed");
  njin::render_texture_draw(ctx,target[0],{},{1,1,1,1});++frame;
}
void render(njin::context &ctx) {
  if(benchmark) {measure(ctx);return;}
  if(frame>=50&&frame<110) {advanced(ctx);return;}
  if(frame>=110) {
    if(++frame==235) {
      const auto stats=njin::renderer_resource_stats(ctx);
      require(stats.occlusion_pool_slots==0&&stats.occlusion_pool_bytes==0,"idle GPU culling pool did not expire");
      njin::quit(ctx);
    }
    return;
  }
  const unsigned stage=frame/5;const bool occlusion=stage%2!=0;
  std::array<float,13*16> data{};
  for(unsigned i=0;i<13;++i) {
    auto *p=data.data()+i*16;p[0]=i==12?40.f:i==11?5.f:(float(i%3)-1)*.1f;
    p[1]=(float(i%2)-.5f)*.1f;p[2]=-3;p[3]=.25f;p[4]=.1f;p[5]=.35f;p[6]=1;p[7]=1;
    if(stage>=6) {p[8]=15;p[9]=35;p[12]=i%2?-1.f:1.f;p[13]=1.5f;p[14]=1;}
  }
  njin::instance_buffer_upload(ctx,instances,data.data(),13);
  njin::render3d_options options;options.occlusion=occlusion;njin::render3d_options_set(ctx,options);
  njin::light3d light;light.shadows=true;light.shadow_size=128;light.shadow_range=100;light.direction={-.5f,-1,-.3f};light.fog_density=0;njin::light3d_set(ctx,light);
  njin::camera3d camera;camera.position=stage==4||stage==5?njin::vec3{4,0,10}:njin::vec3{0,0,10};
  camera.target={0,0,0};camera.projection=stage>=6?njin::camera3d_perspective:njin::camera3d_orthographic;
  camera.fovy=60;camera.ortho_height=10;camera.entities=false;
  const auto dest=target[stage>=4?1:0];const auto before=njin::renderer_resource_stats(ctx);
  njin::begin_3d(ctx,camera,dest,{1,0,1,1});
  if(stage>=8) njin::fx3d_set(ctx,{.mask_center={0,0,-4},.mask_radius=6});
  if(stage!=2&&stage!=3) njin::draw_model(ctx,wall,{}, {.4f,.8f,.2f,1});
  njin::fx3d_set(ctx,{});
  njin::draw_model(ctx,wall,{.position={0,0,-4},.scale={.1f,.1f,.1f}}, {1,.2f,.1f,1});
  njin::material3d material;material.unlit=true;njin::material3d_set(ctx,material);
  njin::draw_instanced3d(ctx,njin::mesh3d_cube,instances,njin::instanced3d_desc{.cull=true,.lod=false,.radius=.9f});
  njin::end_3d(ctx);
  const auto after=njin::renderer_resource_stats(ctx);
  if(occlusion&&frame%5==1) warm_allocations=after.occlusion_buffer_allocations;
  if(occlusion&&frame%5>=2) require(after.occlusion_buffer_allocations==warm_allocations,"steady scene allocated new culling buffers");
  require(after.occlusion_pool_slots<=8,"culling pool grew without additional groups");
  require(after.instances_submitted-before.instances_submitted>=25,"off-camera/occluded shadow instances were lost");
  if(occlusion&&frame%5==3) {
    const auto counts=njin::renderer_occlusion_readback(ctx);
    report("Hi-Z stage %u GPU input %llu visible %llu\n",stage,(unsigned long long)counts[0],(unsigned long long)counts[1]);
    require(counts[0]==(stage==3?14u:15u),"GPU culling did not receive models and original instances");
    if(stage==1||stage==7) require(counts[1]>0&&counts[1]<counts[0],"GPU did not reject hidden instances or rejected visible instances");
    if(stage==3||stage==9) require(counts[1]+1==counts[0],"disoccluded or camera-masked instances were lost");
    const auto shadows=njin::renderer_shadow_culling_readback(ctx);
    report("GPU shadow input %llu visible %llu\n",(unsigned long long)shadows[0],(unsigned long long)shadows[1]);
    require(shadows[0]==counts[0]&&shadows[1]>counts[1],"shadow culling lost independent off-camera/occluded casters");
    require(after.occlusion_builds>before.occlusion_builds&&after.occlusion_indirect_draws>before.occlusion_indirect_draws,"Hi-Z or indirect draws were not dispatched");
  }
  if(frame%5==3) require(njin::render_texture_save(ctx,dest,(capture+"-"+std::to_string(stage)+".png").c_str()),"occlusion capture failed");
  njin::render_texture_draw(ctx,dest,{},{1,1,1,1});
  ++frame;
}
void shutdown(njin::context &ctx) {
  if(!benchmark) for(unsigned stage=0;stage<22;stage+=2) for(unsigned view=0;view<(stage==18?2u:1u);++view) {
    const std::string suffix=view?"-view1":"";
    const auto a=njin::renderer_load_image((capture+"-"+std::to_string(stage)+suffix+".png").c_str());
    const auto b=njin::renderer_load_image((capture+"-"+std::to_string(stage+1)+suffix+".png").c_str());
    require(!a.pixels.empty()&&a.width==b.width&&a.height==b.height,"missing A/B captures");
    unsigned changed=0;for(size_t i=0;i<a.pixels.size();i+=4) {
      int error=0;for(unsigned c=0;c<3;++c) error+=std::abs(int(a.pixels[i+c])-int(b.pixels[i+c]));
      changed+=error>12;
    }
    report("Hi-Z A/B %u changed pixels %u\n",stage/2,changed);
    require(changed<20,"GPU occlusion changed visible output");
  }
  njin::render3d_options_set(ctx,{});njin::instance_buffer_destroy(ctx,instances);njin::model_unload(ctx,wall);
  njin::model_unload(ctx,masked);njin::model_unload(ctx,animated);njin::model_unload(ctx,authored);
  for(auto t:target) njin::render_texture_unload(ctx,t);
}
void setup(njin::context &ctx) {
  njin::ecs_register(ctx,njin::phase_startup,startup);njin::ecs_register(ctx,njin::phase_render,render);njin::ecs_register(ctx,njin::phase_shutdown,shutdown);
}
}
int main(int argc,char **argv) {
  try {
    njin::config cfg{.title="Njin Hi-Z distillation",.width=320,.height=240,.target_fps=60};cfg.audio=false;cfg.graphics_backend=njin::GraphicsBackend::Vulkan;
    for(int i=1;i<argc;++i) {
      if(std::strncmp(argv[i],"--renderer=",11)==0) require(njin::parse_graphics_backend(argv[i]+11,cfg.graphics_backend),"invalid renderer");
      else if(std::strcmp(argv[i],"--gpu-validation")==0) cfg.graphics_validation=true;
      else if(std::strcmp(argv[i],"--benchmark")==0) benchmark=true;
    }
    if(benchmark) cfg.target_fps=0;
    const auto dir=std::filesystem::absolute(argv[0]).parent_path()/"captures";std::filesystem::create_directories(dir);
    assets=(dir.parent_path()/"model-test-assets").string();
    capture=(dir/("occlusion-"+std::string(njin::graphics_backend_name(cfg.graphics_backend)))).string();
    {std::ofstream reset(capture+"-checks.txt");}
    for(unsigned repeat=0;repeat<(benchmark?1u:2u);++repeat) {
      std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);
      njin::mod_register(*ctx,{.name="Hi-Z controls",.setup=setup});njin::run(*ctx);
    }
    return 0;
  } catch(const std::exception &e) {
    if(!capture.empty()) std::ofstream(capture+"-error.txt")<<e.what()<<'\n';
    std::fprintf(stderr,"%s\n",e.what());return 1;
  }
}
