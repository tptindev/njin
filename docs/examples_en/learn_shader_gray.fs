#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;   // the image being drawn, set by raylib
uniform vec2 mouse;           // mouse, 0..1: move right for more gray
out vec4 finalColor;

void main() {
  vec4 c = texture(texture0, fragTexCoord) * fragColor;      // read the image's color at this position
  float g = dot(c.rgb, vec3(0.299, 0.587, 0.114));           // brightness as the human eye perceives it
  finalColor = vec4(mix(c.rgb, vec3(g), mouse.x), c.a);      // mix(a, b, t): t = 0 is a, t = 1 is b
}
