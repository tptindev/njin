$input a_position, a_texcoord0, a_color0, a_normal, a_texcoord1, a_tangent
$output v_texcoord0, v_color0, v_world, v_normal, v_texcoord1, v_tangent, v_instance_color
#include <bgfx_shader.sh>
#include "njin_terrain_relief.sh"
uniform mat4 u_njinMVP;
uniform vec4 u_njinParams;
void main() {
  vec3 base,normal;vec4 splat;
  vec3 position=njinReliefVertex(a_position,u_njinTerrainRelief[1],base,normal,splat);
  vec2 cell=(a_position.xz-u_njinTerrainRelief[0].xy)/u_njinTerrainRelief[0].z;
  float step=u_njinTerrainRelief[4].z;
  // Sai phân trung tâm tránh pháp tuyến suy biến ở mép địa hình.
  vec3 unusedBase,unusedNormal;vec4 unusedSplat;
  vec3 x0=njinReliefPoint(cell-vec2(step,0.0),u_njinTerrainRelief[1],unusedBase,unusedNormal,unusedSplat);
  vec3 x1=njinReliefPoint(cell+vec2(step,0.0),u_njinTerrainRelief[1],unusedBase,unusedNormal,unusedSplat);
  vec3 z0=njinReliefPoint(cell-vec2(0.0,step),u_njinTerrainRelief[1],unusedBase,unusedNormal,unusedSplat);
  vec3 z1=njinReliefPoint(cell+vec2(0.0,step),u_njinTerrainRelief[1],unusedBase,unusedNormal,unusedSplat);
  vec3 reliefNormal=normalize(cross(z1-z0,x1-x0));
  position.y-=a_texcoord1.y;
  vec4 clip=mul(u_njinMVP,vec4(position,1.0));
  clip.z=mix(clip.z,0.5*(clip.z+clip.w),u_njinParams.x);clip.y*=u_njinParams.y;
  gl_Position=clip;v_world=position;v_normal=normal;
  v_texcoord0=base.xz;v_texcoord1=vec2(base.y,0.0);
  v_tangent=vec4(reliefNormal,2.0);v_color0=splat;v_instance_color=vec4(1.0,1.0,1.0,1.0);
}
