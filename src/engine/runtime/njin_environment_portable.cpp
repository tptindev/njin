#include "njin_image_io.h"
#include "njin_environment_portable.h"
#include "njin_model.h"
#include "njin_ctx_get.h"
#include "njin_texture.h"
#include "njin_frame_impl.h"
#include <raymath.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace njin {
namespace {
constexpr float env_pi=3.14159265358979323846f;
using V=Vector3;
V add(V a,V b) {return Vector3Add(a,b);}
V mul(V a,float b) {return Vector3Scale(a,b);}
V direction(float u,float v) {
  const float phi=(u-.5f)*2*env_pi,theta=v*env_pi;
  return {std::cos(phi)*std::sin(theta),std::cos(theta),std::sin(phi)*std::sin(theta)};
}
struct Level {unsigned w,h;std::vector<V> data;};
V sample(const Level &im,V d) {
  const float u=std::atan2(d.z,d.x)/(2*env_pi)+.5f,v=std::acos(std::clamp(d.y,-1.f,1.f))/env_pi;
  const float x=u*im.w-.5f,y=std::clamp(v*im.h-.5f,0.f,float(im.h-1));
  const int ix=int(std::floor(x)),iy=int(y); const float fx=x-ix,fy=y-iy;
  const auto at=[&](int a,int b){return im.data[std::min(unsigned(b),im.h-1)*im.w+unsigned((a%int(im.w)+int(im.w))%int(im.w))];};
  return add(mul(add(mul(at(ix,iy),1-fx),mul(at(ix+1,iy),fx)),1-fy),mul(add(mul(at(ix,iy+1),1-fx),mul(at(ix+1,iy+1),fx)),fy));
}
V sample(const std::vector<Level> &levels,V d,float lod) {
  lod=std::clamp(lod,0.f,float(levels.size()-1)); unsigned a=unsigned(lod),b=std::min(a+1,unsigned(levels.size()-1));
  return add(mul(sample(levels[a],d),1-(lod-a)),mul(sample(levels[b],d),lod-a));
}
// IEEE binary16, round to nearest even. Finite radiance is validated before packing.
uint16_t half(float f) {
  const uint32_t bits=std::bit_cast<uint32_t>(f),mantissa=bits&0x7fffff;
  const int exponent=int((bits>>23)&255)-127;
  if(exponent < -25) return 0;
  if(exponent < -14) {
    const unsigned shift=unsigned(-exponent-1); const uint32_t m=mantissa|0x800000;
    return uint16_t((m+((1u<<(shift-1))-1)+((m>>shift)&1))>>shift);
  }
  const uint32_t rounded=mantissa+0xfff+((mantissa>>13)&1);
  return uint16_t(((exponent+15)<<10)+(rounded>>13));
}
RenderImageAsset pack(const Level &im) {
  RenderImageAsset out{im.w,im.h};out.format=RenderImageFormat::RGBA16F;out.pixels.resize(size_t(im.w)*im.h*8);
  for(size_t i=0;i<im.data.size();++i) {
    const auto v=im.data[i];const float channels[]{v.x,v.y,v.z,1};
    for(unsigned c=0;c<4;++c) {
      if(!std::isfinite(channels[c])||channels[c]<0||channels[c]>65504) throw std::runtime_error("njin IBL: radiance exceeds finite float16 range");
      const auto h=half(channels[c]);std::memcpy(out.pixels.data()+i*8+c*2,&h,2);
    }
  }
  return out;
}
std::vector<Level> source(context &ctx,const texture_slot &slot) {
  Level first{};
  if(!slot.path.empty()) {
    // Decoded with bimg (njin_image_io.h); HDR/EXR are linear, 8-bit images sRGB.
    bool linear=false;RenderImageAsset image;
    try {image=image_io_load(slot.path.c_str(),RenderImageFormat::RGBA32F,&linear);}
    catch(const std::runtime_error &) {throw std::runtime_error("njin IBL: environment image could not be decoded");}
    first.w=image.width;first.h=image.height;first.data.resize(size_t(first.w)*first.h);
    const auto *p=reinterpret_cast<const float*>(image.pixels.data());
    const auto decode=[&](float c){return linear?c:c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);};
    for(size_t i=0;i<first.data.size();++i) first.data[i]={decode(p[i*4]),decode(p[i*4+1]),decode(p[i*4+2])};
  } else {
    const auto &cpu=ctx_ref<window_guard>(ctx).renderer_resources.textures.at(slot.portable.id).cpu;
    if(cpu.format!=RenderImageFormat::RGBA8) throw std::runtime_error("njin IBL: unsupported source image format");
    first.w=cpu.width;first.h=cpu.height;first.data.resize(size_t(first.w)*first.h);
    const auto decode=[](uint8_t c){float f=c/255.f;return f<=.04045f?f/12.92f:std::pow((f+.055f)/1.055f,2.4f);};
    for(size_t i=0;i<first.data.size();++i) first.data[i]={decode(cpu.pixels[i*4]),decode(cpu.pixels[i*4+1]),decode(cpu.pixels[i*4+2])};
  }
  if(!first.w||!first.h||first.w!=first.h*2) throw std::invalid_argument("njin IBL: environment must be a 2:1 equirectangular image");
  for(auto c:first.data) for(float v:{c.x,c.y,c.z}) if(!std::isfinite(v)||v<0||v>65504) throw std::invalid_argument("njin IBL: invalid environment radiance");
  std::vector<Level> levels;levels.push_back(std::move(first));
  while(levels.back().w>1||levels.back().h>1) {
    const auto &last=levels.back();Level next{std::max(1u,last.w/2),std::max(1u,last.h/2)};next.data.resize(size_t(next.w)*next.h);
    for(unsigned y=0;y<next.h;++y) for(unsigned x=0;x<next.w;++x) {
      V sum{};float weight=0;
      // Exact footprint bounds also include the final row/column of odd dimensions.
      for(unsigned yy=y*last.h/next.h;yy<(y+1)*last.h/next.h;++yy) for(unsigned xx=x*last.w/next.w;xx<(x+1)*last.w/next.w;++xx) {
        const float area=std::sin((yy+.5f)*env_pi/last.h);sum=add(sum,mul(last.data[yy*last.w+xx],area));weight+=area;
      }
      next.data[y*next.w+x]=mul(sum,1/weight);
    }
    levels.push_back(std::move(next));
  }
  return levels;
}
void prepare(context &ctx,const std::vector<Level> &src,RenderEnvironmentData &env) {
  Level base{256,128};base.data.resize(size_t(base.w)*base.h);
  const float lod=std::max(0.f,std::log2(float(src[0].w)/base.w));
  for(unsigned y=0;y<base.h;++y) for(unsigned x=0;x<base.w;++x) base.data[y*base.w+x]=sample(src,direction((x+.5f)/base.w,(y+.5f)/base.h),lod);
  float total_area=0;
  for(unsigned y=0;y<64;++y) for(unsigned x=0;x<128;++x) {
    const V d=direction((x+.5f)/128,(y+.5f)/64),c=sample(src,d,std::max(0.f,std::log2(float(src[0].w)/128)));
    const float area=std::sin((y+.5f)*env_pi/64);total_area+=area;
    const float basis[]{.282095f,.488603f*d.y,.488603f*d.z,.488603f*d.x,1.092548f*d.x*d.y,1.092548f*d.y*d.z,.315392f*(3*d.z*d.z-1),1.092548f*d.x*d.z,.546274f*(d.x*d.x-d.y*d.y)};
    for(unsigned k=0;k<9;++k) {env.sh[k][0]+=c.x*area*basis[k];env.sh[k][1]+=c.y*area*basis[k];env.sh[k][2]+=c.z*area*basis[k];}
  }
  for(auto &c:env.sh) for(unsigned k=0;k<3;++k) c[k]*=4*env_pi/total_area;
  Level atlas{128,env.level_height*env.levels};atlas.data.resize(size_t(atlas.w)*atlas.h);
  constexpr unsigned samples=128;
  for(unsigned level=0;level<env.levels;++level) {
    const float rough=float(level)/(env.levels-1),a=rough*rough;
    std::array<V,samples> halfway{};
    for(unsigned i=0;i<samples;++i) {
      uint32_t bits=i;bits=(bits<<16)|(bits>>16);bits=((bits&0x55555555u)<<1)|((bits&0xaaaaaaaau)>>1);bits=((bits&0x33333333u)<<2)|((bits&0xccccccccu)>>2);bits=((bits&0x0f0f0f0fu)<<4)|((bits&0xf0f0f0f0u)>>4);bits=((bits&0x00ff00ffu)<<8)|((bits&0xff00ff00u)>>8);
      const float xi=bits*2.3283064365386963e-10f,phi=2*env_pi*i/samples,ct=std::sqrt((1-xi)/(1+(a*a-1)*xi)),st=std::sqrt(std::max(0.f,1-ct*ct));
      halfway[i]={std::cos(phi)*st,std::sin(phi)*st,ct};
    }
    for(unsigned y=0;y<env.level_height;++y) for(unsigned x=0;x<atlas.w;++x) {
      const V n=direction((x+.5f)/atlas.w,(y+.5f)/env.level_height);
      V color{};float weight=0;
      if(level==0) color=sample(src,n,lod);
      else {
        const V up=std::abs(n.z)<.999f?V{0,0,1}:V{1,0,0},t=Vector3Normalize(Vector3CrossProduct(up,n)),b=Vector3CrossProduct(n,t);
        for(auto h:halfway) {
          const V world_h=add(add(mul(t,h.x),mul(b,h.y)),mul(n,h.z)),l=Vector3Subtract(mul(world_h,2*h.z),n);
          const float nl=Vector3DotProduct(n,l);if(nl<=0) continue;
          const float den=h.z*h.z*(a*a-1)+1,pdf=a*a/(env_pi*den*den*4);
          const float texel_area=(2*env_pi/src[0].w)*(env_pi/src[0].h)*std::max(.001f,std::sqrt(std::max(0.f,1-l.y*l.y)));
          const float mip=std::max(0.f,.5f*std::log2(1/(samples*pdf*texel_area)));
          color=add(color,mul(sample(src,l,mip),nl));weight+=nl;
        }
        color=mul(color,1/std::max(weight,1e-6f));
      }
      atlas.data[(level*env.level_height+y)*atlas.w+x]=color;
    }
  }
  auto base_image=pack(base),atlas_image=pack(atlas);
  env.base=renderer_create_texture(ctx,base_image.view());
  try {env.atlas=renderer_create_texture(ctx,atlas_image.view());} catch(...) {renderer_destroy_texture(ctx,env.base);env.base={};throw;}
}
}
void portable_environment_update(context &ctx,model_store &store) {
  auto &device=*ctx_ref<window_guard>(ctx).graphics;
  auto &retired=store.portable_environment_retired;
  for(auto it=retired.begin();it!=retired.end();) {
    if(device.texture_in_use((*it)->base)||device.texture_in_use((*it)->atlas)) {++it;continue;}
    renderer_destroy_texture(ctx,(*it)->base);renderer_destroy_texture(ctx,(*it)->atlas);it=retired.erase(it);
  }
  const auto &light=store.portable_light;
  if(!std::isfinite(light.environment_intensity)||light.environment_intensity<0||!std::isfinite(light.environment_rotation)) throw std::invalid_argument("njin IBL: invalid intensity or rotation");
  const auto *slot=texture_slot_of(ctx_ref<texture_store>(ctx),light.environment);
  if(light.environment.id&&(!slot||slot->packed||!slot->portable)) throw std::invalid_argument("njin IBL: invalid environment texture");
  if(!slot) {
    if(store.portable_environment) {retired.push_back(store.portable_environment);store.portable_environment.reset();}
    store.portable_environment_source=store.portable_environment_version=0;return;
  }
  if(!store.portable_environment||store.portable_environment_source!=light.environment.id||store.portable_environment_version!=slot->version) {
    auto prepared=std::make_shared<RenderEnvironmentData>();prepare(ctx,source(ctx,*slot),*prepared);
    try {if(store.portable_environment) retired.push_back(store.portable_environment);}
    catch(...) {renderer_destroy_texture(ctx,prepared->base);renderer_destroy_texture(ctx,prepared->atlas);throw;}
    store.portable_environment=std::move(prepared);store.portable_environment_source=light.environment.id;store.portable_environment_version=slot->version;
  }
  store.portable_environment->intensity=light.environment_intensity;store.portable_environment->rotation=light.environment_rotation*env_pi/180;
}
void portable_environment_sky(context &ctx,model_store &store) {
  if(!store.portable_frame_light.environment_visible||!store.portable_environment) return;
  RenderVertex v[]{{-1,1,.999999f,0,0},{1,1,.999999f,1,0},{1,-1,.999999f,1,1},{-1,1,.999999f,0,0},{1,-1,.999999f,1,1},{-1,-1,.999999f,0,1}};
  const auto inverse=MatrixInvert(store.portable_view_projection);const auto eye=store.portable_camera.position;
  for(auto &a:v) {
    const float w=inverse.m3*a.x+inverse.m7*a.y+inverse.m11*a.z+inverse.m15;
    const auto world=Vector3Transform({a.x,a.y,a.z},inverse);
    a.normal={world.x/w-eye.x,world.y/w-eye.y,world.z/w-eye.z};
  }
  MeshDraw draw{v,{}};draw.texture=store.portable_environment->base;draw.environment=store.portable_environment;draw.environment_sky=true;
  draw.opaque_alpha=true;draw.tonemap=static_cast<RenderToneMap>(store.portable_frame_light.tonemap);draw.exposure=store.portable_frame_light.exposure;
  draw.linear_output=store.portable_hdr_active;renderer_draw_mesh(ctx,draw);
}
}
