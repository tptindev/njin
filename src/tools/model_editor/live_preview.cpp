#include "live_preview.h"
#include "raymath.h"
#include "rlgl.h"
#include <array>

namespace model_editor {
namespace {
const char *fragment = R"GLSL(#version 330
out vec4 finalColor;
uniform vec2 viewportSize;
uniform vec3 eye, forwardDir, rightDir, upDir, lowBound, highBound, clay;
uniform float tanHalfFov;
uniform mat4 viewProjection;
uniform int shapeCount;
uniform vec4 rowX[128], rowY[128], rowZ[128];
uniform vec4 dataA[128], dataB[128], dataC[128];
float field(vec3 world) {
  float result = 1e9;
  for (int i=0; i<shapeCount; ++i) {
    vec4 w=vec4(world,1);
    vec3 p=vec3(dot(rowX[i],w),dot(rowY[i],w),dot(rowZ[i],w));
    vec4 a=dataA[i], b=dataB[i];
    int kind=int(a.x), op=int(a.y);
    float d=length(p)-a.z;
    if (kind==1) {
      vec3 q=abs(p)-b.xyz;
      d=length(max(q,vec3(0)))+min(max(q.x,max(q.y,q.z)),0);
    } else if (kind==2) {
      p.y-=clamp(p.y,-a.w*0.5,a.w*0.5); d=length(p)-a.z;
    } else if (kind==3) {
      vec2 q=vec2(length(p.xz)-a.z,abs(p.y)-a.w*0.5);
      d=length(max(q,vec2(0)))+min(max(q.x,q.y),0);
    } else if (kind==4) d=length(vec2(length(p.xz)-a.z,p.y))-dataC[i].x;
    if (i==0) result=d;
    else if (op==0) result=min(result,d);
    else if (op==1) {
      if (b.w<0.00001) result=min(result,d);
      else {
        float h=clamp(0.5+0.5*(d-result)/b.w,0,1);
        result=mix(d,result,h)-b.w*h*(1-h);
      }
    } else if (op==2) result=max(result,-d);
    else result=max(result,d);
  }
  return result;
}
void main() {
  vec2 uv=(gl_FragCoord.xy/viewportSize)*2-1;
  vec3 ray=normalize(forwardDir+rightDir*uv.x*(viewportSize.x/viewportSize.y)*tanHalfFov+upDir*uv.y*tanHalfFov);
  // Avoid NaN for axis-aligned camera rays on a bounds plane.
  vec3 safeRay=mix(vec3(1e-8),ray,greaterThan(abs(ray),vec3(1e-8)));
  vec3 t0=(lowBound-eye)/safeRay, t1=(highBound-eye)/safeRay;
  vec3 mn=min(t0,t1), mx=max(t0,t1);
  float t=max(0,max(mn.x,max(mn.y,mn.z))), end=min(mx.x,min(mx.y,mx.z));
  if (end<t || shapeCount==0) discard;
  vec3 p; bool hit=false;
  for (int step=0;step<128;++step) {
    p=eye+ray*t;
    float d=field(p), eps=max(0.0004,t*tanHalfFov/viewportSize.y*0.4);
    if (abs(d)<eps) { hit=true; break; }
    t+=max(abs(d)*0.85,eps*0.5);
    if (t>end) break;
  }
  if (!hit) discard;
  vec2 e=vec2(0.0007,0);
  vec3 n=normalize(vec3(field(p+e.xyy)-field(p-e.xyy),field(p+e.yxy)-field(p-e.yxy),field(p+e.yyx)-field(p-e.yyx)));
  float shade=0.3+0.7*max(0,dot(n,normalize(vec3(-0.6,1,0.8))));
  finalColor=vec4(clay*shade,1);
  vec4 clip=viewProjection*vec4(p,1);
  gl_FragDepth=(clip.z/clip.w)*0.5+0.5;
})GLSL";
}
bool LivePreview::initialize() {
  shader=LoadShaderFromMemory(nullptr,fragment);
  if (shader.id==rlGetShaderIdDefault()) { shader={}; return false; }
  return shader.id!=0;
}
void LivePreview::shutdown() { if (shader.id) UnloadShader(shader); shader={}; }
void LivePreview::draw(const Field &f,const Camera3D &camera,int w,int h,Vector3 color) {
  if (!valid() || f.shapes.empty()) return;
  std::array<Vector4,128> x{},y{},z{},a{},b{},c{};
  int count=(int)f.shapes.size();
  for (int i=0;i<count;++i) {
    const auto &s=f.shapes[i].shape; const auto &m=f.shapes[i].inverse;
    x[i]={m.m0,m.m4,m.m8,m.m12}; y[i]={m.m1,m.m5,m.m9,m.m13}; z[i]={m.m2,m.m6,m.m10,m.m14};
    a[i]={(float)s.kind,(float)s.operation,s.radius,s.height};
    b[i]={s.size.x,s.size.y,s.size.z,s.blend}; c[i]={s.thickness,0,0,0};
  }
  auto vec3=[&](const char *name,Vector3 value) { SetShaderValue(shader,GetShaderLocation(shader,name),&value,SHADER_UNIFORM_VEC3); };
  auto array=[&](const char *name,const auto &values) { SetShaderValueV(shader,GetShaderLocation(shader,name),values.data(),SHADER_UNIFORM_VEC4,count); };
  Vector3 forward=Vector3Normalize(Vector3Subtract(camera.target,camera.position));
  Vector3 right=Vector3Normalize(Vector3CrossProduct(forward,camera.up)),up=Vector3CrossProduct(right,forward);
  vec3("eye",camera.position); vec3("forwardDir",forward); vec3("rightDir",right); vec3("upDir",up);
  vec3("lowBound",f.low); vec3("highBound",f.high); vec3("clay",color);
  Vector2 size{(float)w,(float)h}; float tangent=tanf(camera.fovy*DEG2RAD*0.5f);
  SetShaderValue(shader,GetShaderLocation(shader,"viewportSize"),&size,SHADER_UNIFORM_VEC2);
  SetShaderValue(shader,GetShaderLocation(shader,"tanHalfFov"),&tangent,SHADER_UNIFORM_FLOAT);
  SetShaderValue(shader,GetShaderLocation(shader,"shapeCount"),&count,SHADER_UNIFORM_INT);
  Matrix vp=MatrixMultiply(GetCameraMatrix(camera),MatrixPerspective(camera.fovy*DEG2RAD,(double)w/h,rlGetCullDistanceNear(),rlGetCullDistanceFar()));
  SetShaderValueMatrix(shader,GetShaderLocation(shader,"viewProjection"),vp);
  array("rowX",x); array("rowY",y); array("rowZ",z); array("dataA",a); array("dataB",b); array("dataC",c);
  BeginShaderMode(shader); rlEnableDepthTest();
  DrawRectangle(0,0,w,h,WHITE);
  rlDrawRenderBatchActive(); rlDisableDepthTest(); EndShaderMode();
}
}
