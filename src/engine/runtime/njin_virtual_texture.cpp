#include "njin_virtual_texture.h"
#include "njin_virtual_texture_internal.h"
#include "njin_stream_internal.h"
#include "njin_ctx_impl.h"
#include "njin_texture.h"
#include "njin_render.h"
#include "njin_path.h"
#include "njin_image_mips.h"
#if NJIN_BGFX
#include "njin_bgfx.h"
#endif
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
namespace njin {
namespace {
bool power2(u32 n) {return n&&!(n&(n-1));}
void put32(std::ostream &out,u32 value) {for(unsigned i=0;i<4;++i) out.put(char(value>>(i*8)));}
u32 get32(std::istream &in) {u32 value=0;for(unsigned i=0;i<4;++i) {const int c=in.get();if(c<0) throw std::runtime_error("NVT: truncated header");value|=u32(c)<<(i*8);}return value;}
void dimensions(u32 size,u32 page) {if(!power2(size)||size>16384||!power2(page)||page<8||page>128||page>size) throw std::invalid_argument("NVT: square power-of-two size <=16384, page 8..128 required");}
}
struct VirtualTexture {
  struct Cell {u32 entry=~u32(0),generation=0,mip=0,x=0,y=0;u64 used=0;bool ready=false;stream_handle job;};
  std::string path;
  u32 size=0,page=0,max_mip=0,side=0,table_rows=0,atlas_width=0,atlas_height=0;
  u32 idle=0;bool alpha=false,closed=false,tail_ready=false;
  texture_handle handle;RenderTextureHandle atlas;
  std::vector<u32> grid,offset;
  std::vector<Cell> cells;
  RenderImageAsset cpu;
  virtual_texture_stats stats;
  usize tile_bytes() const {return usize(page+2)*(page+2)*4;}
  usize table_bytes() const {return usize(atlas_width)*table_rows*4;}
  std::array<float,8> params() const {return {float(size),float(page),float(side),float(table_rows),float(atlas_width),float(atlas_height),float(max_mip),0};}
  void table() {
    // Every lookup resolves to an initialized ancestor, with the permanent tail last.
    std::vector<u32> resident(offset.back()+1,~u32(0));
    for(u32 i=0;i<cells.size();++i) if(cells[i].ready) resident[cells[i].entry]=i;
    for(u32 mip=0;mip<=max_mip;++mip) for(u32 y=0;y<grid[mip];++y) for(u32 x=0;x<grid[mip];++x) {
      u32 selected=0;
      for(u32 ancestor=mip;ancestor<=max_mip;++ancestor) {
        const u32 shift=ancestor-mip,ax=x>>shift,ay=y>>shift;
        const auto entry=offset[ancestor]+ay*grid[ancestor]+ax;
        if(resident[entry]!=~u32(0)) {selected=resident[entry];break;}
      }
      const auto at=usize(offset[mip]+y*grid[mip]+x)*4;
      cpu.pixels[at]=u8(selected%side);cpu.pixels[at+1]=u8(selected/side);
      cpu.pixels[at+2]=u8(cells[selected].mip);cpu.pixels[at+3]=255;
    }
  }
};
namespace {
struct PagePayload:StreamPayload {
  std::weak_ptr<VirtualTexture> owner;
  u32 cell=0,generation=0;
  bool root=false,uploaded=false;
  std::vector<u8> pixels;
  usize atlas_bytes=0,table_bytes=0;
  usize cpu_bytes() const override {return pixels.size()+(root?atlas_bytes*2:0);}
  usize gpu_bytes() const override {return root?atlas_bytes:0;}
  usize next_bytes() const override {return root?atlas_bytes:pixels.size()+table_bytes;}
  usize resident_after_upload(usize) const override {return gpu_bytes();}
  bool done() const override {return uploaded;}
  bool upload_available() const override {const auto s=owner.lock();return !s||root||s->tail_ready||s->closed;}
  void publish(context &,u32) override {}
  void upload(context &ctx) override {
    const auto state=owner.lock();
    if(!state||state->closed||state->cells.at(cell).generation!=generation) throw std::runtime_error("NVT: stale page completion");
    auto &s=*state;auto &record=s.cells[cell];const auto edge=s.page+2;
    const u32 left=(cell%s.side)*edge,top=s.table_rows+(cell/s.side)*edge;
    for(u32 y=0;y<edge;++y) std::copy_n(pixels.data()+usize(y)*edge*4,edge*4,s.cpu.pixels.data()+(usize(top+y)*s.atlas_width+left)*4);
    record.ready=true;s.table();
    auto *slot=const_cast<texture_slot*>(texture_slot_of(*ctx.texture,s.handle));
    if(!slot) throw std::runtime_error("NVT: owning texture unloaded");
    if(root) {
      auto texture=renderer_create_texture(ctx,s.cpu.view());
      try {ctx.window.graphics->ensure_texture_writable(texture,s.cpu.view());}
      catch(...) {renderer_destroy_texture(ctx,texture);throw;}
      renderer_destroy_texture(ctx,slot->portable);slot->portable=texture;s.atlas=texture;
      auto &resource=ctx.window.renderer_resources.textures.at(texture.id);
      resource.virtual_params=s.params();resource.alpha=s.alpha;
      slot->texture.width=int(s.size);slot->texture.height=int(s.size);slot->filter=filter_linear;s.tail_ready=true;
    } else {
      if(!s.tail_ready) {record.ready=false;throw std::runtime_error("NVT: tail is not resident");}
      ctx.window.graphics->update_texture_region(s.atlas,left,top,edge,edge,pixels);
      ctx.window.graphics->update_texture_region(s.atlas,0,0,s.atlas_width,s.table_rows,std::span<const u8>(s.cpu.pixels).first(s.table_bytes()));
      auto &cpu=ctx.window.renderer_resources.textures.at(s.atlas.id).cpu.pixels;
      std::copy_n(s.cpu.pixels.begin(),s.table_bytes(),cpu.begin());
      for(u32 y=0;y<edge;++y) std::copy_n(pixels.data()+usize(y)*edge*4,edge*4,cpu.data()+(usize(top+y)*s.atlas_width+left)*4);
    }
    ++slot->version;uploaded=true;
  }
};
VirtualTexture &state_of(context &ctx,texture_handle h) {
  const auto *slot=texture_slot_of(*ctx.texture,h);
  if(!slot||!slot->virtual_texture||slot->virtual_texture->closed) throw std::invalid_argument("NVT: invalid virtual texture");
  return *slot->virtual_texture;
}
void request_page(context &ctx,const std::shared_ptr<VirtualTexture> &state,u32 mip,u32 x,u32 y,int priority,bool root=false) {
  auto &s=*state;const auto entry=s.offset[mip]+y*s.grid[mip]+x;const auto frame=ctx.window.graphics->frame_serial();
  for(auto &c:s.cells) if(c.entry==entry) {
    c.used=frame;++s.stats.hits;streaming_touch(ctx,c.job,priority);
    const auto info=streaming_info(ctx,c.job);
    if(info.state==stream_state::failed||info.state==stream_state::cancelled) streaming_retry(ctx,c.job);
    return;
  }
  u32 cell=root?0:~u32(0);
  if(!root) for(u32 i=1;i<s.cells.size();++i) {
    if(s.cells[i].entry==~u32(0)) {cell=i;break;}
    if(frame-s.cells[i].used>s.idle&&(cell==~u32(0)||s.cells[i].used<s.cells[cell].used)) cell=i;
  }
  if(cell==~u32(0)) {++s.stats.deferred;return;}
  auto &c=s.cells[cell];if(c.job.id) {streaming_release(ctx,c.job);++s.stats.evictions;}
  ++c.generation;c.entry=entry;c.mip=mip;c.x=x;c.y=y;c.used=frame;c.ready=false;
  const auto generation=c.generation;const auto path=s.path;const auto bytes=s.tile_bytes();const auto offset=u64(20)+u64(entry)*bytes;
  const auto atlas_bytes=s.cpu.pixels.size(),table_bytes=s.table_bytes();const std::weak_ptr<VirtualTexture> weak=state;
  c.job=stream_request(ctx,"nvt:"+std::to_string(s.handle.id)+":"+std::to_string(entry)+":"+std::to_string(generation),priority,false,
    [weak,cell,generation,path,bytes,offset,root,atlas_bytes,table_bytes] {
      auto payload=std::make_unique<PagePayload>();payload->owner=weak;payload->cell=cell;payload->generation=generation;payload->root=root;
      payload->atlas_bytes=atlas_bytes;payload->table_bytes=table_bytes;payload->pixels.resize(bytes);
      std::ifstream file(path,std::ios::binary);file.seekg(std::streamoff(offset));
      if(!file.read(reinterpret_cast<char*>(payload->pixels.data()),std::streamsize(bytes))) throw std::runtime_error("NVT: page read failed: "+path);
      return payload;
    },[](context &,u32){return 0u;},[](context &,u32){});
  streaming_pin(ctx,c.job);++s.stats.misses;
}
}
void virtual_texture_cook(const char *image,const char *output,u32 page) {
  if(!output||!*output) throw std::invalid_argument("NVT: empty output path");
  auto source=renderer_load_image(image);dimensions(source.width,page);
  if(source.width!=source.height) throw std::invalid_argument("NVT: square image required");
  bool alpha=false;for(size_t i=3;i<source.pixels.size();i+=4) alpha|=source.pixels[i]!=255;
  auto view=source.view();view.mips=RenderMipMode::SRGB;const rhi::ImageMipChain chain(view);
  std::ofstream file(output,std::ios::binary|std::ios::trunc);if(!file) throw std::runtime_error("NVT: cannot create output");
  file.write("NVT1",4);put32(file,source.width);put32(file,page);put32(file,chain.levels);put32(file,alpha?1:0);
  size_t offset=0;u32 size=source.width;std::vector<u8> tile(usize(page+2)*(page+2)*4);
  for(u32 mip=0;mip<chain.levels;++mip) {
    const u32 grid=std::max(1u,size/page);
    for(u32 y=0;y<grid;++y) for(u32 x=0;x<grid;++x) {
      for(u32 j=0;j<page+2;++j) for(u32 i=0;i<page+2;++i) {
        const u32 sx=(x*page+i+size-1)%size,sy=(y*page+j+size-1)%size;
        std::copy_n(chain.pixels.data()+offset+(usize(sy)*size+sx)*4,4,tile.data()+(usize(j)*(page+2)+i)*4);
      }
      file.write(reinterpret_cast<const char*>(tile.data()),std::streamsize(tile.size()));
    }
    offset+=usize(size)*size*4;size=std::max(1u,size/2);
  }
  if(!file) throw std::runtime_error("NVT: output write failed");
}
texture_handle virtual_texture_load(context &ctx,const char *path,const virtual_texture_desc &desc) {
  if(ctx.cfg.graphics_backend==GraphicsBackend::OpenGL||ctx.window.graphics->frame_open()) throw std::logic_error("NVT: load on SDL3/bgfx outside render frames");
  if(!path||!*path||desc.cache_side<2||desc.cache_side>16) throw std::invalid_argument("NVT: invalid path/cache size");
#if NJIN_BGFX
  ctx.window.graphics->virtual_texture_shader(rhi::bgfx_virtual_shader_bytes(ctx.window.graphics->shader_profile()));
#endif
  auto s=std::make_shared<VirtualTexture>();s->path=asset_path(path);std::ifstream file(s->path,std::ios::binary);
  char magic[4];if(!file.read(magic,4)||std::memcmp(magic,"NVT1",4)) throw std::runtime_error("NVT: invalid file magic");
  s->size=get32(file);s->page=get32(file);const auto levels=get32(file),flags=get32(file);dimensions(s->size,s->page);
  u32 expected=1;for(u32 n=s->size;n>1;n/=2) ++expected;
  if(levels!=expected||flags>1) throw std::runtime_error("NVT: invalid level count/flags");
  if(flags) throw std::runtime_error("NVT: virtual albedo currently requires opaque pixels");
  s->alpha=false;s->max_mip=levels-1;s->side=desc.cache_side;s->idle=desc.idle_frames;
  u32 count=0,size=s->size;for(u32 mip=0;mip<levels;++mip) {const auto grid=std::max(1u,size/s->page);s->grid.push_back(grid);s->offset.push_back(count);count+=grid*grid;size=std::max(1u,size/2);}
  file.seekg(0,std::ios::end);if(file.tellg()!=std::streamoff(u64(20)+u64(count)*s->tile_bytes())) throw std::runtime_error("NVT: truncated/invalid page directory");
  s->atlas_width=(s->page+2)*s->side;s->table_rows=(count+s->atlas_width-1)/s->atlas_width;s->atlas_height=s->table_rows+(s->page+2)*s->side;
  if(s->atlas_height>ctx.window.graphics->max_texture_size()||s->atlas_width>ctx.window.graphics->max_texture_size()) throw std::runtime_error("NVT: atlas exceeds device limits");
  s->cpu={s->atlas_width,s->atlas_height,std::vector<u8>(usize(s->atlas_width)*s->atlas_height*4)};
  for(size_t i=0;i<s->cpu.pixels.size();i+=4) {s->cpu.pixels[i]=255;s->cpu.pixels[i+2]=255;s->cpu.pixels[i+3]=255;}
  s->cells.resize(usize(s->side)*s->side);s->cells[0].mip=s->max_mip;
  const u8 color[]{255,0,255,255};s->handle=texture_from_pixels(ctx,color,1,1);auto &slot=ctx.texture->slots.at(s->handle.id-1);slot.virtual_texture=s;
  slot.virtual_close=[](context &c,VirtualTexture &texture){virtual_texture_forget(c,texture.handle);};
  slot.texture.width=int(s->size);slot.texture.height=int(s->size);
  try {request_page(ctx,s,s->max_mip,0,0,std::numeric_limits<int>::max(),true);}
  catch(...) {texture_unload(ctx,s->handle);throw;}
  s->stats.gpu_bytes=s->cpu.pixels.size();return s->handle;
}
void virtual_texture_request(context &ctx,texture_handle handle,vec2 lo,vec2 hi,u32 mip,int priority) {
  auto &s=state_of(ctx,handle);
  if(ctx.window.graphics->frame_open()) throw std::logic_error("NVT: request demand during update, before render frame");
  if(!std::isfinite(lo.x)||!std::isfinite(lo.y)||!std::isfinite(hi.x)||!std::isfinite(hi.y)||lo.x<0||lo.y<0||hi.x>1||hi.y>1||hi.x<=lo.x||hi.y<=lo.y)
    throw std::invalid_argument("NVT: UV demand must be a nonempty rectangle in [0,1]");
  mip=std::min(mip,s.max_mip);auto owner=ctx.texture->slots.at(handle.id-1).virtual_texture;
  for(u32 level=s.max_mip+1;level-->mip;) {
    const auto grid=s.grid[level];const u32 x0=u32(lo.x*grid),y0=u32(lo.y*grid),x1=std::min(grid,u32(std::ceil(hi.x*grid))),y1=std::min(grid,u32(std::ceil(hi.y*grid)));
    const auto rank=int(std::min<i64>(std::numeric_limits<int>::max(),i64(priority)+level));
    for(u32 y=y0;y<y1;++y) for(u32 x=x0;x<x1;++x) request_page(ctx,owner,level,x,y,rank);
  }
}
virtual_texture_stats virtual_texture_get(context &ctx,texture_handle handle) {
  auto &s=state_of(ctx,handle);auto result=s.stats;result.tail_ready=s.tail_ready;
  if(!s.tail_ready) result.gpu_bytes=0;
  for(const auto &c:s.cells) if(c.job.id) {
    const auto info=streaming_info(ctx,c.job);result.resident+=c.ready;
    result.pending+=info.state==stream_state::queued||info.state==stream_state::loading||info.state==stream_state::uploading;
    if(info.state==stream_state::failed||!info.error.empty()) result.error=info.error;
  }
  return result;
}
void virtual_texture_forget(context &ctx,texture_handle handle) {
  const auto *slot=texture_slot_of(*ctx.texture,handle);if(!slot||!slot->virtual_texture) return;
  auto s=slot->virtual_texture;s->closed=true;
  if(ctx.streaming) for(auto &c:s->cells) if(c.job.id) streaming_release(ctx,c.job);
}
}
