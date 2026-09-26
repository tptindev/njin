#version 330

in vec2 fragTexCoord;
uniform vec2 resolution;
uniform float time;
out vec4 finalColor;

void main() {
  vec2 p = (fragTexCoord - 0.5) * vec2(resolution.x / resolution.y, 1.0);

  // Dời tâm hình tròn sang trái rồi sang phải theo thời gian: trừ đi một vector, tâm đi theo vector đó.
  p -= vec2(0.4 * sin(time * 2.0), 0.0);

  float d = length(p);
  float inside = 1.0 - smoothstep(0.28, 0.30, d);
  finalColor = vec4(mix(vec3(0.10, 0.10, 0.15), vec3(1.0, 0.8, 0.2), inside), 1.0);
}
