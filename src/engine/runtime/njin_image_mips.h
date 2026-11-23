#pragma once
#include "njin_renderer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace njin::rhi {
// Full immutable RGBA8 or linear RGBA32F chain. Exact footprint integration retains odd edges
// and 1xN tails; color is averaged in linear light, normals are renormalized.
struct ImageMipChain {
  std::vector<std::uint8_t> pixels;
  unsigned levels=1;
  explicit ImageMipChain(const RenderImage &image) : pixels(image.rgba.begin(),image.rgba.end()) {
    if(image.mips==RenderMipMode::None) return;
    if(image.format==RenderImageFormat::RGBA32F) {
      unsigned w=image.width,h=image.height;size_t offset=0;
      while(w>1||h>1) {
        const unsigned nw=std::max(1u,w/2),nh=std::max(1u,h/2);const size_t next=pixels.size();
        pixels.resize(next+size_t(nw)*nh*16);
        for(unsigned y=0;y<nh;++y) for(unsigned x=0;x<nw;++x) {
          const double x0=double(x)*w/nw,x1=double(x+1)*w/nw,y0=double(y)*h/nh,y1=double(y+1)*h/nh;
          double sum[4]{};const double area=(x1-x0)*(y1-y0);
          for(unsigned sy=unsigned(y0);sy<std::min(h,unsigned(std::ceil(y1)));++sy)
            for(unsigned sx=unsigned(x0);sx<std::min(w,unsigned(std::ceil(x1)));++sx) {
              const double weight=(std::min(x1,double(sx+1))-std::max(x0,double(sx)))*(std::min(y1,double(sy+1))-std::max(y0,double(sy)))/area;
              for(unsigned c=0;c<4;++c) {float v;std::memcpy(&v,pixels.data()+offset+(size_t(sy)*w+sx)*16+c*4,4);sum[c]+=v*weight;}
            }
          for(unsigned c=0;c<4;++c) {const float v=float(sum[c]);std::memcpy(pixels.data()+next+(size_t(y)*nw+x)*16+c*4,&v,4);}
        }
        offset=next;w=nw;h=nh;++levels;
      }
      return;
    }
    unsigned w=image.width,h=image.height;size_t offset=0;
    const auto decode=[&](unsigned char c,unsigned channel) {
      float x=c/255.f;
      if(channel<3&&image.mips==RenderMipMode::SRGB) x=x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);
      if(channel<3&&image.mips==RenderMipMode::Normal) x=x*2-1;
      return x;
    };
    while(w>1||h>1) {
      const unsigned nw=std::max(1u,w/2),nh=std::max(1u,h/2);
      const size_t next=pixels.size();pixels.resize(next+size_t(nw)*nh*4);
      for(unsigned y=0;y<nh;++y) for(unsigned x=0;x<nw;++x) {
        const double x0=double(x)*w/nw,x1=double(x+1)*w/nw,y0=double(y)*h/nh,y1=double(y+1)*h/nh;
        double sum[4]{};const double area=(x1-x0)*(y1-y0);
        for(unsigned sy=unsigned(y0);sy<std::min(h,unsigned(std::ceil(y1)));++sy)
          for(unsigned sx=unsigned(x0);sx<std::min(w,unsigned(std::ceil(x1)));++sx) {
            const double weight=(std::min(x1,double(sx+1))-std::max(x0,double(sx)))*(std::min(y1,double(sy+1))-std::max(y0,double(sy)));
            for(unsigned c=0;c<4;++c) sum[c]+=decode(pixels[offset+(size_t(sy)*w+sx)*4+c],c)*weight/area;
          }
        if(image.mips==RenderMipMode::Normal) {
          const double len=std::sqrt(sum[0]*sum[0]+sum[1]*sum[1]+sum[2]*sum[2]);
          if(len>1e-8) for(unsigned c=0;c<3;++c) sum[c]/=len;
          else {sum[0]=sum[1]=0;sum[2]=1;}
        }
        for(unsigned c=0;c<4;++c) {
          double v=sum[c];
          if(c<3&&image.mips==RenderMipMode::Normal) v=v*.5+.5;
          if(c<3&&image.mips==RenderMipMode::SRGB) v=v<=.0031308?v*12.92:1.055*std::pow(v,1/2.4)-.055;
          pixels[next+(size_t(y)*nw+x)*4+c]=std::uint8_t(std::clamp(std::lround(v*255),0l,255l));
        }
      }
      offset=next;w=nw;h=nh;++levels;
    }
  }
};
}
