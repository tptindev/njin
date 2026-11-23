// World-space coverage shared by color, shadow, normal and velocity stages.
uniform vec4 u_njinWorldFx[7];
float njinWorldCut(vec3 pos)
{
  vec4 d=u_njinWorldFx[2];
  if(d.x<=0.0) return 1.0;
  if(d.x>=1.0) discard;
  vec3 p=fract((floor(pos/d.y)+d.z*17.31)*0.3183099+vec3(0.71,0.113,0.419));
  p*=17.0;
  float cut=fract(p.x*p.y*p.z*(p.x+p.y+p.z));
  vec3 axis=u_njinWorldFx[4].xyz-u_njinWorldFx[3].xyz;
  float along=clamp(dot(pos-u_njinWorldFx[3].xyz,axis)/max(dot(axis,axis),0.000001),0.0,1.0);
  cut=mix(cut,along,u_njinWorldFx[3].w);
  if(cut<d.x) discard;
  return cut;
}
float njinBayer2(vec2 p) {return 2.0*p.x+3.0*p.y-4.0*p.x*p.y;}
void njinWorldMask(vec3 pos,vec2 pixel)
{
  vec4 m=u_njinWorldFx[5];if(m.w<=0.0) return;
  vec3 camera=u_njinWorldFx[6].xyz,ab=m.xyz-camera;
  float len2=dot(ab,ab);if(len2<=0.000001) return;
  float t=dot(pos-camera,ab)/len2;
  if(t<=0.0||t>=0.97||pos.y<m.y-m.w*0.6) return;
  float r=m.w*mix(0.35,1.0,t);
  float k=1.0-smoothstep(r*0.6,r,length(pos-(camera+ab*t)));
  vec2 c=mod(floor(pixel),4.0);
  float b=4.0*njinBayer2(mod(c,2.0))+njinBayer2(floor(c/2.0));
  if((b+0.5)/16.0<k) discard;
}
