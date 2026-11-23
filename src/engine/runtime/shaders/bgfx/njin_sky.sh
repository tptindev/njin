// Procedural sky of draw_sky3d(): the legacy world3d_draw.cpp sky_glsl (sky
// gradient, sunset glow, sun disc, fbm clouds on a plane, stars, moon, fog),
// in shaderc syntax. Rows of u_njinSky as RenderSkyEffects documents.
uniform vec4 u_njinSky[11];
float njinSkyHash(vec2 p) {
  p=fract(p*vec2(123.34,456.21));
  p+=dot(p,p+45.32);
  return fract(p.x*p.y);
}
// njinSkyHash for the four cell corners in one instruction stream. Optimizing
// compilers (SPIR-V, DXBC) may contract each scalar call differently, so a corner
// shared by two cells hashed to two values and the clouds showed seams.
vec4 njinSkyHash4(vec4 x,vec4 y) {
  x=fract(x*123.34);y=fract(y*456.21);
  vec4 d=x*(x+45.32)+y*(y+45.32);
  return fract((x+d)*(y+d));
}
float njinSkyNoise(vec2 p) {
  // floor(p+1) rather than floor(p)+1: fxc otherwise distributes (i+1)*k into
  // i*k+k, rounding the far corner differently from the next cell's near corner.
  vec2 i=floor(p),j=floor(p+1.0),f=fract(p);
  f=f*f*(3.0-2.0*f);
  vec4 h=njinSkyHash4(vec4(i.x,j.x,i.x,j.x),vec4(i.y,i.y,j.y,j.y));
  return mix(mix(h.x,h.y,f.x),mix(h.z,h.w,f.x),f.y);
}
float njinSkyFbm(vec2 p) {
  float v=0.0,a=0.5;
  for(int i=0;i<5;++i) {v+=njinSkyNoise(p)*a;p=p*2.03+vec2(17.1,9.2);a*=0.5;}
  return v/0.96875;
}
vec3 njinSkyBase(vec3 dir) {
  vec3 sun=u_njinSky[0].xyz,sunColor=u_njinSky[1].rgb;vec4 params=u_njinSky[6];
  float up=max(dir.y,0.0);
  vec3 c=mix(u_njinSky[3].rgb,u_njinSky[2].rgb,pow(up,0.5));
  if(dir.y<0.0) c=mix(u_njinSky[3].rgb,u_njinSky[4].rgb,smoothstep(0.0,0.25,-dir.y));
  float cosang=dot(dir,sun);
  float side=pow(max(cosang*0.5+0.5,0.0),4.0);
  c+=u_njinSky[5].rgb*(params.x*side*pow(1.0-up,3.0)*0.8);
  float m=max(cosang,0.0);
  c+=sunColor*((pow(m,600.0)*1.2+pow(m,24.0)*0.18)*(1.0-params.y*0.7));
  float r=0.0047*params.w;
  float disc=smoothstep(cos(r*1.4),cos(r),cosang)*(1.0-params.y)*smoothstep(-0.02,0.01,dir.y);
  return c+sunColor*(disc*4.0);
}
// Clouds on a plane u_njinSky[7].x above the ground: colour and coverage.
vec4 njinSkyClouds(vec3 dir,vec3 eye) {
  vec4 params=u_njinSky[6],cloud=u_njinSky[7];
  if(dir.y<=0.01||params.y<=0.0) return vec4(0.0,0.0,0.0,0.0);
  float t=(cloud.x-eye.y)/dir.y;
  if(t<=0.0) return vec4(0.0,0.0,0.0,0.0);
  vec2 p=(eye.xz+dir.xz*t+u_njinSky[9].xy*3.0)/(900.0*cloud.y);
  float n=njinSkyFbm(p),cover=params.y;
  float dens=smoothstep(1.0-cover-0.12,1.0-cover+0.3,n);
  dens=max(dens,smoothstep(0.9,1.0,cover));
  float lit=clamp(n-njinSkyFbm(p+u_njinSky[0].xz*0.06)+0.5,0.0,1.0);
  vec3 horizon=u_njinSky[3].rgb,day=vec3(0.1,0.1,0.1)*u_njinSky[0].w;
  vec3 dark=horizon*0.55+vec3(0.05,0.05,0.05),light=horizon*0.4+u_njinSky[1].rgb*0.75+day;
  vec3 col=mix(dark,light,lit)*(1.0-0.6*params.z)+u_njinSky[5].rgb*params.x*0.35;
  dens*=smoothstep(0.01,0.15,dir.y);
  return vec4(col,clamp(dens,0.0,1.0));
}
vec3 njinSkyColor(vec3 dir,vec3 eye) {
  dir=normalize(dir);
  vec3 c=njinSkyBase(dir);
  vec4 cloud=u_njinSky[7];
  if(cloud.z>0.0&&dir.y>0.0) {
    vec3 q=dir*220.0,cell=floor(q);
    float h=njinSkyHash(cell.xy+cell.z*7.13);
    float star=step(0.996,h)*(1.0-smoothstep(0.08,0.32,length(fract(q)-0.5)));
    float s=star*cloud.z*(0.6+0.4*njinSkyHash(cell.zx));
    c+=vec3(s,s,s);
    float md=dot(dir,-u_njinSky[0].xyz);
    c+=vec3(0.85,0.88,0.95)*(smoothstep(cos(0.012),cos(0.009),md)*cloud.z);
  }
  vec4 cl=njinSkyClouds(dir,eye);
  c=mix(c,cl.rgb,cl.a);
  float fog=clamp(cloud.w*25.0,0.0,1.0);
  c=mix(c,u_njinSky[8].rgb,fog*(1.0-0.5*max(dir.y,0.0)));
  return min(c,vec3(1.0,1.0,1.0));
}
// Weather cover (legacy cover_glsl): rows 0-3 top-down map clip transform; row 4 map on,
// depth bias, box count; rows 5-36 boxes (centre, half size). Map on sampler 8.
uniform vec4 u_njinCover[37];
float njinCovered(vec3 p) {
  float c=0.0;vec4 info=u_njinCover[4];
  if(info.x>0.5) {
    vec4 v=vec4(p,1.0);
    vec4 q=vec4(dot(u_njinCover[0],v),dot(u_njinCover[1],v),dot(u_njinCover[2],v),dot(u_njinCover[3],v));
    vec3 n=vec3(q.x/q.w*0.5+0.5,0.5-q.y/q.w*0.5,q.z/q.w*0.5+0.5);
    if(n.x>0.0&&n.y>0.0&&n.x<1.0&&n.y<1.0) {
      float depth=dot(texture2DLod(s_njinPostAux,n.xy,0.0).rgb,vec3(1.0,1.0/255.0,1.0/65025.0));
      if(n.z>depth+info.y) c=1.0;
    }
  }
  for(int i=0;i<16;++i) {
    if(float(i)>=info.z) break;
    vec3 center=u_njinCover[5+i*2].xyz,extent=u_njinCover[6+i*2].xyz;
    if(p.y<center.y+extent.y) {
      vec2 d=abs(p.xz-center.xz)-extent.xz;
      c=max(c,1.0-smoothstep(-0.25,0.0,max(d.x,d.y)));
    }
  }
  return c;
}
