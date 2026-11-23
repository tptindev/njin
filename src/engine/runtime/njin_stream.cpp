#include "njin_stream_internal.h"
#include "njin_ctx_impl.h"
#include "njin_texture.h"
#include "njin_path.h"
#include "njin_ctx.h"
#include "njin_render.h"
#include "njin_log_impl.h"
#include "njin_image_mips.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <cmath>
namespace njin {
namespace {
using Clock=std::chrono::steady_clock;
std::atomic<u64> next_owner{1};
struct Token { std::atomic<bool> cancel{false},loading{false}; };
struct Work {u32 id,generation;int priority;StreamPrepare prepare;std::shared_ptr<Token> token;u32 delay;};
struct Result {u32 id,generation;std::unique_ptr<StreamPayload> payload;std::string error;};
struct Record {
  std::string key; bool model=false,pinned=false,release=false;
  u32 handle=0,refs=1,generation=0;u64 used=0;
  int priority=0;
  u32 detail=0;
  stream_info info{stream_state::queued};
  StreamPrepare prepare;
  std::function<u32(context &,u32)> placeholder;
  std::function<void(context &,u32)> unload;
  std::shared_ptr<Token> token;
  std::unique_ptr<StreamPayload> payload;
};
}
struct StreamStore {
  u64 serial=next_owner.fetch_add(1);
  stream_config config;
  stream_stats stats;
  std::thread::id owner=std::this_thread::get_id();
  std::vector<Record> records;
  u64 frame=0;
  std::mutex mutex;std::condition_variable wake;
  std::vector<Work> queue;std::deque<Result> results;
  bool stop=false;
  bool internal_unload=false;
  std::thread worker;
  StreamStore():worker([this]{run();}) {}
  ~StreamStore() {
    {std::lock_guard lock(mutex);stop=true;queue.clear();}
    wake.notify_all();worker.join();
    // Payloads remaining here have CPU data only: uploads are main-thread work.
  }
  void check() const {if(owner!=std::this_thread::get_id()) throw std::logic_error("njin streaming: context owner thread required");}
  void run() {
    log_worker_quiet(true);
    for(;;) {
      Work w;
      {
        std::unique_lock lock(mutex);
        wake.wait(lock,[&]{return stop||(!queue.empty()&&results.empty());});
        if(stop) return;
        auto best=std::max_element(queue.begin(),queue.end(),[](const Work &a,const Work &b){return a.priority<b.priority;});
        w=std::move(*best);queue.erase(best);
      }
      Result r{w.id,w.generation};
      w.token->loading=true;
      {
        std::unique_lock lock(mutex);
        wake.wait_for(lock,std::chrono::milliseconds(w.delay),[&]{return stop||w.token->cancel.load();});
        if(stop) return;
      }
      if(!w.token->cancel) {
        try {r.payload=w.prepare();if(!r.payload) r.error="loader returned no CPU asset";}
        catch(const std::exception &e) {r.error=e.what();}
        catch(...) {r.error="unknown CPU loader failure";}
      }
      {std::lock_guard lock(mutex);results.push_back(std::move(r));}
    }
  }
};
std::shared_ptr<StreamStore> stream_store_create() {return std::make_shared<StreamStore>();}
namespace {
StreamStore &store(context &ctx) {
  if(ctx.cfg.graphics_backend==GraphicsBackend::OpenGL) throw std::logic_error("njin streaming: SDL3/bgfx backend required");
  if(!ctx.streaming) ctx.streaming=stream_store_create();
  ctx.streaming->check();return *ctx.streaming;
}
Record &record(StreamStore &s,stream_handle h) {
  if(h.owner!=s.serial||!h.id||h.id>s.records.size()) throw std::invalid_argument("njin streaming: invalid or foreign request");
  return s.records[h.id-1];
}
void enqueue(StreamStore &s,u32 id) {
  auto &r=s.records[id-1];r.token=std::make_shared<Token>();++r.generation;
  r.info={stream_state::queued};
  {std::lock_guard lock(s.mutex);s.queue.push_back({id,r.generation,r.priority,r.prepare,r.token,s.config.test_delay_ms});}
  s.wake.notify_all();
}
void clear(context &ctx,Record &r) {
  auto &s=*ctx.streaming;
  struct Guard {bool &flag;Guard(bool &f):flag(f){flag=true;}~Guard(){flag=false;}} guard(s.internal_unload);
  if(r.payload) {r.payload->discard(ctx);r.payload.reset();}
  if(r.handle) {r.unload(ctx,r.handle);r.handle=0;}
  r.info.cpu_bytes=r.info.gpu_bytes=0;
  r.info.renderable=false;r.info.resident_level=~u32(0);
}
void fallback(context &ctx,Record &r) {
  const auto previous=r.handle;clear(ctx,r);r.handle=r.placeholder(ctx,previous);
}
}
stream_handle stream_request(context &ctx,std::string key,int priority,bool model,StreamPrepare prepare,
                            std::function<u32(context &,u32)> placeholder,std::function<void(context &,u32)> unload) {
  auto &s=store(ctx);
  for(u32 i=0;i<s.records.size();++i) {
    auto &r=s.records[i];
    if(r.key==key&&r.refs&&!r.release) {++r.refs;++s.stats.cache_hits;r.priority=std::max(r.priority,priority);return {i+1,s.serial};}
  }
  Record r;r.key=std::move(key);r.model=model;r.priority=priority;r.prepare=std::move(prepare);
  r.placeholder=std::move(placeholder);r.unload=std::move(unload);r.used=s.frame;
  r.handle=r.placeholder(ctx,0);
  s.records.push_back(std::move(r));const u32 id=u32(s.records.size());enqueue(s,id);return {id,s.serial};
}
void streaming_configure(context &ctx,const stream_config &cfg) {
  if(!cfg.cpu_bytes||!cfg.gpu_bytes||!cfg.upload_bytes_per_frame||!cfg.upload_units_per_frame||!std::isfinite(cfg.upload_ms_per_frame)||cfg.upload_ms_per_frame<=0)
    throw std::invalid_argument("njin streaming: budgets must be positive and finite");
  store(ctx).config=cfg;
}
stream_info streaming_info(context &ctx,stream_handle h) {
  auto &r=record(store(ctx),h);auto out=r.info;
  if(out.state==stream_state::queued&&r.token->loading) out.state=stream_state::loading;
  return out;
}
stream_stats streaming_stats(context &ctx) {return store(ctx).stats;}
void streaming_detail(context &ctx,stream_handle h,u32 level) {
  auto &r=record(store(ctx),h);r.detail=level;r.info.target_level=level;
  if(r.payload&&r.payload->persistent()) {
    r.payload->detail(level);
    r.info.target_level=r.payload->target_level();
    if(!r.payload->done()&&r.refs&&!r.release&&r.info.state!=stream_state::cancelled) r.info.state=stream_state::uploading;
  }
}
void streaming_pin(context &ctx,stream_handle h,bool pinned) {record(store(ctx),h).pinned=pinned;}
void streaming_touch(context &ctx,stream_handle h,int priority) {
  auto &s=store(ctx);auto &r=record(s,h);if(!r.refs) return;
  r.used=s.frame;r.priority=priority;
  if(r.payload&&r.payload->persistent()&&r.info.state==stream_state::ready&&r.payload->resident_level()>r.detail) {
    r.payload->detail(r.detail);r.info.target_level=r.payload->target_level();r.info.state=stream_state::uploading;
  }
  {std::lock_guard lock(s.mutex);for(auto &w:s.queue) if(w.id==h.id) w.priority=priority;}
  if(r.info.state==stream_state::evicted) enqueue(s,h.id);
}
void streaming_release(context &ctx,stream_handle h) {
  auto &r=record(store(ctx),h);if(r.refs&&!--r.refs) {r.release=true;r.token->cancel=true;}
}
void streaming_cancel(context &ctx,stream_handle h) {
  auto &s=store(ctx);auto &r=record(s,h);r.token->cancel=true;
  r.info.state=stream_state::cancelled;s.wake.notify_all();
}
void streaming_retry(context &ctx,stream_handle h) {
  auto &s=store(ctx);auto &r=record(s,h);
  if(!r.refs||r.release) throw std::logic_error("njin streaming: released request cannot retry");
  if(r.info.state!=stream_state::failed&&r.info.state!=stream_state::cancelled&&r.info.state!=stream_state::evicted) return;
  if(ctx.window.graphics->frame_open()) throw std::logic_error("njin streaming: retry outside render frame");
  fallback(ctx,r);enqueue(s,h.id);
}
void stream_forget(context &ctx,bool model,u32 handle) {
  if(!ctx.streaming) return;
  auto &s=store(ctx);if(s.internal_unload) return;
  for(auto &r:s.records) if(r.model==model&&r.handle==handle) {
    r.token->cancel=true;r.handle=0;r.info.state=stream_state::cancelled;r.release=true;r.refs=0;
  }
}
texture_handle streaming_texture(context &ctx,stream_handle h) {
  auto &s=store(ctx);auto &r=record(s,h);if(r.model) throw std::invalid_argument("njin streaming: expected texture");
  streaming_touch(ctx,h,r.priority);return {r.handle};
}
u32 stream_resource(context &ctx,stream_handle h,bool model) {
  auto &s=store(ctx);auto &r=record(s,h);
  if(r.model!=model) throw std::invalid_argument("njin streaming: request type mismatch");
  streaming_touch(ctx,h,r.priority);return r.handle;
}
void streaming_shutdown(context &ctx) {
  if(!ctx.streaming) return;
  auto &s=*ctx.streaming;s.check();
  if(ctx.window.graphics->frame_open()) {ctx.streaming.reset();return;}
  for(auto &r:s.records) {r.token->cancel=true;clear(ctx,r);}
  ctx.streaming.reset(); // destructor signals stop and joins before stores/device die
}
void streaming_update(context &ctx) {
  if(!ctx.streaming) return;
  auto &s=store(ctx);if(ctx.window.graphics->frame_open()) throw std::logic_error("njin streaming: update outside render frame");
  ++s.frame;s.stats.uploaded_bytes=0;s.stats.upload_ms=0;
  std::deque<Result> results;
  {std::lock_guard lock(s.mutex);results.swap(s.results);}s.wake.notify_all();
  // Retire cancellations before admitting completions against the memory budget.
  for(auto &r:s.records) if(r.release||r.info.state==stream_state::cancelled) {
    if(r.release) clear(ctx,r);else if(r.payload||r.info.gpu_bytes) fallback(ctx,r);
    r.info.state=stream_state::cancelled;
  }
  usize cpu=0,gpu=0;
  for(auto &r:s.records) {cpu+=r.info.cpu_bytes;gpu+=r.info.gpu_bytes;}
  for(auto &done:results) {
    auto &r=s.records[done.id-1];
    if(done.generation!=r.generation||r.token->cancel) continue;
    if(!done.error.empty()) {r.info.state=stream_state::failed;r.info.error=done.error;++s.stats.failures;continue;}
    const auto bytes=done.payload->cpu_bytes();
    if(bytes<=s.config.cpu_bytes) {
      for(auto &cold:s.records) {
        if(cpu<=s.config.cpu_bytes-bytes) break;
        if(!cold.pinned&&cold.info.state==stream_state::ready&&s.frame-cold.used>s.config.idle_frames) {
          cpu-=cold.info.cpu_bytes;gpu-=cold.info.gpu_bytes;fallback(ctx,cold);cold.info.state=stream_state::evicted;++s.stats.evictions;
        }
      }
    }
    if(bytes>s.config.cpu_bytes||cpu>s.config.cpu_bytes-bytes||done.payload->gpu_bytes()>s.config.gpu_bytes) {
      r.info={stream_state::failed,0,0,"stream CPU/GPU admission budget exceeded"};++s.stats.failures;continue;
    }
    done.payload->detail(r.detail);
    r.info.state=stream_state::uploading;r.info.cpu_bytes=bytes;r.info.target_level=done.payload->target_level();cpu+=bytes;r.payload=std::move(done.payload);
  }
  // The renderer has completed the previous submission. bgfx owns deferred GPU destruction.
  gpu=0;for(const auto &r:s.records) gpu+=r.info.gpu_bytes;
  const auto make_room=[&](usize required) {
    std::vector<Record*> cold;
    for(auto &r:s.records) if(!r.pinned&&r.info.state==stream_state::ready&&s.frame-r.used>s.config.idle_frames) cold.push_back(&r);
    std::stable_sort(cold.begin(),cold.end(),[](auto a,auto b){return a->used<b->used;});
    for(auto *r:cold) {
      if(gpu+required<=s.config.gpu_bytes) break;
      if(r->payload&&r->payload->coarsen()) {r->info.state=stream_state::uploading;r->info.target_level=r->payload->target_level();continue;}
      gpu-=r->info.gpu_bytes;fallback(ctx,*r);r->info.state=stream_state::evicted;++s.stats.evictions;
    }
    return gpu+required<=s.config.gpu_bytes;
  };
  make_room(0);
  std::vector<Record*> uploads;
  for(auto &r:s.records) if(r.info.state==stream_state::uploading) uploads.push_back(&r);
  std::stable_sort(uploads.begin(),uploads.end(),[](auto a,auto b){return a->priority>b->priority;});
  const auto start=Clock::now();
  u32 units=0;
  for(auto *p:uploads) {
    auto &r=*p;
    if(!r.payload->upload_available()) continue;
    try {
      while(!r.payload->done()) {
        if(units>=s.config.upload_units_per_frame) break;
        const auto bytes=r.payload->next_bytes();
        if(bytes>s.config.upload_bytes_per_frame) {
          if(r.payload->persistent()&&r.info.renderable) {r.info.error="detail upload unit exceeds per-frame byte budget; retaining resident fallback";break;}
          throw std::runtime_error("stream upload unit exceeds per-frame byte budget");
        }
        if(s.stats.uploaded_bytes+bytes>s.config.upload_bytes_per_frame) break;
        const auto after=r.payload->resident_after_upload(r.info.gpu_bytes);
        const auto growth=after>r.info.gpu_bytes?after-r.info.gpu_bytes:0;
        if(growth&&gpu+growth>s.config.gpu_bytes) {
          if(!make_room(growth)) {r.info.error="GPU budget waiting for unpinned idle resources";break;}
        }
        r.payload->upload(ctx);++units;gpu=gpu-r.info.gpu_bytes+after;r.info.gpu_bytes=after;s.stats.uploaded_bytes+=bytes;
        if(r.payload->publish_step(ctx,r.handle)) {r.info.renderable=true;r.info.resident_level=r.payload->resident_level();}
        if(std::chrono::duration<float,std::milli>(Clock::now()-start).count()>=s.config.upload_ms_per_frame) break;
      }
      if(r.payload->done()) {
        r.payload->publish(ctx,r.handle);r.info.renderable=true;r.info.resident_level=r.payload->resident_level();
        if(!r.payload->persistent()) r.payload.reset();
        r.info.state=stream_state::ready;r.info.error.clear();
      }
    } catch(const std::exception &e) {
      gpu-=r.info.gpu_bytes;
      fallback(ctx,r);r.info={stream_state::failed,0,0,e.what()};++s.stats.failures;
    }
    if(std::chrono::duration<float,std::milli>(Clock::now()-start).count()>=s.config.upload_ms_per_frame) break;
  }
  s.stats.upload_ms=std::chrono::duration<float,std::milli>(Clock::now()-start).count();
  s.stats.peak_uploaded_bytes=std::max(s.stats.peak_uploaded_bytes,s.stats.uploaded_bytes);
  s.stats.gpu_budget_blocked=gpu>s.config.gpu_bytes;
  s.stats.cpu_bytes=s.stats.gpu_bytes=0;s.stats.queued=s.stats.loading=s.stats.uploading=0;
  for(auto &r:s.records) {
    s.stats.cpu_bytes+=r.info.cpu_bytes;s.stats.gpu_bytes+=r.info.gpu_bytes;
    const auto state=r.info.state==stream_state::queued&&r.token->loading?stream_state::loading:r.info.state;
    s.stats.queued+=state==stream_state::queued;s.stats.loading+=state==stream_state::loading;s.stats.uploading+=state==stream_state::uploading;
  }
}
namespace {
struct TexturePayload:StreamPayload {
  RenderImageAsset image;bool uploaded=false;RenderTextureHandle texture;
  usize cpu_bytes() const override {return image.pixels.size();}
  usize gpu_bytes() const override {return image.pixels.size();}
  usize next_bytes() const override {return gpu_bytes();}
  bool done() const override {return uploaded;}
  void upload(context &ctx) override {
    texture=renderer_create_texture(ctx,image.view());
    ctx.window.graphics->ensure_texture(texture,image.view());uploaded=true;
  }
  void publish(context &ctx,u32 handle) override {
    auto *slot=const_cast<texture_slot*>(texture_slot_of(*ctx.texture,{handle}));
    if(!slot) throw std::runtime_error("stream texture was unloaded");
    renderer_destroy_texture(ctx,slot->portable);slot->portable=texture;texture={};
    slot->texture.width=int(image.width);slot->texture.height=int(image.height);++slot->version;
  }
  void discard(context &ctx) override {if(texture) {renderer_destroy_texture(ctx,texture);texture={};}}
};
struct MipPayload:StreamPayload {
  RenderImageAsset image;
  RenderMipMode mode;
  rhi::ImageMipChain chain;
  std::vector<usize> offsets;
  std::vector<std::pair<u32,u32>> sizes;
  u32 resident=~u32(0),target=0;
  RenderTextureHandle pending;
  explicit MipPayload(RenderImageAsset source,stream_texture_kind kind):image(std::move(source)),mode(kind==stream_texture_kind::normal?RenderMipMode::Normal:kind==stream_texture_kind::data?RenderMipMode::Linear:RenderMipMode::SRGB),chain([&] {
    auto view=image.view();view.mips=mode;return view;
  }()) {
    u32 w=image.width,h=image.height;usize offset=0;
    for(unsigned i=0;i<chain.levels;++i) {offsets.push_back(offset);sizes.emplace_back(w,h);offset+=usize(w)*h*4;w=std::max(1u,w/2);h=std::max(1u,h/2);}
  }
  bool persistent() const override {return true;}
  usize cpu_bytes() const override {return image.pixels.size()+chain.pixels.size();}
  usize gpu_bytes() const override {return 4;}
  u32 next_level() const {return resident==~u32(0)?chain.levels-1:resident<target?target:resident-1;}
  usize next_bytes() const override {return chain.pixels.size()-offsets.at(next_level());}
  usize resident_after_upload(usize) const override {return next_bytes();}
  bool done() const override {return resident==target;}
  void detail(u32 level) override {target=std::min(level,chain.levels-1);}
  bool coarsen() override {if(resident>=chain.levels-1) return false;target=resident+1;return true;}
  u32 resident_level() const override {return resident;}
  u32 target_level() const override {return target;}
  void upload(context &ctx) override {
    const auto level=next_level();const auto [w,h]=sizes[level];
    const auto data=std::span<const u8>(chain.pixels).subspan(offsets[level]);
    RenderImage view{w,h,data.first(usize(w)*h*4),RenderImageFormat::RGBA8,mode};
    pending=renderer_create_texture(ctx,view);
    ctx.window.graphics->ensure_texture_prepared(pending,view,data,chain.levels-level);
    resident=level;
  }
  bool publish_step(context &ctx,u32 handle) override {
    auto *slot=const_cast<texture_slot*>(texture_slot_of(*ctx.texture,{handle}));
    if(!slot) throw std::runtime_error("stream mip texture unloaded");
    renderer_destroy_texture(ctx,slot->portable);slot->portable=pending;pending={};
    slot->filter=filter_mipmap;slot->texture.width=int(image.width);slot->texture.height=int(image.height);++slot->version;
    return true;
  }
  void publish(context &,u32) override {}
  void discard(context &ctx) override {if(pending) {renderer_destroy_texture(ctx,pending);pending={};}}
};
}
stream_handle texture_request_mips(context &ctx,const char *path,int priority,stream_texture_kind kind) {
  if(!path||!*path) throw std::invalid_argument("njin streaming: empty mip texture path");
  if(kind<stream_texture_kind::color||kind>stream_texture_kind::data) throw std::invalid_argument("njin streaming: invalid mip texture kind");
  const auto resolved=asset_path(path);
  return stream_request(ctx,"mips:"+resolved+":"+std::to_string(int(kind)),priority,false,[resolved,kind] {return std::make_unique<MipPayload>(renderer_load_image(resolved.c_str()),kind);},
    [resolved](context &c,u32 previous) {
      const u8 color[]{255,0,255,255};auto handle=texture_from_pixels(c,color,1,1);
      c.texture->slots.at(handle.id-1).path=resolved;
      c.texture->slots.at(handle.id-1).streaming_mips=true;
      if(previous) {auto &slots=c.texture->slots;slots.at(previous-1)=std::move(slots.at(handle.id-1));slots.at(handle.id-1)={};return previous;}
      return handle.id;
    },[](context &c,u32 id){texture_unload(c,{id});});
}
stream_handle texture_request(context &ctx,const char *path,int priority) {
  if(!path||!*path) throw std::invalid_argument("njin streaming: empty texture path");
  const auto resolved=asset_path(path);
  return stream_request(ctx,"texture:"+resolved,priority,false,[resolved] {
    auto p=std::make_unique<TexturePayload>();p->image=renderer_load_image(resolved.c_str());return p;
  },[](context &c,u32 previous) {
    const u8 rgba[]{255,0,255,255};auto handle=texture_from_pixels(c,rgba,1,1);
    if(previous) {auto &slots=c.texture->slots;slots.at(previous-1)=std::move(slots.at(handle.id-1));slots.at(handle.id-1)={};return previous;}
    return handle.id;
  },
  [](context &c,u32 id) {texture_unload(c,{id});});
}
}
