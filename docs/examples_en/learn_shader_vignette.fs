#version 330

in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 mouse;   // mouse.x: strength of the dark vignette, 0..1
out vec4 finalColor;

void main() {
  vec3 c = texture(texture0, fragTexCoord).rgb;

  float d = distance(fragTexCoord, vec2(0.5));         // 0 at the center, about 0.7 at a corner
  float light = 1.0 - smoothstep(0.30, 0.80, d);       // 1 in the middle, falling off toward the edges
  finalColor = vec4(c * mix(1.0, light, mouse.x), 1.0);
}
