#version 330

in vec2 fragTexCoord;
uniform vec2 resolution;
uniform float time;
out vec4 finalColor;

float fill(float d) {
  float aa = fwidth(d);
  return 1.0 - smoothstep(-aa, aa, d);
}

void main() {
  vec2 p = (fragTexCoord - 0.5) * vec2(resolution.x / resolution.y, 1.0);

  float life = fract(time * 0.4);           // 0 lúc vừa nổ, tiến dần đến 1, rồi lặp lại
  float radius = life * 0.45;
  float d = abs(length(p) - radius) - 0.015; // vòng: cách đường tròn bán kính `radius` không quá 0.015

  float alpha = fill(d) * (1.0 - life);      // mờ dần khi vòng nở ra
  vec3 col = mix(vec3(0.10, 0.10, 0.15), vec3(1.0, 0.85, 0.4), alpha);
  finalColor = vec4(col, 1.0);
}
