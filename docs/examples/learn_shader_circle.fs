#version 330

in vec2 fragTexCoord;
uniform vec2 resolution;   // kích thước cửa sổ, chương trình đưa vào
out vec4 finalColor;

void main() {
  // Đưa tâm về (0, 0) và đo theo đơn vị "chiều cao cửa sổ", nhân với tỉ lệ khung hình
  // để hình tròn không bị dẹt thành hình elip.
  vec2 p = (fragTexCoord - 0.5) * vec2(resolution.x / resolution.y, 1.0);

  float d = length(p);                        // khoảng cách từ tâm
  float inside = 1.0 - smoothstep(0.28, 0.30, d);   // 1 trong hình tròn, 0 ngoài, mờ nhẹ ở mép

  vec3 background = vec3(0.10, 0.10, 0.15);
  vec3 disc = vec3(1.0, 0.8, 0.2);
  finalColor = vec4(mix(background, disc, inside), 1.0);
}
