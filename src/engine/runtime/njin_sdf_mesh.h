#pragma once
#include "njin_renderer.h"
#include "njin_3d.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace njin::sdf_mesh {
inline float positive(float x) { return std::max(x,0.f); }

// Distance to the convex hull of two endpoint spheres, as in the legacy shader.
inline float cone(vec3 p,vec3 a,vec3 b,float ra,float rb) {
  const auto axis=b-a, delta=p-a;
  const float height=length(axis);
  if(height<=std::abs(ra-rb)) return ra>=rb?length(delta)-ra:length(p-b)-rb;
  const auto direction=axis/height;
  const float along=dot(delta,direction), radial=length(delta-direction*along);
  const float slope=(ra-rb)/height, side=std::sqrt(std::max(0.f,1-slope*slope));
  const float projected=-slope*radial+side*along;
  if(projected<0) return length(delta)-ra;
  if(projected>side*height) return length(p-b)-rb;
  return side*radial+slope*along-ra;
}

// Six tetrahedra per cell use the same face diagonals on neighbouring cells.
// Orient each triangle with the gradient of the original distance field.
template<class Distance>
std::shared_ptr<RenderGeometryData> tessellate(vec3 half,float detail,float clay_radius,Distance distance) {
  const float span=2*std::max({half.x,half.y,half.z});
  const float spacing=std::max(span/62,std::min(span/30,detail/4));
  half+=vec3{spacing,spacing,spacing};
  const int nx=int(std::ceil(2*half.x/spacing)),ny=int(std::ceil(2*half.y/spacing)),nz=int(std::ceil(2*half.z/spacing));
  const vec3 step{2*half.x/nx,2*half.y/ny,2*half.z/nz};
  const auto point=[&](int x,int y,int z){return vec3{x*step.x,y*step.y,z*step.z}-half;};
  const auto index=[&](int x,int y,int z){return (size_t(z)*(ny+1)+y)*(nx+1)+x;};
  std::vector<float> grid(size_t(nx+1)*(ny+1)*(nz+1));
  for(int z=0;z<=nz;++z) for(int y=0;y<=ny;++y) for(int x=0;x<=nx;++x)
    grid[index(x,y,z)]=distance(point(x,y,z));

  auto mesh=std::make_shared<RenderGeometryData>();
  const float e=spacing*.02f;
  const auto gradient=[&](vec3 p) {
    const vec3 x{e,0,0},y{0,e,0},z{0,0,e};
    const vec3 g{distance(p+x)-distance(p-x),distance(p+y)-distance(p-y),distance(p+z)-distance(p-z)};
    const float magnitude=length(g);
    return magnitude>0?g/magnitude:vec3{0,1,0};
  };
  const auto emit=[&](vec3 a,vec3 b,vec3 c) {
    const vec3 face=cross(b-a,c-a);
    if(dot(face,face)<=0) return;
    const auto na=gradient(a),nb=gradient(b),nc=gradient(c);
    const auto vertex=[&](vec3 p,vec3 n) {
      RenderVertex v{p.x,p.y,p.z,p.x/clay_radius,p.y/clay_radius};
      v.uv1[0]=p.z/clay_radius;
      v.normal={n.x,n.y,n.z};mesh->vertices.push_back(v);
    };
    vertex(a,na);
    if(dot(face,na+nb+nc)>0) {vertex(c,nc);vertex(b,nb);}
    else {vertex(b,nb);vertex(c,nc);}
  };
  constexpr int tetrahedra[][4]{{0,1,3,7},{0,3,2,7},{0,2,6,7},{0,6,4,7},{0,4,5,7},{0,5,1,7}};
  for(int z=0;z<nz;++z) for(int y=0;y<ny;++y) for(int x=0;x<nx;++x) {
    std::array<float,8> values;
    std::array<vec3,8> corners;
    unsigned mask=0;
    for(int i=0;i<8;++i) {
      const int cx=x+(i&1),cy=y+((i>>1)&1),cz=z+(i>>2);
      values[i]=grid[index(cx,cy,cz)];corners[i]=point(cx,cy,cz);
      if(values[i]<0) mask|=1u<<i;
    }
    if(mask==0||mask==255) continue;
    const auto crossing=[&](int a,int b) {return corners[a]+(corners[b]-corners[a])*(values[a]/(values[a]-values[b]));};
    for(const auto &tet:tetrahedra) {
      int inside[4],outside[4],ni=0,no=0;
      for(int i:tet) if(values[i]<0) inside[ni++]=i; else outside[no++]=i;
      if(ni==1) emit(crossing(inside[0],outside[0]),crossing(inside[0],outside[1]),crossing(inside[0],outside[2]));
      else if(no==1) emit(crossing(outside[0],inside[0]),crossing(outside[0],inside[1]),crossing(outside[0],inside[2]));
      else if(ni==2) {
        const auto a=crossing(inside[0],outside[0]),b=crossing(inside[0],outside[1]);
        const auto c=crossing(inside[1],outside[0]),d=crossing(inside[1],outside[1]);
        emit(a,b,c);emit(b,d,c);
      }
    }
  }
  return mesh;
}
}
