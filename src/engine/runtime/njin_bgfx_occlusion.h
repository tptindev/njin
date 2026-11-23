#pragma once
#include "njin_internal_only.h"
#include "njin_bgfx.h"
#include <bgfx/bgfx.h>
#include <algorithm>
#include <memory>
#include <sstream>
#include <vector>
namespace njin::rhi {
// GPU resources riêng của device; không chuyển native handle qua gameplay/ECS.
class BgfxOcclusion {
  template<class T> static T check(T h) {
    if(!bgfx::isValid(h)) throw std::runtime_error("njin Hi-Z: GPU resource allocation failed");
    return h;
  }
  static void advance(bgfx::ViewId &view) {
    if(unsigned(view)+1>=bgfx::getCaps()->limits.maxViews) throw std::runtime_error("njin Hi-Z: frame exceeds view limit");
    ++view;
  }
  bgfx::ProgramHandle program(unsigned stage,std::string_view format) {
    auto &h=programs[stage];
    if(!bgfx::isValid(h)) {
      const auto bytes=bgfx_compute_shader_bytes(format,stage+5);
      if(bytes.empty()) throw std::runtime_error("njin Hi-Z: compute shader profile was not built");
      h=check(bgfx::createProgram(check(bgfx::createShader(bgfx::copy(bytes.data(),uint32_t(bytes.size())))),true));
    }
    return h;
  }
  void uniforms() {
    if(bgfx::isValid(size_uniform)) return;
    size_uniform=check(bgfx::createUniform("u_njinCullSize",bgfx::UniformType::Vec4));
    info_uniform=check(bgfx::createUniform("u_njinCullInfo",bgfx::UniformType::Vec4));
    bounds_uniform=check(bgfx::createUniform("u_njinCullBounds",bgfx::UniformType::Vec4));
    vp_uniform=check(bgfx::createUniform("u_njinCullVP",bgfx::UniformType::Mat4));
    depth_sampler=check(bgfx::createUniform("s_njinCullDepth",bgfx::UniformType::Sampler));
    pyramid_sampler=check(bgfx::createUniform("s_njinCullPyramid",bgfx::UniformType::Sampler));
    flags_uniform=check(bgfx::createUniform("u_njinCullFlags",bgfx::UniformType::Vec4));
    camera_uniform=check(bgfx::createUniform("u_njinCullCamera",bgfx::UniformType::Vec4));
    lod_uniform=check(bgfx::createUniform("u_njinCullLod",bgfx::UniformType::Vec4));
    planes_uniform=check(bgfx::createUniform("u_njinCullCameraPlanes",bgfx::UniformType::Vec4,6));
  }
  std::array<bgfx::ProgramHandle,4> programs{{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE}};
  bgfx::UniformHandle size_uniform=BGFX_INVALID_HANDLE,info_uniform=BGFX_INVALID_HANDLE,bounds_uniform=BGFX_INVALID_HANDLE,vp_uniform=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle depth_sampler=BGFX_INVALID_HANDLE,pyramid_sampler=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle flags_uniform=BGFX_INVALID_HANDLE,camera_uniform=BGFX_INVALID_HANDLE,lod_uniform=BGFX_INVALID_HANDLE;
  bgfx::UniformHandle planes_uniform=BGFX_INVALID_HANDLE;
  bgfx::TextureHandle pyramid=BGFX_INVALID_HANDLE;
  bgfx::TextureHandle neutral_depth=BGFX_INVALID_HANDLE;
  unsigned width=0,height=0,viewport_w=0,viewport_h=0,levels=0;
  bool ready=false;
  bgfx::ViewId batch_reset=0,batch_cull=0,batch_finalize=0,batch_render=0;
  bool batch_started=false;
  unsigned batch_key=0;
public:
  struct Job {
    bgfx::DynamicVertexBufferHandle source=BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle visible=BGFX_INVALID_HANDLE;
    bgfx::DynamicIndexBufferHandle count=BGFX_INVALID_HANDLE;
    bgfx::IndirectBufferHandle indirect=BGFX_INVALID_HANDLE;
    unsigned input=0,capacity=0;
    bool shadow=false;
    std::uint64_t last_used=0;
    ~Job() {
      if(bgfx::isValid(source)) bgfx::destroy(source);
      if(bgfx::isValid(visible)) bgfx::destroy(visible);
      if(bgfx::isValid(count)) bgfx::destroy(count);
      if(bgfx::isValid(indirect)) bgfx::destroy(indirect);
    }
  };
  std::vector<std::unique_ptr<Job>> jobs;
  size_t active_jobs=0;
  std::uint64_t frame=0;
  // builds, actual dispatches, indirect draws, buffer allocations, pool slots/bytes.
  std::array<std::uint64_t,6> counters{};
  ~BgfxOcclusion() {release();}
  void release() {
    jobs.clear();active_jobs=0;ready=false;counters[4]=counters[5]=0;
    if(bgfx::isValid(pyramid)) {bgfx::destroy(pyramid);pyramid=BGFX_INVALID_HANDLE;}
    if(bgfx::isValid(neutral_depth)) {bgfx::destroy(neutral_depth);neutral_depth=BGFX_INVALID_HANDLE;}
    for(auto &h:programs) if(bgfx::isValid(h)) {bgfx::destroy(h);h=BGFX_INVALID_HANDLE;}
    for(auto *h:{&size_uniform,&info_uniform,&bounds_uniform,&vp_uniform,&depth_sampler,&pyramid_sampler,&flags_uniform,&camera_uniform,&lod_uniform,&planes_uniform})
      if(bgfx::isValid(*h)) {bgfx::destroy(*h);*h=BGFX_INVALID_HANDLE;}
  }
  void begin_frame() {
    ++frame;active_jobs=0;ready=false;batch_started=false;
    // An intermittent large scene must not permanently retain its high-water mark.
    std::erase_if(jobs,[&](const auto &job){return frame-job->last_used>120;});
    pool_stats();
  }
  void pool_stats() {
    counters[4]=jobs.size();counters[5]=0;
    for(const auto &job:jobs) counters[5]+=std::uint64_t(job->capacity)*sizeof(RenderInstance)*2+16+32;
  }
  static bool available() {
    const auto *caps=bgfx::getCaps();
    const auto flags=BGFX_CAPS_COMPUTE|BGFX_CAPS_DRAW_INDIRECT;
    const auto images=BGFX_CAPS_FORMAT_TEXTURE_2D|BGFX_CAPS_FORMAT_TEXTURE_IMAGE_READ|BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE;
    return (caps->supported&flags)==flags&&(caps->formats[bgfx::TextureFormat::R32F]&images)==images
      &&caps->limits.maxComputeBindings>=5&&caps->limits.maxTextureSamplers>=5
      &&caps->limits.maxInstanceData>=5&&caps->limits.maxVertexAttributes>=11;
  }
  static std::string diagnostics() {
    const auto &c=*bgfx::getCaps();
    const auto f=c.formats[bgfx::TextureFormat::R32F];
    std::ostringstream out;
    out<<"Hi-Z renderer="<<bgfx::getRendererName(c.rendererType)
       <<" vendor="<<std::hex<<c.vendorId<<" device="<<c.deviceId
       <<" caps=0x"<<c.supported<<" R32F=0x"<<f<<std::dec
       <<" compute="<<bool(c.supported&BGFX_CAPS_COMPUTE)
       <<" indirect="<<bool(c.supported&BGFX_CAPS_DRAW_INDIRECT)
       <<" image_rw="<<bool(c.supported&BGFX_CAPS_IMAGE_RW)
       <<" sampled="<<bool(f&BGFX_CAPS_FORMAT_TEXTURE_2D)
       <<" image_read="<<bool(f&BGFX_CAPS_FORMAT_TEXTURE_IMAGE_READ)
       <<" image_write="<<bool(f&BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE)
       <<" compute_bindings="<<c.limits.maxComputeBindings
       <<" texture_samplers="<<c.limits.maxTextureSamplers
       <<" instance_slots="<<c.limits.maxInstanceData
       <<" instancing="<<(c.limits.maxInstanceData>=5&&c.limits.maxVertexAttributes>=11)
       <<" vertex_attributes="<<c.limits.maxVertexAttributes
       <<" max_texture="<<c.limits.maxTextureSize
       <<" max_views="<<c.limits.maxViews
       <<" path="<<(available()?"compute-r32f-indirect":"unavailable");
    return out.str();
  }
  void build(bgfx::TextureHandle depth,unsigned w,unsigned h,bgfx::ViewId &view,std::string_view format) {
    if(!available()) throw std::runtime_error(diagnostics());
    uniforms();
    unsigned pw=1,ph=1;while(pw<w) pw*=2;while(ph<h) ph*=2;
    if(pw!=width||ph!=height||!bgfx::isValid(pyramid)) {
      const auto made=check(bgfx::createTexture2D(uint16_t(pw),uint16_t(ph),true,1,bgfx::TextureFormat::R32F,BGFX_TEXTURE_COMPUTE_WRITE));
      if(bgfx::isValid(pyramid)) bgfx::destroy(pyramid);
      pyramid=made;width=pw;height=ph;levels=1;
      for(unsigned side=std::max(pw,ph);side>1;side/=2) ++levels;
    }
    viewport_w=w;viewport_h=h;
    const uint32_t nearest=BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP;
    float size[]{float(w),float(h),float(pw),float(ph)};
    advance(view);bgfx::setUniform(size_uniform,size);bgfx::setTexture(0,depth_sampler,depth,nearest);
    bgfx::setImage(1,pyramid,0,bgfx::Access::Write,bgfx::TextureFormat::R32F);
    bgfx::dispatch(view,program(0,format),(pw+7)/8,(ph+7)/8);
    ++counters[1];
    for(unsigned level=1;level<levels;++level) {
      const unsigned nw=std::max(1u,pw/2),nh=std::max(1u,ph/2);
      const float dims[]{float(pw),float(ph),float(nw),float(nh)};
      advance(view);bgfx::setUniform(size_uniform,dims);
      bgfx::setImage(0,pyramid,uint8_t(level-1),bgfx::Access::Read,bgfx::TextureFormat::R32F);
      bgfx::setImage(1,pyramid,uint8_t(level),bgfx::Access::Write,bgfx::TextureFormat::R32F);
      bgfx::dispatch(view,program(1,format),(nw+7)/8,(nh+7)/8);pw=nw;ph=nh;
      ++counters[1];
    }
    advance(view);ready=true;batch_started=false;++counters[0];
  }
  Job &compact(const MeshDraw &draw,bgfx::ViewId &view,std::string_view format) {
    const auto *cfg=draw.gpu_cull.get();const bool hiz=!cfg||cfg->occlusion;
    if(hiz&&!ready) throw std::logic_error("njin Hi-Z: no depth pyramid for this camera pass");
    if(!available()) throw std::runtime_error(diagnostics());
    uniforms();
    // All independent groups consuming this pyramid share the same dependency
    // stages. bgfx orders views, so every reset precedes every cull/finalize/draw.
    if(!batch_started||batch_key!=(cfg?cfg->view_key:0)) {
      advance(view);batch_reset=view;advance(view);batch_cull=view;
      advance(view);batch_finalize=view;advance(view);batch_render=view;
      batch_started=true;
      batch_key=cfg?cfg->view_key:0;
    }
    const unsigned input=draw.instances?unsigned(draw.instance_count?draw.instance_count:draw.instances->size()):1;
    bgfx::VertexLayout layout;layout.begin();
    for(auto a:{bgfx::Attrib::TexCoord7,bgfx::Attrib::TexCoord6,bgfx::Attrib::TexCoord5,bgfx::Attrib::TexCoord4,bgfx::Attrib::TexCoord3,bgfx::Attrib::TexCoord2})
      layout.add(a,4,bgfx::AttribType::Float);
    layout.end();
    if(active_jobs==jobs.size()||jobs[active_jobs]->capacity<input) {
      auto made=std::make_unique<Job>();
      made->capacity=1;while(made->capacity<input) made->capacity*=2;
      made->source=check(bgfx::createDynamicVertexBuffer(made->capacity,layout,BGFX_BUFFER_COMPUTE_READ));
      made->visible=check(bgfx::createDynamicVertexBuffer(made->capacity,layout,BGFX_BUFFER_COMPUTE_READ_WRITE));
      made->count=check(bgfx::createDynamicIndexBuffer(4,BGFX_BUFFER_INDEX32|BGFX_BUFFER_COMPUTE_READ_WRITE));
      made->indirect=check(bgfx::createIndirectBuffer(1));
      if(active_jobs==jobs.size()) jobs.push_back(std::move(made));else jobs[active_jobs]=std::move(made);
      counters[3]+=4;pool_stats();
    }
    auto *job=jobs[active_jobs++].get();job->input=input;job->last_used=frame;job->shadow=draw.shadow_pass;
    const RenderInstance identity{};
    bgfx::update(job->source,0,bgfx::copy(draw.instances?draw.instances->data():&identity,input*sizeof(RenderInstance)));
    float config[]{float(job->input),float(draw.indices.empty()?draw.vertices.size():draw.indices.size()),draw.indices.empty()?0.f:1.f,0};
    const bool fused=job->input<=64;
    if(!fused) {
      bgfx::setUniform(info_uniform,config);
      bgfx::setBuffer(2,job->count,bgfx::Access::ReadWrite);bgfx::setBuffer(3,job->indirect,bgfx::Access::Write);
      bgfx::dispatch(batch_reset,program(3,format),1);
    }
    config[3]=float(levels);bgfx::setUniform(info_uniform,config);
    const float size[]{float(std::max(width,1u)),float(std::max(height,1u)),float(std::max(viewport_w,1u)),float(std::max(viewport_h,1u))};
    const float flags[]{cfg&&cfg->frustum?(cfg->view_key?2.f:1.f):0.f,hiz?1.f:0.f,cfg&&cfg->select_lod?(cfg->shadow_lod?2.f:1.f):0.f,fused?1.f:0.f};
    const float zero[4]{};
    bgfx::setUniform(flags_uniform,flags);bgfx::setUniform(camera_uniform,cfg?cfg->camera.data():zero);bgfx::setUniform(lod_uniform,cfg?cfg->lod.data():zero);
    const std::array<std::array<float,4>,6> empty_planes{};
    bgfx::setUniform(planes_uniform,cfg?cfg->camera_planes.data():empty_planes.data(),6);
    bgfx::setUniform(size_uniform,size);bgfx::setUniform(vp_uniform,cfg?cfg->view_projection.data():draw.transform.data());bgfx::setUniform(bounds_uniform,cfg?cfg->bounds.data():draw.instance_occlusion_bounds.data());
    bgfx::setBuffer(0,job->source,bgfx::Access::Read);bgfx::setBuffer(1,job->visible,bgfx::Access::Write);bgfx::setBuffer(2,job->count,bgfx::Access::ReadWrite);
    bgfx::setBuffer(3,job->indirect,bgfx::Access::Write);
    // Every statically declared sampler needs a valid descriptor, even when
    // this dispatch only tests a light frustum before the first Hi-Z build.
    if(!hiz&&!bgfx::isValid(neutral_depth)) {
      const float far_depth=1;
      neutral_depth=check(bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::R32F,0,bgfx::copy(&far_depth,sizeof(far_depth))));
    }
    bgfx::setTexture(4,pyramid_sampler,hiz?pyramid:neutral_depth,BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT|BGFX_SAMPLER_MIP_POINT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP);
    bgfx::dispatch(batch_cull,program(2,format),(job->input+63)/64);
    if(!fused) {
      config[3]=1;bgfx::setUniform(info_uniform,config);
      bgfx::setBuffer(2,job->count,bgfx::Access::ReadWrite);bgfx::setBuffer(3,job->indirect,bgfx::Access::Write);
      bgfx::dispatch(batch_finalize,program(3,format),1);
    }
    view=std::max(view,batch_render);
    counters[1]+=fused?1:3;return *job;
  }
  std::array<std::uint64_t,2> readback(bool shadow=false) {
    std::array<std::uint64_t,2> out{};
    std::vector<std::array<uint32_t,4>> values(active_jobs);uint32_t ready_frame=0;
    for(size_t i=0;i<active_jobs;++i) {
      if(jobs[i]->shadow!=shadow) continue;
      bgfx::BufferRegion region;region.init(jobs[i]->count);
      ready_frame=std::max(ready_frame,bgfx::read(region,values[i].data()));
    }
    if(!active_jobs) return out;
    bool completed=false;
    for(unsigned i=0;i<32;++i) if(int32_t(bgfx::frame(BGFX_FRAME_FLUSH)-ready_frame)>=0) {completed=true;break;}
    if(!completed) throw std::runtime_error("njin Hi-Z: GPU counter readback did not complete");
    for(size_t i=0;i<active_jobs;++i) {
      if(jobs[i]->shadow!=shadow) continue;
      if(values[i][0]>jobs[i]->input) throw std::runtime_error("njin Hi-Z: invalid GPU visible count");
      out[0]+=jobs[i]->input;out[1]+=values[i][0];
    }
    return out;
  }
};
}
