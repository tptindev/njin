// Hi-Z bảo thủ theo thiết kế fl; binding/indirect dùng API bgfx.
#include <bgfx_compute.sh>
#if NJIN_OCCLUSION_STAGE == 0
SAMPLER2D(s_njinCullDepth,0);
IMAGE2D_WO(s_njinCullOut,r32f,1);
uniform vec4 u_njinCullSize;
NUM_THREADS(8,8,1)
void main() {
  ivec2 p=ivec2(gl_GlobalInvocationID.xy);
  if(any(greaterThanEqual(p,ivec2(u_njinCullSize.zw)))) return;
  float depth=1.0;
  if(all(lessThan(p,ivec2(u_njinCullSize.xy)))) {
    vec3 rgb=texelFetch(s_njinCullDepth,p,0).rgb;
    depth=min(dot(rgb,vec3(1.0,1.0/255.0,1.0/65025.0)),1.0);
  }
  imageStore(s_njinCullOut,p,vec4(depth,0.0,0.0,0.0));
}
#elif NJIN_OCCLUSION_STAGE == 1
IMAGE2D_RO(s_njinCullIn,r32f,0);
IMAGE2D_WO(s_njinCullOut,r32f,1);
uniform vec4 u_njinCullSize;
NUM_THREADS(8,8,1)
void main() {
  ivec2 p=ivec2(gl_GlobalInvocationID.xy);
  if(any(greaterThanEqual(p,ivec2(u_njinCullSize.zw)))) return;
  ivec2 s=p*2,hi=ivec2(u_njinCullSize.xy)-ivec2(1,1);
  float d=max(max(imageLoad(s_njinCullIn,min(s,hi)).r,imageLoad(s_njinCullIn,min(s+ivec2(1,0),hi)).r),
              max(imageLoad(s_njinCullIn,min(s+ivec2(0,1),hi)).r,imageLoad(s_njinCullIn,min(s+ivec2(1,1),hi)).r));
  imageStore(s_njinCullOut,p,vec4(d,0.0,0.0,0.0));
}
#elif NJIN_OCCLUSION_STAGE == 2
BUFFER_RO(s_njinCullInstances,vec4,0);
BUFFER_WO(s_njinCullVisible,vec4,1);
BUFFER_RW(s_njinCullCount,uint,2);
BUFFER_RW(s_njinCullIndirect,uvec4,3);
SAMPLER2D(s_njinCullPyramid,4);
uniform mat4 u_njinCullVP;
uniform vec4 u_njinCullBounds;
uniform vec4 u_njinCullInfo;
uniform vec4 u_njinCullSize;
uniform vec4 u_njinCullFlags;
uniform vec4 u_njinCullCamera;
uniform vec4 u_njinCullLod;
uniform vec4 u_njinCullCameraPlanes[6];
bool inCamera(vec3 center,float radius) {
  for(int plane=0;plane<6;++plane) if(dot(u_njinCullCameraPlanes[plane].xyz,center)+u_njinCullCameraPlanes[plane].w < -radius) return false;
  return true;
}
bool inFrustum(vec3 center,float radius) {
  // Test the enclosing box against the six homogeneous clip planes. This also
  // works for orthographic views and boxes crossing the eye/near plane.
  vec3 a=vec3(-1.0,-1.0,-1.0),b=a;
  for(int c=0;c<8;++c) {
    vec3 sign=vec3((c&1)!=0?1.0:-1.0,(c&2)!=0?1.0:-1.0,(c&4)!=0?1.0:-1.0);
    vec4 clip=mul(u_njinCullVP,vec4(center+sign*radius,1.0));
    a=max(a,step(vec3(0.0,0.0,0.0),clip.xyz+vec3(clip.w,clip.w,clip.w)));
    b=max(b,step(vec3(0.0,0.0,0.0),vec3(clip.w,clip.w,clip.w)-clip.xyz));
  }
  return all(greaterThanEqual(a,vec3(1.0,1.0,1.0)))&&all(greaterThanEqual(b,vec3(1.0,1.0,1.0)));
}
bool visible(vec3 center,float radius) {
  vec2 lo=vec2(1.0,1.0),hi=vec2(0.0,0.0);float nearest=1.0;
  for(int c=0;c<8;++c) {
    vec3 sign=vec3((c&1)!=0?1.0:-1.0,(c&2)!=0?1.0:-1.0,(c&4)!=0?1.0:-1.0);
    vec4 clip=mul(u_njinCullVP,vec4(center+sign*radius,1.0));
    // Cắt qua near plane: giữ lại, kể cả trường hợp camera orthographic.
    if(clip.w<=0.001||clip.z<=-clip.w) return true;
    vec3 ndc=clip.xyz/clip.w;vec2 uv=ndc.xy*vec2(0.5,-0.5)+vec2(0.5,0.5);
    lo=min(lo,uv);hi=max(hi,uv);nearest=min(nearest,ndc.z*0.5+0.5);
  }
  if(hi.x<0.0||hi.y<0.0||lo.x>1.0||lo.y>1.0) return true;
  lo=clamp(lo,0.0,1.0)*u_njinCullSize.zw/u_njinCullSize.xy;
  hi=clamp(hi,0.0,1.0)*u_njinCullSize.zw/u_njinCullSize.xy;
  vec2 pixels=(hi-lo)*u_njinCullSize.xy;
  float lod=clamp(ceil(log2(max(max(pixels.x,pixels.y),1.0))),0.0,u_njinCullInfo.w-1.0);
  ivec2 size=ivec2(max(u_njinCullSize.xy/exp2(lod),vec2(1.0,1.0)));
  ivec2 a=clamp(ivec2(lo*vec2(size)),ivec2(0,0),size-ivec2(1,1));
  ivec2 b=clamp(ivec2(hi*vec2(size)),ivec2(0,0),size-ivec2(1,1));
  float d=max(max(texelFetch(s_njinCullPyramid,a,int(lod)).r,texelFetch(s_njinCullPyramid,b,int(lod)).r),
              max(texelFetch(s_njinCullPyramid,ivec2(a.x,b.y),int(lod)).r,texelFetch(s_njinCullPyramid,ivec2(b.x,a.y),int(lod)).r));
  return nearest<=d+0.00002;
}
bool accepted(uint i) {
  vec4 x=s_njinCullInstances[i*6u],y=s_njinCullInstances[i*6u+1u],z=s_njinCullInstances[i*6u+2u],p=s_njinCullInstances[i*6u+3u];
  vec3 center=p.xyz+x.xyz*u_njinCullBounds.x+y.xyz*u_njinCullBounds.y+z.xyz*u_njinCullBounds.z;
  // Frobenius cũng bao được shear, không chỉ rotation + scale thông thường.
  float radius=u_njinCullBounds.w*sqrt(dot(x.xyz,x.xyz)+dot(y.xyz,y.xyz)+dot(z.xyz,z.xyz));
  float r=u_njinCullBounds.w*max(max(length(x.xyz),length(y.xyz)),length(z.xyz));
  bool camera_visible=true;
  if((u_njinCullFlags.x>0.5&&u_njinCullFlags.x<1.5)||u_njinCullFlags.z>1.5) camera_visible=inCamera(p.xyz,r);
  if(u_njinCullFlags.x>1.5) {if(!inFrustum(center,radius)) return false;}
  else if(u_njinCullFlags.x>0.5&&!camera_visible) return false;
  if(u_njinCullFlags.z>0.5) {
    // Preserve model_lod_level's distance, strict threshold and half-size steps.
    float distance=length(p.xyz-u_njinCullCamera.xyz);
    float share=r/max(u_njinCullLod.w>0.5?u_njinCullCamera.w:distance*u_njinCullCamera.w,0.0001);
    float below=u_njinCullLod.x;int level=0;
    for(int l=0;l<int(u_njinCullLod.z);++l) {
      if(distance>r&&share<below) {++level;below*=0.5;}else break;
    }
    if(u_njinCullFlags.z>1.5&&!camera_visible) level=0;
    if(level!=int(u_njinCullLod.y)) return false;
  }
  if(u_njinCullFlags.y<0.5) return true;
  return visible(center,radius);
}
SHARED uint group_count;
NUM_THREADS(64,1,1)
void main() {
  uint i=gl_GlobalInvocationID.x;bool fused=u_njinCullFlags.w>0.5;
  if(fused) {if(gl_LocalInvocationID.x==0u) group_count=0u;barrier();}
  if(i<uint(u_njinCullInfo.x)) if(accepted(i)) {
    uint at;
    if(fused) {atomicFetchAndAdd(group_count,1u,at);}else {atomicFetchAndAdd(s_njinCullCount[0],1u,at);}
    for(uint row=0u;row<6u;++row) s_njinCullVisible[at*6u+row]=s_njinCullInstances[i*6u+row];
  }
  if(fused) {
    barrier();
    if(gl_LocalInvocationID.x==0u) {
      s_njinCullCount[0]=group_count;
      if(u_njinCullInfo.z>0.5) {drawIndexedIndirect(s_njinCullIndirect,0u,uint(u_njinCullInfo.y),group_count,0u,0u,0u);}
      else {drawIndirect(s_njinCullIndirect,0u,uint(u_njinCullInfo.y),group_count,0u,0u);}
    }
  }
}
#else
BUFFER_RW(s_njinCullCount,uint,2);
BUFFER_RW(s_njinCullIndirect,uvec4,3);
uniform vec4 u_njinCullInfo;
NUM_THREADS(1,1,1)
void main() {
  if(u_njinCullInfo.w<0.5) {s_njinCullCount[0]=0u;return;}
  if(u_njinCullInfo.z>0.5) {
    drawIndexedIndirect(s_njinCullIndirect,0u,uint(u_njinCullInfo.y),s_njinCullCount[0],0u,0u,0u);
  } else {
    drawIndirect(s_njinCullIndirect,0u,uint(u_njinCullInfo.y),s_njinCullCount[0],0u,0u);
  }
}
#endif
