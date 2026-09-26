#version 330

in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 resolution;
uniform vec2 mouse;   // mouse.x: bán kính mờ, 0..1 (tối đa 6 pixel)
out vec4 finalColor;

void main() {
  vec2 texel = 1.0 / resolution;       // một pixel tính theo toạ độ ảnh
  float radius = mouse.x * 6.0;

  // Trung bình 9 điểm quanh vị trí này (lưới 3 x 3, cách nhau `radius` pixel).
  vec3 sum = vec3(0.0);
  for (int y = -1; y <= 1; y++) {
    for (int x = -1; x <= 1; x++) {
      // Toạ độ ra ngoài 0..1 thì raylib lặp ảnh và lấy nhầm màu từ mép đối diện. clamp giữ nó ở trong,
      // chừa nửa pixel vì đúng 0 hay 1 vẫn bị bộ lọc mượt trộn với mép bên kia.
      vec2 p = clamp(fragTexCoord + vec2(x, y) * texel * radius, texel * 0.5, 1.0 - texel * 0.5);
      sum += texture(texture0, p).rgb;
    }
  }
  finalColor = vec4(sum / 9.0, 1.0);
}
