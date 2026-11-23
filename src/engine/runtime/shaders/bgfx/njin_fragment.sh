#define NJIN_BGFX_EXTENSIONS 1
#define NJIN_TERRAIN_SURFACE 1
#define NJIN_ADVANCED_MATERIALS 1
#define NJIN_CLUSTERED_LIGHTS 1
#include <bgfx_shader.sh>
SAMPLER2D(s_njinColor,0);
#if defined(NJIN_VIRTUAL_TEXTURE)
#include "njin_virtual.sh"
#endif
uniform vec4 u_njinTint;
uniform vec4 u_njinWorldStyle[6];
uniform vec4 u_njinParticleFx;
uniform vec4 u_njinSurfaceFx[4];
uniform vec4 u_njinLegacyPost[10];
uniform vec4 u_njinLightEdges[64];
uniform vec4 u_njinSpriteFx[4];
uniform vec4 u_njinMipFilter[2];
uniform vec4 u_njinEmission;
uniform vec4 u_njinParams;
uniform vec4 u_njinLight[4];
uniform vec4 u_njinMaterial;
uniform vec4 u_njinOutput;
uniform vec4 u_njinLocal[64];
SAMPLER2D(s_njinShadow,1);
#define NJIN_SHADOW_TEX(uv) texture2D(s_njinShadow,uv)
uniform vec4 u_njinShadowRows[32];
uniform vec4 u_njinShadowRect[28];
uniform vec4 u_njinShadowMeta[28];
uniform vec4 u_njinShadowInfo;
uniform vec4 u_njinShadowSplits;
uniform vec4 u_njinShadowForward;
SAMPLER2D(s_njinNormalMap,2);
SAMPLER2D(s_njinMetalRoughMap,3);
SAMPLER2D(s_njinOcclusionMap,4);
SAMPLER2D(s_njinEmissionMap,5);
#define NJIN_NORMAL_TEX(uv) (u_njinMipFilter[0].x>0.5?texture2DLod(s_njinNormalMap,uv,0.0):texture2D(s_njinNormalMap,uv))
#define NJIN_MR_TEX(uv) (u_njinMipFilter[0].y>0.5?texture2DLod(s_njinMetalRoughMap,uv,0.0):texture2D(s_njinMetalRoughMap,uv))
#define NJIN_AO_TEX(uv) (u_njinMipFilter[0].z>0.5?texture2DLod(s_njinOcclusionMap,uv,0.0):texture2D(s_njinOcclusionMap,uv))
#define NJIN_EMISSIVE_TEX(uv) (u_njinMipFilter[0].w>0.5?texture2DLod(s_njinEmissionMap,uv,0.0):texture2D(s_njinEmissionMap,uv))
uniform vec4 u_njinMapUv[10];
uniform vec4 u_njinMapFactors;
uniform vec4 u_njinMapEmission;
SAMPLER2D(s_njinEnvironmentBase,6);
SAMPLER2D(s_njinEnvironmentAtlas,7);
#define NJIN_ENV_BASE(uv) texture2D(s_njinEnvironmentBase,uv)
#define NJIN_ENV_ATLAS(uv) texture2D(s_njinEnvironmentAtlas,uv)
uniform vec4 u_njinEnvSH[9],u_njinEnvParams,u_njinEnvMode;
#define NJIN_ATAN2(y,x) atan2(y,x)
SAMPLER2D(s_njinPostAux,8);
#define NJIN_POST_COLOR(uv) texture2DLod(s_njinColor,uv,0.0)
#define NJIN_POST_AUX(uv) texture2DLod(s_njinPostAux,uv,0.0)
SAMPLER2D(s_njinPostSecond,9);
#define NJIN_POST_SECOND(uv) texture2DLod(s_njinPostSecond,uv,0.0)
uniform vec4 u_njinPost,u_njinPostExtra,u_njinPostConfig,u_njinPostSky,u_njinPostSun,u_njinMask;
uniform mat4 u_njinView;
uniform mat4 u_njinPostProjection,u_njinPostInverse;
uniform mat4 u_njinPreviousClip,u_njinPreviousView;
uniform vec4 u_njinTemporal;
SAMPLER2D(s_njinOpaqueDepth,10);
#define NJIN_OPAQUE_DEPTH(uv) texture2DLod(s_njinOpaqueDepth,uv,0.0)
uniform vec4 u_njinTransparency;
uniform mat4 u_njinTransparencyVP;
SAMPLER2D(s_njinVelocity,11);
#define NJIN_VELOCITY(uv) texture2DLod(s_njinVelocity,uv,0.0)
#include "njin_clusters.sh"
#include "njin_advanced_material.sh"
#include "njin_sky.sh"
#ifndef NJIN_WATER_PIPELINE
vec4 njinTerrainSample(sampler2D map,vec2 uv,float baseOnly) {
  float amount=u_njinWorldStyle[5].y;
  if(u_njinWorldStyle[0].x!=1.0||amount<=0.0)
    return baseOnly>0.5?texture2DLod(map,uv,0.0):texture2D(map,uv);
  float noise=njinWearNoise(uv*0.45)*8.0,cell=floor(noise);
  vec2 dx=dFdx(uv),dy=dFdy(uv);
  float spread=u_njinWorldStyle[5].z;
  float sa=1.0+spread*(fract(sin(dot(vec2(cell,5.3),vec2(127.1,311.7)))*43758.5453)*2.0-1.0);
  float sb=1.0+spread*(fract(sin(dot(vec2(cell+1.0,5.3),vec2(127.1,311.7)))*43758.5453)*2.0-1.0);
  vec2 ua=uv*sa+sin(vec2(3.0,7.0)*cell),ub=uv*sb+sin(vec2(3.0,7.0)*(cell+1.0));
  vec4 a=baseOnly>0.5?texture2DLod(map,ua,0.0):texture2DGrad(map,ua,dx*sa,dy*sa);
  vec4 b=baseOnly>0.5?texture2DLod(map,ub,0.0):texture2DGrad(map,ub,dx*sb,dy*sb);
  vec4 detiled=mix(a,b,smoothstep(0.2,0.8,fract(noise)));
  if(amount>=1.0) return detiled;
  vec4 base=baseOnly>0.5?texture2DLod(map,uv,0.0):texture2D(map,uv);
  return mix(base,detiled,amount);
}
// Các lớp đọc tại mặt gốc, cùng phép chiếu với height map trong shader đỉnh.
vec4 njinReliefAlbedo(sampler2D map,vec2 uv,float baseOnly,vec3 base,vec3 normal) {
  vec3 b=pow(abs(normalize(normal)),vec3(4.0,4.0,4.0));
  b/=max(dot(b,vec3(1.0,1.0,1.0)),0.001);
  float scale=u_njinMapUv[8].x;
  if(b.y>0.97)
    return mix(njinTerrainSample(map,uv,baseOnly),njinTerrainSample(map,base.xz*scale/3.7+vec2(0.31,0.31),baseOnly),0.3);
  vec2 x=base.zy*scale,z=base.xy*scale;
  return (baseOnly>0.5?texture2DLod(map,x,0.0):texture2D(map,x))*b.x+
         njinTerrainSample(map,uv,baseOnly)*b.y+
         (baseOnly>0.5?texture2DLod(map,z,0.0):texture2D(map,z))*b.z;
}
#undef NJIN_NORMAL_TEX
#undef NJIN_MR_TEX
#define NJIN_NORMAL_TEX(uv) njinTerrainSample(s_njinNormalMap,uv,u_njinMipFilter[0].x)
#define NJIN_MR_TEX(uv) njinTerrainSample(s_njinMetalRoughMap,uv,u_njinMipFilter[0].y)
#endif
#include "njin_surface.sh"
#include "njin_world_fx.sh"
#include "njin_particles.sh"
#ifdef NJIN_WATER_PIPELINE
#include "njin_water.sh"
#else
uniform vec4 u_njinTerrainImprint[3];

float njinSnowHash(vec3 cell) {
  return fract(sin(dot(cell,vec3(127.1,311.7,74.7)))*43758.5453);
}

float njinSnowNoise(vec3 position) {
  vec3 cell=floor(position),f=fract(position);
  f=f*f*(3.0-2.0*f);
  float low=mix(mix(njinSnowHash(cell),njinSnowHash(cell+vec3(1.0,0.0,0.0)),f.x),
                mix(njinSnowHash(cell+vec3(0.0,1.0,0.0)),njinSnowHash(cell+vec3(1.0,1.0,0.0)),f.x),f.y);
  cell.z+=1.0;
  float high=mix(mix(njinSnowHash(cell),njinSnowHash(cell+vec3(1.0,0.0,0.0)),f.x),
                 mix(njinSnowHash(cell+vec3(0.0,1.0,0.0)),njinSnowHash(cell+vec3(1.0,1.0,0.0)),f.x),f.y);
  return mix(low,high,f.z);
}

vec4 njinClay(vec3 local,vec3 position,vec3 normal,vec3 clay) {
  vec3 q=local*clay.y;
  float footprint=max(length(dFdx(q)),length(dFdy(q)));
  float amount=clay.x*(1.0-smoothstep(0.35,1.4,footprint));
  float height=njinSnowNoise(q);
  vec3 dx=dFdx(position),dy=dFdy(position),n=normalize(normal);
  vec3 tx=cross(dy,n),ty=cross(n,dx);
  float determinant=dot(dx,tx);
  vec3 slope=vec3(0.0,0.0,0.0);
  if(abs(determinant)>1e-12)
    slope=-(tx*dFdx(height)+ty*dFdy(height))*(amount*0.32*clay.z/(determinant*clay.y));
  return vec4(slope,1.0+amount*0.12*(njinSnowNoise(q*0.32)-0.5));
}

float njinMeshSnow(vec3 position,vec3 normal,vec2 uv,float alpha) {
  float amount=u_njinWorldStyle[4].w;
  float broad=njinSnowNoise(position*0.7),leaf=njinSnowNoise(position*2.3+5.0),fine=njinSnowNoise(position*6.1+13.0);
  float drift=smoothstep(0.6,0.8,leaf*0.6+broad*0.25+fine*0.15+(amount-0.5)*0.45)*0.88;
  float rim=0.0;
  if(u_njinParams.z>0.0&&u_njinAdvanced[1].x<=0.0) {
    vec2 x=dFdx(uv),y=dFdy(uv);
    float det=x.x*y.y-x.y*y.x;
    if(abs(det)>1e-12) {
      float dx=dFdx(position.y),dy=dFdy(position.y);
      vec2 uphill=vec2(dx*y.y-dy*x.y,dy*x.x-dx*y.x)/det;
      vec2 stepUv=uphill*(0.02/max(length(uphill),1e-6));
      float above=max(texture2DLod(s_njinColor,uv+stepUv,0.0).a,texture2DLod(s_njinColor,uv+2.0*stepUv,0.0).a*0.7);
      rim=smoothstep(0.15,0.75,alpha-above)*(1.0-smoothstep(0.012,0.025,max(length(x),length(y))));
    }
  }
  rim*=smoothstep(0.2,0.6,fine*0.6+broad*0.4+amount*0.4)*0.95;
  return smoothstep(-0.1,0.6,normal.y)*max(drift,rim)*min(amount*2.0,1.0);
}

vec2 njinImprintTexel(vec2 cell) {
  float size=u_njinTerrainImprint[0].w;
  vec4 mark=texture2DLod(s_njinHeightMap,(mod(cell,size)+vec2(0.5,0.5))/size,0.0);
  vec2 stamp=floor(mark.ba*255.0+0.5);
  float age=mod(u_njinWorldStyle[5].x-stamp.x-stamp.y*256.0+65536.0,65536.0)*0.01;
  vec4 relief=u_njinTerrainImprint[1];
  return mark.rg*clamp((relief.z-age)/max(relief.w,0.001),0.0,1.0);
}

vec2 njinImprintAt(vec2 xz) {
  vec2 pixel=xz/u_njinTerrainImprint[0].z-vec2(0.5,0.5);
  vec2 cell=floor(pixel),f=fract(pixel);
  vec2 row0=mix(njinImprintTexel(cell),njinImprintTexel(cell+vec2(1.0,0.0)),f.x);
  vec2 row1=mix(njinImprintTexel(cell+vec2(0.0,1.0)),njinImprintTexel(cell+vec2(1.0,1.0)),f.x);
  return mix(row0,row1,f.y);
}

vec4 njinTerrainFinish(inout vec3 albedo,vec3 position,vec3 normal,out float pressed) {
  vec4 window=u_njinTerrainImprint[0];
  vec2 cell=(position.xz-window.xy)/max(window.z,0.001);
  vec2 slope=vec2(0.0,0.0);
  pressed=0.0;
  if(window.w>0.0&&all(greaterThan(cell,vec2(1.5,1.5)))&&all(lessThan(cell,vec2(window.w-1.5,window.w-1.5)))) {
    vec2 x=vec2(window.z,0.0),z=x.yx;
    pressed=njinImprintAt(position.xz).x;
    vec2 rise=u_njinTerrainImprint[1].xy*vec2(-1.0,1.0);
    slope.x=dot(njinImprintAt(position.xz-x)-njinImprintAt(position.xz+x),rise);
    slope.y=dot(njinImprintAt(position.xz-z)-njinImprintAt(position.xz+z),rise);
    slope/=2.0*window.z;
    vec4 tint=u_njinTerrainImprint[2];
    albedo*=mix(njinSplat(1.0),tint.rgb,pressed*tint.a);
  }
  vec4 snow=u_njinWorldStyle[4];
  float coverage=0.0;
  if(snow.w>0.0) {
    vec2 p=position.xz;
    float drift=njinWearNoise(p*0.07)*0.55+njinWearNoise(p*0.5+7.0)*0.3+njinWearNoise(p*3.1+13.0)*0.15;
    float edge=1.0-snow.w*1.25;
    coverage=smoothstep(edge-0.06,edge+0.06,drift)*smoothstep(0.62,0.86,normalize(normal).y)*(1.0-pressed);
    albedo=mix(albedo,snow.rgb,coverage);
  }
  return vec4(slope.x,0.0,slope.y,coverage);
}
#endif
#ifndef NJIN_WATER_PIPELINE
// Rain streaks and snowflakes (legacy precip_fs). Sun colour and ambient in
// u_njinLight rows 1/2 as display values; flake shapes on sampler 0.
// Varyings come in as arguments (bgfx's HLSL scope): uv, flake cell, fade/glint/grow, drop centre.
vec4 njinPrecip(vec2 uv,vec2 cell,vec4 drop,vec3 center) {
  float fade=drop.x*(1.0-njinCovered(center)),glint=drop.y,grow=drop.z;
  vec3 ambient=u_njinLight[2].rgb,lightColor=u_njinLight[1].rgb,c;
  float a;
  if(u_njinWorldStyle[0].y>0.5) {
    vec2 grid=u_njinWorldStyle[4].xy;
    if(grid.x>0.0) {
      vec2 q=(uv-0.5)*grow+0.5;
      float inside=step(0.0,q.x)*step(0.0,q.y)*step(q.x,1.0)*step(q.y,1.0);
      a=texture2D(s_njinColor,(cell+vec2(q.x,1.0-q.y))/grid).a*inside;
    } else a=1.0-smoothstep(0.55,1.0,length(uv-0.5)*2.0);
    c=ambient*1.1+lightColor*0.45;
    a*=0.9;
    vec2 q=abs(uv-0.5);
    float star=exp(-q.x*60.0-q.y*6.0)+exp(-q.y*60.0-q.x*6.0)+exp(-length(q)*12.0);
    a=max(a,min(star,1.0)*min(glint,1.0));
  } else {
    a=(1.0-abs(uv.x-0.5)*2.0)*smoothstep(0.0,0.35,uv.y)*0.45;
    c=vec3(0.85,0.9,1.0)*(ambient*1.8+lightColor*0.3+njinSplat(0.08));
  }
  c=min(c,njinSplat(1.0));
  bool hdr=u_njinOutput.y>0.5;
  if(hdr) c=njinLinear(c)*u_njinOutput.x;
  c+=(lightColor+njinSplat(0.3))*glint*(hdr?4.0*u_njinOutput.x:1.0);
  if(!hdr) c=min(c,njinSplat(1.0));
  return vec4(c,a*fade*u_njinWorldStyle[0].z);
}
#endif
void main()
{
#ifndef NJIN_WATER_PIPELINE
  if(u_njinPost.x>35.5) {
    vec4 map=NJIN_POST_COLOR(v_texcoord0),kind=u_njinLegacyPost[0];
    float alpha=NJIN_POST_AUX(v_texcoord1).a*kind.w;
    if(kind.x>2.5) {gl_FragColor=vec4(1.0,1.0,1.0,map.a);return;}
    vec3 color=map.rgb;
    if(kind.x<0.5) {
      if(kind.y<0.5) color=vec3(0.5,0.5,1.0);
    } else if(kind.x<1.5) {if(kind.y<0.5) color=vec3(0.0,0.8,1.0);}
    else color=kind.y>0.5?njinLinear(color)*kind.z:vec3(0.0,0.0,0.0);
    gl_FragColor=vec4(color,alpha);return;
  }
  if(u_njinPost.x>34.5) {
    vec4 shape=u_njinLegacyPost[0],uvRect=u_njinLegacyPost[1];
    vec2 t=(v_texcoord0-uvRect.xy)/(uvRect.zw-uvRect.xy),p=(t-0.5)*shape.xy*2.0;
    vec2 q=abs(p)-shape.xy+shape.z;
    float distance=length(max(q,vec2(0.0,0.0)))+min(max(q.x,q.y),0.0)-shape.z;
    float aa=max(length(fwidth(p))*0.7071,0.0001);
    float alpha=clamp(0.5-distance/aa,0.0,1.0);
    gl_FragColor=vec4(NJIN_POST_COLOR(v_texcoord0).rgb*u_njinTint.rgb,u_njinTint.a*alpha);return;
  }
  if(u_njinPost.x>0.5) {gl_FragColor=vec4(njinPostColor(v_texcoord0),1.0);return;}
  if(u_njinWorldStyle[0].x>4.5&&u_njinWorldStyle[0].x<5.5) {gl_FragColor=njinPrecip(v_texcoord0,v_texcoord1,v_color0,v_tangent.xyz);return;}
#endif
  vec2 uv0=v_texcoord0,uv1=v_texcoord1;
  vec4 surfaceTangent=v_tangent;
  if(u_njinAdvanced[1].x>0.0) {
    vec3 t,b;njinWorldFrame(normalize(v_normal),v_world,uv0,t,b);surfaceTangent=vec4(0.0,0.0,0.0,0.0);
  }
  uv0=njinParallax(uv0,normalize(v_normal),v_world,u_njinWorldFx[6].xyz);
  vec2 baseUv=njinMapUv(4,uv0,uv1);
#if defined(NJIN_VIRTUAL_TEXTURE)
  vec4 texel=njinVirtualColor(baseUv);
#elif defined(NJIN_WATER_PIPELINE)
  vec4 texel=u_njinMipFilter[1].x>0.5?texture2DLod(s_njinColor,baseUv,0.0):texture2D(s_njinColor,baseUv);
#else
  vec4 texel=njinTerrainSample(s_njinColor,baseUv,u_njinMipFilter[1].x);
  if(u_njinWorldStyle[0].x==1.0&&v_tangent.w>1.5)
    texel=njinReliefAlbedo(s_njinColor,baseUv,u_njinMipFilter[1].x,vec3(v_texcoord0.x,v_texcoord1.x,v_texcoord0.y),v_normal);
#endif
  vec3 topDetail=texel.rgb;
  vec2 offsetA=vec2(0.0,0.0),offsetB=vec2(0.0,0.0);float untileBlend=0.0;
  if(u_njinAdvanced[1].x>0.0
#if defined(NJIN_VIRTUAL_TEXTURE)
     &&false
#endif
  ) {
    njinUntile(uv0,offsetA,offsetB,untileBlend);
    vec2 dx=dFdx(baseUv),dy=dFdy(baseUv);
    vec3 a=u_njinMipFilter[1].x>0.5?texture2DLod(s_njinColor,baseUv+offsetA,0.0).rgb:texture2DGrad(s_njinColor,baseUv+offsetA,dx,dy).rgb;
    vec3 b=u_njinMipFilter[1].x>0.5?texture2DLod(s_njinColor,baseUv+offsetB,0.0).rgb:texture2DGrad(s_njinColor,baseUv+offsetB,dx,dy).rgb;
    topDetail=mix(a,b,untileBlend);
    texel.rgb=topDetail*mix(0.86,1.1,njinWearNoise(uv0*0.21+17.0));
  }
  vec4 vertexColor=v_color0*v_instance_color;
  if(u_njinWorldStyle[0].x>0.5 && u_njinWorldStyle[0].x<1.5) {
    float layer=u_njinWorldStyle[0].y;
    float weight=layer<0.5?v_color0.r:layer<1.5?v_color0.g:layer<2.5?v_color0.b:v_color0.a;
    vertexColor=vec4(1.0,1.0,1.0,weight);
  }
  vec4 color=texel*u_njinTint*vertexColor;
  if(u_njinWorldStyle[0].x>0.5&&u_njinWorldStyle[0].x<1.5&&u_njinWorldStyle[1].w>0.0) {
    vec4 field=u_njinWorldStyle[1],fade=u_njinWorldStyle[2],slope=u_njinWorldStyle[3];
    float weight=fade.z<0.0?1.0:fade.z<0.5?v_color0.r:fade.z<1.5?v_color0.g:fade.z<2.5?v_color0.b:v_color0.a;
    float up=clamp(normalize(v_normal).y,0.0,1.0);
    float incline=slope.y-slope.x>0.000001?smoothstep(slope.x,slope.y,up):step(slope.x,up);
    float height=fade.w < -100000000.0?1.0:smoothstep(fade.w,fade.w+0.4,v_world.y);
    float amount=smoothstep(fade.x,fade.y,length(v_world.xz-slope.zw))*weight*incline*height*field.w;
    float foot=max(length(dFdx(v_world.xz)),length(dFdy(v_world.xz)));
    float fine=1.0-smoothstep(0.04,0.1,foot);
    float tuft=njinWearNoise(v_world.xz*2.5)*0.6+mix(0.5,njinWearNoise(v_world.xz*8.0),fine)*0.4;
    color.rgb=mix(color.rgb,mix(color.rgb,field.rgb,0.6)*mix(0.7,1.0,tuft),amount);
  }
  if(u_njinWorldStyle[0].x>3.5&&u_njinWorldStyle[0].x<4.5) {
    float mode=u_njinWorldStyle[0].y;vec2 q=v_texcoord1*2.0-1.0;
    if(mode>2.5) {float fall=max(1.0-dot(q,q),0.0);color.a*=fall*fall;}
    else if(mode<0.5) {float d=length(q),edge=max(fwidth(d),0.0001);color.a*=1.0-smoothstep(1.0-edge,1.0,d);}
    gl_FragColor=color;return;
  }
  float bare=njinBare(uv0,v_world);
  vec3 underColor=vec3(0.0,0.0,0.0);
  if(bare>0.0) {
    vec2 uv=njinAdvancedUv(2,uv0,uv1),dx=dFdx(uv),dy=dFdy(uv);
    underColor=mix(texture2DGrad(s_njinUnderMap,uv+offsetA,dx,dy).rgb,
                   texture2DGrad(s_njinUnderMap,uv+offsetB,dx,dy).rgb,untileBlend);
    vec3 weights=vec3(0.3,0.59,0.11);
    float blurred=dot(texture2DLod(s_njinColor,baseUv+offsetA,7.0).rgb,weights);
    float marks=dot(topDetail,weights)/max(blurred,0.05);
    underColor*=mix(1.0,clamp(marks,0.5,1.4),0.55);
    color.rgb=mix(color.rgb,underColor,bare);
  }
  if(u_njinWorldStyle[0].x>0.5 && u_njinWorldStyle[0].x<1.5) color.a=vertexColor.a;
  vec3 surfaceNormal=v_normal;
#ifdef NJIN_WATER_PIPELINE
  float waterDepth=v_texcoord1.x,waterEdge=1.0;vec2 waterUnderUv=vec2(0.0,0.0);
#endif
#ifdef NJIN_WATER_PIPELINE
  if(u_njinWorldStyle[0].x>2.5&&u_njinWorldStyle[0].x<3.5) {
    float time=u_njinWorldStyle[1].w;
    float frozen=njinWaterFrozen(uv0,v_texcoord1.x);
    float spacing=max(length(fwidth(uv0)),0.01);
    surfaceNormal=njinWaterRipples(normalize(v_normal),uv0,time,spacing,u_njinWorldStyle[0].w);
    float churn=njinWaterDetail(uv0,time,spacing,surfaceNormal);
    surfaceNormal=normalize(mix(surfaceNormal,vec3(0.0,1.0,0.0),frozen));
    if(u_njinWater[0].z>0.0&&u_njinTransparency.x>0.5) {
      vec3 view=normalize(u_njinWorldFx[6].xyz-v_world);
      if(dot(surfaceNormal,view)<0.0) surfaceNormal=normalize(surfaceNormal+view*(0.01-dot(surfaceNormal,view)));
      waterUnderUv=njinWaterUnder(v_world,surfaceNormal,waterDepth);
      color=mix(u_njinWater[14],u_njinWater[15],1.0-exp(-waterDepth/u_njinWater[13].w));
    }
    float shore=1.0-smoothstep(0.0,max(u_njinWorldStyle[0].y,0.0001),waterDepth);
    float crest=smoothstep(u_njinWorldStyle[2].y*0.4,max(u_njinWorldStyle[2].y,0.0001),v_texcoord1.y)*u_njinWorldStyle[0].z;
    float noise=njinParticleNoise(vec3(v_world.x*5.0,time*0.7,v_world.z*5.0));
    float foam=clamp((shore+crest)*smoothstep(0.25,0.7,noise),0.0,1.0);
    float fizz=njinWaterNoise((uv0-u_njinWater[2].xy)*(120.0/max(u_njinWater[2].w,0.01))+time*4.0);
    foam=max(foam,churn*smoothstep(0.45,0.75,fizz)*0.8)*(1.0-frozen);
    color.rgb=mix(color.rgb,u_njinWorldStyle[1].rgb,foam);
    color.rgb=mix(color.rgb,njinWaterIce(uv0),frozen);
    float waterAlpha=color.a*(u_njinWorldStyle[2].x>0.0?smoothstep(0.0,u_njinWorldStyle[2].x,waterDepth):1.0);
    if(u_njinWater[0].z>0.0) waterAlpha=color.a*(1.0-exp(-waterDepth/max(u_njinWorldStyle[2].x,0.01)));
    color.a=mix(waterAlpha,1.0,foam*u_njinWorldStyle[4].x);
    color.a=mix(color.a,1.0,frozen);
    if(u_njinWorldStyle[2].w>0.5) {
      vec2 q=(v_world.xz-u_njinWorldStyle[3].xy)/(u_njinWorldStyle[3].zw*0.5);
      float edge=1.0-dot(q,q);if(edge<=0.0) discard;
      waterEdge=smoothstep(0.0,0.05,edge);color.a*=waterEdge;
    }
  }
#endif
  if(u_njinParticleFx.x>0.5) {
    color=njinParticle(baseUv,v_world,color);
    if(u_njinOutput.y>0.5) color.rgb=njinLinear(color.rgb);
    gl_FragColor=color;return;
  }
  if(color.a<u_njinParams.z) discard;
  float worldCut=njinWorldCut(v_world);
  if(u_njinShadowInfo.w>=0.0) njinWorldMask(v_world,gl_FragCoord.xy);
  if(u_njinShadowInfo.w<0.0) { gl_FragColor=vec4(njinPackDepth(gl_FragCoord.z),1.0); return; }
  vec4 terrainFinish=vec4(0.0,0.0,0.0,0.0);
  vec3 glitter=vec3(0.0,0.0,0.0);
  float meshSnow=0.0;
  float clayTint=1.0;
  bool terrain=u_njinWorldStyle[0].x>0.5&&u_njinWorldStyle[0].x<1.5;
#ifndef NJIN_WATER_PIPELINE
  if(terrain) {
    float pressed;
    terrainFinish=njinTerrainFinish(color.rgb,v_world,surfaceNormal,pressed);
    glitter.x=max(u_njinWorldStyle[5].w,terrainFinish.w)*(1.0-pressed);
  }
  else if(u_njinWorldStyle[0].x<0.5) {
    if(u_njinWorldStyle[1].x>0.0) {
      vec4 clay=njinClay(vec3(v_texcoord0,v_texcoord1.x),v_world,surfaceNormal,u_njinWorldStyle[1].xyz);
      terrainFinish.xyz=clay.xyz;
      clayTint=clay.w;
      color.rgb*=clayTint;
    }
    glitter=u_njinWorldStyle[5].xyz;
    if(u_njinWorldStyle[4].w>0.0) {
      vec3 mapped=njinMappedNormal(surfaceNormal,v_world,surfaceTangent,uv0,uv1);
      meshSnow=njinMeshSnow(v_world,mapped,baseUv,texel.a);
      color.rgb=mix(color.rgb,u_njinWorldStyle[4].rgb,meshSnow);
      glitter.x*=meshSnow;
    }
  }
#endif
  if(u_njinMask.x>0.5) {gl_FragColor=njinNormalMask(surfaceNormal,v_world,surfaceTangent,uv0,uv1,terrainFinish);return;}
  color.a=mix(color.a,1.0,u_njinParams.w);
  vec3 linearBase=njinLinear(texel.rgb)*njinLinear(u_njinTint.rgb)*njinLinear(vertexColor.rgb);
  if(terrain&&(u_njinWorldStyle[1].w>0.0||u_njinWorldStyle[4].w>0.0)) linearBase=njinLinear(color.rgb);
#ifndef NJIN_WATER_PIPELINE
  if(terrain&&u_njinTerrainImprint[0].w>0.0) linearBase=njinLinear(color.rgb);
#endif
  if(bare>0.0) linearBase=mix(linearBase,njinLinear(underColor),bare);
  if(meshSnow>0.0) linearBase=mix(linearBase,njinLinear(u_njinWorldStyle[4].rgb),meshSnow);
  linearBase*=clayTint;
#ifdef NJIN_WATER_PIPELINE
  if(u_njinWorldStyle[0].x>2.5&&u_njinWorldStyle[0].x<3.5) linearBase=njinLinear(color.rgb);
#endif
  color.rgb=njinSurface(color.rgb,linearBase,surfaceNormal,v_world,uv0,uv1,v_lightmap_uv,surfaceTangent,terrainFinish,glitter);
#ifdef NJIN_WATER_PIPELINE
  // The sky in the water as legacy draws it: reflected ray kept above the horizon,
  // Schlick Fresnel share; the water is at least that opaque.
  if(u_njinWorldStyle[0].x>2.5&&u_njinWorldStyle[0].x<3.5&&u_njinSky[10].w>0.5) {
    vec3 view=normalize(u_njinWorldFx[6].xyz-v_world),n=normalize(surfaceNormal);
    vec3 r=reflect(-view,n);r.y=abs(r.y);
    vec3 refl=njinSkyColor(r,u_njinSky[10].xyz);
    if(u_njinOutput.y>0.5) refl=njinLinear(refl)*u_njinOutput.x;
    float fres=0.02+0.98*pow(1.0-max(dot(n,view),0.0),5.0);
    color.rgb=mix(color.rgb,refl,fres);color.a=max(color.a,fres*waterEdge);
  }
  if(u_njinWorldStyle[0].x>2.5&&u_njinWorldStyle[0].x<3.5)
    color=njinWaterOptics(color,v_world,surfaceNormal,uv0,waterDepth,v_texcoord1.x,waterUnderUv,waterEdge);
#endif
  if(u_njinTransmit[0].x>0.0&&u_njinTransparency.x>0.5) {
    vec3 n=njinMappedNormal(surfaceNormal,v_world,surfaceTangent,uv0,uv1);
    vec3 eye=u_njinWorldFx[6].xyz-v_world;vec3 v=eye/max(length(eye),0.000001);
    if(dot(n,v)<0.0) n=-n;
    vec4 volume=u_njinTransmit[0];float ior=max(volume.y,1.0);
    if(u_njinAdvanced[2].y>0.5) {
      vec2 uv=njinAdvancedUv(4,uv0,uv1);
      volume.x*=u_njinAdvancedUv[9].w>0.5?texture2DLod(s_njinPostSecond,uv,0.0).r:texture2D(s_njinPostSecond,uv).r;
    }
    if(u_njinAdvanced[2].z>0.5) {
      vec2 uv=njinAdvancedUv(5,uv0,uv1);
      volume.z*=u_njinAdvancedUv[11].w>0.5?texture2DLod(s_njinHeightMap,uv,0.0).g:texture2D(s_njinHeightMap,uv).g;
    }
    vec3 ray=refract(-v,n,1.0/ior);
    vec4 clip=mul(u_njinTransparencyVP,vec4(v_world+ray*volume.z,1.0));
    vec4 original=mul(u_njinTransparencyVP,vec4(v_world,1.0));
    vec2 originalUv=vec2(original.x,-original.y)/max(original.w,0.000001)*0.5+0.5;
    vec2 refracted=clip.w>0.0?vec2(clip.x,-clip.y)/clip.w*0.5+0.5:originalUv;
    refracted=clamp(refracted,vec2(0.001,0.001),vec2(0.999,0.999));
    float surfaceDepth=original.z/max(original.w,0.000001)*0.5+0.5;
    if(NJIN_OPAQUE_DEPTH(refracted).r<surfaceDepth) refracted=originalUv;
    float rough=u_njinMaterial.y;
    if(u_njinMapUv[3].w>0.5) rough*=NJIN_MR_TEX(njinMapUv(1,uv0,uv1)).g;
    float r=rough*rough*0.04;
    vec3 behind=NJIN_POST_AUX(refracted).rgb*0.4;
    behind+=(NJIN_POST_AUX(clamp(refracted+vec2(r,0.0),0.001,0.999)).rgb+NJIN_POST_AUX(clamp(refracted-vec2(r,0.0),0.001,0.999)).rgb+
      NJIN_POST_AUX(clamp(refracted+vec2(0.0,r),0.001,0.999)).rgb+NJIN_POST_AUX(clamp(refracted-vec2(0.0,r),0.001,0.999)).rgb)*0.15;
    vec3 tint=linearBase;
    if(volume.w>0.0&&volume.z>0.0) tint*=exp(log(max(u_njinTransmit[1].rgb,vec3(0.0001,0.0001,0.0001)))*volume.z/volume.w);
    float f0=(ior-1.0)/(ior+1.0);f0*=f0;
    float share=volume.x*(1.0-(f0+(1.0-f0)*pow(1.0-max(dot(n,v),0.0),5.0)));
    color.rgb=mix(color.rgb,behind*tint,share);
  }
  vec3 fxFlash=u_njinWorldFx[0].rgb,fxEdge=u_njinWorldFx[1].rgb;
  if(u_njinOutput.y>0.5) {fxFlash=njinLinear(fxFlash);fxEdge=njinLinear(fxEdge);}
  color.rgb=mix(color.rgb,fxFlash,u_njinWorldFx[0].a);
  if(u_njinWorldFx[2].x>0.0 && worldCut<u_njinWorldFx[2].x+u_njinWorldFx[2].w)
    color.rgb=mix(color.rgb,fxEdge,u_njinWorldFx[1].a);
  float fog=1.0-exp(-pow(u_njinSurfaceFx[0].w*length(u_njinWorldFx[6].xyz-v_world),2.0));
  vec3 fogColor=u_njinOutput.y>0.5?njinLinear(u_njinSurfaceFx[0].rgb):u_njinSurfaceFx[0].rgb;
  color.rgb=mix(color.rgb,fogColor,fog);
  if(u_njinWorldStyle[0].x>0.5 && u_njinWorldStyle[0].x<1.5 && u_njinWorldStyle[0].z>0.5) {color.rgb*=color.a;color.a=1.0;}
  if(u_njinTransparency.x>0.5) {gl_FragColor=njinOitColor(color,v_world);return;}
  // Same source-texel patch hash and threshold as the legacy sprite dissolve.
  color.rgb=mix(color.rgb,u_njinSpriteFx[0].rgb,u_njinSpriteFx[0].a);
  if(u_njinSpriteFx[3].x>0.5 && u_njinSpriteFx[2].x>0.0) {
    if(u_njinSpriteFx[2].x>=1.0) {color.a=0.0;gl_FragColor=color;return;}
    vec2 cell=floor(baseUv*u_njinSpriteFx[3].yz/max(u_njinSpriteFx[2].y,1.0));
    vec2 p=cell+u_njinSpriteFx[2].z;
    vec3 p3=fract(vec3(p.x,p.y,p.x)*0.1031);
    p3+=dot(p3,p3.yzx+33.33);
    float noise=fract((p3.x+p3.y)*p3.z);
    float width=u_njinSpriteFx[2].w;
    float distance=noise-(u_njinSpriteFx[2].x*(1.0+width)-width);
    color.rgb=mix(color.rgb,u_njinSpriteFx[1].rgb,(1.0-step(width,distance))*u_njinSpriteFx[1].a);
    color.a*=step(0.0001,distance);
  }
  gl_FragColor=color;
}
