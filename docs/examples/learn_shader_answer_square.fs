#version 330

in vec2 fragTexCoord;
uniform vec2 resolution;
out vec4 finalColor;

void main() {
  vec2 p = (fragTexCoord - 0.5) * vec2(resolution.x / resolution.y, 1.0);

  // Khoảng cách kiểu "ô vuông": lấy số lớn hơn trong |x| và |y|.
  // Những điểm cách tâm cùng khoảng này xếp thành một hình vuông, không phải hình tròn.
  float d = max(abs(p.x), abs(p.y));
  float inside = 1.0 - smoothstep(0.28, 0.30, d);

  finalColor = vec4(mix(vec3(0.10, 0.10, 0.15), vec3(1.0, 0.8, 0.2), inside), 1.0);
}
