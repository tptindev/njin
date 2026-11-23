// Stands in for bgfx_compute.sh when cs_njin_rtshadow.sc is compiled by glslc into the
// Vulkan ray query kernels (njin_raytrace_vk.cpp): bgfx's macros in plain Vulkan GLSL,
// the loose uniforms gathered in one block (cmake/njin_rt_hw_source.cmake strips them
// from the .sc) and the acceleration structure. Bindings: images and buffers at their
// bgfx register, samplers at 16 + register, the uniforms at 8, the scene at 9.
#version 460
#extension GL_EXT_ray_query : require
#define NJIN_RT_HARDWARE 1
#define NUM_THREADS(x,y,z) layout(local_size_x=x,local_size_y=y,local_size_z=z) in;
#define IMAGE2D_WO(name,fmt,reg) layout(fmt,binding=reg) uniform writeonly image2D name
#define SAMPLER2D(name,reg) layout(binding=16+reg) uniform sampler2D name
#define BUFFER_RO(name,type,reg) layout(std430,binding=reg) readonly buffer name##Block {type name[];}
#define SHARED shared
#define texture2DLod(s,uv,l) textureLod(s,uv,l)
#define mul(a,b) ((a)*(b))
layout(std140,binding=8) uniform NjinRtUniforms {
  mat4 u_rtInverse;
  vec4 u_rtSun;
  vec4 u_rtParams;
  vec4 u_rtExtra;
  vec4 u_rtLamps[5];
  vec4 u_rtLights[26];
  vec4 u_rtGrid[13];
};
layout(binding=9) uniform accelerationStructureEXT u_rtScene;
