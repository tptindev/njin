$input v_texcoord0, v_texcoord1, v_color0, v_motion_current, v_motion_previous, v_motion_depth, v_world
#include <bgfx_shader.sh>
SAMPLER2D(s_njinColor,0);
uniform vec4 u_njinTint,u_njinParams,u_njinMapUv[10],u_njinMipFilter[2];
#include "njin_world_fx.sh"
void main()
{
  njinWorldCut(v_world);njinWorldMask(v_world,gl_FragCoord.xy);
  vec2 uv=mix(v_texcoord0,v_texcoord1,u_njinMapUv[9].z);
  vec4 a=u_njinMapUv[8],b=u_njinMapUv[9];uv=vec2(dot(a.xy,uv),dot(a.zw,uv))+b.xy;
  vec4 texel=u_njinMipFilter[1].x>0.5?texture2DLod(s_njinColor,uv,0.0):texture2D(s_njinColor,uv);
  if(texel.a*u_njinTint.a*v_color0.a<u_njinParams.z) discard;
  if(v_motion_current.w<=0.0||v_motion_previous.w<=0.0||v_motion_depth<=0.0) {gl_FragColor=vec4(0.0,0.0,0.0,0.0);return;}
  vec2 current=v_motion_current.xy/v_motion_current.w*vec2(0.5,-0.5);
  vec2 previous=v_motion_previous.xy/v_motion_previous.w*vec2(0.5,-0.5);
  gl_FragColor=vec4(current-previous,v_motion_depth,1.0);
}
