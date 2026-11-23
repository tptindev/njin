// Measures the normal Njin loop. Internal access only reads existing diagnostic
// counters and the exact bgfx submission identity; rendering uses public APIs.
#include <njin.h>
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include <bgfx/bgfx.h>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
std::array<std::uint64_t,2> njin_bench_process_memory();
namespace {
using Clock=std::chrono::steady_clock;
unsigned tick=0,warmup=180,frames=600,width=640,height=480,seed=17;
bool capture=false,validation=false;
std::string workload="occluded",variant="cpu",mesh="cube",output="benchmark";
njin::GraphicsBackend backend=njin::GraphicsBackend::Vulkan;
njin::instance_buffer_handle instances;
njin::model_handle wall;
std::vector<njin::model_handle> models;
njin::render_texture_handle target;
std::vector<float> records;
Clock::time_point launched=Clock::now(),previous;
double create_ms=0,startup_ms=0;
njin::mem_stats prior_memory;
struct Row {
  unsigned tick=0,id=0;
  double wall=0,render=0,update=0,present=0,submit=0,gpu=-1,backend_cpu=-1;
  std::uint64_t ram=0,private_bytes=0,tracked=0,allocations=0;
  std::int64_t gpu_memory=-1,texture_memory=0,rt_memory=0;
  unsigned draws=0,dispatches=0,triangles=0,textures=0,buffers=0;
  njin::RendererResourceStats resources;
};
std::vector<Row> rows;
unsigned last_gpu=UINT32_MAX,duplicates=0,unmatched=0;
std::array<std::uint64_t,2> visible{},shadow_visible{};
double last_submit=0,last_update=0;
void require(bool ok,const char *message) {if(!ok) throw std::runtime_error(message);}
unsigned count() {return workload=="low"?16u:4096u;}
void startup(njin::context &ctx) {
  require(variant!="gpu"||njin::renderer_gpu_occlusion_available(ctx),"GPU occlusion unsupported");
  rows.reserve(warmup+frames+64);
  target=njin::render_texture_load(ctx,width,height);
  const njin::vec3 p[]{{-3,-3,0},{3,-3,0},{3,3,0},{-3,3,0}};
  const njin::u32 indices[]{0,1,2,0,2,3};
  wall=njin::model_create(ctx,{.positions=p,.vertex_count=4,.indices=indices,.index_count=6});
  auto mat=njin::model_material_get(ctx,wall,0);mat.surface.unlit=true;mat.double_sided=true;
  njin::model_material_set(ctx,wall,0,mat);
  if(workload=="groups") for(unsigned i=0;i<128;++i) {
    auto m=njin::model_create(ctx,{.positions=p,.vertex_count=4,.indices=indices,.index_count=6});
    mat.color={float((i*13+seed)%97)/97.f,.6f,.4f,1};
    njin::model_material_set(ctx,m,0,mat);models.push_back(m);
  }
  instances=njin::instance_buffer_create(ctx,16);
  records.resize(count()*16);
  for(unsigned i=0;i<count();++i) {
    auto *r=records.data()+i*16;
    r[0]=(int(i%64)-31.5f)*.10f;r[1]=(int(i/64)-31.5f)*.10f;
    if(workload=="low") {r[0]=(int(i%4)-1.5f)*1.2f;r[1]=(int(i/4)-1.5f)*1.2f;}
    r[2]=-3;r[3]=workload=="low"?.25f:.05f;
    r[4]=.3f+float((i*37+seed)%71)/100.f;r[5]=.5f;r[6]=.8f;r[7]=1;
  }
  njin::instance_buffer_upload(ctx,instances,records.data(),count());
  njin::render3d_options options;options.occlusion=variant=="gpu";njin::render3d_options_set(ctx,options);
  njin::light3d light;light.shadows=workload=="shadows";light.shadow_size=512;
  light.shadow_range=40;light.fog_density=0;light.direction={1,-1,-1};njin::light3d_set(ctx,light);
  previous=Clock::now();prior_memory=njin::mem_info();
  startup_ms=std::chrono::duration<double,std::milli>(previous-launched).count();
  std::cout<<njin::renderer_gpu_occlusion_diagnostics(ctx)<<'\n';
}
void collect(njin::context &ctx) {
  const auto now=Clock::now();
  if(tick) {
    Row r;r.tick=tick-1;
    r.id=njin::ctx_ref<njin::window_guard>(ctx).graphics->submitted_frame_number();
    const auto &f=njin::ctx_ref<njin::frame_state>(ctx);
    r.wall=std::chrono::duration<double,std::milli>(now-previous).count();
    r.render=f.cpu_render_ms;r.update=last_update;r.present=f.cpu_present_ms;r.submit=last_submit;
    const auto memory=njin::mem_info();r.tracked=memory.used;r.allocations=memory.allocs-prior_memory.allocs;prior_memory=memory;
    const auto process_memory=njin_bench_process_memory();
    r.ram=process_memory[0];r.private_bytes=process_memory[1];
    const auto &s=*bgfx::getStats();
    r.draws=s.numDraw;r.dispatches=s.numCompute;r.triangles=s.numPrims[bgfx::Topology::TriList];
    r.textures=s.numTextures;r.buffers=s.numVertexBuffers+s.numIndexBuffers+s.numDynamicVertexBuffers+s.numDynamicIndexBuffers;
    r.gpu_memory=s.gpuMemoryMax>0&&s.gpuMemoryUsed>=0?s.gpuMemoryUsed:-1;
    r.texture_memory=s.textureMemoryUsed;r.rt_memory=s.rtMemoryUsed;
    if(s.cpuTimerFreq>0&&s.cpuTimeEnd>s.cpuTimeBegin) r.backend_cpu=1000.*(s.cpuTimeEnd-s.cpuTimeBegin)/s.cpuTimerFreq;
    r.resources=njin::renderer_resource_stats(ctx);
    rows.push_back(r);
    // gpuFrameNum is the frame submitted by bgfx::frame(), NOT the CPU tick.
    // Skip invalid/unavailable and repeated results; never substitute CPU time.
    if(s.gpuTimerFreq>0&&s.gpuTimeEnd>s.gpuTimeBegin) {
      if(s.gpuFrameNum==last_gpu) ++duplicates;
      else {
        last_gpu=s.gpuFrameNum;
        const auto it=std::lower_bound(rows.begin(),rows.end(),s.gpuFrameNum,[](const Row &row,unsigned id){return row.id<id;});
        if(it==rows.end()||it->id!=s.gpuFrameNum) ++unmatched;
        else it->gpu=1000.*(s.gpuTimeEnd-s.gpuTimeBegin)/s.gpuTimerFreq;
      }
    }
  }
  previous=now;
}
void render(njin::context &ctx) {
  collect(ctx);
  if(tick>=warmup+frames+32) {
    njin::quit(ctx);return;
  }
  const auto start=Clock::now();
  njin::camera3d camera;camera.position={0,0,10};camera.target={0,0,0};
  camera.projection=njin::camera3d_orthographic;camera.ortho_height=10;camera.entities=false;
  // Frame-based path ensures identical camera samples independently of speed.
  if(workload=="moving") camera.position.x=5.f*std::sin(float(int(tick)-int(warmup))*.025f);
  njin::begin_3d(ctx,camera,target,{.08f,.1f,.15f,1});
  if(workload=="occluded"||workload=="moving"||workload=="shadows") njin::draw_model(ctx,wall,{});
  njin::instanced3d_desc desc;desc.cull=variant!="off";desc.lod=false;
  if(workload=="groups") for(unsigned i=0;i<models.size();++i) {
    desc.first=i*32;desc.count=32;njin::draw_instanced3d(ctx,models[i],instances,desc);
  } else njin::draw_instanced3d(ctx,mesh=="sphere"?njin::mesh3d_sphere:njin::mesh3d_cube,instances,desc);
  if(workload=="shadows") {
    // Casters outside the camera retain the independent shadow path.
    for(unsigned i=0;i<16;++i) njin::draw_cube3d(ctx,{7.f,float(i)*.25f-2,-3},{.2f,.2f,.2f},{1,1,1,1});
  }
  njin::end_3d(ctx);
  // begin_frame resets active culling jobs. Read the completed scene in this
  // callback, before the next frame begins, only in a correctness drain frame.
  if(capture&&variant=="gpu"&&tick==warmup+frames+31) {
    visible=njin::renderer_occlusion_readback(ctx);
    shadow_visible=njin::renderer_shadow_culling_readback(ctx);
    require(visible[0]>0,"GPU culling received no instances");
  }
  if(capture&&(tick==warmup||tick==warmup+30||tick==warmup+60)) {
    const auto path=output+"-"+std::to_string(tick-warmup)+".png";
    require(njin::render_texture_save(ctx,target,path.c_str()),"capture failed");
  }
  njin::render_texture_draw(ctx,target,{},{1,1,1,1});
  last_submit=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
  last_update=njin::ctx_ref<njin::frame_state>(ctx).cpu_update_ms;++tick;
}
void shutdown(njin::context &ctx) {
  std::ofstream raw(output+".csv",std::ios::binary);raw<<std::setprecision(12);
  raw<<"tick,submission_id,phase,wall_ms,cpu_render_ms,cpu_update_ms,present_ms,scene_submit_ms,gpu_ms,backend_cpu_ms,rss_bytes,private_bytes,tracked_bytes,allocations,draws,dispatches,triangles,textures,buffers,gpu_memory_bytes,texture_estimate_bytes,rt_estimate_bytes,instances_submitted,instance_uploads,cull_allocations,cull_pool_bytes\n";
  for(const auto &r:rows) raw<<r.tick<<','<<r.id<<','<<(r.tick<warmup?"warmup":r.tick<warmup+frames?"measure":"drain")<<','
    <<r.wall<<','<<r.render<<','<<r.update<<','<<r.present<<','<<r.submit<<','<<r.gpu<<','<<r.backend_cpu<<','
    <<r.ram<<','<<r.private_bytes<<','<<r.tracked<<','<<r.allocations<<','<<r.draws<<','<<r.dispatches<<','<<r.triangles<<','<<r.textures<<','<<r.buffers<<','
    <<r.gpu_memory<<','<<r.texture_memory<<','<<r.rt_memory<<','<<r.resources.instances_submitted<<','<<r.resources.instance_uploads<<','
    <<r.resources.occlusion_buffer_allocations<<','<<r.resources.occlusion_pool_bytes<<'\n';
  require(bool(raw),"writing CSV failed");
  const auto &s=*bgfx::getStats();const auto *caps=bgfx::getCaps();
  std::ofstream meta(output+".json",std::ios::binary);
  meta<<"{\n\"schema\":\"njin.renderer-benchmark/1\",\n\"build_type\":\""<<NJIN_BENCH_BUILD_TYPE
    <<"\",\n\"renderer\":\""<<bgfx::getRendererName(bgfx::getRendererType())<<"\",\n\"workload\":\""<<workload<<"\",\n\"variant\":\""<<variant
    <<"\",\n\"mesh\":\""<<mesh<<"\",\n\"mode\":\""<<(capture?"correctness":"performance")<<"\",\n\"validation_requested\":"<<(validation?"true":"false")
    <<",\n\"seed\":"<<seed<<",\n\"warmup_frames\":"<<warmup<<",\n\"measurement_frames\":"<<frames<<",\n\"drain_frames\":32,\n\"input_instances\":"<<count()
    <<",\n\"requested_width\":"<<width<<",\n\"requested_height\":"<<height<<",\n\"actual_width\":"<<s.width<<",\n\"actual_height\":"<<s.height
    <<",\n\"target_width\":"<<width<<",\n\"target_height\":"<<height<<",\n\"render_scale\":"<<njin::ctx_ref<njin::config>(ctx).render_scale
    <<",\n\"vsync\":"<<(njin::window_vsync(ctx)?"true":"false")<<",\n\"frame_cap\":0,\n\"msaa\":0,\n\"aa\":\"none\",\n\"hdr\":false,\n\"lod\":false,\n\"shadow_size\":"<<(workload=="shadows"?512:0)
    <<",\n\"vendor_id\":"<<caps->vendorId<<",\n\"device_id\":"<<caps->deviceId<<",\n\"create_ms\":"<<create_ms<<",\n\"startup_ms\":"<<startup_ms
    <<",\n\"gpu_duplicate_results\":"<<duplicates<<",\n\"gpu_unmatched_results\":"<<unmatched
    <<",\n\"gpu_timing\":\"bgfx timestamps joined by exact submission id; -1 unavailable\",\n\"backend_counter_frame_identity\":\"bgfx delayed snapshot; not asserted equal to CPU tick\",\n\"upload_bytes\":null,\n\"diagnostic_readback_after_run\":"<<(capture&&variant=="gpu"?"true":"false")
    <<",\n\"diagnostic_input\":"<<visible[0]<<",\n\"diagnostic_visible\":"<<visible[1]<<",\n\"diagnostic_shadow_input\":"<<shadow_visible[0]<<",\n\"diagnostic_shadow_visible\":"<<shadow_visible[1]
    <<",\n\"thermal_control\":\"uncontrolled; no power or clock changes\"\n}\n";
  require(bool(meta),"writing metadata failed");
  njin::instance_buffer_destroy(ctx,instances);njin::model_unload(ctx,wall);
  for(auto m:models) njin::model_unload(ctx,m);
  njin::render_texture_unload(ctx,target);
}
void setup(njin::context &ctx) {
  njin::ecs_register(ctx,njin::phase_startup,startup);
  njin::ecs_register(ctx,njin::phase_render,render);
  njin::ecs_register(ctx,njin::phase_shutdown,shutdown);
}
}
int main(int argc,char **argv) {
  try {
    for(int i=1;i<argc;++i) {
      const std::string a=argv[i];auto value=[&](const char *prefix){return a.substr(std::char_traits<char>::length(prefix));};
      if(a.starts_with("--renderer=")) require(njin::parse_graphics_backend(value("--renderer=").c_str(),backend),"invalid renderer");
      else if(a.starts_with("--workload=")) workload=value("--workload=");
      else if(a.starts_with("--variant=")) variant=value("--variant=");
      else if(a.starts_with("--mesh=")) mesh=value("--mesh=");
      else if(a.starts_with("--output=")) output=value("--output=");
      else if(a.starts_with("--warmup=")) warmup=std::stoul(value("--warmup="));
      else if(a.starts_with("--frames=")) frames=std::stoul(value("--frames="));
      else if(a.starts_with("--width=")) width=std::stoul(value("--width="));
      else if(a.starts_with("--height=")) height=std::stoul(value("--height="));
      else if(a.starts_with("--seed=")) seed=std::stoul(value("--seed="));
      else if(a=="--capture") capture=true;
      else if(a=="--gpu-validation") validation=true;
      else throw std::invalid_argument("unknown argument: "+a);
    }
    require(variant=="cpu"||variant=="gpu"||variant=="off","variant must be off, cpu or gpu");
    require(mesh=="cube"||mesh=="sphere","mesh must be cube or sphere");
    require(mesh=="cube"||workload!="groups","groups uses its own panel models; select cube/default mesh");
    require(backend!=njin::GraphicsBackend::OpenGL,"use bgfx-opengl; legacy renderer is outside this benchmark");
    require(workload=="low"||workload=="visible"||workload=="occluded"||workload=="groups"||workload=="moving"||workload=="shadows","unknown workload");
    require(width>0&&height>0&&width<=8192&&height<=8192&&warmup<=100000&&frames>60&&frames<=100000,"invalid dimensions or duration");
    require(!validation||capture,"validation belongs in a separate correctness run (--capture)");
    const auto parent=std::filesystem::path(output).parent_path();if(!parent.empty()) std::filesystem::create_directories(parent);
    njin::config cfg{.title="Njin renderer benchmark",.width=float(width),.height=float(height),.target_fps=0};
    cfg.audio=false;cfg.vsync=false;cfg.graphics_backend=backend;cfg.graphics_validation=validation;
    const auto started=Clock::now();
    std::unique_ptr<njin::context,void(*)(njin::context*)> ctx(njin::create(cfg),njin::destroy);
    create_ms=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
    njin::mod_register(*ctx,{.name="renderer benchmark",.setup=setup});njin::run(*ctx);
    require(tick>=warmup+frames+32,"benchmark interrupted before completion");return 0;
  } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
