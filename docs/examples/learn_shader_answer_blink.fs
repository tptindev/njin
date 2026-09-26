#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform float time;
out vec4 finalColor;

void main() {
  vec4 c = texture(texture0, fragTexCoord) * fragColor;

  // fract(time * 4.0) đi từ 0 đến 1 bốn lần mỗi giây; step(0.5, x) là 0 khi x < 0.5 và 1 từ 0.5 trở đi.
  // Kết quả là một tín hiệu 0, 1, 0, 1... bốn nhịp mỗi giây, không cần if.
  float on = step(0.5, fract(time * 4.0));
  finalColor = vec4(mix(c.rgb, vec3(1.0), on), c.a);
}
