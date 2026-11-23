// Shared Njin legacy material algorithms, adapted to renderer-owned maps.
SAMPLER2D(s_njinHeightMap,12);
SAMPLER2D(s_njinLightmap,13);
SAMPLER2D(s_njinUnderMap,14);
SAMPLER2D(s_njinUnderNormal,15);
uniform vec4 u_njinAdvanced[3]; // flags; factors; linear lightmap
uniform vec4 u_njinAdvancedUv[12];
uniform vec4 u_njinTransmit[2];
// glTF clearcoat maps on samplers 8/9: sampler (1, 2; 0 unused) for coat R,
// roughness G and coat normal, then normal scale. Two UV rows per sampler.
uniform vec4 u_njinCoat;
uniform vec4 u_njinCoatUv[4];
// Ray traced sun shadow mask on sampler 10: view-projection rows; enabled, texel size.
uniform vec4 u_njinRtShadow[6];
vec2 njinAdvancedUv(int map,vec2 uv0,vec2 uv1) {
  vec4 a=u_njinAdvancedUv[map*2],b=u_njinAdvancedUv[map*2+1];
  vec2 uv=b.z>0.5?uv1:uv0;
  return vec2(dot(a.xy,uv),dot(a.zw,uv))+b.xy;
}
vec2 njinCoatUv(float slot,vec2 uv0,vec2 uv1) {
  vec4 a=slot<1.5?u_njinCoatUv[0]:u_njinCoatUv[2],b=slot<1.5?u_njinCoatUv[1]:u_njinCoatUv[3];
  vec2 uv=b.z>0.5?uv1:uv0;
  return vec2(dot(a.xy,uv),dot(a.zw,uv))+b.xy;
}
vec4 njinCoatSample(float slot,vec2 uv0,vec2 uv1) {
  vec2 uv=njinCoatUv(slot,uv0,uv1);
  if(slot<1.5) return u_njinCoatUv[1].w>0.5?texture2DLod(s_njinPostAux,uv,0.0):texture2D(s_njinPostAux,uv);
  return u_njinCoatUv[3].w>0.5?texture2DLod(s_njinPostSecond,uv,0.0):texture2D(s_njinPostSecond,uv);
}
float njinWearNoise(vec2 p) {
  vec2 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
  vec4 v=fract(sin(vec4(dot(i,vec2(127.1,311.7)),dot(i+vec2(1.0,0.0),vec2(127.1,311.7)),
    dot(i+vec2(0.0,1.0),vec2(127.1,311.7)),dot(i+vec2(1.0,1.0),vec2(127.1,311.7))))*43758.5453);
  return mix(mix(v.x,v.y,f.x),mix(v.z,v.w,f.x),f.y);
}
void njinUntile(vec2 uv,out vec2 first,out vec2 second,out float blend) {
  float noise=njinWearNoise(uv*0.45)*8.0,cell=floor(noise);
  first=sin(vec2(3.0,7.0)*cell);
  second=sin(vec2(3.0,7.0)*(cell+1.0));
  blend=smoothstep(0.2,0.8,fract(noise));
}
void njinWorldFrame(vec3 n,vec3 position,out vec2 uv,out vec3 t,out vec3 b) {
  b=abs(n.y)>0.7?vec3(0.0,0.0,n.y>0.0?-1.0:1.0):vec3(0.0,1.0,0.0);
  t=normalize(cross(b,n));b=cross(n,t);
  uv=vec2(dot(position,t),-dot(position,b))/u_njinAdvanced[1].x;
}
float njinBare(vec2 uv,vec3 position) {
  if(u_njinAdvanced[0].z<0.5||u_njinAdvanced[1].y<=0.0||u_njinAdvanced[1].x<=0.0) return 0.0;
  float wear=njinWearNoise(vec2(uv.x*0.9,uv.y*0.3)+31.0)*0.6+njinWearNoise(uv*1.7+7.0)*0.4;
  wear-=0.25*exp(-max(position.y/u_njinAdvanced[1].x,0.0)*1.2);
  float threshold=1.0-u_njinAdvanced[1].y;
  // Preserve exact endpoints as well as the legacy soft transition.
  return u_njinAdvanced[1].y>=1.0?1.0:smoothstep(threshold-0.16,threshold+0.16,wear);
}
vec3 njinMixUnderNormal(vec3 mapped,vec3 normal,vec3 position,vec2 uv) {
  if(u_njinAdvanced[0].w<0.5||u_njinAdvanced[1].x<=0.0) return mapped;
  vec3 t,b;vec2 worldUv;njinWorldFrame(normal,position,worldUv,t,b);
  vec2 a,bOffset;float blend;njinUntile(uv,a,bOffset,blend);
  vec2 mappedUv=njinAdvancedUv(3,uv,uv),dx=dFdx(mappedUv),dy=dFdy(mappedUv);
  vec3 sampleNormal=mix(texture2DGrad(s_njinUnderNormal,mappedUv+a,dx,dy).xyz,
                        texture2DGrad(s_njinUnderNormal,mappedUv+bOffset,dx,dy).xyz,blend)*2.0-1.0;
  vec3 under=normalize(t*sampleNormal.x+b*sampleNormal.y+normal*sampleNormal.z);
  return normalize(mix(mapped,under,njinBare(uv,position)));
}
vec2 njinParallax(vec2 uv,vec3 normal,vec3 position,vec3 camera) {
  if(u_njinAdvanced[0].x<0.5||u_njinAdvanced[1].z<=0.0||u_njinAdvanced[1].x>0.0) return uv;
  vec3 dp1=dFdx(position),dp2=dFdy(position);vec2 du1=dFdx(uv),du2=dFdy(uv);
  vec3 p2=cross(dp2,normal),p1=cross(normal,dp1);
  vec3 t=p2*du1.x+p1*du2.x,b=p2*du1.y+p1*du2.y;
  float inv=inversesqrt(max(max(dot(t,t),dot(b,b)),0.000000000001));t*=inv;b*=inv;
  vec3 view=normalize(camera-position),vt=vec3(dot(view,t),dot(view,b),dot(view,normal));
  float steps=mix(32.0,8.0,clamp(vt.z,0.0,1.0)),layer=1.0/steps;
  vec2 shift=vt.xy/max(vt.z,0.1)*u_njinAdvanced[1].z/steps;
  vec2 dx=dFdx(uv),dy=dFdy(uv),cur=uv;
  float depth=0.0,h=1.0-texture2DGrad(s_njinHeightMap,njinAdvancedUv(0,cur,cur),dx,dy).r;
  float prevH=h,prevDepth=0.0;
  for(int i=0;i<32;++i) {
    if(depth>=h) break;
    prevH=h;prevDepth=depth;cur-=shift;depth+=layer;
    h=1.0-texture2DGrad(s_njinHeightMap,njinAdvancedUv(0,cur,cur),dx,dy).r;
  }
  float after=h-depth,before=prevH-prevDepth;
  return mix(cur,cur+shift,clamp(after/min(after-before,-0.00001),0.0,1.0));
}
