float njinVolumeProjected(int view,vec3 pos) {
  vec4 clip=njinShadowProject(view,pos);if(clip.w<=0.0) return 1.0;
  vec3 uvz=clip.xyz/clip.w*vec3(0.5,-0.5,0.5)+vec3(0.5,0.5,0.5);
  vec4 rect=u_njinShadowRect[view];uvz.xy=mix(rect.xy,rect.zw,uvz.xy);
  return njinShadowFilter(view,uvz,vec3(0.0,0.0,0.0),vec3(0.0,0.0,0.0));
}
float njinVolumeSun(vec3 pos) {
  if(u_njinShadowInfo.w<0.5||u_njinShadowForward.w<0.5) return 1.0;
  float distance=dot(pos-u_njinLight[3].xyz,u_njinShadowForward.xyz);int view=0;
  for(int k=0;k<4;++k) {if(float(k)>=u_njinShadowForward.w) break;view=k;if(distance<=u_njinShadowSplits[k]) break;}
  if(distance>u_njinShadowSplits[view]) return u_njinLegacyPost[5].w>0.5?0.0:1.0;
  return njinVolumeProjected(view,pos);
}
float njinVolumeLocalParameters(vec4 source,vec4 cone,vec3 pos) {
  if(u_njinShadowInfo.w<0.5) return 1.0;
  int base=int(cone.z+0.5)-1;if(base<0) return 1.0;
  if(cone.y>0.5) return njinVolumeProjected(base,pos);
  vec3 delta=pos-source.xyz,a=abs(delta);float major;vec2 uv;int face;
  if(a.x>=a.y&&a.x>=a.z) {major=a.x;face=delta.x>=0.0?0:1;uv=vec2(delta.x>=0.0?-delta.z:delta.z,-delta.y);}
  else if(a.y>=a.z) {major=a.y;face=delta.y>=0.0?2:3;uv=vec2(delta.x,delta.y>=0.0?delta.z:-delta.z);}
  else {major=a.z;face=delta.z>=0.0?4:5;uv=vec2(delta.z>=0.0?delta.x:-delta.x,-delta.y);}
  float farPlane=source.w,nearPlane=min(0.05,farPlane*0.01);if(major<nearPlane) return 1.0;
  uv=uv/major*0.5+vec2(0.5,0.5);uv.y=1.0-uv.y;
  float depth=farPlane/(farPlane-nearPlane)-nearPlane*farPlane/((farPlane-nearPlane)*major);
  vec4 rect=u_njinShadowRect[base+face];
  return njinShadowFilter(base+face,vec3(mix(rect.xy,rect.zw,uv),depth),vec3(0.0,0.0,0.0),vec3(0.0,0.0,0.0));
}
float njinVolumeNoise(vec3 p) {
  vec3 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
  vec3 a=fract((i+vec3(0.0,0.0,0.0))*0.1031);a+=dot(a,a.yzx+33.33);float n000=fract((a.x+a.y)*a.z);
  a=fract((i+vec3(1.0,0.0,0.0))*0.1031);a+=dot(a,a.yzx+33.33);float n100=fract((a.x+a.y)*a.z);
  a=fract((i+vec3(0.0,1.0,0.0))*0.1031);a+=dot(a,a.yzx+33.33);float n010=fract((a.x+a.y)*a.z);
  a=fract((i+vec3(1.0,1.0,0.0))*0.1031);a+=dot(a,a.yzx+33.33);float n110=fract((a.x+a.y)*a.z);
  a=fract((i+vec3(0.0,0.0,1.0))*0.1031);a+=dot(a,a.yzx+33.33);float n001=fract((a.x+a.y)*a.z);
  a=fract((i+vec3(1.0,0.0,1.0))*0.1031);a+=dot(a,a.yzx+33.33);float n101=fract((a.x+a.y)*a.z);
  a=fract((i+vec3(0.0,1.0,1.0))*0.1031);a+=dot(a,a.yzx+33.33);float n011=fract((a.x+a.y)*a.z);
  a=fract((i+vec3(1.0,1.0,1.0))*0.1031);a+=dot(a,a.yzx+33.33);float n111=fract((a.x+a.y)*a.z);
  return mix(mix(mix(n000,n100,f.x),mix(n010,n110,f.x),f.y),mix(mix(n001,n101,f.x),mix(n011,n111,f.x),f.y),f.z);
}
float njinVolumePhase(float cosine,float g) {return (1.0-g*g)/max(pow(1.0+g*g-2.0*g*cosine,1.5),0.001);}
vec3 njinVolume(vec2 uv) {
  float depth=NJIN_POST_AUX(uv).r;
  vec4 hit=mul(u_njinPostInverse,vec4(uv.x*2.0-1.0,1.0-uv.y*2.0,depth*2.0-1.0,1.0));
  vec4 nearHit=mul(u_njinPostInverse,vec4(uv.x*2.0-1.0,1.0-uv.y*2.0,-1.0,1.0));
  vec3 origin=u_njinLegacyPost[0].w>0.5?nearHit.xyz/nearHit.w:u_njinLegacyPost[0].xyz;
  vec3 delta=hit.xyz/hit.w-origin;float distance=min(length(delta),u_njinLegacyPost[4].y);
  vec3 direction=delta/max(length(delta),0.000001);
  float steps=u_njinLegacyPost[4].z;
  if(u_njinLegacyPost[4].w>0.0) steps=min(256.0,max(steps,ceil(distance/u_njinLegacyPost[4].w)));
  float stride=distance/steps,transmittance=1.0;vec3 scattering=njinSplat(0.0);
  vec3 sun=-normalize(u_njinLight[0].xyz);
  LOOP for(int i=0;i<256;++i) {
    if(float(i)>=steps||transmittance<0.005) break;
    vec3 pos=origin+direction*((float(i)+0.5)*stride);
    float density=u_njinLegacyPost[1].w*exp(clamp(-u_njinLegacyPost[2].y*(pos.y-u_njinLegacyPost[2].x),-30.0,20.0));
    float noise=njinVolumeNoise((pos-u_njinLegacyPost[3].xyz*u_njinLegacyPost[3].w)/u_njinLegacyPost[2].w);
    density*=mix(1.0,smoothstep(0.2,0.8,noise),u_njinLegacyPost[2].z);
    float visibility=njinVolumeSun(pos);
    if(u_njinLegacyPost[5].w>0.5&&u_njinShadowForward.w>0.0) {
      float d=dot(pos-u_njinLight[3].xyz,u_njinShadowForward.xyz);if(d>u_njinShadowSplits[int(u_njinShadowForward.w)-1]) visibility=0.0;
    }
    vec3 radiance=njinLinear(u_njinLight[2].rgb)*u_njinLegacyPost[5].y+
      njinLinear(u_njinLight[1].rgb)*u_njinLight[3].w*u_njinLegacyPost[5].x*visibility*njinVolumePhase(dot(direction,sun),u_njinLegacyPost[4].x);
    vec2 localRange=njinClusterRange(pos);
    if(u_njinLegacyPost[5].z>0.5) LOOP for(int j=0;j<1024;++j) {
      if(float(j)>=localRange.y) break;
      float index=njinClusterLightIndex(localRange.x+float(j));
      vec4 light=njinClusterLight(index,0),color=njinClusterLight(index,1),spot=njinClusterLight(index,2),cone=njinClusterLight(index,3);
      vec3 ray=light.xyz-pos;float r=length(ray);if(r>=light.w) continue;ray/=max(r,0.00001);
      float attenuation=pow(1.0-r/light.w,2.0);
      if(cone.y>0.5) attenuation*=clamp((dot(-ray,normalize(spot.xyz))-spot.w)/max(cone.x-spot.w,0.0001),0.0,1.0);
      radiance+=njinLinear(color.rgb)*color.a*attenuation*njinVolumeLocalParameters(light,cone,pos)*njinVolumePhase(dot(direction,ray),u_njinLegacyPost[4].x);
    }
    float opacity=1.0-exp(-density*stride);
    scattering+=transmittance*opacity*radiance*njinLinear(u_njinLegacyPost[1].rgb);transmittance*=1.0-opacity;
  }
  return NJIN_POST_COLOR(uv).rgb*transmittance+scattering;
}
