// Ray tracing on bgfx (njin_raytrace.h) in compute shaders, against a CPU-built BVH
// (tinybvh BVH_GPU layout: 4 vec4 per node, leaves packed in the child references;
// triangles as v0 + two edges in leaf order, the first edge's w the draw's index).
// Traversal translated from tinybvh c9be310 external/vulkanrt/shaders/tiny.comp (MIT,
// Jacco Bikker); shadow rays stop at the first hit as Wicked Engine's TraceRay_Any does.
// The output image holds the view three times: the shadows (rows 0..h-1), the
// reflections (rows h..2h-1) and the irradiance grid's light (rows 2h..3h-1).
// NJIN_RT_STAGE 0: light3d::ray_traced_shadows, the sun (r) and up to three lamps
//   (g, b, a), legacy rt/shadow.comp.
// NJIN_RT_STAGE 1: the depth-aware denoise of the shadows.
// NJIN_RT_STAGE 2: light3d::ray_traced_reflections, one mirror ray a pixel whose hit is
//   shaded as legacy rt/common.glsl's shade_hit (rgb linear light, a 1 on a hit).
// NJIN_RT_STAGE 3: irradiance3d_desc::ray_traced, legacy rt/gi.comp: one workgroup a
//   grid point updates its ambient cube and its DDGI depth tile (16 x 16 octahedral,
//   tiles 256 to a row) from rays shaded as stage 2's (light bounces frame after frame).
// NJIN_RT_STAGE 4: the grid's light at each pixel of the view (legacy grid_irradiance,
//   rgb linear irradiance over pi, a its weight), read by the draws.
#include "bgfx_compute.sh"

#if NJIN_RT_STAGE == 3
IMAGE2D_WO(i_rtGrid, rgba32f, 0);      // per point 6 rows: the ambient cube, a 0 inside things
IMAGE2D_WO(i_rtGridDepth, rgba32f, 1); // mean distance and its square
#else
IMAGE2D_WO(i_rtOut, rgba16f, 0);
SAMPLER2D(s_rtDepth, 1);   // the camera's packed depth
#endif
#if NJIN_RT_STAGE == 1
SAMPLER2D(s_rtMask, 2);    // stage 0 output
#endif
#if NJIN_RT_STAGE == 0 || NJIN_RT_STAGE == 2 || NJIN_RT_STAGE == 3
#define NJIN_RT_TRACE
#ifndef NJIN_RT_HARDWARE
BUFFER_RO(b_rtNodes, vec4, 2);
#endif
BUFFER_RO(b_rtTris, vec4, 3);
#endif
#if NJIN_RT_STAGE == 2 || NJIN_RT_STAGE == 3
#define NJIN_RT_SHADE
BUFFER_RO(b_rtMats, vec4, 4); // per draw: linear albedo (a: drawn from both sides), linear emission
// Sun colour (linear) and 1 when it lights; ambient (linear) and the lamp count; then
// per lamp position and reach (2..9), colour times intensity and cos of the inner cone
// (10..17), direction and cos of the outer cone, -2 for a point light (18..25).
uniform vec4 u_rtLights[26];
#endif
#if NJIN_RT_STAGE >= 2
#define NJIN_RT_GRID
SAMPLER2D(s_rtGrid, 5);      // last frame's grid (stage 3 writes the other one)
SAMPLER2D(s_rtGridDepth, 6);
// Grid corner, share kept from last frame; size, intensity; points along x, y, z, rays
// per point; grid on, environment on, frame, blend metres; the environment's SH9
// (radiance, times its intensity; row 4 w its rotation).
uniform vec4 u_rtGrid[13];
#endif
uniform mat4 u_rtInverse;  // clip (OpenGL convention) to world
uniform vec4 u_rtSun;      // towards the sun xyz, cone half-angle (radians)
uniform vec4 u_rtParams;   // width, height, rays, frame
uniform vec4 u_rtExtra;    // ray bias metres, denoise radius, scene empty, max distance
uniform vec4 u_rtLamps[5]; // lamps: position, sphere radius; eye xyz, sun traced; lamp count, skip metres

#if NJIN_RT_STAGE != 3
float njinRtDepth(vec2 uv) {
  return dot(texture2DLod(s_rtDepth,uv,0.0).rgb,vec3(1.0,1.0/255.0,1.0/65025.0));
}
vec3 njinRtWorld(vec2 uv,float depth) {
  vec4 clip=vec4(uv.x*2.0-1.0,1.0-uv.y*2.0,depth*2.0-1.0,1.0);
  vec4 p=mul(u_rtInverse,clip);
  return p.xyz/p.w;
}
#endif
float njinRtHash(vec2 p,float s) {
  vec3 p3=fract(vec3(p.x,p.y,s)*vec3(0.1031,0.1030,0.0973));
  p3+=dot(p3,p3.yzx+33.33);
  return fract((p3.x+p3.y)*p3.z);
}

#ifdef NJIN_RT_TRACE
#define NJIN_RT_STACK 32
#ifdef NJIN_RT_HARDWARE
// The same query through the GPU's ray tracing hardware (njin_raytrace_vk.cpp builds one
// acceleration structure from b_rtTris in its order, so a primitive's index times 3 is
// its first row there). Opaque, no culling: both sides hit, as the loop below.
bool njinRtTrace(vec3 O,vec3 D,float tmin,float tmax,bool firstHit,out uint hitTri,out float hitT) {
  rayQueryEXT q;
  uint flags=gl_RayFlagsOpaqueEXT|(firstHit?gl_RayFlagsTerminateOnFirstHitEXT:0u);
  rayQueryInitializeEXT(q,u_rtScene,flags,0xFFu,O,tmin,D,tmax);
  while(rayQueryProceedEXT(q)) {}
  hitTri=0u;hitT=tmax;
  if(rayQueryGetIntersectionTypeEXT(q,true)!=gl_RayQueryCommittedIntersectionTriangleEXT) return false;
  hitTri=uint(rayQueryGetIntersectionPrimitiveIndexEXT(q,true))*3u;
  hitT=rayQueryGetIntersectionTEXT(q,true);
  return true;
}
#else
float njinRtSafeRcp(float x) {return 1.0/(sign(x)*max(abs(x),0.00001));}
// The nearest (or, with `firstHit`, the first found) triangle on O + t*D, tmin < t < tmax:
// its first row in b_rtTris and its distance.
bool njinRtTrace(vec3 O,vec3 D,float tmin,float tmax,bool firstHit,out uint hitTri,out float hitT) {
  vec3 rD=vec3(njinRtSafeRcp(D.x),njinRtSafeRcp(D.y),njinRtSafeRcp(D.z)),Ord=O*rD;
  uint stack[NJIN_RT_STACK];
  uint node=0u,stackPtr=0u;
  bool found=false;
  hitTri=0u;hitT=tmax;
  for(int guard=0;guard<4096;++guard) {
    if((node&0x80000000u)!=0u) {
      uint triCount=(node>>24)&127u,v0=(node&0xffffffu)*3u;
      for(uint i=0u;i<triCount;++i) {
        vec3 a=b_rtTris[v0].xyz,edge1=b_rtTris[v0+1u].xyz,edge2=b_rtTris[v0+2u].xyz;
        vec3 h=cross(D,edge2);float det=dot(edge1,h),f=1.0/det;
        vec3 s=O-a;float u=f*dot(s,h);vec3 q=cross(s,edge1);
        float v=f*dot(D,q),d=f*dot(edge2,q);
        if(abs(det)>0.0000001&&u>=0.0&&v>=0.0&&u+v<=1.0&&d>tmin&&d<hitT) {
          found=true;hitTri=v0;hitT=d;
          if(firstHit) return true;
        }
        v0+=3u;
      }
      if(stackPtr==0u) return found;
      node=stack[--stackPtr];continue;
    }
    uint base=node*4u;
    vec4 lmin=b_rtNodes[base],lmax=b_rtNodes[base+1u],rmin=b_rtNodes[base+2u],rmax=b_rtNodes[base+3u];
    uint left=floatBitsToUint(lmin.w),right=floatBitsToUint(lmax.w);
    vec3 t1a=lmin.xyz*rD-Ord,t2a=lmax.xyz*rD-Ord,t1b=rmin.xyz*rD-Ord,t2b=rmax.xyz*rD-Ord;
    vec3 minta=min(t1a,t2a),maxta=max(t1a,t2a),mintb=min(t1b,t2b),maxtb=max(t1b,t2b);
    float tmina=max(max(max(minta.x,minta.y),minta.z),0.0),tminb=max(max(max(mintb.x,mintb.y),mintb.z),0.0);
    float tmaxa=min(min(min(maxta.x,maxta.y),maxta.z),hitT),tmaxb=min(min(min(maxtb.x,maxtb.y),maxtb.z),hitT);
    bool hitL=tmina<=tmaxa,hitR=tminb<=tmaxb;
    if(hitL&&hitR) {
      bool leftFirst=tmina<=tminb;
      node=leftFirst?left:right;
      if(stackPtr<uint(NJIN_RT_STACK)) stack[stackPtr++]=leftFirst?right:left;
    } else if(hitL) node=left;
    else if(hitR) node=right;
    else {
      if(stackPtr==0u) return found;
      node=stack[--stackPtr];
    }
  }
  return found;
}
#endif
bool njinRtOccluded(vec3 O,vec3 D,float tmin,float tmax) {
  uint tri;float t;
  return njinRtTrace(O,D,tmin,tmax,true,tri,t);
}
#endif
#if NJIN_RT_STAGE != 3 && NJIN_RT_STAGE != 1
// The surface normal at `p` from the neighbouring depths (the nearer side at
// silhouettes), facing the eye.
vec3 njinRtNormal(vec2 uv,vec2 texel,vec3 p) {
  vec3 px=njinRtWorld(uv+vec2(texel.x,0.0),njinRtDepth(uv+vec2(texel.x,0.0)))-p;
  vec3 nx=p-njinRtWorld(uv-vec2(texel.x,0.0),njinRtDepth(uv-vec2(texel.x,0.0)));
  vec3 py=njinRtWorld(uv+vec2(0.0,texel.y),njinRtDepth(uv+vec2(0.0,texel.y)))-p;
  vec3 ny=p-njinRtWorld(uv-vec2(0.0,texel.y),njinRtDepth(uv-vec2(0.0,texel.y)));
  vec3 dx=dot(px,px)<dot(nx,nx)?px:nx,dy=dot(py,py)<dot(ny,ny)?py:ny;
  vec3 n=normalize(cross(dy,dx));
  if(dot(n,u_rtLamps[3].xyz-p)<0.0) n=-n;
  return n;
}
#endif

#ifdef NJIN_RT_GRID
// The irradiance grid (legacy rt/common.glsl): spacing, points, octahedral depths.
vec3 njinGridSpacing() {
  vec3 cnt=u_rtGrid[2].xyz;
  return u_rtGrid[1].xyz/max(cnt-1.0,vec3(1.0,1.0,1.0));
}
vec3 njinGridPoint(vec3 q) {
  vec3 cnt=u_rtGrid[2].xyz;
  vec3 t=mix(vec3(0.5,0.5,0.5),q/max(cnt-1.0,vec3(1.0,1.0,1.0)),step(vec3(1.5,1.5,1.5),cnt));
  return u_rtGrid[0].xyz+u_rtGrid[1].xyz*t;
}
vec2 njinGridOct(vec3 d) {
  d/=abs(d.x)+abs(d.y)+abs(d.z);
  vec2 e=d.z>=0.0?d.xy:(vec2(1.0,1.0)-abs(d.yx))*vec2(d.x>=0.0?1.0:-1.0,d.y>=0.0?1.0:-1.0);
  return e*0.5+0.5;
}
ivec2 njinGridTile(int col) {return ivec2((col-(col/256)*256)*16,(col/256)*16);}
// Point `col`'s mean distance to what is round it towards `d`, and the mean square.
vec2 njinGridDepth(int col,vec3 d) {
  vec2 t=clamp(njinGridOct(d)*16.0-0.5,vec2(0.0,0.0),vec2(15.0,15.0));
  ivec2 a=ivec2(floor(t)),b=min(a+ivec2(1,1),ivec2(15,15)),tile=njinGridTile(col);
  vec2 f=t-vec2(a);
  vec2 m00=texelFetch(s_rtGridDepth,tile+ivec2(a.x,a.y),0).rg,m10=texelFetch(s_rtGridDepth,tile+ivec2(b.x,a.y),0).rg;
  vec2 m01=texelFetch(s_rtGridDepth,tile+ivec2(a.x,b.y),0).rg,m11=texelFetch(s_rtGridDepth,tile+ivec2(b.x,b.y),0).rg;
  return mix(mix(m00,m10,f.x),mix(m01,m11,f.x),f.y);
}
// How much point `col` at `at` sees a surface at `pos` facing `n` (DDGI's weights).
float njinGridVisibility(int col,vec3 at,vec3 pos,vec3 n) {
  vec3 sp=njinGridSpacing();
  vec3 d=pos+n*(0.2*min(sp.x,min(sp.y,sp.z)))-at;
  float r=length(d);
  if(r<0.0001) return 1.0;
  d/=r;
  float wrap=(dot(-d,n)+1.0)*0.5;
  float w=wrap*wrap+0.2;
  vec2 m=njinGridDepth(col,d);
  if(r>m.x) {
    float variance=abs(m.y-m.x*m.x),e=r-m.x;
    float c=variance/(variance+e*e);
    w*=max(c*c*c,0.0);
  }
  if(w<0.2) w*=w*w/0.04;
  return max(w,0.000001);
}
// The grid's light at `pos` facing `n` (linear irradiance over pi, times its
// intensity) and its weight: with `blend` rising over the box's blend margin
// (legacy grid_irradiance, the draws), else 1 anywhere in the box (ambient_at).
vec3 njinGridIrradiance(vec3 pos,vec3 n,bool blend,out float weight) {
  weight=0.0;
  if(u_rtGrid[3].x<0.5) return vec3(0.0,0.0,0.0);
  vec3 lo=u_rtGrid[0].xyz,size=u_rtGrid[1].xyz;
  vec3 inside=min(pos-lo,lo+size-pos);
  float margin=min(inside.x,min(inside.y,inside.z));
  if(margin<0.0) return vec3(0.0,0.0,0.0);
  weight=blend?clamp(margin/max(u_rtGrid[3].w,0.0001),0.0,1.0):1.0;
  if(weight<=0.0) return vec3(0.0,0.0,0.0);
  vec3 cnt=u_rtGrid[2].xyz;
  vec3 g=clamp((pos-lo)/max(size,vec3(0.0001,0.0001,0.0001)),0.0,1.0)*max(cnt-1.0,vec3(0.0,0.0,0.0));
  vec3 g0=floor(g),f=g-g0,n2=n*n;
  int dx=n.x>=0.0?0:1,dy=n.y>=0.0?2:3,dz=n.z>=0.0?4:5;
  vec3 sum=vec3(0.0,0.0,0.0);
  float wsum=0.0;
  for(int c=0;c<8;++c) {
    vec3 o=vec3(float(c-(c/2)*2),float((c/2)-(c/4)*2),float(c/4));
    vec3 q=min(g0+o,max(cnt-1.0,vec3(0.0,0.0,0.0)));
    float w=(o.x>0.5?f.x:1.0-f.x)*(o.y>0.5?f.y:1.0-f.y)*(o.z>0.5?f.z:1.0-f.z);
    int col=int(q.x+cnt.x*(q.y+cnt.y*q.z));
    // A point inside something (alpha 0) does not count, one that does not see `pos` hardly.
    w*=texelFetch(s_rtGrid,ivec2(col,0),0).a;
    if(w<=0.0) continue;
    w*=njinGridVisibility(col,njinGridPoint(q),pos,n);
    sum+=w*(n2.x*texelFetch(s_rtGrid,ivec2(col,dx),0).rgb+n2.y*texelFetch(s_rtGrid,ivec2(col,dy),0).rgb+n2.z*texelFetch(s_rtGrid,ivec2(col,dz),0).rgb);
    wsum+=w;
  }
  if(wsum<=0.0001) {weight=0.0;return vec3(0.0,0.0,0.0);}
  // Where nearly every point is hidden the few left are not trusted alone.
  return sum/max(wsum,0.05)*u_rtGrid[1].w;
}
#endif
#ifdef NJIN_RT_SHADE
// Light from all around along `d` where a ray meets nothing: the environment's
// irradiance over pi, or the ambient colour (legacy sky()).
vec3 njinRtSky(vec3 d) {
  if(u_rtGrid[3].y<0.5) return u_rtLights[1].rgb;
  float c=cos(u_rtGrid[4].w),s=sin(u_rtGrid[4].w);
  vec3 n=vec3(c*d.x-s*d.z,d.y,s*d.x+c*d.z);
  vec3 e=u_rtGrid[4].rgb*0.886227+u_rtGrid[5].rgb*(1.023328*n.y)+u_rtGrid[6].rgb*(1.023328*n.z)+u_rtGrid[7].rgb*(1.023328*n.x)+
    u_rtGrid[8].rgb*(0.858086*n.x*n.y)+u_rtGrid[9].rgb*(0.858086*n.y*n.z)+u_rtGrid[10].rgb*(0.247708*(3.0*n.z*n.z-1.0))+
    u_rtGrid[11].rgb*(0.858086*n.x*n.z)+u_rtGrid[12].rgb*(0.429043*(n.x*n.x-n.y*n.y));
  return max(e,vec3(0.0,0.0,0.0))/3.14159265;
}
// The light leaving the hit of a ray along `r` (triangle row `tri`, at `hp`) back
// towards its start, linear: a matte surface of its draw's colour under the sun
// (through its shadow), the lamps (unshadowed) and the ambient light (the grid's
// where there is one, so light bounces again), plus its glow (legacy shade_hit).
vec3 njinRtShade(uint tri,vec3 hp,vec3 r) {
  vec4 edge=b_rtTris[tri+1u];
  vec3 hn=normalize(cross(edge.xyz,b_rtTris[tri+2u].xyz));
  if(dot(hn,r)>0.0) hn=-hn;
  vec3 ho=hp+hn*0.01;
  uint draw=floatBitsToUint(edge.w);
  vec4 albedo=b_rtMats[draw*2u],glow=b_rtMats[draw*2u+1u];
  float gridWeight;
  vec3 grid=njinGridIrradiance(hp,hn,false,gridWeight);
  vec3 light=gridWeight>0.0?grid:u_rtLights[1].rgb;
  if(u_rtLights[0].w>0.5) {
    vec3 l=normalize(u_rtSun.xyz);
    float nl=dot(hn,l);
    if(nl>0.0&&!njinRtOccluded(ho,l,0.0,u_rtExtra.w)) light+=u_rtLights[0].rgb*nl;
  }
  for(int i=0;i<8;++i) {
    if(float(i)>=u_rtLights[1].w) break;
    vec4 at=u_rtLights[2+i],color=u_rtLights[10+i],spot=u_rtLights[18+i];
    vec3 d=at.xyz-hp;
    float dist=length(d);
    if(dist>=at.w) continue;
    vec3 li=d/max(dist,0.00001);
    float fall=1.0-dist/at.w;
    float att=fall*fall*max(dot(hn,li),0.0);
    if(spot.w>-1.5) att*=clamp((dot(-li,spot.xyz)-spot.w)/max(color.w-spot.w,0.0001),0.0,1.0);
    light+=color.rgb*att;
  }
  return albedo.rgb*light+glow.rgb;
}
#endif

#if NJIN_RT_STAGE == 0
NUM_THREADS(8,8,1)
void main() {
  ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
  vec2 size=u_rtParams.xy;
  if(float(pixel.x)>=size.x||float(pixel.y)>=size.y) return;
  vec2 texel=1.0/size,uv=(vec2(pixel)+0.5)*texel;
  float depth=njinRtDepth(uv);
  if(depth>=0.99999||u_rtExtra.z>0.5) {imageStore(i_rtOut,pixel,vec4(1.0,1.0,1.0,1.0));return;}
  vec3 p=njinRtWorld(uv,depth);
  vec3 n=njinRtNormal(uv,texel,p);
  vec3 origin=p+n*u_rtExtra.x;
  float rays=max(u_rtParams.z,1.0),skip=u_rtLamps[4].y;
  vec4 result=vec4(1.0,1.0,1.0,1.0);
  if(u_rtLamps[3].w>0.5) {
    vec3 sun=normalize(u_rtSun.xyz);
    float lit=0.0;
    if(dot(n,sun)>0.0) {
      // Rays spread over the sun's disc (cone half-angle u_rtSun.w).
      vec3 t=normalize(abs(sun.y)<0.99?cross(sun,vec3(0.0,1.0,0.0)):cross(sun,vec3(1.0,0.0,0.0))),b=cross(sun,t);
      for(int i=0;i<16;++i) {
        if(float(i)>=rays) break;
        float r1=njinRtHash(vec2(pixel),u_rtParams.w*16.0+float(i)),r2=njinRtHash(vec2(pixel)+17.3,u_rtParams.w*16.0+float(i));
        float radius=sqrt(r1)*tan(u_rtSun.w),angle=r2*6.28318530718;
        vec3 d=normalize(sun+(t*cos(angle)+b*sin(angle))*radius);
        if(!njinRtOccluded(origin,d,skip,u_rtExtra.w)) lit+=1.0;
      }
      lit/=rays;
    }
    result.r=lit;
  }
  // Lamps: rays towards points of the light's sphere, up to just short of it.
  for(int k=0;k<3;++k) {
    if(float(k)>=u_rtLamps[4].x) break;
    vec4 lamp=u_rtLamps[k];
    float lit=0.0;
    for(int i=0;i<16;++i) {
      if(float(i)>=rays) break;
      float seed=u_rtParams.w*64.0+float(i*3+k*17);
      vec3 jitter=vec3(njinRtHash(vec2(pixel),seed),njinRtHash(vec2(pixel)+31.7,seed),njinRtHash(vec2(pixel)+57.1,seed))*2.0-1.0;
      vec3 to=lamp.xyz+jitter*lamp.w-origin;
      float len=length(to);
      if(len<0.0001||!njinRtOccluded(origin,to/len,skip,len-lamp.w*0.5)) lit+=1.0;
    }
    if(k==0) result.g=lit/rays; else if(k==1) result.b=lit/rays; else result.a=lit/rays;
  }
  imageStore(i_rtOut,pixel,result);
}
#elif NJIN_RT_STAGE == 1
// Depth-aware box filter of the traced shadows (all four channels) over u_rtExtra.y pixels.
NUM_THREADS(8,8,1)
void main() {
  ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
  vec2 size=u_rtParams.xy;
  if(float(pixel.x)>=size.x||float(pixel.y)>=size.y) return;
  vec2 texel=1.0/size,uv=(vec2(pixel)+0.5)*texel;
  float center=njinRtDepth(uv),radius=u_rtExtra.y;
  vec3 p=njinRtWorld(uv,center);
  vec4 sum=vec4(0.0,0.0,0.0,0.0);float weights=0.0;
  for(int y=-8;y<=8;++y) for(int x=-8;x<=8;++x) {
    if(abs(float(x))>radius||abs(float(y))>radius) continue;
    vec2 at=uv+vec2(float(x),float(y))*texel;
    float d=njinRtDepth(at);
    float w=exp(-abs(length(njinRtWorld(at,d)-p))*8.0);
    sum+=texture2DLod(s_rtMask,vec2(at.x,clamp(at.y,0.0,1.0)/3.0),0.0)*w;weights+=w;
  }
  imageStore(i_rtOut,pixel,sum/max(weights,0.0001));
}
#elif NJIN_RT_STAGE == 2
NUM_THREADS(8,8,1)
void main() {
  ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
  vec2 size=u_rtParams.xy;
  if(float(pixel.x)>=size.x||float(pixel.y)>=size.y) return;
  ivec2 target=ivec2(pixel.x,pixel.y+int(size.y));
  vec2 texel=1.0/size,uv=(vec2(pixel)+0.5)*texel;
  float depth=njinRtDepth(uv);
  if(depth>=0.99999||u_rtExtra.z>0.5) {imageStore(i_rtOut,target,vec4(0.0,0.0,0.0,0.0));return;}
  vec3 p=njinRtWorld(uv,depth);
  vec3 n=njinRtNormal(uv,texel,p);
  vec3 r=reflect(normalize(p-u_rtLamps[3].xyz),n);
  vec3 origin=p+n*u_rtExtra.x;
  uint tri;float t;
  if(!njinRtTrace(origin,r,0.0,u_rtExtra.w,false,tri,t)) {imageStore(i_rtOut,target,vec4(0.0,0.0,0.0,0.0));return;}
  imageStore(i_rtOut,target,vec4(njinRtShade(tri,origin+r*t,r),1.0));
}
#elif NJIN_RT_STAGE == 3
// This point's rays: direction and distance, light and whether it met the inside of something.
SHARED vec4 rayDir[256];
SHARED vec4 rayLight[256];
// Direction `i` of `n` spread evenly over the sphere (spherical Fibonacci).
vec3 njinFibonacci(float i,float n) {
  float z=1.0-(2.0*i+1.0)/n;
  float r=sqrt(max(1.0-z*z,0.0));
  float a=6.2831853*fract(i*0.6180340);
  return vec3(r*cos(a),r*sin(a),z);
}
// A rotation (rows) from three hashes, different every frame.
void njinTurn(float cell,float frame,out vec3 r0,out vec3 r1,out vec3 r2) {
  float a=6.2831853*njinRtHash(vec2(cell,frame),7.0),b=acos(2.0*njinRtHash(vec2(cell,frame),18.0)-1.0),c=6.2831853*njinRtHash(vec2(cell,frame),30.0);
  vec3 x=vec3(sin(b)*cos(a),sin(b)*sin(a),cos(b));
  float s=sin(c),k=cos(c),t=1.0-k;
  r0=vec3(t*x.x*x.x+k,t*x.x*x.y-s*x.z,t*x.x*x.z+s*x.y);
  r1=vec3(t*x.x*x.y+s*x.z,t*x.y*x.y+k,t*x.y*x.z-s*x.x);
  r2=vec3(t*x.x*x.z-s*x.y,t*x.y*x.z+s*x.x,t*x.z*x.z+k);
}
// The direction at the centre of texel `t` of a 16 x 16 octahedral tile.
vec3 njinOctDir(ivec2 t) {
  vec2 e=(vec2(t)+0.5)/16.0*2.0-1.0;
  vec3 v=vec3(e,1.0-abs(e.x)-abs(e.y));
  if(v.z<0.0) v.xy=(vec2(1.0,1.0)-abs(v.yx))*vec2(v.x>=0.0?1.0:-1.0,v.y>=0.0?1.0:-1.0);
  return normalize(v);
}
vec3 njinAxis(int i) {
  return i==0?vec3(1.0,0.0,0.0):i==1?vec3(-1.0,0.0,0.0):i==2?vec3(0.0,1.0,0.0):i==3?vec3(0.0,-1.0,0.0):i==4?vec3(0.0,0.0,1.0):vec3(0.0,0.0,-1.0);
}
NUM_THREADS(64,1,1)
void main() {
  int cell=int(gl_WorkGroupID.x),lid=int(gl_LocalInvocationID.x);
  ivec3 cnt=ivec3(u_rtGrid[2].xyz);
  int total=cnt.x*cnt.y*cnt.z;
  if(cell>=total) return;
  ivec3 g=ivec3(cell-(cell/cnt.x)*cnt.x,(cell/cnt.x)-(cell/(cnt.x*cnt.y))*cnt.y,cell/(cnt.x*cnt.y));
  vec3 pos=njinGridPoint(vec3(g));
  int rays=int(clamp(u_rtGrid[2].w,1.0,256.0));
  vec3 sp=njinGridSpacing();
  float reach=1.5*max(sp.x,max(sp.y,sp.z));
  float keep=clamp(u_rtGrid[0].w,0.0,0.99);
  vec3 r0,r1,r2;
  njinTurn(float(cell),u_rtGrid[3].z,r0,r1,r2);
  for(int r=lid;r<256;r+=64) {
    if(r>=rays) break;
    vec3 f=njinFibonacci(float(r),float(rays));
    vec3 dir=normalize(vec3(dot(r0,f),dot(r1,f),dot(r2,f)));
    uint tri;float t;
    bool hit=njinRtTrace(pos,dir,0.0,2000.0,false,tri,t);
    bool back=false;
    if(hit) {
      // The inside of something (its back, unless it is drawn from both sides): no
      // light, and a short distance, so the point does not see past it.
      vec4 edge=b_rtTris[tri+1u];
      vec3 face=cross(edge.xyz,b_rtTris[tri+2u].xyz);
      back=dot(face,dir)>0.0&&b_rtMats[floatBitsToUint(edge.w)*2u].a<0.5;
    }
    float dist=hit?t:reach;
    rayDir[r]=vec4(dir,back?min(dist,reach)*0.2:min(dist,reach));
    rayLight[r]=vec4(back?vec3(0.0,0.0,0.0):(hit?njinRtShade(tri,pos+dir*t,dir):njinRtSky(dir)),back?1.0:0.0);
  }
  barrier();
  ivec2 tile=njinGridTile(cell);
  // Light: per axis of the ambient cube, the cosine-weighted mean of what came in.
  if(lid<6) {
    vec3 sum=vec3(0.0,0.0,0.0),axis=njinAxis(lid);
    float w=0.0,backs=0.0;
    for(int r=0;r<256;++r) {
      if(r>=rays) break;
      backs+=rayLight[r].a;
      if(rayLight[r].a>0.5) continue;
      float k=dot(rayDir[r].xyz,axis);
      if(k>0.0) {sum+=rayLight[r].rgb*k;w+=k;}
    }
    vec4 old=texelFetch(s_rtGrid,ivec2(cell,lid),0);
    vec3 now=w>0.0?sum/w:vec3(0.0,0.0,0.0);
    // Alpha 0 is a new point or one inside something (most rays meet insides).
    bool inside=backs>0.25*float(rays);
    vec3 kept=old.a>0.5?mix(now,old.rgb,keep):now;
    imageStore(i_rtGrid,ivec2(cell,lid),vec4(kept,inside?0.0:1.0));
  }
  // Depth: per texel of the point's tile (four a thread), the mean distance and its
  // square over the rays near its direction (DDGI's sharpness 50).
  for(int k4=0;k4<4;++k4) {
    int texel=lid+k4*64;
    ivec2 t=ivec2(texel-(texel/16)*16,texel/16);
    vec3 td=njinOctDir(t);
    vec2 m=vec2(0.0,0.0);
    float w=0.0;
    for(int r=0;r<256;++r) {
      if(r>=rays) break;
      float k=pow(max(dot(td,rayDir[r].xyz),0.0),50.0);
      if(k<=0.0001) continue;
      float d=rayDir[r].w;
      m+=vec2(d,d*d)*k;w+=k;
    }
    vec4 old=texelFetch(s_rtGridDepth,tile+t,0);
    vec4 result=old;
    if(w>0.0) {
      m/=w;
      vec2 kept=old.a>0.5?mix(m,old.rg,keep):m;
      result=vec4(kept,0.0,1.0);
    }
    imageStore(i_rtGridDepth,tile+t,result);
  }
}
#else
// The grid's light at each pixel of the view, into the image's third part.
NUM_THREADS(8,8,1)
void main() {
  ivec2 pixel=ivec2(gl_GlobalInvocationID.xy);
  vec2 size=u_rtParams.xy;
  if(float(pixel.x)>=size.x||float(pixel.y)>=size.y) return;
  ivec2 target=ivec2(pixel.x,pixel.y+2*int(size.y));
  vec2 texel=1.0/size,uv=(vec2(pixel)+0.5)*texel;
  float depth=njinRtDepth(uv);
  if(depth>=0.99999) {imageStore(i_rtOut,target,vec4(0.0,0.0,0.0,0.0));return;}
  vec3 p=njinRtWorld(uv,depth);
  vec3 n=njinRtNormal(uv,texel,p);
  float weight;
  vec3 light=njinGridIrradiance(p,n,true,weight);
  imageStore(i_rtOut,target,vec4(light,weight));
}
#endif
