$input a_position, a_texcoord0, a_texcoord1, a_color0, a_texcoord2
$output v_texcoord0, v_texcoord1, v_color0, v_motion_current, v_motion_previous, v_motion_depth, v_world
#include <bgfx_shader.sh>
uniform mat4 u_njinMVP,u_njinWorld;
uniform mat4 u_njinMotionCurrent,u_njinMotionPrevious,u_njinMotionView;
uniform vec4 u_njinParams;
void main()
{
  vec4 p=mul(u_njinMVP,vec4(a_position,1.0));
  p.z=mix(p.z,0.5*(p.z+p.w),u_njinParams.x);p.y*=u_njinParams.y;
  gl_Position=p;
  v_world=mul(u_njinWorld,vec4(a_position,1.0)).xyz;
  v_texcoord0=a_texcoord0;v_texcoord1=a_texcoord1;v_color0=a_color0;
  v_motion_current=mul(u_njinMotionCurrent,vec4(a_position,1.0));
  v_motion_previous=mul(u_njinMotionPrevious,vec4(a_texcoord2,1.0));
  v_motion_depth=-mul(u_njinMotionView,vec4(a_texcoord2,1.0)).z;
}
