$input a_position, a_texcoord0, a_color0, a_normal, a_texcoord1, a_tangent, a_texcoord3
$output v_texcoord0, v_color0, v_world, v_normal, v_texcoord1, v_tangent, v_instance_color, v_lightmap_uv
#include <bgfx_shader.sh>
uniform mat4 u_njinMVP;
uniform mat4 u_njinWorld;
uniform mat4 u_njinNormal;
uniform vec4 u_njinParams;
void main()
{
  vec4 p=mul(u_njinMVP,vec4(a_position,1.0));
  p.z=mix(p.z,0.5*(p.z+p.w),u_njinParams.x);
  p.y*=u_njinParams.y;
  gl_Position=p;
  v_texcoord0=a_texcoord0; v_texcoord1=a_texcoord1;
  v_lightmap_uv=a_texcoord3;
  v_tangent=vec4(mul(u_njinWorld,vec4(a_tangent.xyz,0.0)).xyz,a_tangent.w);
  v_color0=a_color0;v_instance_color=vec4(1.0,1.0,1.0,1.0);
  v_world=mul(u_njinWorld,vec4(a_position,1.0)).xyz;
  v_normal=mul(u_njinNormal,vec4(a_normal,0.0)).xyz;
}
