#include "njin_world3d_impl.h"
#include "njin_model_portable.h"
#include "njin_ctx.h"
#include "njin_ctx_get.h"
#include "njin_frame_impl.h"
#include "njin_texture.h"
#include "njin_quality_impl.h"
#include "njin_material_uniforms.h"
#include <raymath.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace njin {
namespace {
[[noreturn]] void unsupported(const char *what) {throw std::runtime_error(std::string("njin world3d bgfx: ")+what+" is not ported");}
std::shared_ptr<RenderSkyEffects> sky_packet(const sky_params &,vec2,vec3,bool);
sky_params sky_from_light(const light3d &,vec2);
model_store &pass(const context &ctx) {
  model_store &s=ctx_ref<model_store>(const_cast<context&>(ctx));
  if(!s.portable_pass) throw std::logic_error("njin world3d: draw requires begin_3d/end_3d");
  return s;
}
template<class T> T *slot(std::vector<T> &slots,u32 id) {return id&&id<=slots.size()&&slots[id-1].alive?&slots[id-1]:nullptr;}
const texture_slot *image(const context &ctx,texture_handle h) {
  if(!h.id) return nullptr;
  const auto *t=texture_slot_of(ctx_ref<texture_store>(ctx),h);
  if(!t||!t->portable||t->packed) throw std::invalid_argument("njin world3d: invalid or atlas-packed layer texture");
  return t;
}
bool visible(const model_store &s,vec3 lo,vec3 hi) {
  const auto m=MatrixToFloatV(s.portable_view_projection);
  unsigned reject[6]{};
  for(int i=0;i<8;++i) {
    const float x=i&1?hi.x:lo.x,y=i&2?hi.y:lo.y,z=i&4?hi.z:lo.z;
    const float px=m.v[0]*x+m.v[4]*y+m.v[8]*z+m.v[12],py=m.v[1]*x+m.v[5]*y+m.v[9]*z+m.v[13];
    const float pz=m.v[2]*x+m.v[6]*y+m.v[10]*z+m.v[14],w=m.v[3]*x+m.v[7]*y+m.v[11]*z+m.v[15];
    reject[0]+=px < -w;reject[1]+=px>w;reject[2]+=py < -w;reject[3]+=py>w;reject[4]+=pz < -w;reject[5]+=pz>w;
  }
  return std::none_of(std::begin(reject),std::end(reject),[](unsigned n){return n==8;});
}
float near_distance(vec3 eye,vec3 lo,vec3 hi) {return length(eye-vec3{std::clamp(eye.x,lo.x,hi.x),std::clamp(eye.y,lo.y,hi.y),std::clamp(eye.z,lo.z,hi.z)});}
void check_color(rgba c) {for(float x:{c.r,c.g,c.b,c.a}) if(!std::isfinite(x)||x<0||x>1) throw std::invalid_argument("njin world3d: color outside finite [0,1]");}
void rebuild(context &ctx,terrain3d_slot &t) {
  for(const auto &chunk:t.portable_chunks)
    for(auto group:{std::span<const RenderMeshHandle>(chunk.lods),std::span<const RenderMeshHandle>(chunk.relief)}) for(auto mesh:group)
    if(ctx_ref<window_guard>(ctx).graphics->mesh_in_use(mesh)||ctx_ref<window_guard>(ctx).renderer_resources.pending_3d_meshes.contains(mesh.id))
      throw std::logic_error("njin terrain: cannot replace chunks already submitted this frame");
  std::vector<terrain3d_slot::portable_chunk> made;
  try {
    for(int cz=0;cz<t.chunks;++cz) for(int cx=0;cx<t.chunks;++cx) {
      auto &chunk=made.emplace_back();chunk.lo={INFINITY,INFINITY,INFINITY};chunk.hi={-INFINITY,-INFINITY,-INFINITY};
      const int x0=cx*t.chunk,z0=cz*t.chunk,x1=std::min(x0+t.chunk,t.res-1),z1=std::min(z0+t.chunk,t.res-1);
      for(int level=0;level<t.lods;++level) {
        const int step=1<<level;std::vector<int> xs,zs;
        for(int x=x0;x<x1;x+=step) xs.push_back(x);
        xs.push_back(x1);
        for(int z=z0;z<z1;z+=step) zs.push_back(z);
        zs.push_back(z1);
        std::vector<RenderVertex> vertices;std::vector<u16> indices;
        for(int z:zs) for(int x:xs) {
          const auto n=terrain_sample_normal(t,x,z);const size_t index=size_t(z)*t.res+x;
          RenderVertex v{t.desc.origin.x+x*t.spacing,t.heights[index],t.desc.origin.z+z*t.spacing,t.desc.origin.x+x*t.spacing,t.desc.origin.z+z*t.spacing};
          v.normal={n.x,n.y,n.z};for(unsigned l=0;l<4;++l) v.color[l]=t.splat[index*4+l]/255.f;
          vertices.push_back(v);chunk.lo={std::min(chunk.lo.x,v.x),std::min(chunk.lo.y,v.y),std::min(chunk.lo.z,v.z)};
          chunk.hi={std::max(chunk.hi.x,v.x),std::max(chunk.hi.y,v.y),std::max(chunk.hi.z,v.z)};
        }
        const unsigned width=unsigned(xs.size()),height=unsigned(zs.size());
        for(unsigned z=0;z+1<height;++z) for(unsigned x=0;x+1<width;++x) {
          const u16 a=u16(z*width+x),b=u16(a+1),c=u16(a+width),d=u16(c+1);
          indices.insert(indices.end(),{a,c,b,b,c,d});
        }
        const float skirt=std::max(t.spacing*step*2.f,.1f);
        const auto edge=[&](unsigned a,unsigned b) {
          auto va=vertices[a],vb=vertices[b];va.y-=skirt;vb.y-=skirt;
          if(vertices.size()+2>65535) throw std::length_error("njin terrain: chunk exceeds index range");
          const u16 bottom=u16(vertices.size());vertices.push_back(va);vertices.push_back(vb);
          indices.insert(indices.end(),{u16(a),bottom,u16(b),u16(b),bottom,u16(bottom+1)});
        };
        for(unsigned x=0;x+1<width;++x) {edge(x+1,x);edge((height-1)*width+x,(height-1)*width+x+1);}
        for(unsigned z=0;z+1<height;++z) {edge(z*width,(z+1)*width);edge((z+1)*width+width-1,z*width+width-1);}
        chunk.lods.push_back(renderer_create_mesh(ctx,vertices,indices));
      }
    }
  } catch(...) {for(auto &c:made) for(auto m:c.lods) renderer_destroy_mesh(ctx,m);throw;}
  for(auto &c:t.portable_chunks) {for(auto m:c.lods) renderer_destroy_mesh(ctx,m);for(auto m:c.relief) renderer_destroy_mesh(ctx,m);}
  t.portable_chunks=std::move(made);t.portable_edits=t.edits;
}
RenderTextureHandle roughness(context &ctx,terrain3d_slot &t,unsigned l) {
  const auto *src=image(ctx,t.desc.layers[l].roughness);
  const u64 key=src?(src->portable.id^(u64(src->version)<<32)):0;
  if(key==t.portable_roughness_keys[l]) return t.portable_roughness[l];
  RenderTextureHandle created;
  if(src) {
    const auto &im=ctx_ref<window_guard>(ctx).renderer_resources.textures.at(src->portable.id).cpu;
    if(im.format!=RenderImageFormat::RGBA8) unsupported("non-RGBA8 terrain roughness maps");
    auto pixels=im.pixels;for(size_t p=0;p<pixels.size();p+=4) {pixels[p+1]=pixels[p];pixels[p]=pixels[p+2]=0;pixels[p+3]=255;}
    created=renderer_create_texture(ctx,{im.width,im.height,pixels,RenderImageFormat::RGBA8,RenderMipMode::Linear});
  }
  try {if(t.portable_roughness[l]) renderer_destroy_texture(ctx,t.portable_roughness[l]);}
  catch(...) {if(created) renderer_destroy_texture(ctx,created);throw;}
  t.portable_roughness[l]=created;t.portable_roughness_keys[l]=key;return created;
}

// Tạo bộ texture riêng cho shader đỉnh; thay cả bộ sau khi kiểm tra lifetime.
std::shared_ptr<RenderTerrainRelief> relief_images(context &ctx,terrain3d_slot &t) {
  std::array<u64,4> keys{};unsigned width=1,height=1;
  auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
  for(int l=0;l<t.desc.layer_count;++l) if(const auto *src=image(ctx,t.desc.layers[l].height)) {
    keys[l]=src->portable.id^(u64(src->version)<<32);
    const auto &im=resources.textures.at(src->portable.id).cpu;
    if(im.format!=RenderImageFormat::RGBA8) unsupported("non-RGBA8 terrain relief height maps");
    if(width==1&&height==1) {width=im.width;height=im.height;}
  }
  if(t.portable_relief_edits!=t.edits||keys!=t.portable_relief_keys||!t.portable_relief_textures[0]) {
    auto &gpu=*ctx_ref<window_guard>(ctx).graphics;
    for(auto h:t.portable_relief_textures) if(h&&(gpu.texture_in_use(h)||resources.pending_3d_textures.contains(h.id)))
      throw std::logic_error("njin terrain: cannot replace submitted relief textures");
    std::vector<float> ground(size_t(t.res)*t.res*4);
    for(int z=0;z<t.res;++z) for(int x=0;x<t.res;++x) {
      const auto n=terrain_sample_normal(t,x,z);const size_t i=size_t(z)*t.res+x;
      ground[i*4]=n.x;ground[i*4+1]=n.y;ground[i*4+2]=n.z;ground[i*4+3]=t.heights[i];
    }
    std::vector<u8> ground_bytes(ground.size()*sizeof(float));std::memcpy(ground_bytes.data(),ground.data(),ground_bytes.size());
    std::vector<u8> packed(size_t(width)*height*4,128);
    for(int l=0;l<t.desc.layer_count;++l) if(const auto *src=image(ctx,t.desc.layers[l].height)) {
      const auto &im=resources.textures.at(src->portable.id).cpu;
      // Lấy kênh R, nội suy hai chiều khi các lớp dùng độ phân giải khác nhau.
      for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
        const float px=(x+.5f)*im.width/width-.5f,py=(y+.5f)*im.height/height-.5f;
        const int ix=int(std::floor(px)),iy=int(std::floor(py));const float fx=px-ix,fy=py-iy;
        const auto red=[&](int a,int b){return float(im.pixels[(size_t(std::clamp(b,0,int(im.height)-1))*im.width+std::clamp(a,0,int(im.width)-1))*4]);};
        const float value=std::lerp(std::lerp(red(ix,iy),red(ix+1,iy),fx),std::lerp(red(ix,iy+1),red(ix+1,iy+1),fx),fy);
        packed[(size_t(y)*width+x)*4+l]=u8(std::clamp(std::lround(value),0l,255l));
      }
    }
    std::array<RenderTextureHandle,3> made{};
    try {
      made[0]=renderer_create_texture(ctx,{unsigned(t.res),unsigned(t.res),ground_bytes,RenderImageFormat::RGBA32F});
      made[1]=renderer_create_texture(ctx,{unsigned(t.res),unsigned(t.res),t.splat});
      made[2]=renderer_create_texture(ctx,{width,height,packed,RenderImageFormat::RGBA8,RenderMipMode::Linear});
    } catch(...) {for(auto h:made) if(h) renderer_destroy_texture(ctx,h);throw;}
    for(auto h:t.portable_relief_textures) if(h) renderer_destroy_texture(ctx,h);
    t.portable_relief_textures=made;t.portable_relief_keys=keys;t.portable_relief_edits=t.edits;
  }
  const auto &im=resources.textures.at(t.portable_relief_textures[2].id).cpu;
  auto out=std::make_shared<RenderTerrainRelief>();out->textures=t.portable_relief_textures;
  out->rows[0]={t.desc.origin.x,t.desc.origin.z,t.spacing,float(t.res)};
  out->rows[4]={float(im.width),float(im.height),1.f/float(std::clamp(t.desc.relief_detail,1,8)),float(t.desc.layer_count)};
  for(int l=0;l<4;++l) {
    out->rows[2][l]=l<t.desc.layer_count&&t.desc.layers[l].height.id?std::max(t.desc.layers[l].relief,0.f):0;
    out->rows[3][l]=l<t.desc.layer_count?t.desc.layers[l].tile:1;
  }
  return out;
}
void relief_meshes(context &ctx,terrain3d_slot &t,size_t index) {
  auto &chunk=t.portable_chunks[index];if(!chunk.relief.empty()) return;
  const int cx=int(index%t.chunks),cz=int(index/t.chunks);
  const int endx=std::min((cx+1)*t.chunk,t.res-1),endz=std::min((cz+1)*t.chunk,t.res-1);
  const int quarter=std::max(t.chunk/2,1),detail=std::clamp(t.desc.relief_detail,1,std::clamp(250/quarter,1,8));
  std::vector<RenderMeshHandle> made;
  try {
    for(int z0=cz*t.chunk;z0<endz;z0+=quarter) for(int x0=cx*t.chunk;x0<endx;x0+=quarter) {
      const int nx=(std::min(x0+quarter,endx)-x0)*detail,nz=(std::min(z0+quarter,endz)-z0)*detail;
      const unsigned stride=unsigned(nx+1);std::vector<RenderVertex> vertices;std::vector<u16> indices;
      for(int z=0;z<=nz;++z) for(int x=0;x<=nx;++x) {
        const float wx=t.desc.origin.x+(x0+float(x)/detail)*t.spacing,wz=t.desc.origin.z+(z0+float(z)/detail)*t.spacing;
        vertices.push_back({wx,terrain_height_at(t,wx,wz),wz,wx,wz});
      }
      for(int z=0;z<nz;++z) for(int x=0;x<nx;++x) {
        const u16 a=u16(z*stride+x),b=u16(a+1),c=u16(a+stride),d=u16(c+1);
        indices.insert(indices.end(),{a,c,d,a,d,b});
      }
      const auto edge=[&](unsigned a,unsigned b) {
        auto va=vertices[a],vb=vertices[b];
        va.uv1[1]=vb.uv1[1]=std::max(t.spacing,(chunk.hi.y-chunk.lo.y+1.f)*.5f+t.spacing);
        if(vertices.size()+2>65535) throw std::length_error("njin terrain relief: grid exceeds index range");
        const u16 bottom=u16(vertices.size());vertices.push_back(va);vertices.push_back(vb);
        indices.insert(indices.end(),{u16(a),bottom,u16(b),u16(b),bottom,u16(bottom+1)});
      };
      for(int x=0;x<nx;++x) {edge(x+1,x);edge(nz*stride+x,nz*stride+x+1);}
      for(int z=0;z<nz;++z) {edge(z*stride,(z+1)*stride);edge((z+1)*stride+nx,z*stride+nx);}
      made.push_back(renderer_create_mesh(ctx,vertices,indices));
    }
  } catch(...) {for(auto h:made) renderer_destroy_mesh(ctx,h);throw;}
  chunk.relief=std::move(made);
}
void ground_images(context &ctx,const terrain3d_slot &t,grass3d_slot &g) {
  for(int l=0;l<t.desc.layer_count;++l) {
    const auto *src=image(ctx,t.desc.layers[l].albedo);auto &out=g.ground[l];
    if(!src) {if(out.size) g.portable_edits=~0u;out={};continue;}
    if(src->virtual_texture) throw std::invalid_argument("NVT: CPU grass ground-color baking requires a regular source texture");
    if(out.tex==src->portable.id&&out.version==src->version) continue;
    g.portable_edits=~0u;
    const auto &im=ctx_ref<window_guard>(ctx).renderer_resources.textures.at(src->portable.id).cpu;
    if(im.format!=RenderImageFormat::RGBA8) unsupported("non-RGBA8 grass ground images");
    out.size=64;out.rgba.resize(64*64*4);out.tex=u32(src->portable.id);out.version=src->version;
    for(unsigned y=0;y<64;++y) for(unsigned x=0;x<64;++x)
      std::copy_n(im.pixels.data()+(size_t(y*im.height/64)*im.width+x*im.width/64)*4,4,out.rgba.data()+(y*64+x)*4);
  }
}
}
void portable_terrain_release(context &ctx,terrain3d_slot &t) {
  auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;auto &gpu=ctx_ref<window_guard>(ctx).graphics;
  for(const auto &chunk:t.portable_chunks)
    for(auto group:{std::span<const RenderMeshHandle>(chunk.lods),std::span<const RenderMeshHandle>(chunk.relief)}) for(auto mesh:group)
    if(gpu->mesh_in_use(mesh)||resources.pending_3d_meshes.contains(mesh.id)) throw std::logic_error("njin terrain: cannot destroy a submitted chunk");
  for(auto texture:t.portable_roughness) if(texture&&(gpu->texture_in_use(texture)||resources.pending_3d_textures.contains(texture.id)))
    throw std::logic_error("njin terrain: cannot destroy a submitted roughness map");
  for(auto texture:t.portable_relief_textures) if(texture&&(gpu->texture_in_use(texture)||resources.pending_3d_textures.contains(texture.id)))
    throw std::logic_error("njin terrain: cannot destroy submitted relief textures");
  if(t.imprint.portable_texture) {
    renderer_destroy_texture(ctx,t.imprint.portable_texture);
    t.imprint.portable_texture={};
  }
  for(auto &chunk:t.portable_chunks) {for(auto mesh:chunk.lods) renderer_destroy_mesh(ctx,mesh);for(auto mesh:chunk.relief) renderer_destroy_mesh(ctx,mesh);}
  for(auto texture:t.portable_roughness) if(texture) renderer_destroy_texture(ctx,texture);
  for(auto h:t.portable_relief_textures) if(h) renderer_destroy_texture(ctx,h);
  t.portable_relief_textures={};t.portable_relief_keys={};t.portable_relief_edits=~0u;
  t.portable_chunks.clear();t.portable_roughness={};t.portable_roughness_keys={};
}
void portable_terrain_draw(const context &ctx,terrain3d_handle handle) {
  auto &s=pass(ctx);auto &world=ctx_ref<world3d_store>(const_cast<context&>(ctx));auto *t=terrain_of(const_cast<context&>(ctx),handle);if(!t) return;
  const grass3d_desc *cover=nullptr;
  for(const auto &g:ctx_ref<world3d_store>(const_cast<context&>(ctx))->grasses)
    if(g.alive&&g.desc.terrain.id==handle.id&&g.desc.far_cover>0) {cover=&g.desc;break;}
  if(cover) {
    check_color(cover->tip_color);check_color(cover->tint_color);
    for(float value:{cover->far_cover,cover->draw_distance,cover->fade,cover->tint,cover->max_slope,cover->min_height})
      if(!std::isfinite(value)) throw std::invalid_argument("njin terrain: non-finite grass far-cover setting");
  }
  const float snow=std::isfinite(t->desc.snow)?std::clamp(t->desc.snow,0.f,1.f):0.f;
  if(snow>0) check_color(t->desc.snow_color);
  for(int l=0;l<t->desc.layer_count;++l) {
    const auto &layer=t->desc.layers[l];check_color(layer.color);
    if(!std::isfinite(layer.relief)||layer.relief<0) throw std::invalid_argument("njin terrain: invalid relief depth");
    if(const auto *height=image(ctx,layer.height);height&&height->virtual_texture) throw std::invalid_argument("NVT: terrain height/relief requires a regular CPU image");
    for(float value:{layer.detile,layer.detile_scale,layer.glitter})
      if(!std::isfinite(value)) throw std::invalid_argument("njin terrain: non-finite layer finish");
    if(!std::isfinite(layer.tile)||layer.tile<=0) throw std::invalid_argument("njin terrain: invalid tile size");
    image(ctx,layer.albedo);image(ctx,layer.normal);image(ctx,layer.roughness);
    if(const auto *map=image(ctx,layer.albedo);map&&map->virtual_texture&&(layer.detile>0||layer.relief>0))
      throw std::invalid_argument("NVT: terrain detile/relief color projection requires a regular source texture");
    for(auto handle:{layer.normal,layer.roughness}) if(const auto *map=image(ctx,handle);map&&map->virtual_texture)
      throw std::invalid_argument("NVT: terrain currently supports virtual albedo only");
    if(layer.roughness.id&&!s.portable_material.pbr) unsupported("terrain roughness maps with Phong lighting; select a PBR material");
  }
  auto &mutable_ctx=const_cast<context&>(ctx);
  if(t->portable_edits!=t->edits||t->portable_chunks.empty()) rebuild(mutable_ctx,*t);
  std::array<RenderTextureHandle,4> rough{};for(int l=0;l<t->desc.layer_count;++l) rough[l]=roughness(mutable_ctx,*t,unsigned(l));
  const bool has_relief=std::any_of(t->desc.layers,t->desc.layers+t->desc.layer_count,[](const auto &layer){return layer.relief>0&&layer.height.id;})&&t->desc.relief_distance>0;
  std::shared_ptr<RenderTerrainRelief> relief;
  float relief_bound=0;
  if(has_relief) {
    if(!std::isfinite(t->desc.relief_distance)) throw std::invalid_argument("njin terrain: non-finite relief distance");
    relief=relief_images(mutable_ctx,*t);
    const auto eye=s.portable_camera.position;const auto serial=ctx_ref<window_guard>(ctx).graphics->frame_serial();
    if(t->portable_relief_frame!=serial) {
      t->portable_relief_previous_eye=t->portable_relief_frame+1==serial?t->portable_relief_eye:eye;
      t->portable_relief_eye=eye;t->portable_relief_frame=serial;
    }
    const auto old=t->portable_relief_previous_eye;
    relief->rows[1]={eye.x,eye.y,eye.z,t->desc.relief_distance};relief->rows[5]={old.x,old.y,old.z,t->desc.relief_distance};
    const int quarter=std::max(t->chunk/2,1),detail=std::clamp(t->desc.relief_detail,1,std::clamp(250/quarter,1,8));
    relief->rows[4][2]=1.f/detail;
    for(float d:relief->rows[2]) relief_bound=std::max(relief_bound,d*.5f);
  }
  const float now=elapsed(ctx);
  auto &im=t->imprint;
  std::shared_ptr<RenderTerrainImprint> imprint;
  if(im.placed&&!im.marks.empty()&&now-im.marks.back().born<im.desc.lifetime) {
    const unsigned size=unsigned(im.desc.size);
    const RenderImage image{size,size,im.texels};
    if(!im.portable_texture) im.portable_texture=renderer_create_texture(mutable_ctx,image);
    else if(!im.dirty.empty()) renderer_update_texture(mutable_ctx,im.portable_texture,image);
    im.dirty.clear();
    imprint=std::make_shared<RenderTerrainImprint>();
    imprint->texture=im.portable_texture;
    imprint->window={im.x0*im.desc.texel,im.z0*im.desc.texel,im.desc.texel,float(size)};
    imprint->relief={im.desc.depth,im.desc.rim,im.desc.lifetime,im.desc.fade};
    imprint->tint={im.desc.tint.r,im.desc.tint.g,im.desc.tint.b,im.desc.tint.a};
  }
  for(size_t i=0;i<t->portable_chunks.size();++i) {
    auto &chunk=t->portable_chunks[i];
    const vec3 pad{relief_bound,relief_bound,relief_bound};const bool in_view=visible(s,chunk.lo-pad,chunk.hi+pad);
    const float distance=near_distance(s.portable_camera.position,chunk.lo,chunk.hi);
    unsigned level=0;float threshold=std::max(t->desc.lod_distance,.01f)*ctx_ref<quality_state>(ctx)->q.lod_scale;
    while(level+1<chunk.lods.size()&&distance>threshold) {++level;threshold*=2;}
    const bool detailed=relief&&level==0&&distance<t->desc.relief_distance;
    if(!detailed&&!chunk.relief.empty()) {
      auto &gpu=*ctx_ref<window_guard>(ctx).graphics;
      const auto &pending=ctx_ref<window_guard>(ctx).renderer_resources.pending_3d_meshes;
      if(std::none_of(chunk.relief.begin(),chunk.relief.end(),[&](auto h){return gpu.mesh_in_use(h)||pending.contains(h.id);})) {
        for(auto h:chunk.relief) renderer_destroy_mesh(mutable_ctx,h);
        chunk.relief.clear();
      }
    }
    if(!in_view&&!t->desc.cast_shadows) continue;
    if(detailed) relief_meshes(mutable_ctx,*t,i);
    const auto meshes=detailed?std::span<const RenderMeshHandle>(chunk.relief):std::span<const RenderMeshHandle>(&chunk.lods[level],1);
    for(auto mesh:meshes) for(int l=0;l<t->desc.layer_count;++l) {
      if(!in_view&&l) break;
      const auto &layer=t->desc.layers[l];MeshDraw packet{};packet.mesh=mesh;packet.terrain_relief=detailed?relief:nullptr;packet.cull=RenderCull::None;
      if(const auto *map=image(ctx,layer.albedo)) packet.texture=map->portable;
      if(const auto *map=image(ctx,layer.normal)) packet.maps.normal.texture=map->portable;
      packet.linear_sampling=true;packet.maps.metal_rough.texture=rough[l];
      packet.color_uv.values={1/layer.tile,0,0,1/layer.tile,0,0};packet.maps.normal.uv=packet.color_uv;packet.maps.metal_rough.uv=packet.color_uv;
      packet.world_style[0]={1,float(l),l==0?1.f:0.f,world->wetness};packet.blend=l==0?RenderBlend::Opaque:RenderBlend::Additive;packet.depth_write=l==0;packet.opaque_alpha=false;
      packet.world_style[4]={t->desc.snow_color.r,t->desc.snow_color.g,t->desc.snow_color.b,snow};
      packet.world_style[5]={std::fmod(std::max(now,0.f)*100.f,65536.f),
        std::clamp(layer.detile,0.f,1.f),std::clamp(layer.detile_scale,0.f,.9f),std::clamp(layer.glitter,0.f,4.f)};
      packet.terrain_imprint=imprint;
      if(cover) {
        const auto &d=*cover;const float range=std::max(d.draw_distance*ctx_ref<quality_state>(ctx)->q.view_distance,1.f);
        const auto color=lerp(d.tip_color,d.tint_color,std::clamp(d.tint,0.f,1.f)*.5f);
        packet.world_style[1]={color.r,color.g,color.b,std::clamp(d.far_cover,0.f,1.f)};
        packet.world_style[2]={std::max(range-std::max(d.fade,.01f),0.f),range,d.layer>=0&&d.layer<4?float(d.layer):-1.f,d.min_height};
        packet.world_style[3]={std::cos(std::clamp(d.max_slope,0.f,90.f)*3.14159265359f/180),std::cos(std::clamp(d.max_slope-6,0.f,90.f)*3.14159265359f/180),s.portable_camera.position.x,s.portable_camera.position.z};
      }
      packet.shadow_only=!in_view;
      auto material=s.portable_material;material.specular=t->desc.specular+world->wetness*0.6f;
      material.shininess=t->desc.shininess+(90.0f-t->desc.shininess)*world->wetness;material.cast_shadows=t->desc.cast_shadows&&l==0;material.alpha_cutoff=0;
      if(rough[l]) {material.roughness=1;material.metallic=0;}
      portable_world_submit(ctx,std::move(packet),material,{layer.color.r,layer.color.g,layer.color.b,1},true,(1ULL<<61)^mesh.id);
    }
  }
}
void portable_grass_draw(const context &ctx,grass3d_handle handle) {
  auto &s=pass(ctx);auto &world=ctx_ref<world3d_store>(const_cast<context&>(ctx));auto *g=slot(world->grasses,handle.id);if(!g) return;
  const auto *t=terrain_of(ctx,g->desc.terrain);if(!t) return;const auto &d=g->desc;
  if(!std::isfinite(d.far_cover)||d.far_cover<0||d.far_cover>1) throw std::invalid_argument("njin grass: far cover must be finite in [0,1]");
  for(float f:{d.density,d.height,d.height_jitter,d.width,d.color_jitter,d.patchiness,d.height_variation,d.tint,d.curve,d.ground_color,d.root_shade,d.draw_distance,d.fade,d.sway})
    if(!std::isfinite(f)||f<0) throw std::invalid_argument("njin grass: invalid finite/nonnegative setting");
  check_color(d.base_color);check_color(d.tip_color);check_color(d.tint_color);
  auto &mutable_ctx=const_cast<context&>(ctx);ground_images(mutable_ctx,*t,*g);
  const int count=int(std::ceil(t->desc.size/g->cell));if(count<=0||count>4096) throw std::length_error("njin grass: invalid cell extent");
  if(g->portable_edits!=t->edits||g->portable_cells.size()!=size_t(count)*count) {g->portable_cells.clear();g->portable_cells.resize(size_t(count)*count);g->portable_ready.assign(size_t(count)*count,false);g->portable_edits=t->edits;}
  if(!world->portable_blade) {
    std::vector<RenderVertex> vertices;
    for(int segment=0;segment<3;++segment) {
      const float a=segment/3.f,b=(segment+1)/3.f;
      const RenderVertex points[]{{-1,a,0,0,1-a},{1,a,0,1,1-a},{1,b,0,1,1-b},{-1,b,0,0,1-b}};
      for(int index:{0,1,2,0,2,3}) vertices.push_back(points[index]);
    }
    world->portable_blade=renderer_create_mesh(mutable_ctx,vertices);
  }
  const float reach=d.draw_distance*ctx_ref<quality_state>(ctx)->q.view_distance;g->drawn=0;
  if(reach<=0) return;
  const auto eye=s.portable_camera.position;
  const int min_x=std::clamp(int(std::floor((eye.x-reach-t->desc.origin.x)/g->cell)),0,count-1),max_x=std::clamp(int(std::floor((eye.x+reach-t->desc.origin.x)/g->cell)),0,count-1);
  const int min_z=std::clamp(int(std::floor((eye.z-reach-t->desc.origin.z)/g->cell)),0,count-1),max_z=std::clamp(int(std::floor((eye.z+reach-t->desc.origin.z)/g->cell)),0,count-1);
  for(int z=min_z;z<=max_z;++z) for(int x=min_x;x<=max_x;++x) {
    const vec3 lo{t->desc.origin.x+x*g->cell,t->min_h,t->desc.origin.z+z*g->cell},hi{lo.x+g->cell,t->max_h+d.height*(2+d.height_jitter),lo.z+g->cell};
    if(near_distance(eye,lo,hi)>reach||!visible(s,lo,hi)) continue;
    const size_t cell=size_t(z)*count+x;auto &data=g->portable_cells[cell];if(!g->portable_ready[cell]) {grass_cell_blades(*t,*g,x,z,data);g->portable_ready[cell]=true;}
    auto instances=std::make_shared<std::vector<RenderInstance>>();instances->reserve(data.size()/12);
    for(size_t i=0;i+11<data.size();i+=12) {
      const float *p=data.data()+i;const float c=std::cos(p[4]),sn=std::sin(p[4]);
      RenderInstance instance;instance.transform={c*p[5],0,sn*p[5],0,0,p[3],0,0,-sn,0,c,0,p[0],p[1],p[2],1};
      instance.color={std::clamp(p[8],0.f,1.f),std::clamp(p[9],0.f,1.f),std::clamp(p[10],0.f,1.f),std::clamp(p[11],0.f,1.f)};
      const auto normal=terrain_normal_at(*t,p[0],p[2]);instance.data={p[6],p[7],normal.x,normal.z};instances->push_back(instance);
    }
    if(instances->empty()) continue;
    g->drawn+=int(instances->size());MeshDraw packet{};packet.mesh=world->portable_blade;packet.instances=instances;packet.blend=RenderBlend::Opaque;
    packet.world_style={{{2,0,0,0},{d.base_color.r,d.base_color.g,d.base_color.b,d.root_shade},{d.tip_color.r,d.tip_color.g,d.tip_color.b,d.ground_color},
      {d.tint_color.r,d.tint_color.g,d.tint_color.b,d.curve},{world->wind.x,world->wind.y,world->time,d.sway},{std::max(0.f,reach-std::max(d.fade,.001f)),std::max(reach,.001f),eye.x,eye.z}}};
    auto material=s.portable_material;material.cast_shadows=d.cast_shadows;material.specular=0;material.alpha_cutoff=0;
    portable_world_submit(ctx,std::move(packet),material,{1,1,1,1},true);
  }
}
void portable_water_draw(const context &ctx,water3d_handle handle) {
  auto &s=pass(ctx);auto &world=ctx_ref<world3d_store>(const_cast<context&>(ctx));const auto *w=slot(world->waters,handle.id);if(!w) return;const auto &d=w->desc;
  for(float f:{d.level,d.center.x,d.center.y,d.size.x,d.size.y,d.depth_fade,d.clarity,d.foam_width,d.crest_foam,d.ripples,d.specular,d.shininess,d.detail,d.bed})
    if(!std::isfinite(f)) throw std::invalid_argument("njin water: non-finite setting");
  if(d.detail<=0||d.detail>4||d.wave_count<0||d.wave_count>water3d_wave_max||d.depth_fade<=0||d.clarity<0||d.size.x<0||d.size.y<0) throw std::invalid_argument("njin water: invalid size/detail/waves/depth");
  check_color(d.shallow_color);check_color(d.deep_color);check_color(d.foam_color);
  check_color(d.ice_color);
  for(float value:{d.ice,d.ice_snow,d.pour,d.refract,d.reflect,d.caustics}) if(!std::isfinite(value)||value<0||value>1)
    throw std::invalid_argument("njin water: ice/snow/pour/optics must be finite in [0,1]");
  check_color(d.absorb);
  if(!std::isfinite(d.caustic_size)||d.caustic_size<=0) throw std::invalid_argument("njin water: caustic size must be positive and finite");
  if(!std::isfinite(d.pour_at.x)||!std::isfinite(d.pour_at.y)||!std::isfinite(d.pour_reach)||d.pour_reach<=0)
    throw std::invalid_argument("njin water: invalid pour position/radius");
  const auto *terrain=terrain_of(ctx,d.terrain);const bool ocean=d.size.x==0||d.size.y==0;
  // The open sea follows the camera in whole 2 m steps, so the waves do not slide along.
  const vec2 center=ocean?vec2{std::round(s.portable_camera.position.x/2)*2,std::round(s.portable_camera.position.z/2)*2}:d.center;
  const vec2 size=ocean?vec2{16000,16000}:d.size;const unsigned cells=unsigned(std::clamp(64.f*d.detail,8.f,254.f));
  auto geometry=std::make_shared<RenderGeometryData>();geometry->vertices.reserve(size_t(cells+1)*(cells+1));
  float amplitude=0;for(int i=0;i<d.wave_count;++i) {
    const auto &wave=d.waves[i];if(!std::isfinite(wave.wavelength)||wave.wavelength<=0||!std::isfinite(wave.direction.x)||!std::isfinite(wave.direction.y)||!std::isfinite(wave.steepness)||!std::isfinite(wave.speed)) throw std::invalid_argument("njin water: invalid wave");
    amplitude+=std::abs(wave.steepness*wave.wavelength/6.28318530718f);
  }
  const auto push=[&](float px,float pz,float spacing) {
    vec3 offset,normal;water_displace(d,px,pz,world->time,offset,normal,spacing);
    offset=offset*(1-d.ice);normal=normalize(lerp(vec3{0,1,0},normal,1-d.ice));
    const float depth=terrain?std::max(0.f,d.level-terrain_height_at(*terrain,px,pz)):d.bed>0?d.bed:d.depth_fade*4;
    const float blend=1-std::exp(-depth/d.depth_fade);auto color=lerp(d.shallow_color,d.deep_color,blend);
    RenderVertex v{px+offset.x,d.level+offset.y,pz+offset.z,px,pz,1,{color.r,color.g,color.b,color.a}};
    v.normal={normal.x,normal.y,normal.z};v.uv1={depth,offset.y};geometry->vertices.push_back(v);
  };
  if(ocean) {
    // Legacy make_ocean: rings round the camera out to 8 km, each wider by the same share, so
    // the vertices are about square everywhere, fine at the feet and coarse at the horizon.
    // Each wave fades where the rings get too coarse for it (water_displace's spacing), as
    // Wicked Engine's projected-grid ocean fades its displacement with distance.
    const int segments=std::clamp(int(std::lround(128.f*d.detail)),48,192);
    const float grow=1+6.28318530718f/float(segments);
    std::vector<float> radii{0.f};
    for(float r=.3f;r<6000.f;r*=grow) radii.push_back(r);
    radii.push_back(8000.f);
    geometry->vertices.reserve(1+(radii.size()-1)*size_t(segments));
    push(center.x,center.y,.3f);
    for(size_t k=1;k<radii.size();++k) for(int i=0;i<segments;++i) {
      const float a=float(i)/float(segments)*6.28318530718f,r=radii[k];
      push(center.x+std::cos(a)*r,center.y+std::sin(a)*r,std::max(r*(grow-1),.3f));
    }
    if(geometry->vertices.size()>65535) throw std::length_error("njin water: ocean detail produces too many vertices");
    const auto ring=[&](size_t k,int i) {return u16(1+(k-1)*size_t(segments)+size_t(i%segments));};
    for(int i=0;i<segments;++i) geometry->indices.insert(geometry->indices.end(),{u16(0),ring(1,i+1),ring(1,i)});
    for(size_t k=1;k+1<radii.size();++k) for(int i=0;i<segments;++i)
      geometry->indices.insert(geometry->indices.end(),{ring(k,i),ring(k,i+1),ring(k+1,i+1),ring(k,i),ring(k+1,i+1),ring(k+1,i)});
  } else {
    for(unsigned z=0;z<=cells;++z) for(unsigned x=0;x<=cells;++x)
      push(center.x+(float(x)/cells-.5f)*size.x,center.y+(float(z)/cells-.5f)*size.y,0);
    // The public index stream is 16 bit; split the 257x257 extreme at 255 cells.
    if(geometry->vertices.size()>65535) throw std::length_error("njin water: detail produces too many vertices; use detail below 4");
    for(unsigned z=0;z<cells;++z) for(unsigned x=0;x<cells;++x) {
      const u16 a=u16(z*(cells+1)+x),b=u16(a+1),c=u16(a+cells+1),d0=u16(c+1);
      geometry->indices.insert(geometry->indices.end(),{a,c,b,b,c,d0});
    }
  }
  MeshDraw packet{};packet.geometry=geometry;packet.depth_write=false;packet.blend=RenderBlend::Alpha;
  packet.world_style[0]={3,d.foam_width,d.crest_foam,d.ripples};packet.world_style[1]={d.foam_color.r,d.foam_color.g,d.foam_color.b,world->time};
  packet.world_style[2]={d.clarity,amplitude,d.lamps?1.f:0.f,d.round&&!ocean?1.f:0.f};packet.world_style[3]={center.x,center.y,size.x,size.y};
  packet.world_style[4][0]=d.foam_color.a;
  // Rain rings follow the sky's rain this frame (legacy: only while draw_sky3d drew it).
  packet.world_style[4][1]=world->sky_drawn&&std::isfinite(d.rain_rings)?std::clamp(world->sky.rain*std::clamp(d.rain_rings,0.f,1.f),0.f,1.f):0.f;
  packet.world_style[5]={s.portable_camera.near_plane,s.portable_camera.far_plane,s.portable_camera.projection==camera3d_orthographic?1.f:0.f,d.caustic_size};
  auto effects=std::make_shared<RenderWaterEffects>();auto &rows=effects->parameters;
  rows[0]={d.ice,d.ice_snow,d.refract,d.reflect};rows[1]={d.ice_color.r,d.ice_color.g,d.ice_color.b,d.caustics};
  effects->absorption={d.absorb.r,d.absorb.g,d.absorb.b,d.depth_fade};
  effects->shallow_color={d.shallow_color.r,d.shallow_color.g,d.shallow_color.b,d.shallow_color.a};
  effects->deep_color={d.deep_color.r,d.deep_color.g,d.deep_color.b,d.deep_color.a};
  rows[2]={d.pour_at.x,d.pour_at.y,d.pour,d.pour_reach};
  for(unsigned i=0;i<water3d_splash_max;++i) {
    const auto &ring=w->rings[i];const float age=world->time-ring.start;
    if(ring.strength<=0||age<0||std::exp(-age/(1.2f+ring.size*4))<.02f) continue;
    rows[3+i]={ring.at.x,ring.at.y,age,ring.strength};rows[11+i/4][i%4]=ring.size;
  }
  packet.water=std::move(effects);
  // The sky the legacy water reflects: draw_sky3d's this frame, else one made from the light.
  packet.sky=sky_packet(world->sky_drawn?world->sky:sky_from_light(s.portable_frame_light,world->wind),world->drift,s.portable_camera.position,false);
  auto material=s.portable_material;material.specular=d.specular;material.shininess=d.shininess;material.cast_shadows=false;
  portable_world_submit(ctx,std::move(packet),material,{1,1,1,1},false,(1ULL<<62)|handle.id);
}
namespace {
std::shared_ptr<RenderSkyEffects> sky_packet(const sky_params &k,vec2 drift,vec3 eye,bool backdrop) {
  auto sky=std::make_shared<RenderSkyEffects>();sky->backdrop=backdrop;
  sky->rows={{{k.sun.x,k.sun.y,k.sun.z,k.day},{k.sun_color.x,k.sun_color.y,k.sun_color.z,0},{k.zenith.x,k.zenith.y,k.zenith.z,0},
              {k.horizon.x,k.horizon.y,k.horizon.z,0},{k.ground.x,k.ground.y,k.ground.z,0},{k.glow.x,k.glow.y,k.glow.z,0},
              {k.twilight,k.clouds,k.cloud_darkness,k.sun_size},{k.cloud_height,k.cloud_scale,k.stars,k.fog},
              {k.fog_color.x,k.fog_color.y,k.fog_color.z,0},{drift.x,drift.y,0,0},{eye.x,eye.y,eye.z,0}}};
  return sky;
}
// Legacy sky_params_from_light on this pass's light: what water reflects with no draw_sky3d.
sky_params sky_from_light(const light3d &l,vec2 wind) {
  sky_params p;
  const vec3 d=length_sq(l.direction)>0.0f?normalize(l.direction):vec3{0.0f,-1.0f,0.0f};
  p.sun=-d;p.sun_color={l.color.r,l.color.g,l.color.b};
  p.horizon={l.fog_color.r,l.fog_color.g,l.fog_color.b};
  p.zenith=vec3{l.ambient.r,l.ambient.g,l.ambient.b}*1.15f;
  p.ground=p.horizon*0.4f;p.twilight=0.0f;p.day=1.0f;p.clouds=0.0f;p.stars=0.0f;
  p.wind=wind;p.fog=l.fog_density;p.fog_color=p.horizon;
  return p;
}
} // namespace
void portable_sky_draw(context &ctx,model_store &store) {
  world3d_store &w=*ctx_ref<world3d_store>(ctx).operator->();
  if(!w.sky_drawn) return;
  if(store.portable_atmosphere_drawn) throw std::logic_error("njin atmosphere: draw_sky3d and draw_atmosphere3d require separate views");
  w.sky_drawn=false;
  const auto eye=store.portable_camera.position;
  const auto sky=sky_packet(w.sky,w.drift,eye,true);
  // The same far-plane quad as the environment sky: each corner carries its view direction.
  RenderVertex v[]{{-1,1,.999999f,0,0},{1,1,.999999f,1,0},{1,-1,.999999f,1,1},{-1,1,.999999f,0,0},{1,-1,.999999f,1,1},{-1,-1,.999999f,0,1}};
  const auto inverse=MatrixInvert(store.portable_view_projection);
  for(auto &a:v) {
    const float iw=inverse.m3*a.x+inverse.m7*a.y+inverse.m11*a.z+inverse.m15;
    const auto p=Vector3Transform({a.x,a.y,a.z},inverse);
    a.normal={p.x/iw-eye.x,p.y/iw-eye.y,p.z/iw-eye.z};
  }
  if(!store.portable_white) {const unsigned char pixel[]{255,255,255,255};store.portable_white=renderer_create_texture(ctx,{1,1,pixel});}
  MeshDraw draw{v,{}};draw.texture=store.portable_white;draw.sky=sky;draw.opaque_alpha=true;
  draw.tonemap=static_cast<RenderToneMap>(store.portable_frame_light.tonemap);draw.exposure=store.portable_frame_light.exposure;
  draw.linear_output=store.portable_hdr_active;renderer_draw_mesh(ctx,draw);
}
namespace {
// The legacy precipitation shader's PCG hash (world3d_draw.cpp), on the CPU: the same
// 32-bit integer steps, so each drop gets the random numbers it has on OpenGL.
uint32_t pcg(uint32_t v) {
  const uint32_t state=v*747796405u+2891336453u;
  const uint32_t word=((state>>((state>>28u)+4u))^state)*277803737u;
  return (word>>22u)^word;
}
std::array<float,3> hash3(uint32_t n) {
  constexpr float k=1.0f/4294967295.0f;
  return {float(pcg(n))*k,float(pcg(n+7919u))*k,float(pcg(n+104729u))*k};
}
constexpr unsigned rain_drops=9000,snow_flakes=7000; // as world3d_draw.cpp
constexpr unsigned cover_size=256;
constexpr float cover_half=18.0f; // metres each side of the camera, as render3d.cpp
std::array<std::array<float,4>,4> clip_rows(const Matrix &m) {
  return {{{m.m0,m.m4,m.m8,m.m12},{m.m1,m.m5,m.m9,m.m13},{m.m2,m.m6,m.m10,m.m14},{m.m3,m.m7,m.m11,m.m15}}};
}
Matrix matrix_of(const std::array<float,16> &a) {
  return {a[0],a[4],a[8],a[12],a[1],a[5],a[9],a[13],a[2],a[6],a[10],a[14],a[3],a[7],a[11],a[15]};
}
// weather3d_cover_set boxes and, with cover_auto, the depth of every caster seen straight
// down round the camera (legacy render_cover), in the shadow pass's packed-depth format.
std::shared_ptr<const RenderCoverData> weather_cover(context &ctx,model_store &store,world3d_store &w) {
  auto data=std::make_shared<RenderCoverData>();
  for(const auto &c:w.covers) {
    if(data->box_count>=16) break;
    data->boxes[data->box_count*2]={c.center.x,c.center.y,c.center.z,0};
    data->boxes[data->box_count*2+1]={c.size.x*.5f,c.size.y*.5f,c.size.z*.5f,0};
    ++data->box_count;
  }
  data->bias=0.15f/600.0f;
  // The map only while it rains or snows (legacy cover_wanted).
  if(!w.cover_auto||!w.portable_precip) return data;
  const auto eye=store.portable_camera.position;
  const float texel=2*cover_half/cover_size;
  const float cx=std::floor(eye.x/texel)*texel,cz=std::floor(eye.z/texel)*texel,top=eye.y+200;
  const Matrix vp=MatrixMultiply(MatrixLookAt({cx,top,cz},{cx,top-1,cz},{0,0,-1}),
                                 MatrixOrtho(-cover_half,cover_half,-cover_half,cover_half,0.1,600.0));
  if(!w.portable_cover) w.portable_cover=renderer_create_target(ctx,cover_size,cover_size);
  auto &resources=ctx_ref<window_guard>(ctx).renderer_resources;
  const auto previous=resources.active_target;
  if(previous) renderer_end_target(ctx);
  try {
    renderer_begin_target(ctx,w.portable_cover,{1,1,1,1});
    for(const auto &packet:store.portable_queue) if(packet.cast_shadow) {
      MeshDraw draw=packet;draw.shadows.reset();draw.shadow_pass=true;
      if(packet.transparent) draw.alpha_cutoff=std::max(draw.alpha_cutoff,.5f);
      draw.lighting.enabled=false;draw.local_light_count=0;draw.linear_output=false;
      draw.resolve=false;draw.tonemap=RenderToneMap::None;draw.clusters.reset();
      const auto mvp=MatrixToFloatV(MatrixMultiply(matrix_of(packet.world),vp));
      std::copy_n(mvp.v,16,draw.transform.begin());renderer_draw_mesh(ctx,draw);
    }
    renderer_end_target(ctx);
    if(previous) renderer_resume_target(ctx,previous);
  } catch(...) {
    if(resources.active_target.id==w.portable_cover.id) {try {renderer_end_target(ctx);} catch(...) {}}
    if(previous&&!resources.active_target) {try {renderer_resume_target(ctx,previous);} catch(...) {}}
    throw;
  }
  data->texture=renderer_target_texture(ctx,w.portable_cover);
  data->rows=clip_rows(vp);
  return data;
}
} // namespace
std::shared_ptr<const RenderCoverData> portable_weather_cover(context &ctx,model_store &store) {
  world3d_store &w=*ctx_ref<world3d_store>(ctx).operator->();
  w.portable_frame_cover.reset();
  if(w.portable_precip||w.wetness>0) w.portable_frame_cover=weather_cover(ctx,store,w);
  return w.portable_frame_cover;
}
void portable_precip_draw(context &ctx,model_store &store) {
  world3d_store &w=*ctx_ref<world3d_store>(ctx).operator->();
  const auto cover=w.portable_frame_cover;
  w.portable_frame_cover.reset();
  if(!w.portable_precip) return;
  w.portable_precip=false;
  if(!w.portable_drops) {
    auto drops=std::make_shared<std::vector<RenderInstance>>(std::max(rain_drops,snow_flakes));
    for(uint32_t id=0;id<drops->size();++id) {
      const auto r=hash3(id),speed=hash3(id+15485863u),k=hash3(id+32452843u);
      (*drops)[id].color={r[0],r[1],r[2],speed[0]};(*drops)[id].data={k[0],k[1],k[2],0};
    }
    w.portable_drops=std::move(drops);
  }
  if(!w.portable_drop_quad) {
    const RenderVertex quad[]{{0,0,0,0,0},{1,0,0,1,0},{1,1,0,1,1},{0,1,0,0,1}};
    const std::uint16_t indices[]{0,1,2,0,2,3};
    w.portable_drop_quad=renderer_create_mesh(ctx,quad,indices);
  }
  const auto &light=store.portable_frame_light;const auto eye=store.portable_camera.position;
  const auto vp=MatrixToFloatV(store.portable_view_projection);
  const auto *flakes=w.flakes.id?texture_slot_of(ctx_ref<texture_store>(ctx),w.flakes):nullptr;
  const bool shapes=flakes&&flakes->portable&&!flakes->packed;
  if(!store.portable_white) {const unsigned char pixel[]{255,255,255,255};store.portable_white=renderer_create_texture(ctx,{1,1,pixel});}
  const auto submit=[&](bool snow,float amount) {
    MeshDraw draw{};draw.mesh=w.portable_drop_quad;draw.instances=w.portable_drops;draw.persistent_instances=true;
    draw.instance_count=unsigned(float(snow?snow_flakes:rain_drops)*amount);
    if(!draw.instance_count) return;
    std::copy_n(vp.v,16,draw.transform.begin());
    draw.texture=snow&&shapes?flakes->portable:store.portable_white;
    draw.linear_sampling=!(snow&&shapes)||flakes->filter!=filter_nearest;
    draw.blend=RenderBlend::Alpha;draw.depth_write=false;draw.cast_shadow=false;draw.cover=cover;
    draw.lighting.color={light.color.r,light.color.g,light.color.b};
    draw.lighting.ambient={light.ambient.r,light.ambient.g,light.ambient.b};
    draw.exposure=light.exposure;draw.linear_output=store.portable_hdr_active;
    draw.world_style[0]={5,snow?1.f:0.f,1,snow&&shapes?w.flake_sparkle:0.f};
    draw.world_style[1]={eye.x,eye.y,eye.z,w.time};
    draw.world_style[2]={w.sky.wind.x,w.sky.wind.y,w.drift.x,w.drift.y};
    draw.world_style[3]=snow?std::array<float,4>{26,16,1.2f,1}:std::array<float,4>{32,22,9,0};
    draw.world_style[4]={snow&&shapes?float(w.flake_columns):0.f,snow&&shapes?float(w.flake_rows):0.f,0,0};
    renderer_draw_mesh(ctx,draw);
  };
  if(w.sky.rain>0) submit(false,w.sky.rain);
  if(w.sky.snow>0) submit(true,w.sky.snow);
}
void portable_scatter_draw(const context &ctx,scatter3d_handle handle) {
  auto &s=pass(ctx);auto &w=ctx_ref<world3d_store>(const_cast<context&>(ctx));const auto *scatter=slot(w->scatters,handle.id);if(!scatter||!terrain_of(ctx,scatter->desc.terrain)) return;
  const float reach=ctx_ref<quality_state>(ctx)->q.view_distance;
  for(const auto &cell:scatter->cells) {
    if(cell.items.empty()||!cell.buffer.id) continue;
    const vec3 pad{8,30,8},lo=cell.lo-pad,hi=cell.hi+pad;
    const float distance=near_distance(s.portable_camera.position,lo,hi);
    if(distance>scatter->desc.draw_distance*reach||!visible(s,lo,hi)) continue;
    const auto model=distance>scatter->desc.lod_distance*reach&&scatter->desc.far_model.id?scatter->desc.far_model:scatter->desc.model;
    portable_draw_instances(ctx,model,-1,cell.buffer,0,u32(cell.items.size()),{});
  }
}
}
