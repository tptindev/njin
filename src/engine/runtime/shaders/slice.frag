#version 450
layout(set=2,binding=0) uniform sampler2D image;
layout(location=0) in vec2 texcoord;
layout(location=0) out vec4 color;
layout(set=3,binding=0) uniform Material { vec4 tint; } material;
void main() { color=texture(image,texcoord)*material.tint; }
