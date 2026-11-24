#include "njin_internal_only.h"
#include "njin_lightmap.h"
#include "njin_lightmap_internal.h"
#define TINYBVH_NO_SIMD
#include <tiny_bvh.h>
#include "njin_rlmath.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace njin {
namespace {
using V=Vector3;
using U=Vector2;
[[noreturn]] void fail(const char *message) {throw std::runtime_error(std::string("njin lightmap: ")+message);}
void finite(float v) {if(!std::isfinite(v)) fail("nonfinite input");}
struct Hash {
  uint64_t value=14695981039346656037ull;
  void byte(uint8_t b) {value=(value^b)*1099511628211ull;}
  void word(uint64_t v) {for(unsigned i=0;i<8;++i) byte(uint8_t(v>>(8*i)));}
  void scalar(float v) {finite(v);word(std::bit_cast<uint32_t>(v));}
};
Matrix matrix(const std::array<float,16> &a) {
  return {a[0],a[4],a[8],a[12],a[1],a[5],a[9],a[13],a[2],a[6],a[10],a[14],a[3],a[7],a[11],a[15]};
}
float edge(U a,U b,U p) {return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);}
struct Tri {V p[3],n[3];U uv[3];unsigned index[3];};
// Positive intersection area rejects overlaps even smaller than one texel.
bool overlaps(const Tri &a,const Tri &b) {
  // Shared-edge neighbours on opposite sides have exactly zero overlap;
  // avoid manufacturing a sliver through floating-point polygon clipping.
  for(unsigned e=0;e<3;++e) for(unsigned f=0;f<3;++f) {
    const auto a0=a.index[e],a1=a.index[(e+1)%3],b0=b.index[f],b1=b.index[(f+1)%3];
    if((a0==b0&&a1==b1)||(a0==b1&&a1==b0)) {
      if(edge(a.uv[e],a.uv[(e+1)%3],a.uv[(e+2)%3])*edge(a.uv[e],a.uv[(e+1)%3],b.uv[(f+2)%3])<0) return false;
    }
  }
  std::vector<U> polygon{a.uv[0],a.uv[1],a.uv[2]};
  const float sign=edge(b.uv[0],b.uv[1],b.uv[2])>0?1.f:-1.f;
  for(unsigned e=0;e<3&&!polygon.empty();++e) {
    std::vector<U> next;
    for(size_t i=0;i<polygon.size();++i) {
      U p=polygon[i],q=polygon[(i+1)%polygon.size()];
      const float dp=sign*edge(b.uv[e],b.uv[(e+1)%3],p),dq=sign*edge(b.uv[e],b.uv[(e+1)%3],q);
      if(dp>=0) next.push_back(p);
      if((dp<0)!=(dq<0)) {const float t=dp/(dp-dq);next.push_back({p.x+t*(q.x-p.x),p.y+t*(q.y-p.y)});}
    }
    polygon=std::move(next);
  }
  double area=0;
  for(size_t i=0;i<polygon.size();++i) {auto p=polygon[i],q=polygon[(i+1)%polygon.size()];area+=double(p.x)*q.y-double(p.y)*q.x;}
  return std::abs(area)>1e-9;
}
void settings(const LightmapSettings &s) {
  if(s.indirect_bounces>8||!s.indirect_samples||s.indirect_samples>1024) fail("invalid indirect quality");
  if(!std::isfinite(s.texels_per_unit)||s.texels_per_unit<0||s.texels_per_unit>10000) fail("invalid texel density (0..10000)");
  if(s.size<8||s.size>512||s.samples<1||s.samples>64||s.padding<1||s.padding>s.size/4||!std::isfinite(s.bias)||s.bias<=0) fail("invalid settings (size 8..512, samples 1..64, padding 1..size/4, bias > 0)");
}
std::vector<Tri> triangles(const LightmapInstance &instance,const LightmapSettings &s,const std::function<bool(float)> &progress,bool packed=true) {
  if(instance.dynamic) fail("dynamic/skinned geometry is unsupported");
  if(!instance.geometry) fail("missing geometry snapshot");
  const auto &g=*instance.geometry;
  const unsigned width=instance.page_width?instance.page_width:s.size,height=instance.page_height?instance.page_height:s.size;
  if(width<8||height<8||width>512||height>512) fail("invalid page size");
  if(width<=2*s.padding||height<=2*s.padding) fail("page too small for padding");
  const size_t count=g.indices.empty()?g.vertices.size():g.indices.size();
  if(!count||count%3||count>300000||g.vertices.size()>100000) fail("invalid/oversized triangle list");
  if(instance.receiver&&count>24576) fail("receiver exceeds 8192 triangle UV validation limit");
  const auto &a=instance.placement.transform;
  for(float v:a) finite(v);
  if(a[3]!=0||a[7]!=0||a[11]!=0||a[15]!=1) fail("transform must be affine");
  Matrix m=matrix(a);finite(MatrixDeterminant(m));if(std::abs(MatrixDeterminant(m))<1e-10f) fail("singular transform");
  Matrix normal=MatrixTranspose(MatrixInvert(m));
  std::vector<Tri> out(count/3);
  for(size_t i=0;i<count;++i) {
    unsigned id=g.indices.empty()?unsigned(i):g.indices[i];
    if(id>=g.vertices.size()) fail("index out of bounds");
    const auto &v=g.vertices[id];auto &t=out[i/3];const unsigned c=unsigned(i%3);
    for(float f:{v.x,v.y,v.z,v.w,v.normal[0],v.normal[1],v.normal[2],v.lightmap_uv[0],v.lightmap_uv[1]}) finite(f);
    if(v.w!=1) fail("vertex w must be 1");
    t.p[c]=Vector3Transform({v.x,v.y,v.z},m);
    t.n[c]=Vector3Normalize(Vector3Transform({v.normal[0],v.normal[1],v.normal[2]},normal));
    for(float f:{t.p[c].x,t.p[c].y,t.p[c].z,t.n[c].x,t.n[c].y,t.n[c].z}) finite(f);
    if(Vector3Length(t.n[c])<.5f) fail("missing normal");
    t.uv[c]={v.lightmap_uv[0],v.lightmap_uv[1]};t.index[c]=id;
    if(instance.receiver) {
      const float mx=packed?float(s.padding)/width:0.f,my=packed?float(s.padding)/height:0.f;
      if(t.uv[c].x<mx-1e-6f||t.uv[c].y<my-1e-6f||t.uv[c].x>1-mx+1e-6f||t.uv[c].y>1-my+1e-6f) fail("lightmap UV missing/outside padded page");
    }
  }
  for(const auto &t:out) {
    const float area=Vector3Length(Vector3CrossProduct(Vector3Subtract(t.p[1],t.p[0]),Vector3Subtract(t.p[2],t.p[0])));finite(area);
    if(area<1e-10f) fail("zero-area world triangle");
    if(instance.receiver&&std::abs(edge(t.uv[0],t.uv[1],t.uv[2]))<1e-10f) fail("missing or zero-area UV1");
  }
  if(instance.receiver) {
    for(unsigned i=0;i<out.size();++i) {
      if(i%32==0&&progress&&!progress(0)) fail("bake cancelled");
      for(unsigned j=0;j<i;++j) if(overlaps(out[i],out[j])) fail("overlapping lightmap UV1 triangles");
    }
  }
  return out;
}
uint32_t random(uint32_t &state) {state=state*1664525u+1013904223u;return state;}
V cosine_direction(V n,uint32_t &state) {
  const float u=float(random(state)>>8)/16777216.f,v=float(random(state)>>8)/16777216.f;
  const V up=std::abs(n.y)<.99f?V{0,1,0}:V{1,0,0};
  const V x=Vector3Normalize(Vector3CrossProduct(up,n)),y=Vector3CrossProduct(n,x);
  const float r=std::sqrt(u),phi=v*6.283185307179586f;
  return Vector3Add(Vector3Add(Vector3Scale(x,r*std::cos(phi)),Vector3Scale(y,r*std::sin(phi))),Vector3Scale(n,std::sqrt(1-u)));
}
struct PathTriangle {const Tri *triangle;const LightmapInstance *instance;};
float srgb(float v) {return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);}
std::array<float,3> material_texture(const LightmapTexture &texture,const PathTriangle &hit,float a,float b,float c) {
  if(!texture.image) return {1,1,1};
  const auto &image=*texture.image;const auto &g=*hit.instance->geometry;const auto &t=*hit.triangle;
  const auto &v0=g.vertices[t.index[0]],&v1=g.vertices[t.index[1]],&v2=g.vertices[t.index[2]];
  const auto coords=[&](const RenderVertex &v){return texture.uv.set?U{v.uv1[0],v.uv1[1]}:U{v.u,v.v};};
  const auto p0=coords(v0),p1=coords(v1),p2=coords(v2);U uv{a*p0.x+b*p1.x+c*p2.x,a*p0.y+b*p1.y+c*p2.y};
  const auto &m=texture.uv.values;uv={m[0]*uv.x+m[1]*uv.y+m[4],m[2]*uv.x+m[3]*uv.y+m[5]};finite(uv.x);finite(uv.y);
  const auto wrap=[](float value,RenderWrap mode) {
    if(mode==RenderWrap::Clamp) return std::clamp(value,0.f,1.f);
    const float period=mode==RenderWrap::Mirror?2.f:1.f;float v=std::fmod(value,period);if(v<0) v+=period;
    return mode==RenderWrap::Mirror&&v>1?2-v:v;
  };
  uv={wrap(uv.x,texture.wrap_s),wrap(uv.y,texture.wrap_t)};
  const auto address=[](int i,unsigned size,RenderWrap mode) {
    if(mode==RenderWrap::Clamp) return unsigned(std::clamp(i,0,int(size)-1));
    const int period=int(size)*(mode==RenderWrap::Mirror?2:1);i=(i%period+period)%period;
    return unsigned(i>=int(size)?period-1-i:i);
  };
  const auto at=[&](int x,int y,unsigned channel) {
    const size_t index=size_t(address(y,image.height,texture.wrap_t))*image.width+address(x,image.width,texture.wrap_s);
    float value=0;
    if(image.format==RenderImageFormat::RGBA8) value=srgb(image.pixels[index*4+channel]/255.f);
    else if(image.format==RenderImageFormat::RGBA32F) std::memcpy(&value,image.pixels.data()+index*16+channel*4,4);
    else {
      uint16_t bits;std::memcpy(&bits,image.pixels.data()+index*8+channel*2,2);const unsigned exponent=(bits>>10)&31,mantissa=bits&1023;
      if(exponent==31) fail("nonfinite material image");
      value=std::ldexp(float(exponent?1024+mantissa:mantissa),exponent?int(exponent)-25:-24);if(bits&0x8000) value=-value;
    }
    finite(value);if(value<0) fail("negative material radiance/reflectance");return value;
  };
  std::array<float,3> value{};
  const float x=uv.x*image.width-.5f,y=uv.y*image.height-.5f;const int ix=int(std::floor(x)),iy=int(std::floor(y));const float fx=x-ix,fy=y-iy;
  for(unsigned channel=0;channel<3;++channel) value[channel]=texture.linear?
    (at(ix,iy,channel)*(1-fx)+at(ix+1,iy,channel)*fx)*(1-fy)+(at(ix,iy+1,channel)*(1-fx)+at(ix+1,iy+1,channel)*fx)*fy:
    at(int(std::floor(uv.x*image.width)),int(std::floor(uv.y*image.height)),channel);
  return value;
}
std::array<float,3> indirect(tinybvh::BVH &bvh,const std::vector<PathTriangle> &triangles,V origin,V normal,V toward,
                           const LightmapScene &scene,const LightmapSettings &settings,uint32_t &seed) {
  std::array<float,3> result{},throughput{1,1,1};V direction=cosine_direction(normal,seed);
  for(unsigned bounce=0;bounce<settings.indirect_bounces;++bounce) {
    tinybvh::Ray ray({origin.x,origin.y,origin.z},{direction.x,direction.y,direction.z});bvh.Intersect(ray);
    if(ray.hit.t>=1e29f||ray.hit.prim>=triangles.size()) break;
    const auto &hit=triangles[ray.hit.prim];const auto &t=*hit.triangle;
    const float b=ray.hit.u,c=ray.hit.v,a=1-b-c;
    V n=Vector3Normalize(Vector3Add(Vector3Add(Vector3Scale(t.n[0],a),Vector3Scale(t.n[1],b)),Vector3Scale(t.n[2],c)));
    if(Vector3DotProduct(n,direction)>=0) break; // opaque back face absorbs; no invented two-sided light.
    V geometric=Vector3Normalize(Vector3CrossProduct(Vector3Subtract(t.p[1],t.p[0]),Vector3Subtract(t.p[2],t.p[0])));
    if(Vector3DotProduct(geometric,n)<0) geometric=Vector3Negate(geometric);
    origin=Vector3Add(Vector3Add(origin,Vector3Scale(direction,ray.hit.t)),Vector3Scale(geometric,settings.bias));
    const float cosine=std::max(0.f,Vector3DotProduct(n,toward));float visibility=0;
    if(cosine>0) {tinybvh::Ray shadow({origin.x,origin.y,origin.z},{toward.x,toward.y,toward.z});visibility=bvh.IsOccluded(shadow)?0.f:cosine;}
    const auto albedo=material_texture(hit.instance->albedo_texture,hit,a,b,c),emission=material_texture(hit.instance->emission_texture,hit,a,b,c);
    for(unsigned channel=0;channel<3;++channel) {
      const auto &g=*hit.instance->geometry;
      const float vertex=srgb(a*g.vertices[t.index[0]].color[channel]+b*g.vertices[t.index[1]].color[channel]+c*g.vertices[t.index[2]].color[channel]);
      const float reflectance=hit.instance->albedo[channel]*albedo[channel]*vertex;
      if(reflectance>1.0001f) fail("diffuse material reflectance exceeds one");
      const float emitted=hit.instance->emission[channel]+(hit.instance->emission_texture.image?emission[channel]*hit.instance->emission_factor[channel]:0.f);
      result[channel]+=throughput[channel]*(emitted+reflectance*scene.illuminance[channel]*visibility/3.14159265358979323846f);
      throughput[channel]*=reflectance;
    }
    direction=cosine_direction(n,seed);
  }
  return result;
}
void progress_check(const std::function<bool(float)> &fn,float p) {if(fn&&!fn(p)) fail("bake cancelled");}
void word(std::vector<uint8_t> &out,uint64_t v) {for(unsigned i=0;i<8;++i) out.push_back(uint8_t(v>>(8*i)));}
uint64_t checksum(std::span<const uint8_t> data) {Hash h;for(auto b:data) h.byte(b);return h.value;}
uint64_t geometry_hash(const RenderGeometryData &g) {
  Hash h;h.word(g.vertices.size());h.word(g.indices.size());
  for(auto &v:g.vertices) {
    for(float f:{v.x,v.y,v.z,v.w,v.u,v.v,v.normal[0],v.normal[1],v.normal[2],v.uv1[0],v.uv1[1],v.lightmap_uv[0],v.lightmap_uv[1]}) h.scalar(f);
    for(float f:v.color) h.scalar(f);
    for(float f:v.tangent) h.scalar(f);
  }
  for(auto index:g.indices) h.word(index);
  return h.value;
}
std::array<float,21> vertex_fields(const RenderVertex &v) {
  return {v.x,v.y,v.z,v.u,v.v,v.w,v.color[0],v.color[1],v.color[2],v.color[3],v.normal[0],v.normal[1],v.normal[2],v.uv1[0],v.uv1[1],v.tangent[0],v.tangent[1],v.tangent[2],v.tangent[3],v.lightmap_uv[0],v.lightmap_uv[1]};
}
RenderVertex vertex_from(const std::array<float,21> &a) {
  RenderVertex v{a[0],a[1],a[2],a[3],a[4],a[5]};v.color={a[6],a[7],a[8],a[9]};v.normal={a[10],a[11],a[12]};v.uv1={a[13],a[14]};v.tangent={a[15],a[16],a[17],a[18]};v.lightmap_uv={a[19],a[20]};return v;
}
}

uint64_t lightmap_geometry_hash(const RenderGeometryData &g) {return geometry_hash(g);}
uint64_t lightmap_fingerprint(const LightmapScene &scene,const LightmapSettings &s) {
  settings(s);if(scene.identity.empty()||scene.instances.empty()||scene.instances.size()>256) fail("missing scene identity/instances or more than 256 instances");
  Hash h;h.word(4);for(unsigned char c:scene.identity) h.byte(c);h.word(scene.identity.size());
  h.word(s.size);h.word(s.samples);h.word(s.padding);h.word(s.seed);h.scalar(s.bias);
  h.word(s.generate_uv);h.scalar(s.texels_per_unit);
  h.word(s.indirect_bounces);h.word(s.indirect_samples);
  for(float f:scene.direction) h.scalar(f);
  float length=0;for(float f:scene.direction) length+=f*f;finite(length);if(length<1e-10f) fail("zero light direction");
  for(float f:scene.illuminance) {h.scalar(f);if(f<0||f>100000) fail("invalid linear illuminance");}
  std::set<uint64_t> ids;
  for(const auto &i:scene.instances) {
    if(!i.identity||!ids.insert(i.identity).second||!i.geometry) fail("missing/duplicate instance identity or geometry");
    h.word(i.identity);h.word(i.receiver);h.word(i.dynamic);
    for(float f:i.placement.transform) h.scalar(f);
    const auto &g=*i.geometry;h.word(g.vertices.size());h.word(g.indices.size());
    h.word(geometry_hash(g));h.word(i.source_identity);h.word(i.mesh_index);h.word(i.page_width);h.word(i.page_height);
    for(float f:i.albedo) {h.scalar(f);if(f<0||f>1) fail("invalid diffuse reflectance");}
    for(float f:i.emission) {h.scalar(f);if(f<0) fail("negative emission");}
    for(float f:i.emission_factor) {h.scalar(f);if(f<0) fail("negative emission factor");}
    for(const auto *texture:{&i.albedo_texture,&i.emission_texture}) {
      h.word(texture->image?1:0);if(!texture->image) continue;
      const auto &image=*texture->image;
      if(!image.width||!image.height||image.width>16384||image.height>16384||unsigned(image.format)>unsigned(RenderImageFormat::RGBA32F)||
         image.pixels.size()!=size_t(image.width)*image.height*render_image_pixel_bytes(image.format)||texture->uv.set>1||unsigned(texture->wrap_s)>unsigned(RenderWrap::Mirror)||unsigned(texture->wrap_t)>unsigned(RenderWrap::Mirror)) fail("invalid CPU material image/sampler");
      h.word(image.width);h.word(image.height);h.word(unsigned(image.format));h.word(texture->uv.set);h.word(texture->linear);h.word(unsigned(texture->wrap_s));h.word(unsigned(texture->wrap_t));
      for(float f:texture->uv.values) h.scalar(f);
      for(auto byte:image.pixels) h.byte(byte);
    }
    for(auto index:g.indices) h.word(index);
  }
  return h.value;
}

LightmapAsset lightmap_bake(const LightmapScene &source,const LightmapSettings &s,const std::function<bool(float)> &callback) {
  progress_check(callback,0);
  LightmapAsset asset;asset.fingerprint=lightmap_fingerprint(source,s);asset.settings=s;
  for(auto instance:source.instances) {if(s.generate_uv) instance.receiver=false;triangles(instance,s,callback,false);}
  const auto uv_progress=[&](float value){return !callback||callback(value*.25f);};
  const auto prepared=lightmap_prepare_uv(source,s,uv_progress);
  const std::function<bool(float)> progress=[&](float value){return !callback||callback(.25f+.75f*value);};
  const auto &scene=prepared;
  progress_check(progress,0);
  std::vector<std::vector<Tri>> geometry;
  std::vector<tinybvh::bvhvec4> corners;
  for(const auto &instance:scene.instances) {
    geometry.push_back(triangles(instance,s,progress));
    for(const auto &t:geometry.back()) for(V p:t.p) corners.emplace_back(p.x,p.y,p.z,0);
    progress_check(progress,0);
  }
  tinybvh::BVH bvh;bvh.Build(corners.data(),unsigned(corners.size()/3));
  std::vector<PathTriangle> paths;
  for(size_t i=0;i<geometry.size();++i) for(const auto &triangle:geometry[i]) paths.push_back({&triangle,&scene.instances[i]});
  const V toward=Vector3Normalize({-scene.direction[0],-scene.direction[1],-scene.direction[2]});
  size_t receivers=0;for(auto &i:scene.instances) receivers+=i.receiver;
  if(!receivers) fail("no receiver");
  if(receivers*size_t(s.size)*s.size*16>size_t(255)*1024*1024) fail("bake exceeds 255 MiB asset limit");
  for(size_t instance=0;instance<scene.instances.size();++instance) {
    if(!scene.instances[instance].receiver) continue;
    LightmapPage page;page.instance=scene.instances[instance].identity;
    page.placement=scene.instances[instance].placement;page.geometry_fingerprint=geometry_hash(*scene.instances[instance].geometry);
    page.source_instance=scene.instances[instance].source_identity?scene.instances[instance].source_identity:page.instance;
    page.source_geometry_fingerprint=scene.instances[instance].source_geometry_fingerprint?scene.instances[instance].source_geometry_fingerprint:page.geometry_fingerprint;
    page.mesh_index=scene.instances[instance].mesh_index;page.geometry=scene.instances[instance].geometry;
    const unsigned width=scene.instances[instance].page_width?scene.instances[instance].page_width:s.size;
    const unsigned height=scene.instances[instance].page_height?scene.instances[instance].page_height:s.size;
    page.image={width,height,{}};page.image.format=RenderImageFormat::RGBA32F;
    page.image.mips=RenderMipMode::Linear;
    const size_t pixels=size_t(width)*height;
    std::vector<std::array<float,4>> values(pixels,{0,0,0,1});std::vector<bool> covered(pixels);
    const auto &tris=geometry[instance];
    for(unsigned y=0;y<height;++y) {
      progress_check(progress,float(asset.pages.size()+double(y)/height)/receivers);
      for(unsigned x=0;x<width;++x) {
        const size_t pixel=size_t(y)*width+x;
        uint32_t state=s.seed^uint32_t(pixel)^uint32_t(page.instance*2654435761ull);
        unsigned hits=0;float visibility=0;std::array<float,3> bounced{};
        for(unsigned sample=0;sample<s.samples;++sample) {
          const float jx=s.samples==1?.5f:float(random(state)>>8)/16777216.f;
          const float jy=s.samples==1?.5f:float(random(state)>>8)/16777216.f;
          U uv{(x+jx)/width,(y+jy)/height};
          for(const auto &t:tris) {
            const float area=edge(t.uv[0],t.uv[1],t.uv[2]);
            const float a=edge(t.uv[1],t.uv[2],uv)/area,b=edge(t.uv[2],t.uv[0],uv)/area,c=1-a-b;
            if(a<0||b<0||c<0) continue;
            V p=Vector3Add(Vector3Add(Vector3Scale(t.p[0],a),Vector3Scale(t.p[1],b)),Vector3Scale(t.p[2],c));
            V n=Vector3Normalize(Vector3Add(Vector3Add(Vector3Scale(t.n[0],a),Vector3Scale(t.n[1],b)),Vector3Scale(t.n[2],c)));
            // Offset along the interpolated outward normal, independent of winding:
            // reflected/nonuniform placements use inverse-transpose normals.
            V geometric=Vector3Normalize(Vector3CrossProduct(Vector3Subtract(t.p[1],t.p[0]),Vector3Subtract(t.p[2],t.p[0])));
            if(Vector3DotProduct(geometric,n)<0) geometric=Vector3Negate(geometric);
            p=Vector3Add(p,Vector3Scale(geometric,s.bias));
            tinybvh::Ray ray({p.x,p.y,p.z},{toward.x,toward.y,toward.z});
            if(!bvh.IsOccluded(ray)) visibility+=std::max(0.f,Vector3DotProduct(n,toward));
            if(s.indirect_bounces) for(unsigned path=0;path<s.indirect_samples;++path) {
              if(path%256==0) progress_check(progress,float(asset.pages.size()+double(y)/height)/receivers);
              uint32_t path_seed=state^(sample*0x9e3779b9u)^(path*0x85ebca6bu);
              const auto incoming=indirect(bvh,paths,p,n,toward,scene,s,path_seed);
              for(unsigned channel=0;channel<3;++channel) bounced[channel]+=incoming[channel]/s.indirect_samples;
            }
            ++hits;break;
          }
        }
        if(hits) {covered[pixel]=true;for(unsigned c=0;c<3;++c) values[pixel][c]=scene.illuminance[c]*(visibility/hits)/3.14159265358979323846f+bounced[c]/hits;}
      }
    }
    // Multi-source nearest dilation in Manhattan distance, linear work in page
    // size. Copy samples, never blur across shadow edges. One chart per page.
    std::vector<unsigned> distance(pixels,UINT32_MAX);std::vector<size_t> queue;
    for(size_t p=0;p<pixels;++p) if(covered[p]) {distance[p]=0;queue.push_back(p);}
    if(queue.empty()) fail("receiver UV chart has no covered texels; increase density/size");
    for(size_t head=0;head<queue.size();++head) {
      const size_t p=queue[head];
      if(head%width==0) progress_check(progress,float(asset.pages.size()+.99)/receivers);
      const int x=int(p%width),y=int(p/width);
      for(auto delta:{std::pair{-1,0},std::pair{1,0},std::pair{0,-1},std::pair{0,1}}) {
        const int xx=x+delta.first,yy=y+delta.second;if(xx<0||yy<0||xx>=int(width)||yy>=int(height)) continue;
        const size_t q=size_t(yy)*width+xx;if(distance[q]!=UINT32_MAX) continue;
        distance[q]=distance[p]+1;values[q]=values[p];queue.push_back(q);
      }
    }
    page.image.pixels.resize(pixels*16);std::memcpy(page.image.pixels.data(),values.data(),pixels*16);
    asset.pages.push_back(std::move(page));
  }
  progress_check(progress,1);return asset;
}

void lightmap_save(const char *path,const LightmapAsset &asset) {
  settings(asset.settings);if(asset.pages.empty()||asset.pages.size()>4096) fail("invalid page count");
  std::vector<uint8_t> bytes;word(bytes,0x33504d4c4e494a4eull);word(bytes,asset.fingerprint);
  const auto &s=asset.settings;word(bytes,s.size);word(bytes,s.samples);word(bytes,s.padding);word(bytes,s.seed);word(bytes,std::bit_cast<uint32_t>(s.bias));
  word(bytes,s.generate_uv);word(bytes,std::bit_cast<uint32_t>(s.texels_per_unit));word(bytes,s.indirect_bounces);word(bytes,s.indirect_samples);word(bytes,asset.pages.size());
  const auto scalar=[&](float v) {finite(v);const auto bits=std::bit_cast<uint32_t>(v);for(unsigned b=0;b<4;++b) bytes.push_back(uint8_t(bits>>(8*b)));};
  std::set<uint64_t> ids;
  for(const auto &p:asset.pages) {
    if(!p.instance||!ids.insert(p.instance).second||p.image.width<8||p.image.height<8||p.image.width>512||p.image.height>512||p.image.format!=RenderImageFormat::RGBA32F||p.image.pixels.size()!=size_t(p.image.width)*p.image.height*16||!p.geometry) fail("invalid page");
    word(bytes,p.instance);
    word(bytes,p.geometry_fingerprint);for(float v:p.placement.transform) {finite(v);word(bytes,std::bit_cast<uint32_t>(v));}
    word(bytes,p.source_instance);word(bytes,p.source_geometry_fingerprint);word(bytes,p.mesh_index);word(bytes,p.image.width);word(bytes,p.image.height);
    word(bytes,p.geometry->vertices.size());word(bytes,p.geometry->indices.size());
    for(const auto &v:p.geometry->vertices) for(float value:vertex_fields(v)) scalar(value);
    for(auto index:p.geometry->indices) word(bytes,index);
    for(size_t i=0;i<p.image.pixels.size();i+=4) {
      float v;std::memcpy(&v,p.image.pixels.data()+i,4);if(!std::isfinite(v)||v<0||(i%16==12&&v!=1)) fail("invalid page value");
      const uint32_t bits=std::bit_cast<uint32_t>(v);for(unsigned b=0;b<4;++b) bytes.push_back(uint8_t(bits>>(8*b)));
    }
  }
  word(bytes,checksum(bytes));
  if(bytes.size()>size_t(256)*1024*1024) fail("asset exceeds 256 MiB limit");
  static std::atomic<unsigned> serial{0};
  auto target=std::filesystem::path(path),directory=target;
  directory+=std::string(".tmp-")+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial);
  if(!std::filesystem::create_directory(directory)) fail("cannot reserve temporary asset directory");
  auto temp=directory/"asset";
  struct Clean {std::filesystem::path p,d;~Clean(){std::error_code ec;std::filesystem::remove(p,ec);std::filesystem::remove(d,ec);}} clean{temp,directory};
  {std::ofstream stream(temp,std::ios::binary|std::ios::trunc);stream.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));stream.close();if(!stream) fail("asset write failed");}
#ifdef _WIN32
  if(!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) fail("atomic asset replacement failed");
#else
  std::filesystem::rename(temp,target);
#endif
}

LightmapAsset lightmap_load(const char *path,uint64_t expected) {
  std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file) fail("asset missing");
  auto size=file.tellg();if(size<72||size>std::streamoff(256)*1024*1024) fail("invalid asset size");
  std::vector<uint8_t> bytes(static_cast<size_t>(size));file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),size);if(!file) fail("asset read failed");
  size_t cursor=0;auto read=[&](){if(cursor+8>bytes.size()) fail("truncated asset");uint64_t v=0;for(unsigned i=0;i<8;++i) v|=uint64_t(bytes[cursor++])<<(8*i);return v;};
  if(read()!=0x33504d4c4e494a4eull) fail("unsupported asset version; rebake");
  LightmapAsset asset;asset.fingerprint=read();if(asset.fingerprint!=expected) fail("stale scene/settings fingerprint");
  auto number=[&](){auto n=read();if(n>UINT32_MAX) fail("invalid integer");return uint32_t(n);};
  auto &s=asset.settings;s.size=number();s.samples=number();s.padding=number();s.seed=number();s.bias=std::bit_cast<float>(number());settings(s);
  const auto generated=number();if(generated>1) fail("invalid UV generation mode");s.generate_uv=generated!=0;s.texels_per_unit=std::bit_cast<float>(number());settings(s);
  s.indirect_bounces=number();s.indirect_samples=number();settings(s);
  const auto count=number();if(!count||count>4096) fail("invalid page count");
  const auto scalar=[&]() {if(cursor+4>bytes.size()) fail("truncated float");uint32_t bits=0;for(unsigned b=0;b<4;++b) bits|=uint32_t(bytes[cursor++])<<(b*8);const float v=std::bit_cast<float>(bits);finite(v);return v;};
  std::set<uint64_t> ids;
  for(unsigned i=0;i<count;++i) {
    LightmapPage p;p.instance=read();if(!p.instance||!ids.insert(p.instance).second) fail("invalid page identity");
    p.geometry_fingerprint=read();for(float &v:p.placement.transform) {v=std::bit_cast<float>(number());finite(v);}
    p.source_instance=read();p.source_geometry_fingerprint=read();p.mesh_index=number();
    const auto width=number(),height=number(),vertices=number(),indices=number();
    if(width<8||height<8||width>512||height>512||!vertices||vertices>65535||indices>300000||indices%3||cursor+size_t(vertices)*84+size_t(indices)*8+size_t(width)*height*16+8>bytes.size()) fail("invalid page geometry layout");
    auto geometry=std::make_shared<RenderGeometryData>();geometry->vertices.reserve(vertices);geometry->indices.reserve(indices);
    for(unsigned v=0;v<vertices;++v) {std::array<float,21> fields;for(float &value:fields) value=scalar();geometry->vertices.push_back(vertex_from(fields));}
    for(unsigned index=0;index<indices;++index) {auto value=number();if(value>=vertices) fail("invalid page index");geometry->indices.push_back(uint16_t(value));}
    if(geometry_hash(*geometry)!=p.geometry_fingerprint) fail("page geometry fingerprint mismatch");
    p.geometry=geometry;
    p.image={width,height,{}};p.image.format=RenderImageFormat::RGBA32F;p.image.pixels.resize(size_t(width)*height*16);
    p.image.mips=RenderMipMode::Linear;
    for(size_t j=0;j<p.image.pixels.size();j+=4) {
      uint32_t bits=0;for(unsigned b=0;b<4;++b) bits|=uint32_t(bytes[cursor++])<<(8*b);
      float v=std::bit_cast<float>(bits);if(!std::isfinite(v)||v<0||(j%16==12&&v!=1)) fail("invalid texel");std::memcpy(p.image.pixels.data()+j,&v,4);
    }
    asset.pages.push_back(std::move(p));
  }
  if(cursor+8!=bytes.size()) fail("trailing asset data");
  auto actual=checksum(std::span<const uint8_t>(bytes).first(cursor));if(read()!=actual) fail("asset checksum mismatch");return asset;
}
LightmapBinding lightmap_upload(context &ctx,const LightmapAsset &asset,uint64_t instance) {
  for(const auto &p:asset.pages) if(p.instance==instance) {
    RenderTextureMap map;map.texture=renderer_create_texture(ctx,p.image.view());map.uv.set=2;
    map.wrap_s=map.wrap_t=RenderWrap::Clamp;map.min_filter=RenderMinFilter::LinearMipLinear;return {map,p.geometry_fingerprint,p.placement,p.source_instance,p.source_geometry_fingerprint,p.mesh_index,p.geometry};
  }
  fail("instance has no page");
}
std::vector<LightmapBinding> lightmap_upload_instance(context &ctx,const LightmapAsset &asset,uint64_t object) {
  std::vector<LightmapBinding> bindings;
  try {for(const auto &p:asset.pages) if(p.source_instance==object) bindings.push_back(lightmap_upload(ctx,asset,p.instance));}
  catch(...) {for(auto &b:bindings) renderer_destroy_texture(ctx,b.map.texture);throw;}
  if(bindings.empty()) fail("object has no lightmap chart");
  return bindings;
}
void lightmap_unload(context &ctx,std::vector<LightmapBinding> &bindings) {for(auto &b:bindings) renderer_destroy_texture(ctx,b.map.texture);bindings.clear();}
void lightmap_bind(MeshDraw &draw,const LightmapBinding &binding) {
  const auto &map=binding.map;
  if(!draw.geometry||draw.world!=binding.placement.transform) fail("binding geometry/transform is stale");
  const auto fingerprint=geometry_hash(*draw.geometry);
  if(fingerprint!=binding.geometry_fingerprint&&fingerprint!=binding.source_geometry_fingerprint) fail("binding geometry is stale");
  if(!map.texture||map.uv.set!=2||draw.instances||draw.previous_geometry||draw.lighting.metallic>0||draw.transmission[0][0]>0) fail("static diffuse requires dedicated lightmap UV, nonmetal opaque nonanimated noninstanced draw");
  if(binding.geometry) {draw.geometry=binding.geometry;draw.mesh={};draw.vertices=draw.geometry->vertices;draw.indices=draw.geometry->indices;}
  draw.maps.lightmap=map;draw.maps.lightmap_linear=true;draw.maps.lightmap_static_direct=true;draw.lighting.enabled=true;
}
}
