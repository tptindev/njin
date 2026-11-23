#pragma once
#include "njin_internal_only.h"
#include "njin_stream.h"
#include <atomic>
#include <functional>
#include <memory>
namespace njin {
// Worker owns a payload exclusively until completion; main owns it afterwards.
struct StreamPayload {
  virtual ~StreamPayload()=default;
  virtual usize cpu_bytes() const=0;
  virtual usize gpu_bytes() const=0;
  virtual usize next_bytes() const=0;
  virtual void upload(context &)=0;
  virtual bool done() const=0;
  virtual void publish(context &,u32)=0;
  virtual void discard(context &) {} // main-thread partial GPU cleanup
  virtual bool persistent() const {return false;}
  virtual bool publish_step(context &,u32) {return false;}
  virtual void detail(u32) {}
  virtual u32 resident_level() const {return 0;}
  virtual u32 target_level() const {return 0;}
  virtual bool coarsen() {return false;}
  virtual usize resident_after_upload(usize previous) const {return previous+next_bytes();}
  virtual bool upload_available() const {return true;}
};
using StreamPrepare=std::function<std::unique_ptr<StreamPayload>()>;
struct StreamStore;
std::shared_ptr<StreamStore> stream_store_create();
stream_handle stream_request(context &,std::string,int,bool,StreamPrepare,
                            std::function<u32(context &,u32)>,std::function<void(context &,u32)>);
void stream_forget(context &,bool,u32);
u32 stream_resource(context &,stream_handle,bool);
void streaming_shutdown(context &);
}
