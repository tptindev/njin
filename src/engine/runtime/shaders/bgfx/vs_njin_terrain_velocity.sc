$input a_position, a_texcoord0, a_texcoord1, a_color0, a_texcoord2
$output v_texcoord0, v_texcoord1, v_color0, v_motion_current, v_motion_previous, v_motion_depth, v_world
#include <bgfx_shader.sh>
#include "njin_terrain_relief.sh"
uniform mat4 u_njinMVP;
uniform mat4 u_njinMotionCurrent;
uniform mat4 u_njinMotionPrevious;
uniform mat4 u_njinMotionView;
uniform vec4 u_njinParams;
void main() {
  vec3 base,normal;vec4 splat;
  vec3 current=njinReliefVertex(a_position,u_njinTerrainRelief[1],base,normal,splat);
  vec3 previous=njinReliefVertex(a_texcoord2,u_njinTerrainRelief[5],base,normal,splat);
  current.y-=a_texcoord1.y;previous.y-=a_texcoord1.y;
  vec4 clip=mul(u_njinMVP,vec4(current,1.0));
  clip.z=mix(clip.z,0.5*(clip.z+clip.w),u_njinParams.x);clip.y*=u_njinParams.y;
  gl_Position=clip;v_world=current;
  v_texcoord0=a_texcoord0;v_texcoord1=a_texcoord1;v_color0=splat;
  v_motion_current=mul(u_njinMotionCurrent,vec4(current,1.0));
  v_motion_previous=mul(u_njinMotionPrevious,vec4(previous,1.0));
  v_motion_depth=-mul(u_njinMotionView,vec4(previous,1.0)).z;
}
