$input v_texcoord0, v_color0, v_world, v_normal, v_texcoord1, v_tangent, v_instance_color, v_lightmap_uv
#include <bgfx_shader.sh>
SAMPLER2D(s_njinColor,0);
void main()
{
  gl_FragDepth=texture2DLod(s_njinColor,v_texcoord0,0.0).r;
  gl_FragColor=vec4(0.0,0.0,0.0,0.0);
}
