$input a_position, i_data0, i_data1
$output v_texcoord0, v_color0
// draw_instanced on bgfx: i_data0 = (x, y, width, height) in pixels, i_data1 = colour.
#include <bgfx_shader.sh>
#include <njin_vertex.sh>
void main()
{
  vec2 p = i_data0.xy + a_position.xy * i_data0.zw;
  gl_Position = njin_clip(mul(u_njinMVP, vec4(p, 0.0, 1.0)));
  v_texcoord0 = a_position.xy;
  v_color0 = i_data1;
}
