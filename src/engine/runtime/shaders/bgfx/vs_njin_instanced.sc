$input a_position, a_texcoord0, a_color0, a_normal, a_texcoord1, a_tangent, a_texcoord3, i_data0, i_data1, i_data2, i_data3, i_data4, i_data5
$output v_texcoord0, v_color0, v_world, v_normal, v_texcoord1, v_tangent, v_instance_color, v_lightmap_uv
#include <bgfx_shader.sh>
uniform mat4 u_njinMVP;
uniform mat4 u_njinWorld;
uniform mat4 u_njinNormal;
uniform vec4 u_njinParams;
uniform vec4 u_njinWorldStyle[6];
void main() {
  mat4 instance=mtxFromCols(i_data0,i_data1,i_data2,i_data3);
  vec4 local=mul(instance,vec4(a_position,1.0));
  if(u_njinWorldStyle[0].x>1.5 && u_njinWorldStyle[0].x<2.5) {
    float t=a_position.y,phase=i_data5.y,h=length(i_data1.xyz);
    vec2 wind=u_njinWorldStyle[4].xy;float time=u_njinWorldStyle[4].z,speed=length(wind);
    vec2 dir=speed>0.001?wind/speed:vec2(1.0,0.0);
    float distance=length(u_njinWorldStyle[5].zw-i_data3.xz);
    float k=1.0-smoothstep(u_njinWorldStyle[5].x,u_njinWorldStyle[5].y,distance);
    float gust=sin(time*1.6+phase+dot(i_data3.xz,dir)*0.3)*0.5+0.5;
    float flutter=sin(time*4.7+phase*3.0)*0.12;
    float bend=(clamp(speed/8.0,0.0,1.3)*(0.35+0.65*gust)+flutter*clamp(speed/3.0,0.0,1.0))*u_njinWorldStyle[4].w;
    float own=u_njinWorldStyle[3].w*(0.6+0.4*fract(phase*3.71));
    vec3 arch=normalize(i_data2.xyz)*(fract(phase*7.13)<0.5?-1.0:1.0);
    local.xyz=i_data3.xyz+i_data0.xyz*a_position.x*(1.0-0.85*t)*(0.4+0.6*k)+vec3(0.0,h*k*t,0.0)+
      (vec3(dir.x,0.0,dir.y)*bend+arch*own)*h*k*t*t;
    local.y-=0.5*(bend*bend+own*own)*h*k*t*t*t;
  }
  vec4 particleColor=vec4(1.0,1.0,1.0,1.0);
  if(u_njinWorldStyle[0].x>3.5&&u_njinWorldStyle[0].x<4.5) {
    vec4 settings=u_njinWorldStyle[1];float age=max(settings.w-i_data5.z,0.0),life=max(i_data5.w,0.0001),t=age/life;
    float drag=settings.z,u=drag*age;vec2 displacement;
    if(u<0.1) {
      float tail=1.0-u*0.25*(1.0-u*0.2);
      float a=age*(1.0-u*0.5*(1.0-u/3.0*tail)),b=0.5*age*age*(1.0-u/3.0*tail);
      displacement=i_data5.xy*a+settings.xy*b;
    } else {float e=1.0-exp(-u);displacement=i_data5.xy*(e/drag)+settings.xy*((age-e/drag)/drag);}
    vec2 position=i_data3.xy+displacement;
    vec2 center=u_njinWorldStyle[4].xy+u_njinWorldStyle[4].zw*position.x+u_njinWorldStyle[5].xy*position.y;
    float size=mix(u_njinWorldStyle[0].z,u_njinWorldStyle[0].w,clamp(t,0.0,1.0))*i_data2.y;
    particleColor=mix(u_njinWorldStyle[2],u_njinWorldStyle[3],clamp(t,0.0,1.0));
    if(age>=life||particleColor.a<=0.0) size=0.0;
    vec2 q=a_position.xy*u_njinWorldStyle[5].zw*max(size,0.0);
    float angle=i_data2.x*age*0.01745329252,c=cos(angle),s=sin(angle);
    q=vec2(q.x*c-q.y*s,q.x*s+q.y*c);
    q=i_data0.xy*q.x+i_data1.xy*q.y;
    local=vec4(center+q,0.0,1.0);
  }
  vec4 clip=mul(u_njinMVP,local);
  clip.z=mix(clip.z,0.5*(clip.z+clip.w),u_njinParams.x);clip.y*=u_njinParams.y;gl_Position=clip;
  v_texcoord0=a_texcoord0;v_texcoord1=a_texcoord1;v_color0=a_color0;v_instance_color=i_data4;
  v_lightmap_uv=a_texcoord3;
  if(u_njinWorldStyle[0].x>3.5&&u_njinWorldStyle[0].x<4.5) {v_color0=particleColor;v_instance_color=vec4(1.0,1.0,1.0,1.0);}
  v_world=mul(u_njinWorld,local).xyz;
  vec3 c0=cross(i_data1.xyz,i_data2.xyz),c1=cross(i_data2.xyz,i_data0.xyz),c2=cross(i_data0.xyz,i_data1.xyz);
  float determinant=dot(i_data0.xyz,c0);
  vec3 n=(c0*a_normal.x+c1*a_normal.y+c2*a_normal.z)/determinant;
  v_normal=mul(u_njinNormal,vec4(n,0.0)).xyz;
  if(u_njinWorldStyle[0].x>1.5 && u_njinWorldStyle[0].x<2.5) {
    float t=a_position.y;
    vec3 root=mix(u_njinWorldStyle[1].rgb,i_data4.gba,u_njinWorldStyle[2].a);
    vec3 color=mix(root,u_njinWorldStyle[2].rgb,t);
    color=mix(color,u_njinWorldStyle[3].rgb,i_data4.r)*i_data5.x*mix(u_njinWorldStyle[1].a,1.0,t);
    v_color0=vec4(color,1.0);v_instance_color=vec4(1.0,1.0,1.0,1.0);
    v_normal=vec3(i_data5.z,sqrt(max(1.0-dot(i_data5.zw,i_data5.zw),0.0)),i_data5.w);
  }
  v_tangent=vec4(mul(u_njinWorld,mul(instance,vec4(a_tangent.xyz,0.0))).xyz,a_tangent.w*(determinant<0.0?-1.0:1.0));
  // draw_sky3d rain/snow: legacy world3d_draw.cpp precip_vs_main, with each drop's
  // PCG random numbers precomputed on the CPU (i_data4 = r, speed; i_data5 = shape).
  // Rows: mode 5, snow, amount, sparkle; eye, time; wind, drift; box width/height,
  // fall speed, snow; flake grid. The drop centre goes to the fragment (v_tangent).
  if(u_njinWorldStyle[0].x>4.5&&u_njinWorldStyle[0].x<5.5) {
    vec3 r=i_data4.xyz,eye=u_njinWorldStyle[1].xyz;
    float speed=0.75+0.5*i_data4.w,time=u_njinWorldStyle[1].w;
    vec4 precip=u_njinWorldStyle[3];vec2 wind=u_njinWorldStyle[2].xy,drift=u_njinWorldStyle[2].zw;
    vec3 box=vec3(precip.x,precip.y,precip.x);
    bool snow=precip.w>0.5;float slow=snow?0.6:1.0;
    vec3 vel=vec3(wind.x,-precip.z*speed,wind.y)*slow;
    vec3 corner=eye-box*vec3(0.5,0.25,0.5);
    vec3 moved=vec3(drift.x,-precip.z*speed*time,drift.y)*slow;
    vec3 p=corner+mod(r*box+moved-corner,box);
    if(snow) p.xz+=vec2(sin(time*1.3+r.x*40.0),cos(time*1.1+r.z*40.0))*0.3;
    vec3 toEye=normalize(eye-p),pos;
    vec2 cell=vec2(0.0,0.0);float glint=0.0,grow=1.0;
    if(snow) {
      vec3 right=normalize(cross(vec3(0.0,1.0,0.0),toEye)),up=cross(toEye,right);
      float size=0.035+0.035*r.x;
      vec2 grid=u_njinWorldStyle[4].xy;
      if(grid.x>0.0) {
        vec3 k=i_data5.xyz;
        cell=floor(k.xy*grid);
        float spin=k.z*6.2832+time*(k.x-0.5)*2.4;
        vec3 r0=right*cos(spin)+up*sin(spin);
        up=up*cos(spin)-right*sin(spin);right=r0;
        size*=1.4;
        float facet=3.0*spin+time*(1.5+2.0*k.y)+k.z*40.0;
        float near=1.0-smoothstep(2.0,6.0,distance(eye,p));
        glint=u_njinWorldStyle[0].w*near*pow(0.5+0.5*cos(facet),24.0);
        grow=1.0+2.0*min(glint,1.0);size*=grow;
      }
      pos=p+(right*(a_position.x-0.5)+up*(a_position.y-0.5))*size;
    } else {
      vec3 axis=normalize(vel),side=normalize(cross(axis,toEye));
      pos=p+axis*(a_position.y-0.5)*0.8+side*(a_position.x-0.5)*0.02;
    }
    float fade=smoothstep(0.4,1.6,distance(eye,p))*(1.0-smoothstep(box.x*0.38,box.x*0.5,length(p.xz-eye.xz)));
    vec4 dropClip=mul(u_njinMVP,vec4(pos,1.0));
    dropClip.z=mix(dropClip.z,0.5*(dropClip.z+dropClip.w),u_njinParams.x);dropClip.y*=u_njinParams.y;gl_Position=dropClip;
    v_texcoord0=a_position.xy;v_texcoord1=cell;v_color0=vec4(fade,glint,grow,1.0);v_instance_color=vec4(1.0,1.0,1.0,1.0);
    v_world=pos;v_normal=vec3(0.0,1.0,0.0);v_tangent=vec4(p,1.0);
  }
}
