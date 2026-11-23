// Legacy water surface formulas adapted from world3d_draw.cpp.
// No scene/depth sampling here: these effects only change the water surface.
uniform vec4 u_njinWater[16];
float njinWaterHash(vec2 p) {
  p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);
}
float njinWaterNoise(vec2 p) {
  vec2 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
  return mix(mix(njinWaterHash(i),njinWaterHash(i+vec2(1.0,0.0)),f.x),
    mix(njinWaterHash(i+vec2(0.0,1.0)),njinWaterHash(i+vec2(1.0,1.0)),f.x),f.y);
}
float njinWaterFrozen(vec2 rest,float depth) {
  if(u_njinWater[0].x<=0.0) return 0.0;
  float ragged=njinWaterNoise(rest*0.3)*0.7+njinWaterNoise(rest*1.1+9.0)*0.3;
  float reach=u_njinWater[0].x*1.3-(1.0-exp(-max(depth,0.0)/1.5))+(ragged-0.5)*0.12;
  return smoothstep(-0.03,0.03,reach);
}
// Rain on the water (legacy rain_rings): one drop at a time per grid cell, at its own
// place and moment, a ring running out and dying within a round; `rain` of the rounds
// have a drop. Returns the rings' slope. Neighbour cells are floor(p+offset): the
// optimizers otherwise fold i+offset into the hash differently per unrolled cell.
vec2 njinRainRings(vec2 p,float rain,float t) {
  vec2 grad=vec2(0.0,0.0);
  for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
    vec2 c=floor(p+vec2(float(x),float(y)));
    // Vector hashes (njinSkyHash4): the scalar calls rounded differently per call on
    // the SPIR-V/fxc optimizers, moving drops away from where OpenGL puts them.
    float cycle=t*1.4+njinSkyHash4(vec4(c.x,c.x,c.x,c.x),vec4(c.y,c.y,c.y,c.y)).x*7.0,turn=floor(cycle),age=fract(cycle);
    vec4 h=njinSkyHash4(vec4(c.x+turn*0.37+3.1,c.x+turn+11.7,c.x+turn+23.3,0.0),vec4(c.y+turn*0.37+3.1,c.y+turn+11.7,c.y+turn+23.3,0.0));
    if(h.x>rain) continue;
    vec2 at=c+h.yz;
    vec2 dp=p-at;float d=length(dp);
    float x0=(d-age*0.9)/0.1,fade=(1.0-age)*(1.0-age);
    grad+=dp/max(d,0.0001)*sin(x0*3.14159)*exp(-x0*x0)*fade;
  }
  return grad;
}
// Small ripples riding on the waves (legacy world3d_draw.cpp): two drifting noise slopes,
// faded out as a pixel covers more than 0.3 m so the far sea does not alias.
vec3 njinWaterRipples(vec3 normal,vec2 rest,float time,float spacing,float strength) {
  if(strength<=0.0) return normal;
  vec2 q=rest*1.7+vec2(time*0.35,time*0.21),r=rest*3.1-vec2(time*0.27,-time*0.31);float e=0.05;
  vec2 grad=vec2(njinWaterNoise(q+vec2(e,0.0))-njinWaterNoise(q-vec2(e,0.0)),
    njinWaterNoise(q+vec2(0.0,e))-njinWaterNoise(q-vec2(0.0,e)))/(2.0*e);
  grad+=0.5*vec2(njinWaterNoise(r+vec2(e,0.0))-njinWaterNoise(r-vec2(e,0.0)),
    njinWaterNoise(r+vec2(0.0,e))-njinWaterNoise(r-vec2(0.0,e)))/(2.0*e);
  float keep=1.0-smoothstep(0.3,2.0,spacing);
  return normalize(normal-vec3(grad.x,0.0,grad.y)*0.06*strength*keep);
}
float njinWaterDetail(vec2 rest,float time,float spacing,inout vec3 normal) {
  float churn=0.0;vec4 pour=u_njinWater[2];
  if(pour.z>0.0) {
    float reach=max(pour.w,0.01);vec2 dp=rest-pour.xy;float d=length(dp),k=62.831853/reach;
    float wave=cos(d*k-time*k*reach*0.8+njinWaterNoise(dp*(40.0/reach)+time*1.3)*2.5);
    float fade=exp(-3.0*d/reach)*smoothstep(0.0,reach*0.04,d);
    float keep=1.0-smoothstep(0.5,1.0,spacing*k*0.16);
    vec2 slope=dp/max(d,0.0001)*wave*fade*keep*0.45*pour.z;
    normal=normalize(normal-vec3(slope.x,0.0,slope.y));
    churn=pour.z*(1.0-smoothstep(reach*0.04,reach*0.18,d));
    if(churn>0.0) {
      vec2 q=dp*(90.0/reach)+vec2(time*2.1,-time*1.7);float e=0.1;
      vec2 grad=vec2(njinWaterNoise(q+vec2(e,0.0))-njinWaterNoise(q-vec2(e,0.0)),
        njinWaterNoise(q+vec2(0.0,e))-njinWaterNoise(q-vec2(0.0,e)))/(2.0*e);
      normal=normalize(normal-vec3(grad.x,0.0,grad.y)*0.25*churn);
    }
  }
  LOOP for(int i=0;i<8;++i) {
    vec4 ring=u_njinWater[3+i];if(ring.w<=0.0) continue;
    vec4 sizes=u_njinWater[11+i/4];int channel=i-i/4*4;
    float size=channel==0?sizes.x:channel==1?sizes.y:channel==2?sizes.z:sizes.w;
    float life=exp(-ring.z/(1.2+size*4.0));if(life<0.02) continue;
    vec2 dp=rest-ring.xy;float d=length(dp),wl=max(size*0.45,0.025);
    float front=(0.18+size*1.2)*ring.z+size*0.3,behind=front-d;
    float trail=(behind-wl*1.5)/(wl*1.8);float train=exp(-trail*trail);
    float spread=1.0/(1.0+d/max(size,0.02));
    float keep=1.0-smoothstep(0.5,1.0,spacing*6.2831853/wl*0.16);
    vec2 slope=dp/max(d,0.0001)*sin(6.2831853*behind/wl)*train*spread*life*ring.w*keep*1.2;
    normal=normalize(normal-vec3(slope.x,0.0,slope.y));
    churn=max(churn,ring.w*exp(-ring.z/0.35)*(1.0-smoothstep(size*0.25,size*0.7,d)));
  }
  // Rain (water3d_desc::rain_rings times the sky's rain): two grids of drops half a
  // metre and a third apart; where a ring is finer than a pixel, a rough speckle.
  float rain=u_njinWorldStyle[4].y;
  if(rain>0.0) {
    float keep=1.0-smoothstep(0.03,0.09,spacing);
    if(keep>0.0) {
      vec2 g=njinRainRings(rest/0.5,rain,time)+njinRainRings(rest/0.33+5.3,rain,time+0.37);
      normal=normalize(normal-vec3(g.x,0.0,g.y)*0.35*keep);
    }
    vec2 q=rest*9.0+vec2(time*3.1,-time*2.7);float e=0.1;
    vec2 grad=vec2(njinSkyNoise(q+vec2(e,0.0))-njinSkyNoise(q-vec2(e,0.0)),njinSkyNoise(q+vec2(0.0,e))-njinSkyNoise(q-vec2(0.0,e)))/(2.0*e);
    normal=normalize(normal-vec3(grad.x,0.0,grad.y)*0.05*rain*(1.0-keep));
  }
  return churn;
}
vec3 njinWaterIce(vec2 rest) {
  float mottle=njinWaterNoise(rest*0.9)*0.6+njinWaterNoise(rest*4.3+3.7)*0.4;
  vec3 color=u_njinWater[1].rgb*mix(0.86,1.06,mottle);
  float drift=njinWaterNoise(rest*0.07)*0.55+njinWaterNoise(rest*0.5+7.0)*0.3+
    njinWaterNoise(rest*3.1+13.0)*0.15;
  float edge=1.0-u_njinWater[0].y*1.25;
  float snow=u_njinWater[0].y>0.0?smoothstep(edge-0.06,edge+0.06,drift):0.0;
  return mix(color,vec3(0.95,0.96,1.0),snow);
}
float njinWaterViewDepth(float z) {
  vec4 camera=u_njinWorldStyle[5];
  return camera.z>0.5?mix(camera.x,camera.y,z):
    2.0*camera.x*camera.y/max(camera.y+camera.x-(z*2.0-1.0)*(camera.y-camera.x),0.000001);
}
vec3 njinWaterProject(vec3 position) {
  vec4 clip=mul(u_njinTransparencyVP,vec4(position,1.0));
  return vec3(clip.x,-clip.y,clip.z)/max(clip.w,0.000001)*0.5+0.5;
}
// Return the bent UV and vertical optical thickness. Reject foreground hits.
vec2 njinWaterUnder(vec3 position,vec3 normal,out float depth) {
  vec3 eye=u_njinWorldFx[6].xyz-position;vec3 view=eye/max(length(eye),0.000001);
  vec3 bent=refract(-view,normal,0.75),at=njinWaterProject(position);
  float here=njinWaterViewDepth(at.z);
  float straight=njinWaterViewDepth(NJIN_OPAQUE_DEPTH(at.xy).r)-here;
  float run=clamp(straight,0.0,1.0);
  vec2 delta=njinWaterProject(position+bent*run).xy-njinWaterProject(position-view*run).xy;
  vec2 uv=clamp(at.xy+delta*u_njinWater[0].z,vec2(0.001,0.001),vec2(0.999,0.999));
  float through=njinWaterViewDepth(NJIN_OPAQUE_DEPTH(uv).r)-here;
  if(through<0.0) {uv=at.xy;through=straight;}
  depth=max(through,0.0)*max(abs(bent.y),0.2);return uv;
}
float njinWaterSeams(vec2 p,float soft,float time) {
  vec2 i=floor(p),f=fract(p);float first=8.0,second=8.0;
  for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
    vec2 cell=vec2(float(x),float(y));
    vec2 hash=vec2(njinWaterHash(i+cell),njinWaterHash(i+cell+17.31));
    vec2 offset=cell+0.5+0.4*sin(time*0.8+6.2831853*hash)-f;float d=dot(offset,offset);
    if(d<first) {second=first;first=d;} else if(d<second) second=d;
  }
  float seam=1.0-smoothstep(0.0,0.07+soft,sqrt(second)-sqrt(first));return seam*seam*seam;
}
float njinWaterWeb(vec2 p,float soft,float time) {
  vec2 warp=vec2(njinWaterNoise(p*0.45+time*0.07),njinWaterNoise(p*0.45+vec2(4.7,2.3)-time*0.06));
  p+=(warp-0.5)*1.4;p*=0.8+0.45*njinWaterNoise(p*0.13+vec2(2.9,8.1));
  p+=(vec2(njinWaterNoise(p*1.6+time*0.15),njinWaterNoise(p*1.6+vec2(9.1,3.3)))-0.5)*0.45;
  float a=njinWaterSeams(p,soft,time),b=njinWaterSeams(p*1.37+vec2(5.2,1.7),soft,time);
  return (0.15*(a+b)+1.1*a*b)*smoothstep(0.4,0.8,njinWaterNoise(p*0.17-time*0.03));
}
vec3 njinWaterCaustic(vec3 position,vec3 normal,float depth,float spacing) {
  vec3 light=-normalize(u_njinLight[0].xyz);float size=u_njinWorldStyle[5].w;
  if(u_njinWater[1].w<=0.0||light.y<=0.0) return vec3(0.0,0.0,0.0);
  float farFade=smoothstep(0.12,0.45,spacing/size);
  float shallow=smoothstep(0.0,0.12,depth)*exp(-depth/max(u_njinWater[13].w*0.5,0.05));
  if(farFade>=1.0||shallow<=0.01) return vec3(0.0,0.0,0.0);
  vec3 eye=normalize(u_njinWorldFx[6].xyz-position),bent=refract(-eye,normal,0.75);
  vec2 bed=position.xz+bent.xz/max(-bent.y,0.25)*depth;
  float lit=njinSunShadow(vec3(bed.x,position.y-depth,bed.y),vec3(0.0,1.0,0.0))*light.y;
  vec2 p=(bed+light.xz/max(light.y,0.2)*depth)/size+normal.xz*0.8;
  vec2 split=vec2(0.012,0.008)*(1.0+depth);float soft=spacing/size,time=u_njinWorldStyle[1].w;
  vec3 web=vec3(njinWaterWeb(p+split,soft,time),njinWaterWeb(p,soft,time),njinWaterWeb(p-split,soft,time));
  return web*njinLinear(u_njinLight[1].rgb)*u_njinLight[3].w*u_njinWater[1].w*lit*shallow*(1.0-farFade);
}
vec3 njinWaterReflection(vec3 position,vec3 direction,float here,out float confidence) {
  confidence=0.0;float stepLength=max(here*0.012,0.05),distance=stepLength,last=0.0;
  LOOP for(int i=0;i<48;++i) {
    vec4 clip=mul(u_njinTransparencyVP,vec4(position+direction*distance,1.0));
    if(clip.w<=0.0||(u_njinWorldStyle[5].z<0.5&&clip.w<=u_njinWorldStyle[5].x)) break;
    vec3 p=njinWaterProject(position+direction*distance);
    if(any(lessThan(p,vec3(0.0,0.0,0.0)))||any(greaterThan(p,vec3(1.0,1.0,1.0)))) break;
    float behind=njinWaterViewDepth(p.z)-njinWaterViewDepth(NJIN_OPAQUE_DEPTH(p.xy).r);
    if(behind>0.0) {
      if(behind>stepLength*3.0+0.3) break;
      float low=last,high=distance;
      for(int k=0;k<5;++k) {
        float middle=(low+high)*0.5;vec3 q=njinWaterProject(position+direction*middle);
        if(njinWaterViewDepth(q.z)>njinWaterViewDepth(NJIN_OPAQUE_DEPTH(q.xy).r)) high=middle;else low=middle;
      }
      vec2 uv=njinWaterProject(position+direction*high).xy;
      vec2 edge=smoothstep(vec2(0.0,0.0),vec2(0.08,0.08),uv)*(1.0-smoothstep(vec2(0.92,0.92),vec2(1.0,1.0),uv));
      confidence=edge.x*edge.y;return NJIN_POST_AUX(uv).rgb;
    }
    last=distance;distance+=stepLength;stepLength*=1.08;
  }
  return vec3(0.0,0.0,0.0);
}
vec4 njinWaterOptics(vec4 color,vec3 position,vec3 normal,vec2 rest,float depth,float still,vec2 underUv,float edgeFade) {
  float refractAmount=u_njinWater[0].z,reflectAmount=u_njinWater[0].w;
  if(u_njinTransparency.x<0.5||(refractAmount<=0.0&&reflectAmount<=0.0&&u_njinWater[1].w<=0.0)) return color;
  float frozen=njinWaterFrozen(rest,still);
  vec3 view=normalize(u_njinWorldFx[6].xyz-position);vec3 bent=refract(-view,normal,0.75);
  float spacing=max(length(dFdx(rest)),length(dFdy(rest)));
  vec3 caustic=njinWaterCaustic(position,normal,depth,spacing)*(1.0-frozen);
  if(reflectAmount>0.0) {
    vec3 ray=reflect(-view,normal);ray=normalize(vec3(ray.x,max(ray.y,0.02),ray.z));
    float confidence;vec3 hit=njinWaterReflection(position,ray,njinWaterViewDepth(njinWaterProject(position).z),confidence);
    vec3 fallback=u_njinEnvMode.x>0.5?njinEnvironmentRadiance(ray,0.0)*u_njinOutput.x:u_njinPostSky.rgb;
    if(u_njinSky[10].w>0.5) {
      vec3 sky=njinSkyColor(vec3(ray.x,abs(ray.y),ray.z),u_njinSky[10].xyz);
      fallback=u_njinOutput.y>0.5?njinLinear(sky)*u_njinOutput.x:sky;
    }
    float fresnel=0.02+0.98*pow(1.0-max(dot(normal,view),0.0),5.0);
    float share=(fresnel+(1.0-fresnel)*confidence*0.6)*reflectAmount*(1.0-frozen);
    color.rgb=mix(color.rgb,mix(fallback,hit,confidence),share);
  }
  if(refractAmount>0.0) {
    vec3 under=NJIN_POST_AUX(underUv).rgb;
    under*=(1.0+caustic*1.5)*pow(max(u_njinWater[13].rgb,vec3(0.0001,0.0001,0.0001)),njinSplat(depth*(1.0+1.0/max(abs(bent.y),0.2))));
    color.rgb=mix(under,color.rgb,clamp(color.a,0.0,1.0));color.a=edgeFade;
  } else {color.rgb+=caustic*0.5*exp(-depth/u_njinWater[13].w)*u_njinOutput.x;color.a=max(color.a,max(caustic.r,max(caustic.g,caustic.b))*0.5*edgeFade);}
  return color;
}
