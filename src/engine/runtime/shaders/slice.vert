#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
layout(location=2) in float clip_w;
layout(location=0) out vec2 texcoord;
layout(set=1,binding=0) uniform Camera { mat4 transform; } camera;
void main() {
  vec4 clip=camera.transform*vec4(position,clip_w);
  // SDL GPU normalizes viewport Y across its backends (Vulkan uses a
  // negative viewport height). Only the OpenGL depth range needs conversion.
  gl_Position=vec4(clip.xy,(clip.z+clip.w)*0.5,clip.w);
  texcoord=uv;
}
