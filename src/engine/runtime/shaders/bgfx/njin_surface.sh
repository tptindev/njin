vec3 njinSplat(float v) { return vec3(v,v,v); }
// Njin shared surface math. GGX / correlated Smith / Schlick, glTF metallic-roughness.
// Reference: Khronos glTF-Sample-Renderer BRDF specification; no backend objects here.
vec3 njinLinear(vec3 c) {
  return mix(c/12.92,pow(max((c+0.055)/1.055,njinSplat(0.0)),njinSplat(2.4)),step(njinSplat(0.04045),c));
}
vec3 njinDisplay(vec3 c) {
  c=max(c,njinSplat(0.0));
  return mix(c*12.92,1.055*pow(c,njinSplat(1.0/2.4))-0.055,step(njinSplat(0.0031308),c));
}
vec3 njinACES(vec3 c) {
  vec3 into0 = vec3(0.59719,0.35458,0.04823);
  vec3 into1 = vec3(0.07600,0.90834,0.01566);
  vec3 into2 = vec3(0.02840,0.13383,0.83777);
  vec3 outof0 = vec3(1.60475,-0.53108,-0.07367);
  vec3 outof1 = vec3(-0.10208,1.10813,-0.00605);
  vec3 outof2 = vec3(-0.00327,-0.07276,1.07602);
  c = vec3(dot(into0,c/0.6),dot(into1,c/0.6),dot(into2,c/0.6));
  vec3 a = c * (c + 0.0245786) - 0.000090537;
  vec3 b = c * (0.983729 * c + 0.4329510) + 0.238081;
  return clamp(vec3(dot(outof0,a/b),dot(outof1,a/b),dot(outof2,a/b)), 0.0, 1.0);
}

// AgX: log encoding between -12.47 and +4.03 stops around mid grey, a sigmoid
// fitted to Blender's, and back to linear.
vec3 njinAgX(vec3 c) {
  vec3 inset0 = vec3(0.842479062253094,0.0784335999999992,0.0792237451477643);
  vec3 inset1 = vec3(0.0423282422610123,0.878468636469772,0.0791661274605434);
  vec3 inset2 = vec3(0.0423756549057051,0.0784336,0.879142973793104);
  vec3 outset0 = vec3(1.19687900512017,-0.0980208811401368,-0.0990297440797205);
  vec3 outset1 = vec3(-0.0528968517574562,1.15190312990417,-0.0989611768448433);
  vec3 outset2 = vec3(-0.0529716355144438,-0.0980434501171241,1.15107367264116);
  const float lo = -12.47393, hi = 4.026069;
  c = vec3(dot(inset0,max(c,njinSplat(1e-10))),dot(inset1,max(c,njinSplat(1e-10))),dot(inset2,max(c,njinSplat(1e-10))));
  c = clamp((log2(c) - lo) / (hi - lo), 0.0, 1.0);
  vec3 x2 = c * c, x4 = x2 * x2;
  c = 15.5 * x4 * x2 - 40.14 * x4 * c + 31.96 * x4 - 6.868 * x2 * c + 0.4298 * x2 + 0.1191 * c - 0.00232;
  return clamp(pow(max(vec3(dot(outset0,c),dot(outset1,c),dot(outset2,c)), njinSplat(0.0)), njinSplat(2.2)), 0.0, 1.0);
}

// Khronos PBR Neutral.
vec3 njinNeutral(vec3 c) {
  const float start = 0.76, desaturation = 0.15;
  float x = min(c.r, min(c.g, c.b));
  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
  c -= offset;
  float peak = max(c.r, max(c.g, c.b));
  if (peak < start)
    return c;
  float d = 1.0 - start;
  float top = 1.0 - d * d / (peak + d - start);
  c *= top / peak;
  float g = 1.0 - 1.0 / (desaturation * (peak - top) + 1.0);
  return mix(c, njinSplat(top), g);
}

vec3 njinTone(vec3 c) {
  c=max(c,njinSplat(0.0));
  if(u_njinMaterial.w>3.5) c=njinNeutral(c);
  else if(u_njinMaterial.w>2.5) c=njinAgX(c);
  else if(u_njinMaterial.w>1.5) c=njinACES(c);
  return njinDisplay(clamp(c,0.0,1.0));
}
// RGB24 packed projected depth keeps the atlas portable to RGBA8 targets.
vec3 njinPackDepth(float depth) {
  vec3 digits=fract(min(depth,0.99999994)*vec3(1.0,255.0,65025.0));
  return digits-digits.yzz*vec3(1.0/255.0,1.0/255.0,0.0);
}
vec4 njinShadowProject(int view,vec3 p) {
  int row=int(u_njinShadowMeta[view].z+0.5)*4;
  vec4 v=vec4(p,1.0);
  return vec4(dot(u_njinShadowRows[row],v),dot(u_njinShadowRows[row+1],v),
    dot(u_njinShadowRows[row+2],v),dot(u_njinShadowRows[row+3],v));
}
vec4 njinShadowDelta(int view,vec3 delta) {
  int row=int(u_njinShadowMeta[view].z+0.5)*4;
  return vec4(dot(u_njinShadowRows[row].xyz,delta),dot(u_njinShadowRows[row+1].xyz,delta),dot(u_njinShadowRows[row+2].xyz,delta),dot(u_njinShadowRows[row+3].xyz,delta));
}
float njinShadowFilter(int view,vec3 uvz,vec3 dx,vec3 dy) {
  vec4 rect=u_njinShadowRect[view];
  // Receiver-plane depth at each actual point-sampled texel center. This
  // compensates a sloped receiver across the PCF footprint, rather than
  // increasing a constant bias and detaching contact shadows.
  vec2 slope=vec2(0.0,0.0);
  float determinant=dx.x*dy.y-dx.y*dy.x;
  if(abs(determinant)>0.000000000001)
    slope=vec2(dx.z*dy.y-dy.z*dx.y,dy.z*dx.x-dx.z*dy.x)/determinant;
  if(uvz.x<rect.x || uvz.x>rect.z || uvz.y<rect.y || uvz.y>rect.w || uvz.z<0.0 || uvz.z>1.0) return 1.0;
  float result=0.0;
  vec2 halfTexel=u_njinShadowInfo.xy*0.5;
  for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
    vec2 uv=clamp(uvz.xy+vec2(float(x),float(y))*u_njinShadowInfo.xy*u_njinShadowInfo.z,rect.xy+halfTexel,rect.zw-halfTexel);
    uv=(floor(uv/u_njinShadowInfo.xy)+vec2(0.5,0.5))*u_njinShadowInfo.xy;
    float depth=dot(NJIN_SHADOW_TEX(uv).rgb,vec3(1.0,1.0/255.0,1.0/65025.0));
    float receiver=uvz.z+dot(slope,uv-uvz.xy);
    result+=receiver-u_njinShadowMeta[view].y<=depth?1.0:0.0;
  }
  return result/9.0;
}
float njinProjectedShadow(int view,vec3 position,vec3 normal) {
  vec3 receiver=position+normal*u_njinShadowMeta[view].x;
  // Different fragments can select different cascades. Differentiate world
  // position first, then apply THIS fragment's projection Jacobian, avoiding
  // derivative discontinuities between atlas tiles/cascade matrices.
  vec3 worldDx=dFdx(receiver),worldDy=dFdy(receiver);
  vec4 clip=njinShadowProject(view,receiver);
  vec4 cx=njinShadowDelta(view,worldDx),cy=njinShadowDelta(view,worldDy);
  if(clip.w<=0.0) return 1.0;
  vec3 dx=(cx.xyz*clip.w-clip.xyz*cx.w)/(clip.w*clip.w)*vec3(0.5,-0.5,0.5);
  vec3 dy=(cy.xyz*clip.w-clip.xyz*cy.w)/(clip.w*clip.w)*vec3(0.5,-0.5,0.5);
  return njinShadowFilter(view,vec3(clip.x/clip.w*0.5+0.5,0.5-clip.y/clip.w*0.5,clip.z/clip.w*0.5+0.5),dx,dy);
}
#ifdef NJIN_ADVANCED_MATERIALS
// The ray traced texture holds three images of the view: the shadows, the
// reflections, the irradiance grid's light (u_njinRtShadow[4].z is a texel of the
// whole height).
vec4 njinRtFetch(vec3 position,float part) {
  vec4 v=vec4(position,1.0);
  vec4 q=vec4(dot(u_njinRtShadow[0],v),dot(u_njinRtShadow[1],v),dot(u_njinRtShadow[2],v),dot(u_njinRtShadow[3],v));
  vec2 uv=clamp(vec2(q.x/q.w*0.5+0.5,0.5-q.y/q.w*0.5),vec2(0.0,0.0),vec2(0.9999,0.9999));
  uv.y=(uv.y+part)/3.0;
  uv=(floor(uv/u_njinRtShadow[4].yz)+vec2(0.5,0.5))*u_njinRtShadow[4].yz;
  return texture2DLod(s_njinOpaqueDepth,uv,0.0);
}
// light3d::ray_traced_shadows: this pixel's traced (and denoised) visibility of the
// sun (r) and of the lamps given a channel (g, b, a).
vec4 njinRtMask(vec3 position) {return njinRtFetch(position,0.0);}
// light3d::ray_traced_reflections: the light a mirror ray met (linear), and in w the
// reflection_roughness when it met something, else 0 (njinRtWeight).
vec4 njinRtReflection(vec3 position) {
  if(u_njinRtShadow[4].w<=0.0) return vec4(0.0,0.0,0.0,0.0);
  vec4 h=njinRtFetch(position,1.0);
  return vec4(h.rgb,h.a>0.5?u_njinRtShadow[4].w:0.0);
}
// A grid captured once (irradiance3d_set without ray tracing), on sampler 10: an ambient
// cube per point (a column per point, a row per direction), trilinear between the eight
// points round `position`, faded over the box's edge band, as legacy grid_irradiance
// (render3d.cpp) samples its captured grid. Rows 6..8: corner, blend; size, intensity;
// points along x, y, z.
vec4 njinCapturedGrid(vec3 position,vec3 n) {
  vec3 lo=u_njinRtShadow[6].xyz,size=max(u_njinRtShadow[7].xyz,vec3(1e-4,1e-4,1e-4)),cnt=u_njinRtShadow[8].xyz;
  vec3 rel=(position-lo)/size;
  vec3 inside=min(position-lo,lo+size-position);
  float weight=clamp(min(inside.x,min(inside.y,inside.z))/max(u_njinRtShadow[6].w,1e-4),0.0,1.0);
  if(weight<=0.0) return vec4(0.0,0.0,0.0,0.0);
  vec3 g=clamp(rel,0.0,1.0)*max(cnt-1.0,vec3(0.0,0.0,0.0));
  vec3 g0=floor(g),f=g-g0,n2=n*n;
  float total=cnt.x*cnt.y*cnt.z;
  float dx=n.x>=0.0?0.0:1.0,dy=n.y>=0.0?2.0:3.0,dz=n.z>=0.0?4.0:5.0;
  vec3 sum=vec3(0.0,0.0,0.0);
  for(int c=0;c<8;c++) {
    vec3 o=vec3(float(c-(c/2)*2),float((c/2)-(c/4)*2),float(c/4));
    vec3 p=min(g0+o,max(cnt-1.0,vec3(0.0,0.0,0.0)));
    float w=(o.x>0.5?f.x:1.0-f.x)*(o.y>0.5?f.y:1.0-f.y)*(o.z>0.5?f.z:1.0-f.z);
    float u=(p.x+cnt.x*(p.y+cnt.y*p.z)+0.5)/total;
    sum+=w*(n2.x*texture2DLod(s_njinOpaqueDepth,vec2(u,(dx+0.5)/6.0),0.0).rgb+
            n2.y*texture2DLod(s_njinOpaqueDepth,vec2(u,(dy+0.5)/6.0),0.0).rgb+
            n2.z*texture2DLod(s_njinOpaqueDepth,vec2(u,(dz+0.5)/6.0),0.0).rgb);
  }
  return vec4(sum*u_njinRtShadow[7].w,weight);
}
// irradiance3d_desc::ray_traced: the grid's light here (linear) and its weight; or the
// captured grid's.
vec4 njinRtGrid(vec3 position,vec3 n) {
  if(u_njinRtShadow[5].x>1.5) return njinCapturedGrid(position,n);
  if(u_njinRtShadow[5].x<0.5) return vec4(0.0,0.0,0.0,0.0);
  return njinRtFetch(position,2.0);
}
#endif
// How much of the specular light the traced reflection replaces at `rough` (legacy rt_reflection).
float njinRtWeight(vec4 traced,float rough) {
  return traced.w>0.0?1.0-smoothstep(traced.w*0.5,traced.w,rough):0.0;
}
float njinSunShadow(vec3 position,vec3 normal) {
#ifdef NJIN_ADVANCED_MATERIALS
  if(u_njinRtShadow[4].x>0.5) return njinRtMask(position).r;
#endif
  if(u_njinShadowInfo.w<0.5 || u_njinShadowForward.w<0.5) return 1.0;
  float distance=dot(position-u_njinLight[3].xyz,u_njinShadowForward.xyz);
  int view=0;
  for(int i=0;i<4;++i) {
    if(float(i)>=u_njinShadowForward.w) break;
    view=i;
    if(distance<=u_njinShadowSplits[i]) break;
  }
  if(distance>u_njinShadowSplits[view]) return 1.0;
  return njinProjectedShadow(view,position,normal);
}
float njinLocalShadowParameters(vec4 source,vec4 cone,vec3 position,vec3 normal) {
#ifdef NJIN_ADVANCED_MATERIALS
  if(u_njinRtShadow[4].x>0.5&&cone.w>0.5) {
    vec4 mask=njinRtMask(position);
    return cone.w<1.5?mask.g:cone.w<2.5?mask.b:mask.a;
  }
#endif
  if(u_njinShadowInfo.w<0.5) return 1.0;
  int base=int(cone.z+0.5)-1;
  if(base<0) return 1.0;
  if(cone.y>0.5) return njinProjectedShadow(base,position,normal);
  vec3 delta=position+normal*u_njinShadowMeta[base].x-source.xyz;
  vec3 a=abs(delta),worldDx=dFdx(delta),worldDy=dFdy(delta); float major,majorDx,majorDy; vec2 uv,uvDx,uvDy; int face;
  if(a.x>=a.y && a.x>=a.z) {
    major=a.x; face=delta.x>=0.0?0:1; uv=vec2(delta.x>=0.0?-delta.z:delta.z,-delta.y);
    majorDx=worldDx.x*(delta.x>=0.0?1.0:-1.0);majorDy=worldDy.x*(delta.x>=0.0?1.0:-1.0);uvDx=vec2(delta.x>=0.0?-worldDx.z:worldDx.z,-worldDx.y);uvDy=vec2(delta.x>=0.0?-worldDy.z:worldDy.z,-worldDy.y);
  } else if(a.y>=a.z) {
    major=a.y; face=delta.y>=0.0?2:3; uv=vec2(delta.x,delta.y>=0.0?delta.z:-delta.z);
    majorDx=worldDx.y*(delta.y>=0.0?1.0:-1.0);majorDy=worldDy.y*(delta.y>=0.0?1.0:-1.0);uvDx=vec2(worldDx.x,delta.y>=0.0?worldDx.z:-worldDx.z);uvDy=vec2(worldDy.x,delta.y>=0.0?worldDy.z:-worldDy.z);
  } else {
    major=a.z; face=delta.z>=0.0?4:5; uv=vec2(delta.z>=0.0?delta.x:-delta.x,-delta.y);
    majorDx=worldDx.z*(delta.z>=0.0?1.0:-1.0);majorDy=worldDy.z*(delta.z>=0.0?1.0:-1.0);uvDx=vec2(delta.z>=0.0?worldDx.x:-worldDx.x,-worldDx.y);uvDy=vec2(delta.z>=0.0?worldDy.x:-worldDy.x,-worldDy.y);
  }
  float farPlane=source.w,nearPlane=min(0.05,farPlane*0.01);
  if(major<nearPlane) return 1.0;
  uvDx=(uvDx*major-uv*majorDx)/(major*major)*vec2(0.5,-0.5);uvDy=(uvDy*major-uv*majorDy)/(major*major)*vec2(0.5,-0.5);
  uv=uv/major*0.5+vec2(0.5,0.5); uv.y=1.0-uv.y;
  vec4 rect=u_njinShadowRect[base+face];
  float depth=farPlane/(farPlane-nearPlane)-nearPlane*farPlane/((farPlane-nearPlane)*major);
  float derivative=nearPlane*farPlane/((farPlane-nearPlane)*major*major);
  return njinShadowFilter(base+face,vec3(mix(rect.xy,rect.zw,uv),depth),vec3(uvDx*(rect.zw-rect.xy),derivative*majorDx),vec3(uvDy*(rect.zw-rect.xy),derivative*majorDy));
}
float njinLocalShadow(int source,vec3 position,vec3 normal) {return njinLocalShadowParameters(u_njinLocal[source*4],u_njinLocal[source*4+3],position,normal);}
vec2 njinMapUv(int map,vec2 uv0,vec2 uv1) {
  vec4 basis=u_njinMapUv[map*2],info=u_njinMapUv[map*2+1];
  vec2 uv=info.z>0.5?uv1:uv0;
  return vec2(dot(basis.xy,uv),dot(basis.zw,uv))+info.xy;
}
// Tangent frame for a map whose UV is basis*uv+offset (basis = 2x2 rows).
bool njinTangentFrame(vec3 n,vec3 position,vec4 tangent,vec4 basis,vec2 uv,out vec3 t,out vec3 b) {
  if(abs(tangent.w)>0.5 && length(tangent.xyz)>0.000001) {
    t=normalize(tangent.xyz-n*dot(n,tangent.xyz));
    b=cross(n,t)*tangent.w*u_njinMapFactors.z;
    vec4 a=basis; float determinant=a.x*a.w-a.y*a.z;
    if(abs(determinant)<0.00000001) return false;
    vec3 transformed_t=(t*a.w-b*a.z)/determinant;
    vec3 transformed_b=(-t*a.y+b*a.x)/determinant;
    t=transformed_t/max(length(transformed_t),0.000001);
    b=transformed_b/max(length(transformed_b),0.000001);
  } else {
    vec3 dx=dFdx(position),dy=dFdy(position); vec2 ux=dFdx(uv),uy=dFdy(uv);
    float determinant=ux.x*uy.y-ux.y*uy.x;
    t=n;b=n;
    if(abs(determinant)<0.00000001) return false;
    t=(dx*uy.y-dy*ux.y)/determinant;
    b=(-dx*uy.x+dy*ux.x)/determinant;
    t=t-n*dot(n,t); t/=max(length(t),0.000001);
    b=b-n*dot(n,b); b/=max(length(b),0.000001);
  }
  return true;
}
vec3 njinMappedBaseNormal(vec3 normal,vec3 position,vec4 tangent,vec2 uv0,vec2 uv1) {
  vec3 n=normal/max(length(normal),0.000001);
  if(u_njinMapUv[1].w<0.5) {
#ifdef NJIN_ADVANCED_MATERIALS
    return njinMixUnderNormal(n,n,position,uv0);
#else
    return n;
#endif
  }
  vec2 uv=njinMapUv(0,uv0,uv1);
  vec3 t,b;
  if(!njinTangentFrame(n,position,tangent,u_njinMapUv[0],uv,t,b)) return n;
#ifdef NJIN_ADVANCED_MATERIALS
  // World-projected rows grow downward; the public normal-map green axis is up.
  if(u_njinAdvanced[1].x>0.0) b=-b;
#endif
  vec3 mapped=NJIN_NORMAL_TEX(uv).xyz*2.0-vec3(1.0,1.0,1.0);
#ifdef NJIN_ADVANCED_MATERIALS
  if(u_njinAdvanced[1].x>0.0) {
    vec2 a,bOffset;float blend;njinUntile(uv0,a,bOffset,blend);
    vec2 dx=dFdx(uv),dy=dFdy(uv);
    vec3 first=u_njinMipFilter[0].x>0.5?texture2DLod(s_njinNormalMap,uv+a,0.0).xyz:texture2DGrad(s_njinNormalMap,uv+a,dx,dy).xyz;
    vec3 second=u_njinMipFilter[0].x>0.5?texture2DLod(s_njinNormalMap,uv+bOffset,0.0).xyz:texture2DGrad(s_njinNormalMap,uv+bOffset,dx,dy).xyz;
    mapped=mix(first,second,blend)*2.0-1.0;
  }
#endif
  mapped.xy*=u_njinMapFactors.x;
  vec3 world=t*mapped.x+b*mapped.y+n*mapped.z;
#ifdef NJIN_ADVANCED_MATERIALS
  return njinMixUnderNormal(world/max(length(world),0.000001),n,position,uv0);
#else
  return world/max(length(world),0.000001);
#endif
}
vec3 njinMappedNormal(vec3 normal,vec3 position,vec4 tangent,vec2 uv0,vec2 uv1) {
#ifdef NJIN_TERRAIN_SURFACE
  if(u_njinWorldStyle[0].x==1.0&&tangent.w>1.5) {
    vec3 base=vec3(uv0.x,uv1.x,uv0.y);
    vec3 mapped=njinMappedBaseNormal(normal,base,vec4(0.0,0.0,0.0,0.0),uv0,uv1);
    return normalize(mapped-normalize(normal)+normalize(tangent.xyz));
  }
#endif
  return njinMappedBaseNormal(normal,position,tangent,uv0,uv1);
}
vec3 njinFinishNormal(vec3 mapped,vec3 normal,vec4 finish) {
  vec3 n=mix(mapped,normalize(normal),finish.w*0.7)+finish.xyz;
  return n/max(length(n),0.000001);
}
#ifdef NJIN_ADVANCED_MATERIALS
// KHR_materials_clearcoat normal: perturbs the geometric normal, not the base map.
vec3 njinCoatNormal(vec3 normal,vec3 position,vec4 tangent,vec2 uv0,vec2 uv1) {
  vec3 n=normal/max(length(normal),0.000001);
  float slot=u_njinCoat.z;
  vec2 uv=njinCoatUv(slot,uv0,uv1);
  vec3 t,b;
  if(!njinTangentFrame(n,position,tangent,slot<1.5?u_njinCoatUv[0]:u_njinCoatUv[2],uv,t,b)) return n;
  vec3 mapped=njinCoatSample(slot,uv0,uv1).xyz*2.0-vec3(1.0,1.0,1.0);
  mapped.xy*=u_njinCoat.w;
  vec3 world=t*mapped.x+b*mapped.y+n*mapped.z;
  return world/max(length(world),0.000001);
}
#endif
vec4 njinNormalMask(vec3 normal,vec3 position,vec4 tangent,vec2 uv0,vec2 uv1,vec4 finish) {
  vec3 n=normalize(mul(u_njinView,vec4(njinFinishNormal(njinMappedNormal(normal,position,tangent,uv0,uv1),normal,finish),0.0)).xyz);
  n/=max(abs(n.x)+abs(n.y)+abs(n.z),0.000001);
  vec2 oct=n.xy;
  if(n.z<0.0) oct=(vec2(1.0,1.0)-abs(oct.yx))*vec2(oct.x>=0.0?1.0:-1.0,oct.y>=0.0?1.0:-1.0);
  float rough=u_njinMaterial.y;
  if(u_njinMapUv[3].w>0.5) rough*=NJIN_MR_TEX(njinMapUv(1,uv0,uv1)).g;
  return vec4(oct*0.5+vec2(0.5,0.5),mix(rough,0.6,finish.w),u_njinMask.y);
}
float njinToonBand(float x) {
  if(u_njinSurfaceFx[1].x<2.0) return x;
  float steps=u_njinSurfaceFx[1].x-1.0;
  float s=(x-u_njinSurfaceFx[1].z)/max(1.0-u_njinSurfaceFx[1].z,0.0001)*steps;
  float width=max(u_njinSurfaceFx[1].y,0.001)*0.5,level=0.0;
  for(int i=0;i<7;++i) if(float(i)<steps) level+=smoothstep(float(i)-width,float(i)+width,s);
  return level/steps;
}
float njinDielectricF0() {
#ifdef NJIN_ADVANCED_MATERIALS
  float ior=u_njinTransmit[0].y;
  if(ior>=1.0) {float ratio=(ior-1.0)/(ior+1.0);return ratio*ratio;}
#endif
  return 0.04;
}
vec3 njinDirect(vec3 albedo,vec3 n,vec3 v,vec3 l,vec3 radiance,vec2 surface,vec3 tangent,vec2 coat,vec3 coatNormal) {
  vec3 h=l+v; h/=max(length(h),0.000001);
  float nl=max(dot(n,l),0.0),nv=max(dot(n,v),0.0),nh=max(dot(n,h),0.0),vh=max(dot(v,h),0.0);
  if(u_njinMaterial.z>0.5) {
    float metal=surface.x,rough=max(surface.y,0.045);
    float a=rough*rough,a2=a*a,d=nh*nh*(a2-1.0)+1.0;
    float distribution=a2/(3.14159265359*max(d*d,0.00000001));
    float visibility=0.5/max(nl*sqrt(nv*nv*(1.0-a2)+a2)+nv*sqrt(nl*nl*(1.0-a2)+a2),0.000001);
    vec3 f0=mix(njinSplat(njinDielectricF0()),albedo,metal);
    vec3 fresnel=f0+(njinSplat(1.0)-f0)*pow(clamp(1.0-vh,0.0,1.0),5.0);
    vec4 ext=u_njinSurfaceFx[3];
    if(abs(ext.z)>0.0 && dot(tangent,tangent)>0.000001) {
      vec3 t=normalize(tangent-n*dot(n,tangent)),b=cross(n,t);
      t=t*cos(ext.w)+b*sin(ext.w);b=cross(n,t);
      float at=max(a*(1.0+ext.z),0.001),ab=max(a*(1.0-ext.z),0.001),a2x=at*ab;
      vec3 w=vec3(ab*dot(t,h),at*dot(b,h),a2x*nh);
      float q=a2x/max(dot(w,w),0.00000001);
      distribution=a2x*q*q/3.14159265359;
      visibility=0.5/max(nl*length(vec3(at*dot(t,v),ab*dot(b,v),nv))+nv*length(vec3(at*dot(t,l),ab*dot(b,l),nl)),0.000001);
    }
    vec3 diffuse=(njinSplat(1.0)-fresnel)*(1.0-metal)*albedo/3.14159265359;
    vec3 result=diffuse+fresnel*distribution*visibility;
    if(coat.x>0.0) {
      float nhc=max(dot(coatNormal,h),0.0),nlc=max(dot(coatNormal,l),0.0);
      float cr=max(coat.y,0.045),ca=cr*cr,ca2=ca*ca,q=nhc*nhc*(ca2-1.0)+1.0;
      float fc=(0.04+0.96*pow(1.0-vh,5.0))*coat.x;
      float coatSpecular=ca2/(3.14159265359*max(q*q,0.00000001))*0.25/max(vh*vh,0.0001)*fc;
      return (result*(1.0-fc)*njinToonBand(nl)+njinSplat(coatSpecular*njinToonBand(nlc)))*radiance;
    }
    return result*radiance*njinToonBand(nl);
  }
  float spec=nl>0.0?pow(nh,u_njinLight[2].w)*u_njinLight[1].w:0.0;
  if(u_njinSurfaceFx[1].x>=2.0) spec=smoothstep(0.5-u_njinSurfaceFx[1].y*0.5,0.5+max(u_njinSurfaceFx[1].y,0.001)*0.5,spec);
  return (albedo*njinToonBand(nl)+njinSplat(spec))*radiance;
}
// Equirectangular GGX atlas and SH9 use the legacy Njin environment convention.
vec3 njinEnvironmentDirection(vec3 d) {
  float c=cos(u_njinEnvParams.z),s=sin(u_njinEnvParams.z);
  return vec3(c*d.x-s*d.z,d.y,s*d.x+c*d.z);
}
vec2 njinEnvironmentUv(vec3 d) {
  d=normalize(njinEnvironmentDirection(d));
  return vec2(NJIN_ATAN2(d.z,d.x)/6.28318530718+0.5,acos(clamp(d.y,-1.0,1.0))/3.14159265359);
}
vec3 njinEnvironmentIrradiance(vec3 n) {
  n=njinEnvironmentDirection(n);
  return max(njinSplat(0.0),u_njinEnvSH[0].rgb*0.886227+u_njinEnvSH[1].rgb*(1.023328*n.y)+u_njinEnvSH[2].rgb*(1.023328*n.z)+u_njinEnvSH[3].rgb*(1.023328*n.x)+u_njinEnvSH[4].rgb*(0.858086*n.x*n.y)+u_njinEnvSH[5].rgb*(0.858086*n.y*n.z)+u_njinEnvSH[6].rgb*(0.247708*(3.0*n.z*n.z-1.0))+u_njinEnvSH[7].rgb*(0.858086*n.x*n.z)+u_njinEnvSH[8].rgb*(0.429043*(n.x*n.x-n.y*n.y)));
}
vec3 njinEnvironmentRadiance(vec3 d,float rough) {
  vec2 uv=njinEnvironmentUv(d);uv.y=clamp(uv.y,u_njinEnvParams.w,1.0-u_njinEnvParams.w);
  float at=clamp(rough,0.0,1.0)*(u_njinEnvParams.x-1.0),a=floor(at),b=min(a+1.0,u_njinEnvParams.x-1.0);
  return mix(NJIN_ENV_ATLAS(vec2(uv.x,(a+uv.y)/u_njinEnvParams.x)).rgb,NJIN_ENV_ATLAS(vec2(uv.x,(b+uv.y)/u_njinEnvParams.x)).rgb,at-a)*u_njinEnvParams.y;
}
vec3 njinEnvironmentLight(vec3 albedo,vec2 surface,vec3 n,vec3 v,vec2 coat,vec3 coatNormal,vec4 traced,vec4 grid) {
  float nv=max(dot(n,v),0.0),metal=surface.x,rough=max(surface.y,0.045);
  vec3 f0=mix(njinSplat(njinDielectricF0()),albedo,metal);
  vec3 fresnel=f0+(max(njinSplat(1.0-rough),f0)-f0)*pow(1.0-nv,5.0);
  vec4 fit=rough*vec4(-1.0,-0.0275,-0.572,0.022)+vec4(1.0,0.0425,1.04,-0.04);
  float a=min(fit.x*fit.x,exp2(-9.28*nv))*fit.x+fit.y;
  vec2 ab=vec2(-1.04,1.04)*a+fit.zw;
  // The traced irradiance grid (irradiance over pi) in place of the environment's in its box.
  vec3 diffuse=(njinSplat(1.0)-fresnel)*(1.0-metal)*albedo*mix(njinEnvironmentIrradiance(n)/3.14159265359,grid.rgb,grid.w);
  vec3 reflection=reflect(-v,n);
  vec3 lit=diffuse+mix(njinEnvironmentRadiance(reflection,rough),traced.rgb,njinRtWeight(traced,surface.y))*(f0*ab.x+njinSplat(ab.y));
  if(coat.x>0.0) {
    float nvc=max(dot(coatNormal,v),0.0);
    float fresnelCoat=(0.04+0.96*pow(1.0-nvc,5.0))*coat.x;
    vec3 coatLight=mix(njinEnvironmentRadiance(reflect(-v,coatNormal),max(coat.y,0.045)),traced.rgb,njinRtWeight(traced,coat.y));
    lit=mix(lit,coatLight,fresnelCoat);
  }
  return lit;
}
// The traced reflection on a PBR surface lit without an environment (no specular
// light of its own there): its split-sum specular share.
vec3 njinRtSpecular(vec3 albedo,vec2 surface,vec3 n,vec3 v,vec4 traced) {
  float w=njinRtWeight(traced,surface.y);
  if(w<=0.0) return vec3(0.0,0.0,0.0);
  float nv=max(dot(n,v),0.0),rough=max(surface.y,0.045);
  vec3 f0=mix(njinSplat(njinDielectricF0()),albedo,surface.x);
  vec4 fit=rough*vec4(-1.0,-0.0275,-0.572,0.022)+vec4(1.0,0.0425,1.04,-0.04);
  float a=min(fit.x*fit.x,exp2(-9.28*nv))*fit.x+fit.y;
  vec2 ab=vec2(-1.04,1.04)*a+fit.zw;
  return traced.rgb*w*(f0*ab.x+njinSplat(ab.y));
}
vec3 njinCrystalHash(vec3 p) {
  p=fract(p*vec3(0.1031,0.1030,0.0973));
  p+=dot(p,p.yxz+33.33);
  return fract((p.xxy+p.yxx)*p.zyx);
}
float njinCrystal(vec3 position,vec3 normal,float foot,out vec3 face) {
  float cell=0.03;
  vec3 id=floor(position/cell);
  face=normalize(njinCrystalHash(id+31.0)*2.0-1.0+0.0001);
  if(dot(face,normal)<0.0) face=-face;
  if(njinCrystalHash(id).x>0.5) return 0.0;
  vec3 delta=(id+0.2+0.6*njinCrystalHash(id+17.0))*cell-position;
  delta-=normal*dot(delta,normal);
  float radius=max(cell*0.12,foot*0.6);
  return (1.0-smoothstep(radius*0.5,radius,length(delta)))*min(cell*1.5/max(foot,0.00001),1.0);
}
float njinCrystalMirror(vec3 face,vec3 light,vec3 eye) {
  return pow(max(dot(reflect(-light,face),eye),0.0),40.0);
}
vec3 njinSurface(vec3 base,vec3 linearBase,vec3 normal,vec3 position,vec2 uv0,vec2 uv1,vec2 lightmapUv,vec4 tangent,vec4 finish,vec3 glitter) {
  float footprint=length(fwidth(position));
  if(u_njinEnvMode.x>1.5) {
#ifdef NJIN_ADVANCED_MATERIALS
    // draw_sky3d(): display colours as the legacy sky, linear and exposed for HDR.
    if(u_njinEnvMode.x>2.5) {
      vec3 c=njinSkyColor(normal,u_njinSky[10].xyz);
      return u_njinOutput.y>0.5?njinLinear(max(c,njinSplat(0.0)))*u_njinOutput.x:c;
    }
#endif
    vec3 sky=NJIN_ENV_BASE(njinEnvironmentUv(normal)).rgb*u_njinEnvParams.y*u_njinOutput.x;
    return u_njinOutput.y>0.5?sky:njinTone(sky);
  }
  if(u_njinOutput.z>0.5) return njinTone(base*u_njinOutput.x);
  bool linearLight=u_njinMaterial.z>0.5 || u_njinMaterial.w>0.5 || u_njinOutput.y>0.5;
#ifdef NJIN_ADVANCED_MATERIALS
  linearLight=linearLight||u_njinAdvanced[2].w>0.5;
#endif
  vec3 albedo=linearLight?linearBase:base;
  vec3 result=albedo;
  vec3 staticDiffuse=vec3(0.0,0.0,0.0);
  vec2 surface=u_njinMaterial.xy;
  if(u_njinMapUv[3].w>0.5) { vec4 channels=NJIN_MR_TEX(njinMapUv(1,uv0,uv1)); surface*=vec2(channels.b,channels.g); }
  float ownRough=surface.y;
  surface.y=mix(surface.y,0.6,finish.w);
  float ao=1.0;
  if(u_njinMapUv[5].w>0.5) ao=mix(1.0,NJIN_AO_TEX(njinMapUv(2,uv0,uv1)).r,u_njinMapFactors.y);
  if(u_njinLight[0].w>0.5) {
    vec3 n=njinFinishNormal(njinMappedNormal(normal,position,tangent,uv0,uv1),normal,finish);
    vec2 coat=u_njinSurfaceFx[3].xy;
    vec3 coatNormal=n;
#ifdef NJIN_ADVANCED_MATERIALS
    if(u_njinCoat.x>0.5) coat.x*=njinCoatSample(u_njinCoat.x,uv0,uv1).r;
    if(u_njinCoat.y>0.5) coat.y*=njinCoatSample(u_njinCoat.y,uv0,uv1).g;
    if(u_njinCoat.z>0.5) coatNormal=njinCoatNormal(normal,position,tangent,uv0,uv1);
#endif
#ifdef NJIN_ADVANCED_MATERIALS
    // draw_sky3d wetness on terrain (world style 1, wetness in row 0 w): darker where
    // water stands on the flat, shinier on rough layers; dry under cover and snow.
    if(u_njinWorldStyle[0].x>0.5&&u_njinWorldStyle[0].x<1.5&&u_njinWorldStyle[0].w>0.0) {
      float wet=u_njinWorldStyle[0].w*smoothstep(0.6,0.95,n.y)*(1.0-njinCovered(position))*(1.0-finish.w);
      // Legacy darkens the display colour; with linear lighting apply it there too.
      float dark=mix(1.0,0.6,wet);
      albedo=linearLight?njinLinear(njinDisplay(albedo)*dark):albedo*dark;
      if(u_njinMapUv[3].w>0.5) surface.y=mix(mix(ownRough,0.5,wet),0.6,finish.w);
    }
#endif
    vec3 l=-normalize(u_njinLight[0].xyz);
    vec3 eye=u_njinLight[3].xyz-position;
    vec3 v=eye/max(length(eye),0.000001);
    vec3 sun=(linearLight?njinLinear(u_njinLight[1].rgb):u_njinLight[1].rgb)*u_njinLight[3].w;
    vec3 ambient=linearLight?njinLinear(u_njinLight[2].rgb):u_njinLight[2].rgb;
    // irradiance3d_desc::ray_traced: the grid's light in place of `ambient` in its box
    // (a lightmap still wins, as on the legacy renderer).
    vec4 gridLight=vec4(0.0,0.0,0.0,0.0);
#ifdef NJIN_ADVANCED_MATERIALS
    gridLight=njinRtGrid(position,n);
    gridLight.rgb=linearLight?gridLight.rgb:njinDisplay(gridLight.rgb);
    if(gridLight.w>0.0) ambient=mix(ambient,gridLight.rgb,gridLight.w);
#endif
    vec3 crystalFace=n;
    float crystal=0.0;
    if(glitter.x>0.0) {
      crystal=njinCrystal(position,n,footprint,crystalFace)*glitter.x*(1.0-smoothstep(30.0,60.0,length(eye)));
      if(glitter.z>0.5) crystal*=surface.x;
      if(u_njinOutput.y>0.5) crystal*=6.0;
    }
#ifdef NJIN_ADVANCED_MATERIALS
    if(u_njinAdvanced[0].y>0.5) {
      vec2 lmUv=u_njinAdvanced[2].w>0.5?lightmapUv:(abs(dFdx(uv1).x)+abs(dFdy(uv1).y)>0.0000001?uv1:uv0);
      vec3 baked=texture2D(s_njinLightmap,njinAdvancedUv(1,lmUv,lmUv)).rgb;
      vec3 bakedLinear=u_njinAdvanced[2].x>0.5?baked:njinLinear(baked);
      ambient=(linearLight?bakedLinear:njinDisplay(bakedLinear))*u_njinAdvanced[1].w;
      staticDiffuse=ambient;
      gridLight.w=0.0;
    }
#endif
    vec3 geometric=normal/max(length(normal),0.000001);
    float sunShadow=njinSunShadow(position,geometric);
    result=albedo*ambient*ao*(u_njinMaterial.z>0.5?1.0-surface.x:1.0)+njinDirect(albedo,n,v,l,sun,surface,tangent.xyz,coat,coatNormal)*sunShadow;
    if(crystal>0.0&&dot(n,l)>0.0) result+=sun*(sunShadow*crystal*njinCrystalMirror(crystalFace,l,v));
    bool useEnvironment=u_njinEnvMode.x>0.5 && u_njinMaterial.z>0.5;
#ifdef NJIN_ADVANCED_MATERIALS
    useEnvironment=useEnvironment&&u_njinAdvanced[0].y<0.5;
#endif
    vec4 traced=vec4(0.0,0.0,0.0,0.0);
#ifdef NJIN_ADVANCED_MATERIALS
    if(u_njinMaterial.z>0.5) {traced=njinRtReflection(position);traced.rgb=linearLight?traced.rgb:njinDisplay(traced.rgb);}
#endif
    if(useEnvironment) result+=(njinEnvironmentLight(albedo,surface,n,v,coat,coatNormal,traced,gridLight)-albedo*ambient*(1.0-surface.x))*ao;
    else if(u_njinMaterial.z>0.5) result+=njinRtSpecular(albedo,surface,n,v,traced)*ao;
#ifdef NJIN_CLUSTERED_LIGHTS
    vec2 range=njinClusterRange(position);
    LOOP for(int i=0;i<1024;++i) {
      if(float(i)>=range.y) break;
      float index=njinClusterLightIndex(range.x+float(i));
      vec4 source=njinClusterLight(index,0),color=njinClusterLight(index,1),spot=njinClusterLight(index,2),cone=njinClusterLight(index,3);
#else
    for(int i=0;i<16;++i) {
      if(float(i)>=u_njinOutput.w) break;
      vec4 source=u_njinLocal[i*4],color=u_njinLocal[i*4+1],spot=u_njinLocal[i*4+2],cone=u_njinLocal[i*4+3];
#endif
      vec3 delta=source.xyz-position; float distance=length(delta);
      if(distance>=source.w) continue;
      vec3 direction=delta/max(distance,0.00001);
      float attenuation=1.0-distance/source.w; attenuation*=attenuation;
      if(cone.y>0.5) attenuation*=clamp((dot(-direction,normalize(spot.xyz))-spot.w)/max(cone.x-spot.w,0.0001),0.0,1.0);
      vec3 radiance=(linearLight?njinLinear(color.rgb):color.rgb)*color.a*attenuation;
      float localShadow=njinLocalShadowParameters(source,cone,position,geometric);
      result+=njinDirect(albedo,n,v,direction,radiance,surface,tangent.xyz,coat,coatNormal)*localShadow;
      if(crystal>0.0&&glitter.y>0.5&&dot(n,direction)>0.0)
        result+=radiance*(localShadow*crystal*njinCrystalMirror(crystalFace,direction,v));
    }
  }
#ifdef NJIN_ADVANCED_MATERIALS
  // Static direct diffuse already contains visibility. Replace all realtime
  // lighting (including IBL/RT), apply albedo once; emission stays below.
  if(u_njinAdvanced[2].w>0.5) result=albedo*staticDiffuse;
#endif
  if(u_njinMapUv[7].w>0.5) {
    vec3 texel=u_njinMapUv[7].w>1.5?vec3(1.0,1.0,1.0):NJIN_EMISSIVE_TEX(njinMapUv(3,uv0,uv1)).rgb;
    vec3 glow=(linearLight?njinLinear(texel):texel)*(linearLight?u_njinMapEmission.rgb:njinDisplay(u_njinMapEmission.rgb));
    result+=glow*u_njinMapEmission.a;
  }
  result+=(linearLight?njinLinear(u_njinEmission.rgb):u_njinEmission.rgb)*u_njinEmission.w;
  if(u_njinLight[0].w>0.5) {
    vec3 eye=u_njinLight[3].xyz-position;vec3 v=eye/max(length(eye),0.000001);
    float nv=max(dot(normalize(normal),v),0.0);
    vec3 rim=linearLight?njinLinear(u_njinSurfaceFx[2].rgb):u_njinSurfaceFx[2].rgb;
    result+=rim*u_njinSurfaceFx[2].a*pow(1.0-nv,3.0);
  }
  if(u_njinOutput.y>0.5) return result*(u_njinLight[0].w>0.5?u_njinOutput.x:1.0);
  if(linearLight) return njinTone(result*u_njinOutput.x);
  return result;
}

// Adapted from Njin modules/render3d_hdr.cpp: same 13-tap/Karis down,
// 9-tap tent up, and Lottes FXAA. HDR bloom has no brightness threshold.
vec3 njinPostAt(vec2 uv,vec2 o) { return max(NJIN_POST_COLOR(uv + o * u_njinPost.yz).rgb, njinSplat(0.0)); }
float njinPostWeight(vec3 c) { return 1.0 / (1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722))); }
vec3 njinPostDown(vec2 uv) {
  vec3 a = njinPostAt(uv,vec2(-2.0, 2.0)), b = njinPostAt(uv,vec2(0.0, 2.0)), c = njinPostAt(uv,vec2(2.0, 2.0));
  vec3 d = njinPostAt(uv,vec2(-2.0, 0.0)), e = njinPostAt(uv,vec2(0.0, 0.0)), f = njinPostAt(uv,vec2(2.0, 0.0));
  vec3 g = njinPostAt(uv,vec2(-2.0, -2.0)), h = njinPostAt(uv,vec2(0.0, -2.0)), i = njinPostAt(uv,vec2(2.0, -2.0));
  vec3 j = njinPostAt(uv,vec2(-1.0, 1.0)), k = njinPostAt(uv,vec2(1.0, 1.0)), l = njinPostAt(uv,vec2(-1.0, -1.0)), m = njinPostAt(uv,vec2(1.0, -1.0));
  vec3 o;
  if (u_njinPostExtra.y>0.5) {
    vec3 g0 = (j + k + l + m) * 0.25;
    vec3 g1 = (a + b + d + e) * 0.25, g2 = (b + c + e + f) * 0.25, g3 = (d + e + g + h) * 0.25, g4 = (e + f + h + i) * 0.25;
    float w0 = njinPostWeight(g0) * 0.5, w1 = njinPostWeight(g1) * 0.125, w2 = njinPostWeight(g2) * 0.125, w3 = njinPostWeight(g3) * 0.125,
          w4 = njinPostWeight(g4) * 0.125;
    o = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / max(w0 + w1 + w2 + w3 + w4, 1e-4);
  } else {
    o = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
  }
  return max(o,njinSplat(0.0));
}
vec3 njinPostUp(vec2 uv) {
  vec3 s = njinPostAt(uv,vec2(0.0,0.0)) * 4.0;
  s += (njinPostAt(uv,vec2(0.0, 1.0)) + njinPostAt(uv,vec2(-1.0, 0.0)) + njinPostAt(uv,vec2(1.0, 0.0)) + njinPostAt(uv,vec2(0.0, -1.0))) * 2.0;
  s += njinPostAt(uv,vec2(-1.0, 1.0)) + njinPostAt(uv,vec2(1.0, 1.0)) + njinPostAt(uv,vec2(-1.0, -1.0)) + njinPostAt(uv,vec2(1.0, -1.0));
  return s/16.0;
}
float njinPostLuma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
vec3 njinPostFxaa(vec2 uv) {
  vec2 texel=u_njinPost.yz;
  vec3 nw = NJIN_POST_COLOR( uv + vec2(-1.0, -1.0) * texel).rgb;
  vec3 ne = NJIN_POST_COLOR( uv + vec2(1.0, -1.0) * texel).rgb;
  vec3 sw = NJIN_POST_COLOR( uv + vec2(-1.0, 1.0) * texel).rgb;
  vec3 se = NJIN_POST_COLOR( uv + vec2(1.0, 1.0) * texel).rgb;
  vec3 m = NJIN_POST_COLOR( uv).rgb;
  float lnw = njinPostLuma(nw), lne = njinPostLuma(ne), lsw = njinPostLuma(sw), lse = njinPostLuma(se), lm = njinPostLuma(m);
  float lo = min(lm, min(min(lnw, lne), min(lsw, lse)));
  float hi = max(lm, max(max(lnw, lne), max(lsw, lse)));
  if (hi - lo < max(0.0312, hi * 0.125)) {
    return m;
  }
  vec2 dir = vec2(-((lnw + lne) - (lsw + lse)), (lnw + lsw) - (lne + lse));
  float reduce = max((lnw + lne + lsw + lse) * (0.25 * 0.125), 1.0 / 128.0);
  float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
  dir = clamp(dir * rcp, vec2(-8.0,-8.0), vec2(8.0,8.0)) * texel;
  vec3 a = 0.5 * (NJIN_POST_COLOR( uv + dir * (1.0 / 3.0 - 0.5)).rgb + NJIN_POST_COLOR( uv + dir * (2.0 / 3.0 - 0.5)).rgb);
  vec3 b = a * 0.5 + 0.25 * (NJIN_POST_COLOR( uv - dir * 0.5).rgb + NJIN_POST_COLOR( uv + dir * 0.5).rgb);
  float lb = njinPostLuma(b);
  return lb < lo || lb > hi ? a : b;
}
// View-space reconstruction from the common OpenGL projection convention.
// Hardware depth is sampled as [0,1] on every backend, including bgfx GL.
vec3 njinPostPosition(vec2 uv,float depth) {
  vec4 h=mul(u_njinPostInverse,vec4(uv.x*2.0-1.0,1.0-uv.y*2.0,depth*2.0-1.0,1.0));
  return h.xyz/max(h.w,0.000001);
}
vec3 njinPostSsao(vec2 uv) {
  vec2 pixel=u_njinPost.yz;
  uv=(floor(uv/pixel+vec2(0.001,0.001))+vec2(0.5,0.5))*pixel;
  float depth=NJIN_POST_COLOR(uv).r;
  if(depth>=0.999999) return njinSplat(1.0);
  vec3 p=njinPostPosition(uv,depth);
  vec2 lx=uv-vec2(pixel.x,0.0),rx=uv+vec2(pixel.x,0.0),uy=uv-vec2(0.0,pixel.y),dy=uv+vec2(0.0,pixel.y);
  vec3 pl=njinPostPosition(lx,NJIN_POST_COLOR(lx).r),pr=njinPostPosition(rx,NJIN_POST_COLOR(rx).r);
  vec3 pu=njinPostPosition(uy,NJIN_POST_COLOR(uy).r),pd=njinPostPosition(dy,NJIN_POST_COLOR(dy).r);
  vec3 dx=abs(pl.z-p.z)<abs(pr.z-p.z)?p-pl:pr-p;
  vec3 ddy=abs(pu.z-p.z)<abs(pd.z-p.z)?p-pu:pd-p;
  vec3 normal=cross(dx,ddy);normal/=max(length(normal),0.000001);
  if(dot(normal,-p)<0.0) normal=-normal;
  vec3 helper=abs(normal.y)<0.99?vec3(0.0,1.0,0.0):vec3(1.0,0.0,0.0);
  vec3 tangent=normalize(cross(helper,normal)),bitangent=cross(normal,tangent);
  float angle=fract(sin(dot(floor(uv/pixel),vec2(12.9898,78.233)))*43758.5453)*6.2831853;
  float occluded=0.0,radius=u_njinPostExtra.z,samples=u_njinPostExtra.w;
  for(int i=0;i<32;++i) {
    if(float(i)>=samples) break;
    float t=(float(i)+0.5)/samples,theta=angle+float(i)*2.39996323;
    float z=t,r=sqrt(max(0.0,1.0-z*z));
    vec3 hemisphere=tangent*(cos(theta)*r)+bitangent*(sin(theta)*r)+normal*z;
    vec3 test=p+hemisphere*radius*(0.2+0.8*t*t);
    vec4 projected=mul(u_njinPostProjection,vec4(test,1.0));
    if(projected.w<=0.0) continue;
    vec2 coord=vec2(projected.x/projected.w*0.5+0.5,0.5-projected.y/projected.w*0.5);
    if(coord.x<0.0||coord.x>1.0||coord.y<0.0||coord.y>1.0) continue;
    float sampleDepth=NJIN_POST_COLOR(coord).r;if(sampleDepth>=0.999999) continue;
    float actual=njinPostPosition(coord,sampleDepth).z;
    float range=smoothstep(0.0,1.0,radius/max(abs(actual-p.z),0.000001));
    occluded+=(actual>=test.z+radius*0.025?1.0:0.0)*range;
  }
  return njinSplat(clamp(1.0-u_njinPost.w*occluded/samples,0.0,1.0));
}
vec3 njinPostAoBlur(vec2 uv) {
  float centerDepth=NJIN_POST_AUX(uv).r;
  if(centerDepth>=0.999999) return njinSplat(1.0);
  float center=njinPostPosition(uv,centerDepth).z,sum=0.0,weights=0.0;
  vec2 axis=u_njinPost.x<6.5?vec2(u_njinPost.y,0.0):vec2(0.0,u_njinPost.z);
  for(int i=-2;i<=2;++i) {
    vec2 at=uv+axis*float(i);float sampleDepth=NJIN_POST_AUX(at).r;
    float z=njinPostPosition(at,sampleDepth).z;
    float spatial=exp(-float(i*i)*0.5),range=exp(-abs(z-center)*4.0/max(u_njinPostExtra.z,0.000001));
    float weight=sampleDepth>=0.999999?0.0:spatial*range;
    sum+=NJIN_POST_COLOR(at).r*weight;weights+=weight;
  }
  return njinSplat(sum/max(weights,0.000001));
}
// Port of Njin's existing growing-step SSR / six-step binary refinement.
// The normal/roughness mask uses the same mapped surface as the forward pass.
vec2 njinSsrProject(vec3 p) {
  vec4 c=mul(u_njinPostProjection,vec4(p,1.0));
  return vec2(c.x/c.w*0.5+0.5,0.5-c.y/c.w*0.5);
}
vec3 njinPostSsr(vec2 uv) {
  vec3 base=NJIN_POST_COLOR(uv).rgb;
  vec4 mask=NJIN_POST_SECOND(uv);
  float depth=NJIN_POST_AUX(uv).r;
  if(mask.a<=0.0||depth>=0.999999||mask.b>=0.95) return base;
  vec2 oct=mask.xy*2.0-vec2(1.0,1.0);
  vec3 n=vec3(oct,1.0-abs(oct.x)-abs(oct.y));
  if(n.z<0.0) n.xy=(vec2(1.0,1.0)-abs(n.yx))*vec2(n.x>=0.0?1.0:-1.0,n.y>=0.0?1.0:-1.0);
  n=normalize(n);
  vec3 p=njinPostPosition(uv,depth);
  // Orthographic rays are parallel; perspective rays start at the eye.
  vec3 incident=abs(u_njinPostProjection[3][3])>0.5?vec3(0.0,0.0,-1.0):normalize(p);
  if(dot(n,-incident)<0.0) n=-n;
  vec3 ray=reflect(incident,n);
  float distance=u_njinPostExtra.z,steps=u_njinPostExtra.w;
  float bias=max(0.002,min(0.025,u_njinPostConfig.x*0.05));
  p+=n*bias;
  float previous=0.0,t=0.0,found=0.0;
  vec2 hit=uv;
  for(int i=0;i<128;++i) {
    if(float(i)>=steps) break;
    float f=(float(i)+1.0)/steps;
    t=distance*f*f;
    vec3 samplePosition=p+ray*t;
    if(samplePosition.z>=-0.001) break;
    vec2 at=njinSsrProject(samplePosition);
    if(at.x<0.0||at.x>1.0||at.y<0.0||at.y>1.0) break;
    float sd=NJIN_POST_AUX(at).r;
    float delta=njinPostPosition(at,sd).z-samplePosition.z;
    if(sd<0.999999&&delta>0.0&&delta<max(u_njinPostConfig.x,t-previous)) {
      float a=previous,b=t;
      for(int k=0;k<6;++k) {
        float mid=(a+b)*0.5;vec3 q=p+ray*mid;vec2 coord=njinSsrProject(q);
        float actual=NJIN_POST_AUX(coord).r;
        if(actual<0.999999&&njinPostPosition(coord,actual).z>q.z) b=mid;else a=mid;
      }
      hit=njinSsrProject(p+ray*b);
      float hd=NJIN_POST_AUX(hit).r;
      // Refine again against thickness; do not reflect a distant wall behind a thin edge.
      float error=njinPostPosition(hit,hd).z-(p+ray*b).z;
      if(hd<0.999999&&error>=0.0&&error<=u_njinPostConfig.x) {
        vec2 edge=smoothstep(vec2(0.0,0.0),vec2(0.08,0.08),hit)*(vec2(1.0,1.0)-smoothstep(vec2(0.92,0.92),vec2(1.0,1.0),hit));
        found=edge.x*edge.y*(1.0-smoothstep(distance*0.75,distance,b));
      }
      break;
    }
    previous=t;
  }
  vec3 reflected=u_njinPostSky.rgb;
  if(found>0.0) {
    // Spatial roughness approximation without claiming a mip/temporal SSR filter.
    vec2 blur=u_njinPost.yz*mask.b*mask.b*12.0;
    vec3 color=NJIN_POST_COLOR(hit).rgb*0.5;
    color+=(NJIN_POST_COLOR(hit+vec2(blur.x,0.0)).rgb+NJIN_POST_COLOR(hit-vec2(blur.x,0.0)).rgb+NJIN_POST_COLOR(hit+vec2(0.0,blur.y)).rgb+NJIN_POST_COLOR(hit-vec2(0.0,blur.y)).rgb)*0.125;
    reflected=mix(reflected,color,found);
  }
  float fresnel=mask.a+(1.0-mask.a)*pow(1.0-max(dot(n,-incident),0.0),5.0);
  float weight=u_njinPost.w*fresnel*mix(u_njinPostSky.a,1.0,found)*(1.0-smoothstep(0.6,0.95,mask.b));
  return mix(base,reflected,clamp(weight,0.0,1.0));
}
vec3 njinDepthNormal(vec2 uv,vec3 p) {
  vec2 pixel=u_njinPost.yz;
  vec2 lx=uv-vec2(pixel.x,0.0),rx=uv+vec2(pixel.x,0.0),uy=uv-vec2(0.0,pixel.y),dy=uv+vec2(0.0,pixel.y);
  vec3 l=njinPostPosition(lx,NJIN_POST_AUX(lx).r),r=njinPostPosition(rx,NJIN_POST_AUX(rx).r);
  vec3 u=njinPostPosition(uy,NJIN_POST_AUX(uy).r),d=njinPostPosition(dy,NJIN_POST_AUX(dy).r);
  vec3 dx=abs(l.z-p.z)<abs(r.z-p.z)?p-l:r-p;
  vec3 ddy=abs(u.z-p.z)<abs(d.z-p.z)?p-u:d-p;
  vec3 n=cross(dx,ddy);n/=max(length(n),0.000001);
  return dot(n,-p)<0.0?-n:n;
}
// Port of Njin's depth/crease outlines: draw on the nearer side, not the sky.
vec3 njinPostOutline(vec2 uv) {
  vec3 color=NJIN_POST_COLOR(uv).rgb;
  vec2 pixel=u_njinPost.yz;
  uv=(floor(uv/pixel+vec2(0.001,0.001))+vec2(0.5,0.5))*pixel;
  float depth=NJIN_POST_AUX(uv).r;
  if(depth>=0.999999) return color;
  vec3 p=njinPostPosition(uv,depth),n=njinDepthNormal(uv,p);
  float distance=length(p),width=u_njinPostExtra.z,edge=0.0,gap=0.02+0.004*distance*width;
  for(int i=0;i<4;++i) {
    vec2 direction=i==0?vec2(1.0,0.0):i==1?vec2(0.0,1.0):i==2?vec2(-1.0,0.0):vec2(0.0,-1.0);
    vec2 at=uv+pixel*width*direction;
    float sd=NJIN_POST_AUX(at).r;
    if(sd>=0.999999) {edge=1.0;break;}
    vec3 q=njinPostPosition(at,sd);
    if(dot(p-q,n)>gap) {edge=1.0;break;}
    if(i<2&&u_njinPostConfig.y>=-1.0&&dot(n,njinDepthNormal(at,q))<u_njinPostConfig.y) edge=1.0;
  }
  if(u_njinPostConfig.z>0.0) edge*=1.0-smoothstep(u_njinPostConfig.z*0.5,u_njinPostConfig.z,distance);
  return mix(color,u_njinPostSky.rgb,edge*u_njinPost.w*u_njinPostSky.a);
}
// Njin legacy screen-space sun occlusion, 48-tap radial scattering and lens ghosts.
vec3 njinPostShaftMask(vec2 uv) {
  float sky=NJIN_POST_COLOR(uv).r>=0.999999?1.0:0.0;
  float distance=length((uv-u_njinPostSun.xy)*vec2(u_njinPostSun.w,1.0));
  return njinSplat(sky*exp(-distance*6.0));
}
vec3 njinPostShaftBlur(vec2 uv) {
  vec2 delta=(uv-u_njinPostSun.xy)*u_njinPostExtra.z/48.0;
  float fade=1.0;vec3 sum=njinSplat(0.0);
  for(int i=0;i<48;++i) {sum+=NJIN_POST_COLOR(uv).rgb*fade;fade*=0.965;uv-=delta;}
  return sum*(1.2/48.0);
}
vec3 njinPostFlareVisibility() {
  float sky=0.0;
  for(int i=0;i<25;++i) {
    float radius=0.012*sqrt((float(i)+0.5)/25.0),angle=float(i)*2.3999632;
    vec2 at=u_njinPostSun.xy+vec2(cos(angle)/u_njinPostSun.w,sin(angle))*radius;
    if(at.x>=0.0&&at.y>=0.0&&at.x<=1.0&&at.y<=1.0&&NJIN_POST_COLOR(at).r>=0.999999) sky+=1.0;
  }
  return njinSplat(sky/25.0*u_njinPostSun.z);
}
vec3 njinPostFlare(vec2 uv) {
  float visibility=NJIN_POST_AUX(vec2(0.5,0.5)).r;
  vec2 sun=u_njinPostSun.xy,k=vec2(u_njinPostSun.w,1.0),toward=vec2(0.5,0.5)-sun;
  vec3 color=njinSplat(0.0);
  for(int i=0;i<5;++i) {
    float at=0.45,radius=0.06;vec3 tint=vec3(1.0,0.6,0.3);
    if(i==1) {at=0.8;radius=0.03;tint=vec3(0.5,0.9,0.6);}
    if(i==2) {at=1.25;radius=0.09;tint=vec3(0.4,0.6,1.0);}
    if(i==3) {at=1.6;radius=0.045;tint=vec3(1.0,0.8,0.5);}
    if(i==4) {at=2.1;radius=0.12;tint=vec3(0.6,0.5,1.0);}
    float distance=length((uv-(sun+toward*at))*k);
    color+=tint*(1.0-smoothstep(radius*0.6,radius,distance))*0.2;
  }
  vec2 fromCenter=(uv-vec2(0.5,0.5))*k;
  float ring=exp(-pow((length(fromCenter)-0.42)/0.025,2.0));
  float side=max(dot(normalize(fromCenter+vec2(0.00001,0.00001)),normalize((sun-vec2(0.5,0.5))*k+vec2(0.00001,0.00001))),0.0);
  color+=vec3(1.0,0.85,0.7)*ring*side*0.25*u_njinPostConfig.w;
  color+=vec3(1.0,0.9,0.75)*exp(-length((uv-sun)*k)*14.0)*0.5;
  return NJIN_POST_COLOR(uv).rgb+color*u_njinPostSky.rgb*u_njinPost.w*visibility;
}
// McGuire/Bavoil weighting, matching Njin's existing legacy OIT shader.
vec4 njinOitColor(vec4 color,vec3 position) {
  vec4 clip=mul(u_njinTransparencyVP,vec4(position,1.0));
  vec3 ndc=clip.xyz/max(clip.w,0.000001);
  vec2 uv=vec2(ndc.x*0.5+0.5,0.5-ndc.y*0.5);
  float depth=ndc.z*0.5+0.5;
  if(depth>NJIN_OPAQUE_DEPTH(uv).r+0.000001) discard;
  float alpha=clamp(color.a,0.0,1.0);
  if(u_njinTransparency.x>1.5) return vec4(alpha,alpha,alpha,alpha);
  float weight=clamp(pow(min(1.0,alpha*10.0)+0.01,3.0)*1e8*pow(1.0-depth*0.9,3.0),0.01,3000.0);
  // A common scale cancels in the quotient, while retaining HDR headroom.
  return vec4(color.rgb*alpha,alpha)*(weight/3000.0);
}
vec3 to_ycocg(vec3 c) {
  return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}
vec3 from_ycocg(vec3 c) {
  float t = c.x - c.z;
  return vec3(t + c.y, c.x + c.z, t - c.y);
}
// The history read with a Catmull-Rom filter (five bilinear taps): plain
// bilinear blurs a little more each frame the camera moves.
vec3 history_at(vec2 uv) {
  vec2 size = 1.0 / u_njinPost.yz;
  vec2 pos = uv * size;
  vec2 c = floor(pos - 0.5) + 0.5;
  vec2 f = pos - c;
  vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
  vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
  vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
  vec2 w3 = f * f * (-0.5 + 0.5 * f);
  vec2 w12 = w1 + w2;
  vec2 t0 = (c - 1.0) * u_njinPost.yz, t3 = (c + 2.0) * u_njinPost.yz, t12 = (c + w2 / w12) * u_njinPost.yz;
  vec3 r = NJIN_POST_AUX(vec2(t12.x, t0.y)).rgb * w12.x * w0.y +
           NJIN_POST_AUX(vec2(t0.x, t12.y)).rgb * w0.x * w12.y +
           NJIN_POST_AUX(t12).rgb * w12.x * w12.y +
           NJIN_POST_AUX(vec2(t3.x, t12.y)).rgb * w3.x * w12.y +
           NJIN_POST_AUX(vec2(t12.x, t3.y)).rgb * w12.x * w3.y;
  float wsum = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
  return max(r / wsum, njinSplat(0.0));
}
vec3 clip_box(vec3 lo, vec3 hi, vec3 h) {
  vec3 c = 0.5 * (hi + lo);
  vec3 e = 0.5 * (hi - lo) + 1e-4;
  vec3 v = h - c;
  vec3 a = abs(v / e);
  float m = max(a.x, max(a.y, a.z));
  return m > 1.0 ? c + v / m : h;
}

// Temporal helpers adapt Njin's existing post3d.cpp history filter and YCoCg clipping.
vec3 njinMotionAt(vec2 uv) {
  vec4 velocity=NJIN_VELOCITY(uv);
  if(velocity.a>0.5) return velocity.xyz;
  float depth=NJIN_OPAQUE_DEPTH(uv).r;
  vec3 p=njinPostPosition(uv,depth);
  vec4 clip=mul(u_njinPreviousClip,vec4(p,1.0));
  vec4 view=mul(u_njinPreviousView,vec4(p,1.0));
  if(clip.w<=0.00001) return vec3(0.0,0.0,-1.0);
  vec2 previous=clip.xy/clip.w*vec2(0.5,-0.5)+vec2(0.5,0.5);
  // Return unjittered motion; TAA adds the separate jitter delta below.
  return vec3(uv-previous+u_njinTemporal.xy,-view.z);
}
vec3 njinPostTaa(vec2 uv) {
  vec3 current=NJIN_POST_COLOR(uv).rgb;
  if(u_njinPostExtra.y>0.5) return current;
  vec3 motion=njinMotionAt(uv);vec2 historyUv=uv-motion.xy+u_njinTemporal.xy;
  if(motion.z<=0.0||historyUv.x<0.0||historyUv.y<0.0||historyUv.x>1.0||historyUv.y>1.0) return current;
  float lowDepth=1e30,highDepth=0.0;
  for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
    float z=NJIN_POST_SECOND(historyUv+vec2(float(x),float(y))*u_njinPost.yz).r;
    lowDepth=min(lowDepth,z);highDepth=max(highDepth,z);
  }
  if(motion.z<lowDepth*.97-.05||motion.z>highDepth*1.03+.05) return current;
  vec3 mean=njinSplat(0.0),square=njinSplat(0.0),neighbors=njinSplat(0.0),low=current,high=current;
  for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
    vec3 c=NJIN_POST_COLOR(uv+vec2(float(x),float(y))*u_njinPost.yz).rgb;
    low=min(low,c);high=max(high,c);vec3 yc=to_ycocg(c);mean+=yc/9.0;square+=yc*yc/9.0;
    if(abs(x)+abs(y)==1) neighbors+=c;
  }
  vec3 sigma=sqrt(max(square-mean*mean,njinSplat(0.0)));
  float speed=length(motion.xy/u_njinPost.yz);
  float gamma=mix(1.25,.75,clamp(speed/4.0,0.0,1.0));
  vec3 history=from_ycocg(clip_box(mean-gamma*sigma,mean+gamma*sigma,to_ycocg(history_at(historyUv))));
  float feedback=mix(.92,.75,clamp(speed/12.0,0.0,1.0));
  vec3 resolved=mix(current,history,feedback);
  vec3 sharpened=resolved+(current*4.0-neighbors)*u_njinTemporal.w*.25;
  return clamp(sharpened,max(low,njinSplat(0.0)),max(high,njinSplat(0.0)));
}
vec3 njinPostMotionBlur(vec2 uv) {
  vec3 current=NJIN_POST_COLOR(uv).rgb;
  if(u_njinTemporal.z<.5) return current;
  vec3 own=njinMotionAt(uv);vec2 velocity=own.xy;
  // Spread foreground motion into nearby uncovered pixels, without changing UI.
  for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
    vec2 at=clamp(uv+vec2(float(x),float(y))*u_njinPost.yz*8.0,0.0,1.0);
    vec3 candidate=njinMotionAt(at);
    if(candidate.z>0.0&&candidate.z<=own.z+.05&&dot(candidate.xy,candidate.xy)>dot(velocity,velocity)) velocity=candidate.xy;
  }
  float speed=length(velocity);if(speed<.25*min(u_njinPost.y,u_njinPost.z)) return current;
  velocity*=min(1.0,.06/max(speed,.000001))*u_njinPost.w;
  vec3 sum=njinSplat(0.0);float weight=0.0;
  for(int i=0;i<32;++i) {
    if(float(i)>=u_njinPostExtra.w) break;
    float offset=float(i)/max(u_njinPostExtra.w-1.0,1.0)-.5;
    vec2 at=clamp(uv+velocity*offset,0.0,1.0);
    float w=1.0-abs(offset);sum+=NJIN_POST_AUX(at).rgb*w;weight+=w;
  }
  return sum/max(weight,.00001);
}

#ifdef NJIN_BGFX_EXTENSIONS
#include "njin_legacy_post.sh"
#endif
#ifdef NJIN_VOLUME_ONLY
#include "njin_volume.sh"
#endif
vec3 njinPostColor(vec2 uv) {
#ifdef NJIN_BGFX_EXTENSIONS

  // Modes 31..33 run in the specialized 2D lighting pipeline.
  if(u_njinPost.x>30.5) return NJIN_POST_COLOR(uv).rgb;
  if(u_njinPost.x>26.5) {
    if(u_njinPost.x<27.5) return njinLegacyPost(uv);
    if(u_njinPost.x<28.5) return njinLegacyBlur(uv);
    if(u_njinPost.x<29.5) {vec3 c=NJIN_POST_COLOR(uv).rgb;float peak=max(c.r,max(c.g,c.b));return c*max(peak-u_njinPost.w,0.0)/max(peak,0.0001);}
    return njinLegacyDof(uv);
  }
#endif
  if(u_njinPost.x>25.5) return njinPostMotionBlur(uv);
  if(u_njinPost.x>24.5) return vec3(-njinPostPosition(uv,NJIN_POST_COLOR(uv).r).z,0.0,0.0);
  if(u_njinPost.x>23.5) return njinPostTaa(uv);
  if(u_njinPost.x>22.5) return NJIN_POST_COLOR(uv).rgb;
  if(u_njinPost.x<1.5) return njinPostDown(uv);
  if(u_njinPost.x<2.5) return njinPostUp(uv);
  if(u_njinPost.x<3.5) {
    vec3 color=NJIN_POST_COLOR(uv).rgb;
    if(u_njinPost.w>0.0) color=mix(color,NJIN_POST_AUX(uv).rgb/u_njinPostExtra.x,u_njinPost.w);
    if(u_njinPostExtra.y>0.5) color*=NJIN_POST_SECOND(vec2(0.5,0.5)).r;
    return njinTone(max(color,njinSplat(0.0)));
  }
  if(u_njinPost.x<4.5) return njinPostFxaa(uv);
  if(u_njinPost.x<5.5) return njinPostSsao(uv);
  if(u_njinPost.x<7.5) return njinPostAoBlur(uv);
  if(u_njinPost.x<8.5) return NJIN_POST_COLOR(uv).rgb*NJIN_POST_AUX(uv).rgb;
  if(u_njinPost.x<9.5) return njinPostSsr(uv);
  if(u_njinPost.x<10.5) return njinPostOutline(uv);
  if(u_njinPost.x<11.5) return njinPostShaftMask(uv);
  if(u_njinPost.x<12.5) return njinPostShaftBlur(uv);
  if(u_njinPost.x<13.5) return NJIN_POST_COLOR(uv).rgb+NJIN_POST_AUX(uv).rgb*u_njinPostSky.rgb*u_njinPost.w;
  if(u_njinPost.x<14.5) return njinPostFlareVisibility();
  if(u_njinPost.x<18.5) return njinPostFlare(uv);
  if(u_njinPost.x<19.5) {
    float luminance=max(dot(max(NJIN_POST_COLOR(uv).rgb,njinSplat(0.0)),vec3(0.2126,0.7152,0.0722)),0.0001);
    vec2 delta=uv-vec2(0.5,0.5);float weight=exp(-dot(delta,delta)*6.0);
    return vec3(log2(luminance)*weight,weight,0.0);
  }
  if(u_njinPost.x<20.5) {
    vec2 halfPixel=u_njinPost.yz*0.5;
    return (NJIN_POST_COLOR(uv+halfPixel).rgb+NJIN_POST_COLOR(uv-halfPixel).rgb+NJIN_POST_COLOR(uv+vec2(halfPixel.x,-halfPixel.y)).rgb+NJIN_POST_COLOR(uv+vec2(-halfPixel.x,halfPixel.y)).rgb)*0.25;
  }
  if(u_njinPost.x>21.5) {
    vec4 sum=NJIN_POST_AUX(uv);
    float alpha=clamp(1.0-NJIN_POST_SECOND(uv).r,0.0,1.0);
    return mix(NJIN_POST_COLOR(uv).rgb,sum.rgb/max(sum.a,0.00000001),alpha);
  }
  vec2 sum=NJIN_POST_COLOR(vec2(0.5,0.5)).rg;
  float luminance=exp2(clamp(sum.x/max(sum.y,0.000001),-20.0,20.0));
  float target=clamp(u_njinPostSky.x/max(luminance,0.00001),u_njinPostSky.y,u_njinPostSky.z);
  float adapted=target;
  if(u_njinPostExtra.y<0.5) adapted=exp2(mix(log2(max(NJIN_POST_AUX(vec2(0.5,0.5)).r,0.000001)),log2(max(target,0.000001)),u_njinPostSky.w));
  return vec3(clamp(adapted,u_njinPostSky.y,u_njinPostSky.z),0.0,0.0);
}
