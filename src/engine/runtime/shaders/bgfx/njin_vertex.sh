// For a shader package's own vertex stage (njin_shader_asset VERTEX): the engine's
// transform and the clip-space fix-ups its own vertex stage applies (depth range and the
// render-target flip of the current backend). Write gl_Position = njin_clip(mul(u_njinMVP, ...)).
#ifndef NJIN_VERTEX_SH
#define NJIN_VERTEX_SH
uniform mat4 u_njinMVP;
uniform vec4 u_njinParams;
vec4 njin_clip(vec4 p)
{
  p.z = mix(p.z, 0.5 * (p.z + p.w), u_njinParams.x);
  p.y *= u_njinParams.y;
  return p;
}
#endif
