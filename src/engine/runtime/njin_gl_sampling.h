#pragma once
#include "njin_internal_only.h"
#include "njin_gl_proc.h"
#include "njin_renderer.h"
#include <unordered_map>
#include <stdexcept>
namespace njin::rhi {
// GL 3.3 sampler objects keep per-map sampling independent when several slots
// borrow the same texture. Lifetime is bounded by RaylibGraphicsDevice/context.
class GlSampling {
  using Generate=void(*)(int,unsigned*);
  using Bind=void(*)(unsigned,unsigned);
  using Parameter=void(*)(unsigned,unsigned,int);
  using Delete=void(*)(int,const unsigned*);
  Generate generate=nullptr; Bind bind=nullptr; Parameter parameter=nullptr; Delete release=nullptr;
  std::unordered_map<unsigned,unsigned> samplers;
public:
  ~GlSampling() { if(release) for(auto [key,id]:samplers) release(1,&id); }
  void set(unsigned unit,bool linear,RenderWrap s,RenderWrap t,RenderMinFilter min=RenderMinFilter::Inherit) {
    if(!generate) {
      generate=reinterpret_cast<Generate>(detail::gl_proc_address("glGenSamplers"));
      bind=reinterpret_cast<Bind>(detail::gl_proc_address("glBindSampler"));
      parameter=reinterpret_cast<Parameter>(detail::gl_proc_address("glSamplerParameteri"));
      release=reinterpret_cast<Delete>(detail::gl_proc_address("glDeleteSamplers"));
      if(!generate||!bind||!parameter||!release) {generate=nullptr;throw std::runtime_error("njin OpenGL: GL 3.3 sampler objects are unavailable");}
    }
    const unsigned key=unsigned(linear)|(unsigned(s)<<1)|(unsigned(t)<<3)|(unsigned(min)<<5);
    auto it=samplers.find(key);
    if(it==samplers.end()) {
      unsigned id=0; generate(1,&id);
      if(!id) throw std::runtime_error("njin OpenGL: sampler allocation failed");
      const auto wrap=[](RenderWrap w){return w==RenderWrap::Clamp?0x812F:w==RenderWrap::Mirror?0x8370:0x2901;};
      const unsigned filters[]{0,0x2600,0x2601,0x2700,0x2701,0x2702,0x2703};
      parameter(id,0x2801,min==RenderMinFilter::Inherit?(linear?0x2601:0x2600):filters[unsigned(min)]); parameter(id,0x2800,linear?0x2601:0x2600);
      parameter(id,0x2802,wrap(s)); parameter(id,0x2803,wrap(t));
      try { it=samplers.emplace(key,id).first; } catch(...) { release(1,&id); throw; }
    }
    bind(unit,it->second);
  }
  void clear() noexcept { if(bind) for(unsigned i=0;i<11;++i) bind(i,0); }
};
}
