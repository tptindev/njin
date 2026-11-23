$input v_texcoord0, v_color0, v_world, v_normal, v_texcoord1, v_tangent, v_instance_color, v_lightmap_uv
#include <bgfx_shader.sh>
// Njin-owned spherical transport; km, 1/km, lux and cd/m^2. See notices.
SAMPLER2D(s_atmTransmittance,0);
SAMPLER2D(s_atmSkyView,1);
SAMPLER2D(s_atmMultiple,2);
SAMPLER2D(s_atmAerial,3);
SAMPLER2D(s_atmAerialT,4);
SAMPLER2D(s_atmScene,5);
SAMPLER2D(s_atmDepth,6);
uniform vec4 u_atm[18];
#define ATM_PI 3.14159265358979323846
vec3 atmSplat(float x) {return vec3(x,x,x);}
float atmAzimuth(float y,float x) {return abs(x)+abs(y)<1e-8?0.0:atan2(y,x);}
float atmTop(float r,float mu) {return max(0.0,-r*mu+sqrt(max(0.0,r*r*(mu*mu-1.0)+u_atm[0].y*u_atm[0].y)));}
bool atmGround(float r,float mu) {return mu<0.0 && r*r*(1.0-mu*mu)<=u_atm[0].x*u_atm[0].x;}
float atmLength(float r,float mu) {
  if(atmGround(r,mu)) return max(0.0,-r*mu-sqrt(max(0.0,u_atm[0].x*u_atm[0].x-r*r*(1.0-mu*mu))));
  return atmTop(r,mu);
}
void atmMedium(float h,out vec3 rayleigh,out float mie,out vec3 extinction) {
  h=max(0.0,h);rayleigh=u_atm[1].rgb*exp(-h/u_atm[1].w);
  float density=exp(-h/u_atm[2].z);mie=u_atm[2].x*density;
  float ozone=max(0.0,1.0-abs(h-u_atm[3].w)/u_atm[4].x);
  extinction=max(rayleigh+atmSplat((u_atm[2].x+u_atm[2].y)*density)+u_atm[3].rgb*ozone,atmSplat(1e-9));
}
vec3 atmTransmission(float r,float mu) {
  if(atmGround(r,mu)) return atmSplat(0.0);
  float x=0.5+0.5*sign(mu)*sqrt(abs(mu)),y=sqrt(clamp((r-u_atm[0].x)/(u_atm[0].y-u_atm[0].x),0.0,1.0));
  return texture2DLod(s_atmTransmittance,vec2((x*255.0+0.5)/256.0,(y*63.0+0.5)/64.0),0.0).rgb;
}
vec3 atmMultiple(float r,float mu) {
  if(u_atm[10].w<=0.0) return atmSplat(0.0);
  vec2 uv=vec2(mu*0.5+0.5,sqrt(clamp((r-u_atm[0].x)/(u_atm[0].y-u_atm[0].x),0.0,1.0)));
  return texture2DLod(s_atmMultiple,(uv*31.0+0.5)/32.0,0.0).rgb*u_atm[10].w;
}
float atmHorizon() {return acos(-sqrt(max(0.0,1.0-pow(u_atm[0].x/u_atm[0].z,2.0))));}
vec3 atmDirection(vec2 uv) {
  float az=(uv.x-0.5)*2.0*ATM_PI,q=2.0*uv.y-1.0,zen=clamp(atmHorizon()-0.5*ATM_PI*q*abs(q),0.0,ATM_PI);
  return vec3(sin(az)*sin(zen),cos(zen),cos(az)*sin(zen));
}
vec2 atmViewUv(vec3 d) {
  float q=clamp((atmHorizon()-acos(clamp(d.y,-1.0,1.0)))/(0.5*ATM_PI),-1.0,1.0);
  return vec2(0.5+atmAzimuth(d.x,d.z)/(2.0*ATM_PI),0.5+0.5*sign(q)*sqrt(abs(q)));
}
vec3 atmSky(vec3 d) {return texture2DLod(s_atmSkyView,(atmViewUv(d)*vec2(191.0,107.0)+0.5)/vec2(192.0,108.0),0.0).rgb;}
float atmNight() {return 1.0-smoothstep(-0.12,0.05,u_atm[5].y);}
vec3 atmIncident(vec3 up,float r) {
  return u_atm[6].rgb*u_atm[4].y*atmTransmission(r,dot(up,u_atm[5].xyz))*max(0.0,dot(up,u_atm[5].xyz))+
         u_atm[9].rgb*u_atm[9].w*atmTransmission(r,dot(up,u_atm[8].xyz))*max(0.0,dot(up,u_atm[8].xyz));
}
float atmPhase(float c) {float g=u_atm[2].w;return (1.0-g*g)/(4.0*ATM_PI*pow(max(1e-4,1.0+g*g-2.0*g*c),1.5));}
vec3 atmSource(vec3 p,vec3 d,vec3 rs,float ms) {
  float r=length(p);vec3 up=p/r,source=atmSplat(0.0);
  for(int k=0;k<2;++k) {
    vec3 light=k==0?u_atm[5].xyz:u_atm[8].xyz,e=k==0?u_atm[6].rgb*u_atm[4].y:u_atm[9].rgb*u_atm[9].w;
    float c=dot(d,light),mu=dot(up,light);
    source+=e*(atmTransmission(r,mu)*(rs*(3.0*(1.0+c*c)/(16.0*ATM_PI))+atmSplat(ms*atmPhase(c)))+atmMultiple(r,mu)*(rs+atmSplat(ms)));
  }
  return source*u_atm[6].w;
}
vec3 atmIntegrate(vec3 d,float distance,int samples,out vec3 throughput) {
  vec3 color=atmSplat(0.0);throughput=atmSplat(1.0);
  for(int i=0;i<samples;++i) {
    float a=float(i)/float(samples),b=float(i+1)/float(samples),dt=distance*(b*b-a*a),t=distance*(a*a+b*b)*0.5;
    vec3 p=vec3(0.0,u_atm[0].z,0.0)+d*t,rs,ext;float ms;atmMedium(length(p)-u_atm[0].x,rs,ms,ext);
    vec3 stepT=exp(-ext*dt);color+=throughput*atmSource(p,d,rs,ms)*(atmSplat(1.0)-stepT)/ext;throughput*=stepT;
  }
  return color;
}
vec3 atmFibonacci(float i,float n) {
  float y=1.0-2.0*(i+0.5)/n,phi=i*2.399963229728653,x=sqrt(max(0.0,1.0-y*y));return vec3(x*cos(phi),y,x*sin(phi));
}
vec3 atmMultipleIntegral(vec2 uv) {
  float r=u_atm[0].x+(u_atm[0].y-u_atm[0].x)*uv.y*uv.y,mu=uv.x*2.0-1.0;
  vec3 sun=vec3(sqrt(max(0.0,1.0-mu*mu)),mu,0.0),energy=atmSplat(0.0),transfer=atmSplat(0.0);
  int dirs=int(u_atm[13].x),steps=int(u_atm[13].y);
  for(int j=0;j<dirs;++j) {
    vec3 d=atmFibonacci(float(j),float(dirs)),throughput=atmSplat(1.0);float distance=atmLength(r,d.y),dt=distance/float(steps);
    for(int i=0;i<steps;++i) {
      vec3 p=vec3(0.0,r,0.0)+d*(dt*(float(i)+0.5)),rs,ext;float radius=length(p),ms;atmMedium(radius-u_atm[0].x,rs,ms,ext);
      vec3 stepT=exp(-ext*dt),scat=rs+atmSplat(ms),integral=throughput*(atmSplat(1.0)-stepT)/ext;
      transfer+=integral*scat;energy+=integral*scat*atmTransmission(radius,dot(p/radius,sun))/(4.0*ATM_PI);throughput*=stepT;
    }
    if(atmGround(r,d.y)) {vec3 up=normalize(vec3(0.0,r,0.0)+d*distance);energy+=throughput*u_atm[10].rgb*max(0.0,dot(up,sun))*atmTransmission(u_atm[0].x,dot(up,sun))/ATM_PI;}
  }
  return (energy/float(dirs))/max(atmSplat(0.01),atmSplat(1.0)-transfer/float(dirs));
}
vec3 atmEnvironmentDirection(vec2 uv) {
  float phi=(uv.x-0.5)*2.0*ATM_PI,theta=uv.y*ATM_PI;return vec3(cos(phi)*sin(theta),cos(theta),sin(phi)*sin(theta));
}
vec2 atmEnvironmentUv(vec3 d) {return vec2(atmAzimuth(d.z,d.x)/(2.0*ATM_PI)+0.5,acos(clamp(d.y,-1.0,1.0))/ATM_PI);}
vec3 atmEnvironmentBase(vec3 d) {
  vec2 uv=atmEnvironmentUv(d);float x=uv.x*256.0-0.5,left=floor(x);
  vec2 a=vec2((mod(left+256.0,256.0)+0.5)/256.0,uv.y),b=vec2((mod(left+257.0,256.0)+0.5)/256.0,uv.y);
  return mix(texture2DLod(s_atmScene,a,0.0).rgb,texture2DLod(s_atmScene,b,0.0).rgb,fract(x));
}
float atmRadical(uint bits) {
  bits=(bits<<16u)|(bits>>16u);bits=((bits&0x55555555u)<<1u)|((bits&0xaaaaaaaau)>>1u);
  bits=((bits&0x33333333u)<<2u)|((bits&0xccccccccu)>>2u);bits=((bits&0x0f0f0f0fu)<<4u)|((bits&0xf0f0f0f0u)>>4u);
  bits=((bits&0x00ff00ffu)<<8u)|((bits&0xff00ff00u)>>8u);return float(bits)*2.3283064365386963e-10;
}
vec3 atmPrefilter(vec2 uv) {
  float level=min(5.0,floor(uv.y*6.0));vec3 n=atmEnvironmentDirection(vec2(uv.x,uv.y*6.0-level));
  if(level<0.5) return atmEnvironmentBase(n);
  float rough=level/5.0,a=rough*rough;vec3 up=abs(n.y)<0.99?vec3(0.0,1.0,0.0):vec3(1.0,0.0,0.0);
  vec3 tangent=normalize(cross(up,n)),bitangent=cross(n,tangent),color=atmSplat(0.0);float weight=0.0;int samples=int(u_atm[13].z);
  for(int i=0;i<samples;++i) {
    float u=atmRadical(uint(i)),phi=2.0*ATM_PI*float(i)/float(samples),c=sqrt((1.0-u)/(1.0+(a*a-1.0)*u)),s=sqrt(max(0.0,1.0-c*c));
    vec3 h=tangent*(s*cos(phi))+bitangent*(s*sin(phi))+n*c,l=normalize(2.0*dot(n,h)*h-n);float nl=max(0.0,dot(n,l));
    color+=atmEnvironmentBase(l)*nl;weight+=nl;
  }
  return color/max(weight,1e-6);
}
vec3 atmAerialSample(sampler2D image,vec2 angle,float slice) {
  return texture2DLod(image,vec2((slice*32.0+0.5+angle.x*31.0)/512.0,(0.5+angle.y*31.0)/32.0),0.0).rgb;
}
vec3 atmComposite(vec2 uv) {
  vec3 scene=texture2DLod(s_atmScene,uv,0.0).rgb;float depth=texture2DLod(s_atmDepth,uv,0.0).r;if(depth>=0.9999999) return scene;
  vec4 clip=vec4(uv.x*2.0-1.0,1.0-uv.y*2.0,depth*2.0-1.0,1.0);
  vec4 hit=vec4(dot(u_atm[14],clip),dot(u_atm[15],clip),dot(u_atm[16],clip),dot(u_atm[17],clip));
  vec3 delta=hit.xyz/hit.w-u_atm[13].xyz;float distance=length(delta)*0.001;
  vec2 angle=atmViewUv(normalize(delta));float slice=sqrt(clamp(distance/u_atm[12].y,0.0,1.0))*16.0-1.0;
  float lo=max(0.0,floor(slice)),hi=min(15.0,lo+1.0),fraction=max(0.0,fract(slice));
  vec3 scatter=mix(atmAerialSample(s_atmAerial,angle,lo),atmAerialSample(s_atmAerial,angle,hi),fraction);
  vec3 trans=mix(atmAerialSample(s_atmAerialT,angle,lo),atmAerialSample(s_atmAerialT,angle,hi),fraction);
  if(slice<0.0) {float fade=clamp(distance/(u_atm[12].y/256.0),0.0,1.0);scatter=atmAerialSample(s_atmAerial,angle,0.0)*fade;trans=mix(atmSplat(1.0),atmAerialSample(s_atmAerialT,angle,0.0),fade);}
  return scene*trans+scatter*u_atm[7].x;
}
uint atmHash(ivec3 cell) {uint h=uint(cell.x)*73856093u^uint(cell.y)*19349663u^uint(cell.z)*83492791u;h^=h>>16u;h*=2246822519u;h^=h>>13u;return h;}
vec3 atmBackground(vec3 d) {
  vec3 color=atmSky(d),trans=atmTransmission(u_atm[0].z,d.y);
  float cosSun=cos(u_atm[7].y),disk=smoothstep(cosSun,cos(u_atm[7].y*0.9),dot(d,u_atm[5].xyz))*u_atm[5].w;
  color+=disk*trans*u_atm[6].rgb*u_atm[4].y*u_atm[6].w/(2.0*ATM_PI*(1.0-cosSun));
  float cosMoon=cos(u_atm[11].w),cm=dot(d,u_atm[8].xyz);
  if(cm>cosMoon&&u_atm[8].w>0.5) {
    vec3 md=u_atm[8].xyz,tangent=normalize(cross(abs(md.y)<0.99?vec3(0.0,1.0,0.0):vec3(1.0,0.0,0.0),md));
    vec3 q=(d-md*cm)/sin(u_atm[11].w),normal=q-md*sqrt(max(0.0,1.0-dot(q,q)));float k=2.0*u_atm[12].x-1.0;
    vec3 phaseLight=tangent*sqrt(max(0.0,1.0-k*k))-md*k;
    float lit=max(0.0,dot(normal,phaseLight))*smoothstep(cosMoon,cos(u_atm[11].w*0.95),cm);
    color+=trans*u_atm[9].rgb*(1.5*u_atm[4].z*u_atm[6].w*lit/(2.0*ATM_PI*(1.0-cosMoon)));
  }
  if(u_atm[11].y>0.0&&atmNight()>0.0) {
    float a=u_atm[11].z;vec3 rotated=vec3(cos(a)*d.x-sin(a)*d.z,d.y,sin(a)*d.x+cos(a)*d.z),p=rotated*220.0;uint h=atmHash(ivec3(floor(p)));
    if(h%1000u>994u) color+=trans*atmSplat((1.0-smoothstep(0.05,0.35,length(fract(p)-0.5)))*u_atm[11].y*u_atm[6].w*atmNight());
  }
  return color*u_atm[7].x;
}
void main() {
  float mode=u_atm[0].w;vec3 color=atmSplat(0.0);
  if(mode<1.5) {
    vec2 uv=clamp((v_texcoord0*vec2(256.0,64.0)-0.5)/vec2(255.0,63.0),0.0,1.0);
    float q=2.0*uv.x-1.0,mu=q*abs(q),r=u_atm[0].x+(u_atm[0].y-u_atm[0].x)*uv.y*uv.y,distance=atmTop(r,mu),dt=distance/64.0;vec3 optical=atmSplat(0.0);
    for(int i=0;i<64;++i) {float t=(float(i)+0.5)*dt;vec3 rs,ext;float ms;atmMedium(sqrt(max(0.0,r*r+t*t+2.0*r*mu*t))-u_atm[0].x,rs,ms,ext);optical+=ext*dt;}
    color=exp(-optical);
  } else if(mode<2.5) {
    vec2 uv=clamp((v_texcoord0*vec2(192.0,108.0)-0.5)/vec2(191.0,107.0),0.0,1.0);vec3 d=atmDirection(uv),throughput;float distance=atmLength(u_atm[0].z,d.y);
    color=atmIntegrate(d,distance,48,throughput);
    if(atmGround(u_atm[0].z,d.y)) color+=throughput*u_atm[10].rgb*atmIncident(normalize(vec3(0.0,u_atm[0].z,0.0)+d*distance),u_atm[0].x)*(u_atm[6].w/ATM_PI);
    else color+=atmSplat(u_atm[11].x*u_atm[6].w*atmNight());
  } else if(mode<3.5) color=atmBackground(normalize(v_normal));
  else if(mode<4.5) color=atmMultipleIntegral(clamp((v_texcoord0*32.0-0.5)/31.0,0.0,1.0));
  else if(mode<6.5) {
    float x=v_texcoord0.x*512.0,slice=min(15.0,floor(x/32.0));vec2 uv=clamp(vec2((x-slice*32.0-0.5)/31.0,(v_texcoord0.y*32.0-0.5)/31.0),0.0,1.0);
    vec3 d=atmDirection(uv),throughput;float distance=min(u_atm[12].y*pow((slice+1.0)/16.0,2.0),atmLength(u_atm[0].z,d.y));
    color=atmIntegrate(d,distance,24,throughput);if(mode>5.5) color=throughput;
  } else if(mode<7.5) color=atmSky(atmEnvironmentDirection(v_texcoord0));
  else if(mode<8.5) color=atmPrefilter(v_texcoord0);
  else color=atmComposite(v_texcoord0);
  gl_FragColor=vec4(clamp(color,atmSplat(0.0),atmSplat(65504.0)),1.0);
}
