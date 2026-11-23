#include "njin_atmosphere_portable.h"
#include <algorithm>
#include <cmath>
#include <future>

namespace njin {
namespace {
using Rows=std::array<std::array<float,4>,18>;
vec3 rgb(const std::array<float,4> &a) {return {a[0],a[1],a[2]};}
vec3 splat(float v) {return {v,v,v};}
vec3 mul(vec3 a,vec3 b) {return {a.x*b.x,a.y*b.y,a.z*b.z};}
vec3 div(vec3 a,vec3 b) {return {a.x/b.x,a.y/b.y,a.z/b.z};}
vec3 exp3(vec3 a) {return {std::exp(a.x),std::exp(a.y),std::exp(a.z)};}
vec3 floor3(vec3 a,float b) {return {std::max(a.x,b),std::max(a.y,b),std::max(a.z,b)};}
vec3 fibonacci(unsigned i,unsigned count) {
  const float y=1-2*(i+.5f)/count,az=i*2.399963229728653f,x=std::sqrt(std::max(0.f,1-y*y));
  return {x*std::cos(az),y,x*std::sin(az)};
}
}
// CPU quadrature only for SH9: small medium-only tables are cached, no GPU readback
// or CPU specular prefilter. GPU sky/AP retain their higher resolution tables.
class CpuAtmosphere {
  Rows medium;
  std::vector<vec3> transmission,multiple;
  bool ground(double r,double mu) const {return mu<0&&r*r*(1-mu*mu)<=double(medium[0][0])*medium[0][0];}
  double distance(double r,double mu) const {
    const double radius=ground(r,mu)?medium[0][0]:medium[0][1];
    const double root=std::sqrt(std::max(0.0,radius*radius-r*r*(1-mu*mu)));
    return std::max(0.0,-r*mu+(ground(r,mu)?-root:root));
  }
  void density(double h,vec3 &rs,float &ms,vec3 &ext) const {
    h=std::max(0.0,h);rs=rgb(medium[1])*float(std::exp(-h/medium[1][3]));
    const float m=float(std::exp(-h/medium[2][2]));ms=medium[2][0]*m;
    const float ozone=float(std::max(0.0,1-std::abs(h-medium[3][3])/medium[4][0]));
    ext=floor3(rs+splat((medium[2][0]+medium[2][1])*m)+rgb(medium[3])*ozone,1e-9f);
  }
  static vec3 lookup(const std::vector<vec3> &data,unsigned width,unsigned height,float x,float y) {
    x=std::clamp(x,0.f,1.f)*(width-1);y=std::clamp(y,0.f,1.f)*(height-1);
    const auto ix=unsigned(x),iy=unsigned(y),jx=std::min(ix+1,width-1),jy=std::min(iy+1,height-1);
    const float a=x-ix,b=y-iy;
    return (data[iy*width+ix]*(1-a)+data[iy*width+jx]*a)*(1-b)+(data[jy*width+ix]*(1-a)+data[jy*width+jx]*a)*b;
  }
  vec3 trans(double r,float mu) const {
    if(ground(r,mu)) return {};
    const float x=.5f+.5f*std::copysign(std::sqrt(std::abs(mu)),mu);
    const float y=float(std::sqrt(std::clamp((r-medium[0][0])/(medium[0][1]-medium[0][0]),0.0,1.0)));
    return lookup(transmission,128,32,x,y);
  }
  vec3 ms(double r,float mu) const {
    return lookup(multiple,16,16,mu*.5f+.5f,float(std::sqrt(std::clamp((r-medium[0][0])/(medium[0][1]-medium[0][0]),0.0,1.0))));
  }
public:
  explicit CpuAtmosphere(const Rows &rows):medium(rows),transmission(128*32),multiple(16*16) {
    const double R=rows[0][0],H=rows[0][1]-R;
    for(unsigned y=0;y<32;++y) for(unsigned x=0;x<128;++x) {
      const double q=2.*x/127-1,mu=q*std::abs(q),r=R+H*double(y*y)/(31*31),top=rows[0][1];
      const double len=std::max(0.0,-r*mu+std::sqrt(std::max(0.0,top*top-r*r*(1-mu*mu))));
      vec3 optical{};
      for(unsigned i=0;i<48;++i) {
        const double t=len*(i+.5)/48;vec3 rs,ext;float m;density(std::sqrt(r*r+t*t+2*r*mu*t)-R,rs,m,ext);optical+=ext*float(len/48);
      }
      transmission[y*128+x]=exp3(-optical);
    }
    for(unsigned y=0;y<16;++y) for(unsigned x=0;x<16;++x) {
      const double r=R+H*double(y*y)/(15*15);const float mu=2.f*x/15-1;
      const vec3 sun{std::sqrt(std::max(0.f,1-mu*mu)),mu,0};vec3 energy{},transfer{};
      for(unsigned j=0;j<32;++j) {
        const vec3 d=fibonacci(j,32);const double len=distance(r,d.y);const float dt=float(len/20);vec3 throughput=splat(1);
        for(unsigned i=0;i<20;++i) {
          const vec3 p=vec3{0,float(r),0}+d*(dt*(i+.5f));const float radius=length(p);vec3 rs,ext;float m;density(radius-R,rs,m,ext);
          const vec3 step=exp3(ext*(-dt)),scat=rs+splat(m),integral=mul(throughput,div(splat(1)-step,ext));
          transfer+=mul(integral,scat);energy+=mul(mul(integral,scat),trans(radius,dot(p/radius,sun)))*(1/(4*pi));throughput=mul(throughput,step);
        }
        if(ground(r,d.y)) {
          const auto up=normalize(vec3{0,float(r),0}+d*float(len));
          energy+=mul(mul(throughput,rgb(rows[10])),trans(R,dot(up,sun)))*(std::max(0.f,dot(up,sun))/pi);
        }
      }
      multiple[y*16+x]=div(energy/32,floor3(splat(1)-transfer/32,.01f));
    }
  }
  vec3 sky(const Rows &rows,vec3 d) const {
    const float r=rows[0][2];const double len=distance(r,d.y);vec3 color{},throughput=splat(1);
    for(unsigned i=0;i<32;++i) {
      const float a=i/32.f,b=(i+1)/32.f,t=float(len*(a*a+b*b)*.5),dt=float(len*(b*b-a*a));
      const vec3 p=vec3{0,r,0}+d*t;const float radius=length(p);vec3 rs,ext;float m;density(radius-rows[0][0],rs,m,ext);vec3 source{};
      for(unsigned k=0;k<2;++k) {
        const vec3 light=rgb(rows[k?8:5]),e=rgb(rows[k?9:6])*(k?rows[9][3]:rows[4][1]);
        const float c=dot(d,light),mu=dot(p/radius,light),g=rows[2][3];
        const float phase=(1-g*g)/(4*pi*std::pow(std::max(1e-4f,1+g*g-2*g*c),1.5f));
        source+=mul(e,mul(trans(radius,mu),rs*(3*(1+c*c)/(16*pi))+splat(m*phase))+mul(ms(radius,mu),rs+splat(m))*rows[10][3]);
      }
      const auto step=exp3(ext*(-dt));color+=mul(throughput,mul(source,div(splat(1)-step,ext)))*rows[6][3];throughput=mul(throughput,step);
    }
    if(ground(r,d.y)) {
      const vec3 up=normalize(vec3{0,r,0}+d*float(len));vec3 incident{};
      for(unsigned k=0;k<2;++k) {const vec3 light=rgb(rows[k?8:5]);incident+=mul(rgb(rows[k?9:6]),trans(rows[0][0],dot(up,light)))*((k?rows[9][3]:rows[4][1])*std::max(0.f,dot(up,light)));}
      color+=mul(mul(throughput,rgb(rows[10])),incident)*(rows[6][3]/pi);
    } else {
      const float f=std::clamp((rows[5][1]+.12f)/.17f,0.f,1.f),night=1-f*f*(3-2*f);
      color+=splat(rows[11][0]*rows[6][3]*night);
    }
    return color;
  }
};
std::array<std::array<float,4>,9> atmosphere_cpu_sh(AtmosphereState &s,bool rebuild) {
  if(rebuild||!s.cpu) s.cpu=std::make_shared<CpuAtmosphere>(s.current);
  std::array<std::array<float,4>,9> sh{};
  for(unsigned i=0;i<512;++i) {
    const auto d=fibonacci(i,512),c=s.cpu->sky(s.current,d);
    const float basis[]{.282095f,.488603f*d.y,.488603f*d.z,.488603f*d.x,1.092548f*d.x*d.y,1.092548f*d.y*d.z,.315392f*(3*d.z*d.z-1),1.092548f*d.x*d.z,.546274f*(d.x*d.x-d.y*d.y)};
    for(unsigned k=0;k<9;++k) {const float w=basis[k]*(4*pi/512);sh[k][0]+=c.x*w;sh[k][1]+=c.y*w;sh[k][2]+=c.z*w;}
  }
  return sh;
}
struct AtmosphereResult {
  std::array<std::array<float,4>,9> sh;
  std::shared_ptr<CpuAtmosphere> cpu;
  double milliseconds;
};
class AtmosphereJob {
public:
  std::future<AtmosphereResult> work;
  explicit AtmosphereJob(std::future<AtmosphereResult> future):work(std::move(future)) {}
};
void atmosphere_cpu_begin(AtmosphereState &s,const Rows &rows,bool rebuild) {
  const auto cached=s.cpu;
  s.job=std::make_shared<AtmosphereJob>(std::async(std::launch::async,[rows,cached,rebuild] {
    const auto start=std::chrono::steady_clock::now();
    AtmosphereState local;local.current=rows;local.cpu=cached;
    const auto sh=atmosphere_cpu_sh(local,rebuild);
    return AtmosphereResult{sh,local.cpu,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()};
  }));
  s.job_rows=rows;s.stats.environment_pending=true;
}
bool atmosphere_cpu_ready(const AtmosphereState &s) {
  return s.job&&s.job->work.wait_for(std::chrono::seconds(0))==std::future_status::ready;
}
std::array<std::array<float,4>,9> atmosphere_cpu_finish(AtmosphereState &s) {
  auto job=std::move(s.job);s.stats.environment_pending=false;
  const auto result=job->work.get();s.cpu=result.cpu;s.stats.last_environment_worker_ms=result.milliseconds;
  return result.sh;
}
}
