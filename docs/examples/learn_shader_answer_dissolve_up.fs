#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 mouse;
out vec4 finalColor;

void main() {
  vec4 c = texture(texture0, fragTexCoord) * fragColor;

  // Thay số ngẫu nhiên bằng vị trí dọc: pixel càng thấp càng có n lớn, nên tan sau cùng.
  // fragTexCoord.y = 0 ở trên cùng, = 1 ở dưới cùng.
  float n = fragTexCoord.y;

  float t = mouse.x * 1.2 - 0.1;
  if (n < t) discard;
  float edge = 1.0 - smoothstep(0.0, 0.08, n - t);
  finalColor = vec4(mix(c.rgb, vec3(1.0, 0.6, 0.1), edge), c.a);
}
