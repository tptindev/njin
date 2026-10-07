#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 mouse;
out vec4 finalColor;

void main() {
  vec4 c = texture(texture0, fragTexCoord) * fragColor;

  // Replace the random number with the vertical position: the lower the pixel, the larger n, so it dissolves last.
  // fragTexCoord.y = 0 at the very top, = 1 at the very bottom.
  float n = fragTexCoord.y;

  float t = mouse.x * 1.2 - 0.1;
  if (n < t) discard;
  float edge = 1.0 - smoothstep(0.0, 0.08, n - t);
  finalColor = vec4(mix(c.rgb, vec3(1.0, 0.6, 0.1), edge), c.a);
}
