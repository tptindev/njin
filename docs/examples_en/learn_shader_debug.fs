#version 330

in vec2 fragTexCoord;
uniform sampler2D texture0;
out vec4 finalColor;

void main() {
  // To find out what a number in a shader is, show it as a color: there is no printf on the GPU.
  // Here: the image's opacity as gray. Black areas are transparent, white areas fully opaque.
  float a = texture(texture0, fragTexCoord).a;
  finalColor = vec4(vec3(a), 1.0);
}
