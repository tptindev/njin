float njinCross2(vec2 a,vec2 b) {return a.x*b.y-a.y*b.x;}
vec2 njinOccluderUv(vec2 world) {
  vec4 camera=u_njinLegacyPost[5],r=u_njinLegacyPost[6],region=u_njinLegacyPost[8];
  vec2 p=world-camera.xy;
  vec2 screen=vec2(r.x*p.x-r.y*p.y,r.y*p.x+r.x*p.y)*r.z+camera.zw;
  return (screen+region.xy)/region.zw;
}
float njinPixelVisibility(vec2 receiver,vec2 target) {
  if(u_njinMapUv[5].w<0.5||u_njinLegacyPost[9].w<0.5) return 1.0;
  vec2 start=njinOccluderUv(target),finish=njinOccluderUv(receiver);
  float texels=length((finish-start)*u_njinLegacyPost[8].zw)*u_njinLegacyPost[9].y;
  float steps=clamp(ceil(texels),1.0,u_njinLegacyPost[9].z);
  bool initial=true,blocker=false;
  LOOP for(int i=0;i<4096;++i) {
    if(float(i)>=steps) break;
    vec2 uv=mix(start,finish,(float(i)+0.5)/steps);
    bool occupied=false;
    if(all(greaterThanEqual(uv,vec2(0.0,0.0)))&&all(lessThanEqual(uv,vec2(1.0,1.0)))) occupied=NJIN_AO_TEX(uv).a>=u_njinLegacyPost[9].x;
    if(initial) {if(!occupied) initial=false;continue;}
    if(occupied) blocker=true;
    else if(blocker) return 0.0;
  }
  // A receiver inside the first encountered caster is lit by that light.
  return 1.0;
}
vec4 njinEdgeEntry(float index) {
  vec2 extent=u_njinPostExtra.zw;
  return texture2DLod(s_njinPostAux,(vec2(mod(index,extent.x),floor(index/extent.x))+vec2(0.5,0.5))/extent,0.0);
}
vec2 njinEdgeRange(vec2 receiver,vec2 center,bool sun) {
  vec4 strip=njinEdgeEntry(0.0);
  vec2 delta=receiver-center;
  float angle=atan2(delta.y,delta.x);
  float bin=sun?(dot(receiver,strip.xy)-strip.z)*strip.w:(angle+3.14159265359)*(32.0/6.28318530718);
  vec2 range=njinEdgeEntry(1.0+clamp(floor(bin),0.0,31.0)).xy;
  float size=u_njinPostExtra.z*u_njinPostExtra.w;
  range.x=clamp(range.x,33.0,size);
  range.y=clamp(range.y,0.0,min(size-range.x,4096.0));
  return range;
}
float njinLightVisibility(vec2 receiver,vec2 target,vec2 edges,bool bucketed) {
  vec2 ray=target-receiver;
  LOOP for(int i=0;i<int(edges.y);++i) {
    vec4 edge;
    if(bucketed) edge=njinEdgeEntry(edges.x+float(i));
    else edge=u_njinLightEdges[i];
    vec2 e=edge.zw-edge.xy;
    float den=njinCross2(ray,e);
    if(abs(den)<0.00001||dot(ray,vec2(e.y,-e.x))>=0.0) continue;
    vec2 q=edge.xy-receiver;float t=njinCross2(q,e)/den,u=njinCross2(q,ray)/den;
    if(t>0.001&&t<0.999&&u>=0.0&&u<=1.0) return 0.0;
  }
  return njinPixelVisibility(receiver,target);
}
vec3 njinLight2D(vec2 uv) {
  vec3 base=NJIN_POST_COLOR(uv).rgb;
  if(u_njinPost.x<31.5) {
    vec3 m=u_njinMapUv[3].w>0.5?NJIN_MR_TEX(uv).rgb:vec3(0.0,0.8,1.0);
    vec3 glow=u_njinMapUv[7].w>0.5?NJIN_EMISSIVE_TEX(uv).rgb:vec3(0.0,0.0,0.0);
    return njinLinear(base)*njinLinear(u_njinLegacyPost[0].rgb)*m.b*(1.0-m.r)+glow;
  }
  if(u_njinPost.x>32.5) {
    vec3 c=base*u_njinLegacyPost[0].w;float mode=u_njinLegacyPost[7].w;
    if(mode>1.5) c=clamp((c*(2.51*c+0.03))/(c*(2.43*c+0.59)+0.14),0.0,1.0);
    else if(mode>0.5) c=c/(njinSplat(1.0)+c);
    else {vec3 over=max(c-njinSplat(0.6),njinSplat(0.0));c=min(c,njinSplat(0.6))+0.4*(njinSplat(1.0)-exp(-over/0.4));}
    return pow(max(c,njinSplat(0.0)),njinSplat(1.0/2.2));
  }
  vec4 light=u_njinLegacyPost[1],dir=u_njinLegacyPost[3],kind=u_njinLegacyPost[4];
  vec4 camera=u_njinLegacyPost[5],rotation=u_njinLegacyPost[6];
  vec2 p=(uv*u_njinLegacyPost[7].xy-camera.zw)/rotation.z;
  p=vec2(rotation.x*p.x+rotation.y*p.y,-rotation.y*p.x+rotation.x*p.y)+camera.xy;
  float attenuation=1.0,cone=1.0;vec2 target=light.xy;vec3 l;
  if(kind.x>1.5) {target=p-dir.xy*u_njinLegacyPost[7].z;l=vec3(-dir.xy*cos(kind.w),sin(kind.w));}
  else {
    vec2 delta=light.xy-p;float distance=length(delta),x=distance/light.z;
    if(x>=1.0) return njinSplat(0.0);
    float window=pow(1.0-pow(x,4.0),2.0);
    if(kind.y<0.5) attenuation=window/(1.0+pow(distance/max(kind.z,1.0),2.0));
    else if(kind.y<1.5) attenuation=1.0-x;
    else if(kind.y<2.5) attenuation=pow(1.0-x*x,2.0);
    else attenuation=1.0-smoothstep(0.9,1.0,x);
    if(kind.x>0.5) cone=smoothstep(dir.z,dir.w,dot(-delta/max(distance,0.0001),dir.xy));
    l=normalize(vec3(delta,light.w));
  }
  float visibility=1.0;
  if(rotation.w!=0.0||(u_njinMapUv[5].w>0.5&&u_njinLegacyPost[9].w>0.5)) {
    vec2 delta=target-p,across=vec2(-delta.y,delta.x)/max(length(delta),0.0001);
    float radius=kind.x>1.5?kind.z*u_njinLegacyPost[7].z/600.0:kind.z;
    bool bucketed=rotation.w<0.0;
    vec2 edges=vec2(0.0,clamp(rotation.w,0.0,64.0));
    if(bucketed) edges=njinEdgeRange(p,light.xy,kind.x>1.5);
    visibility=0.0;
    LOOP for(int k=0;k<8;++k) visibility+=njinLightVisibility(p,target+across*((float(k)+0.5)/4.0-1.0)*radius,edges,bucketed);
    visibility*=0.125;
  }
  vec3 n=vec3(0.0,0.0,1.0),v=n;
  if(u_njinMapUv[1].w>0.5) {n=NJIN_NORMAL_TEX(uv).xyz*2.0-1.0;n.y=-n.y;n=normalize(n);}
  vec3 material=u_njinMapUv[3].w>0.5?NJIN_MR_TEX(uv).rgb:vec3(0.0,0.8,1.0);
  vec3 h=normalize(l+v);float nl=max(dot(n,l),0.0),nv=max(dot(n,v),0.0),nh=max(dot(n,h),0.0),vh=max(dot(v,h),0.0);
  float rough=max(material.g,0.045),a=rough*rough,a2=a*a,q=nh*nh*(a2-1.0)+1.0;
  float distribution=a2/(3.14159265359*q*q),visibilityBrdf=0.5/max(nl*sqrt(nv*nv*(1.0-a2)+a2)+nv*sqrt(nl*nl*(1.0-a2)+a2),0.00001);
  vec3 albedo=njinLinear(base),f0=mix(njinSplat(0.04),albedo,material.r),f=f0+(njinSplat(1.0)-f0)*pow(1.0-vh,5.0);
  vec3 result=albedo*(njinSplat(1.0)-f)*(1.0-material.r)+f*distribution*visibilityBrdf*3.14159265359;
  return result*njinLinear(u_njinLegacyPost[2].rgb)*u_njinLegacyPost[2].w*nl*attenuation*cone*visibility;
}
