#include <njin.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <numeric>
#include <cmath>
namespace {
using namespace njin;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
fs::path root,assets;
std::string backend;
int frame=0,stage=0,stage_frame=0,attempt=0;
bool sync_draw=false,saw_loading=false,saw_uploading=false;
stream_config cfg;
std::vector<stream_handle> textures,models;
stream_handle missing,corrupt,cancelled,unloaded,pressure,oversized,arrival;
stream_handle scene_job;
scene_handle scene_a,scene_b;
std::array<texture_handle,8> sync_textures;
std::array<model_handle,5> sync_models;
std::vector<double> frame_ms;
Clock::time_point previous;
usize peak_cpu=0,peak_gpu=0,peak_upload=0;
usize peak_heap=0;
u32 peak_queue=0;
double sync_load_ms=0,max_upload_ms=0;
u64 evictions=0;
void check(bool ok,const char *message) {if(!ok) throw std::runtime_error(message);}
void bmp(const fs::path &path,unsigned seed) {
  std::ofstream f(path,std::ios::binary);unsigned char header[54]{};
  header[0]='B';header[1]='M';header[2]=54;header[3]=12;header[10]=54;header[14]=40;header[18]=32;header[22]=32;header[26]=1;header[28]=24;
  f.write(reinterpret_cast<char*>(header),54);
  for(unsigned y=0;y<32;++y) for(unsigned x=0;x<32;++x) {
    unsigned char pixel[]{static_cast<unsigned char>(x*7+seed),static_cast<unsigned char>(y*7),static_cast<unsigned char>((x^y)*7)};
    f.write(reinterpret_cast<char*>(pixel),3);
  }
}
stream_state state(context &c,stream_handle h) {return streaming_info(c,h).state;}
void advance() {++stage;stage_frame=frame;}
void startup(context &c) {
  cfg={};cfg.test_delay_ms=20;cfg.upload_bytes_per_frame=8192;cfg.gpu_bytes=128*1024;cfg.idle_frames=8;
  cfg.upload_units_per_frame=1;
  streaming_configure(c,cfg);
  textures.clear();models.clear();sync_draw=false;saw_loading=saw_uploading=false;frame_ms.clear();peak_cpu=peak_gpu=peak_upload=peak_heap=0;peak_queue=0;sync_load_ms=max_upload_ms=0;stage=frame=0;previous=Clock::now();
  for(unsigned i=0;i<8;++i) {
    const auto path=(root/("texture-"+std::to_string(i)+".bmp")).string();bmp(path,i);
    textures.push_back(texture_request(c,path.c_str(),int(8-i)));
    const auto start=Clock::now();sync_textures[i]=texture_load(c,path.c_str());
    sync_load_ms+=std::chrono::duration<double,std::milli>(Clock::now()-start).count();check(sync_textures[i].id,"sync BMP load failed");
  }
  auto duplicate=texture_request(c,(root/"texture-0.bmp").string().c_str());
  check(duplicate.id==textures[0].id,"duplicate texture request did not share work");streaming_release(c,duplicate);
  const char *model_names[]{"static-model.gltf","pbr-model.gltf","animated-weighted-model.gltf","animated-node-morphs.gltf","obj-materials.obj"};
  for(const auto *name:model_names) models.push_back(model_request(c,(assets/name).string().c_str(),10,true));
  auto duplicate_model=model_request(c,(assets/"static-model.gltf").string().c_str(),10,true);
  check(duplicate_model.id==models[0].id,"duplicate model request did not share work");streaming_release(c,duplicate_model);
  streaming_pin(c,textures[0]);streaming_pin(c,models[0]);streaming_pin(c,models[1]);
  streaming_pin(c,models[2]);streaming_pin(c,models[3]);
  streaming_pin(c,models[4]);
  fs::remove(root/"retry.bmp");missing=texture_request(c,(root/"retry.bmp").string().c_str(),20);
  {std::ofstream f(root/"corrupt.bmp");f<<"invalid image";}
  corrupt=texture_request(c,(root/"corrupt.bmp").string().c_str(),20);
  cancelled=texture_request(c,(root/"cancel.bmp").string().c_str(),20);streaming_cancel(c,cancelled);
  bmp(root/"unload.bmp",1);unloaded=texture_request(c,(root/"unload.bmp").string().c_str(),20);
  texture_unload(c,streaming_texture(c,unloaded));
  check(state(c,unloaded)==stream_state::cancelled,"unload failed to invalidate stream request");
  for(unsigned i=0;i<sync_models.size();++i) {
    const auto path=(assets/model_names[i]).string();
    const auto start=Clock::now();sync_models[i]=model_load(c,{.path=path.c_str(),.pbr=true});check(sync_models[i].id,"synchronous model failed");
    sync_load_ms+=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
  }
}
void update(context &c) {
  auto now=Clock::now();if(frame) frame_ms.push_back(std::chrono::duration<double,std::milli>(now-previous).count());previous=now;
  auto stats=streaming_stats(c);peak_cpu=std::max(peak_cpu,stats.cpu_bytes);peak_gpu=std::max(peak_gpu,stats.gpu_bytes);peak_upload=std::max(peak_upload,stats.uploaded_bytes);evictions=stats.evictions;
  max_upload_ms=std::max(max_upload_ms,double(stats.upload_ms));peak_queue=std::max(peak_queue,stats.queued+stats.loading+stats.uploading);peak_heap=std::max(peak_heap,mem_info().used);
  check(stats.uploaded_bytes<=cfg.upload_bytes_per_frame,"upload burst exceeded byte budget");
  saw_loading|=stats.loading>0;saw_uploading|=stats.uploading>0;
  if(frame>1500) {
    for(auto h:textures) {auto info=streaming_info(c,h);std::fprintf(stderr,"texture %u state=%d %s\n",h.id,int(info.state),info.error.c_str());}
    for(auto h:models) {auto info=streaming_info(c,h);std::fprintf(stderr,"model %u state=%d %s\n",h.id,int(info.state),info.error.c_str());}
    std::fprintf(stderr,"missing=%d corrupt=%d\n",int(state(c,missing)),int(state(c,corrupt)));
    throw std::runtime_error("stream fixture timeout at stage "+std::to_string(stage));
  }
  if(stage==0) {
    for(auto h:models) if(state(c,h)==stream_state::failed) throw std::runtime_error(streaming_info(c,h).error);
    bool ready=true;for(auto h:textures) ready&=state(c,h)==stream_state::ready;for(auto h:models) ready&=state(c,h)==stream_state::ready;
    if(ready&&state(c,missing)==stream_state::failed&&state(c,corrupt)==stream_state::failed) {
      for(unsigned i=0;i<sync_models.size();++i) check(model_anim_count(c,streaming_model(c,models[i]))==model_anim_count(c,sync_models[i]),"streaming lost animation metadata");
      check(state(c,cancelled)==stream_state::cancelled,"cancel state lost");
      bmp(root/"retry.bmp",0);streaming_retry(c,missing);advance();
    }
  } else if(stage==1&&state(c,missing)==stream_state::ready) {advance();}
  else if(stage==2&&frame>stage_frame+30) {sync_draw=true;advance();}
  else if(stage==3&&frame>stage_frame+30) {
    const auto a=renderer_load_image((root/(backend+"-stream.png")).string().c_str());
    const auto b=renderer_load_image((root/(backend+"-sync.png")).string().c_str());
    check(a.pixels==b.pixels,"settled stream differs from synchronous image");
    sync_draw=false;streaming_pin(c,models[0],false);streaming_pin(c,models[1],false);
    streaming_pin(c,models[2],false);streaming_pin(c,models[3],false);
    streaming_pin(c,models[4],false);
    cfg.gpu_bytes=stats.gpu_bytes-8192;cfg.idle_frames=2;streaming_configure(c,cfg);
    bmp(root/"arrival.bmp",1);arrival=texture_request(c,(root/"arrival.bmp").string().c_str(),30);advance();
  } else if(stage==4&&state(c,arrival)==stream_state::ready) {
    check(streaming_stats(c).evictions>0,"low GPU budget failed to evict");
    check(state(c,textures[0])==stream_state::ready,"pinned texture evicted");
    cfg.gpu_bytes=128*1024;streaming_configure(c,cfg);
    for(auto h:textures) streaming_touch(c,h,30);advance();
  } else if(stage==5) {
    bool ready=true;for(auto h:textures) ready&=state(c,h)==stream_state::ready;
    if(ready) {
      cfg.cpu_bytes=64;streaming_configure(c,cfg);bmp(root/"pressure.bmp",0);
      pressure=texture_request(c,(root/"pressure.bmp").string().c_str(),30);advance();
    }
  } else if(stage==6&&state(c,pressure)==stream_state::failed) {
    check(!streaming_info(c,pressure).error.empty(),"budget error not observable");
    cfg.cpu_bytes=128*1024*1024;streaming_configure(c,cfg);streaming_retry(c,pressure);advance();
  } else if(stage==7&&state(c,pressure)==stream_state::ready) {
    cfg.upload_bytes_per_frame=32;streaming_configure(c,cfg);bmp(root/"oversized.bmp",0);
    oversized=texture_request(c,(root/"oversized.bmp").string().c_str(),30);advance();
  } else if(stage==8&&state(c,oversized)==stream_state::failed) {
    cfg.upload_bytes_per_frame=8192;streaming_configure(c,cfg);streaming_retry(c,oversized);advance();
  } else if(stage==9&&state(c,oversized)==stream_state::ready) {
    // Destroy/retry while partial model uploads exist; generation must reject old completion.
    streaming_cancel(c,models[0]);streaming_retry(c,models[0]);advance();
  } else if(stage==10&&state(c,models[0])==stream_state::uploading) {
    streaming_cancel(c,models[0]);advance();
  } else if(stage==11) {
    check(state(c,models[0])==stream_state::cancelled,"upload cancellation lost");
    streaming_retry(c,models[0]);advance();
  } else if(stage==12&&state(c,models[0])==stream_state::ready) {
    check(saw_loading,"controlled delay never observed loading");
    check(saw_uploading,"no CPU/GPU handoff observed");
    // Scene on_exit releases a delayed asset while it is still loading.
    cfg.test_delay_ms=500;streaming_configure(c,cfg);
    bmp(root/"scene.bmp",0);scene_job=texture_request(c,(root/"scene.bmp").string().c_str());scene_set(c,scene_a);advance();
  } else if(stage==13&&scene_current(c).id==scene_a.id) {scene_set(c,scene_b);advance();}
  else if(stage==14&&scene_current(c).id==scene_b.id&&state(c,scene_job)==stream_state::cancelled) {
    // Deliberately leave another job alive through run/context shutdown.
    bmp(root/"shutdown.bmp",0);texture_request(c,(root/"shutdown.bmp").string().c_str());quit(c);
  }
}
void render(context &c) {
  auto light=light3d_get(c);light.hdr=false;light.shadows=false;light.tonemap=tonemap3d_none;light3d_set(c,light);
  camera3d camera;camera.position={stage==2||stage==3?0.f:std::sin(frame*.08f)*2,0,8};camera.target={0,0,0};camera.entities=false;
  begin_3d(c,camera);
  for(unsigned i=0;i<sync_models.size();++i) {
    const auto model=sync_draw?sync_models[i]:streaming_model(c,models[i]);
    if(model.id) {
      if(i==0) for(int m=0;m<model_material_count(c,model);++m) {
        auto material=model_material_get(c,model,m);material.albedo=sync_draw?sync_textures[0]:streaming_texture(c,textures[0]);model_material_set(c,model,m,material);
      }
      draw_model(c,model,{.position={float(i%2)*3-2,float(i/2)*-1.5f-.5f,0},.scale={.8f,.8f,.8f}});
    }
  }
  // Normal gameplay material handles sample streamed textures while jobs are in flight.
  for(unsigned i=0;i<8;++i) {
    auto material=model_material_get(c,sync_models[0],1);
    material.albedo=sync_draw?sync_textures[i]:streaming_texture(c,textures[stage<4?i:0]);
    model_material_set(c,sync_models[0],1,material);
    draw_model(c,sync_models[0],{.position={float(i)*.7f-3,2,0},.scale={.2f,.2f,.2f}});
  }
  end_3d(c);
  if(stage==2&&frame==stage_frame+1) screenshot(c,(root/(backend+"-stream.png")).string().c_str());
  if(stage==3&&frame==stage_frame+1) screenshot(c,(root/(backend+"-sync.png")).string().c_str());
  ++frame;
}
void shutdown(context &c) {
  for(auto m:sync_models) model_unload(c,m);for(auto t:sync_textures) texture_unload(c,t);
  std::sort(frame_ms.begin(),frame_ms.end());
  const double mean=frame_ms.empty()?0:std::accumulate(frame_ms.begin(),frame_ms.end(),0.)/frame_ms.size();
  std::printf("STREAM PASS backend=%s run=%d frames=%d mean_ms=%.3f p99_ms=%.3f worst_ms=%.3f upload_peak=%zu cpu_accounted_peak=%zu gpu_estimate_peak=%zu cache_hits=%llu evictions=%llu image_equal=1\n",
    backend.c_str(),attempt,frame,mean,frame_ms[frame_ms.size()*99/100],frame_ms.back(),peak_upload,peak_cpu,peak_gpu,
    static_cast<unsigned long long>(streaming_stats(c).cache_hits),static_cast<unsigned long long>(evictions));
  std::printf("STREAM METRICS sync_cpu_load_ms=%.3f max_upload_ms=%.3f queue_peak=%u tracked_heap_peak=%zu\n",sync_load_ms,max_upload_ms,peak_queue,peak_heap);
  std::ofstream metrics(root/(backend+"-metrics.txt"),std::ios::app);
  metrics<<"run="<<attempt<<" frames="<<frame<<" mean_ms="<<mean<<" p99_ms="<<frame_ms[frame_ms.size()*99/100]<<" worst_ms="<<frame_ms.back()
    <<" sync_cpu_load_ms="<<sync_load_ms<<" max_upload_ms="<<max_upload_ms<<" upload_peak="<<peak_upload<<" cpu_accounted_peak="<<peak_cpu
    <<" gpu_estimate_peak="<<peak_gpu<<" queue_peak="<<peak_queue<<" heap_peak="<<peak_heap<<" evictions="<<evictions<<" image_equal=1"<<std::endl;
}
void scene_exit(context &c) {streaming_release(c,scene_job);}
void setup(context &c) {
  scene_a=scene_register(c,{.name="streaming region A",.on_exit=scene_exit});scene_b=scene_register(c,{.name="streaming region B"});
  ecs_register(c,phase_startup,startup);ecs_register(c,phase_update,update);ecs_register(c,phase_render,render);ecs_register(c,phase_shutdown,shutdown);
}
}
int main(int argc,char **argv) {
  try {
    njin::config config{.title="Njin streaming regression",.width=640,.height=480,.target_fps=240};config.audio=false;config.graphics_backend=njin::GraphicsBackend::Vulkan;config.clear_bg_color={.03f,.04f,.05f,1};
    for(int i=1;i<argc;++i) if(std::strncmp(argv[i],"--renderer=",11)==0) check(njin::parse_graphics_backend(argv[i]+11,config.graphics_backend),"invalid renderer");else if(std::strcmp(argv[i],"--gpu-validation")==0) config.graphics_validation=true;
    backend=njin::graphics_backend_name(config.graphics_backend);const auto bin=fs::absolute(argv[0]).parent_path();assets=bin/"model-test-assets";root=bin/"stream-fixtures";fs::create_directories(root);
    fs::remove(root/(backend+"-metrics.txt"));
    stream_handle previous_request;
    for(attempt=0;attempt<2;++attempt) {
      std::unique_ptr<njin::context,void(*)(njin::context*)> c(njin::create(config),njin::destroy);
      if(attempt) {bool rejected=false;try {streaming_info(*c,previous_request);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"foreign context accepted stale request");}
      njin::mod_register(*c,{.name="stream regression",.setup=setup});njin::run(*c);
      previous_request=textures[0];
    }
    return 0;
  } catch(const std::exception &e) {std::fprintf(stderr,"STREAM FAIL: %s\n",e.what());return 1;}
}
