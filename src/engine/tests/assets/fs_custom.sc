$input v_texcoord0, v_color0, v_world, v_normal, v_texcoord1, v_tangent, v_instance_color
#include <bgfx_shader.sh>
SAMPLER2D(s_testBase,0);
SAMPLER2D(s_testExtra,1);
uniform vec4 u_njinTint;
uniform vec4 u_testGain;
uniform vec4 u_testInteger;
uniform vec4 u_testPair;
uniform vec4 u_testTriplet;
uniform vec4 u_testColor;
uniform vec4 u_testArray[2];
void main() {
  // Integer uniforms preserve all 32 bits in two exactly representable float lanes.
  int integer=int(uint(u_testInteger.x)|(uint(u_testInteger.y)<<16u));
  float valid=integer==-123456789 ? 1.0 : 0.0;
  vec4 image=texture2D(s_testBase,v_texcoord0)*texture2D(s_testExtra,v_texcoord0);
  vec3 channels=vec3(u_testPair.x,u_testPair.y,u_testTriplet.z);
  gl_FragColor=image*u_njinTint*v_color0*u_testColor*vec4(u_testGain.x,u_testGain.x,u_testGain.x,1.0)*vec4(channels,1.0)*u_testArray[0]*u_testArray[1]*valid;
}
