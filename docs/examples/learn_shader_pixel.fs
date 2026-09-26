#version 330

in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 resolution;
uniform vec2 mouse;   // mouse.x: cỡ ô, từ 1 đến 20 pixel
out vec4 finalColor;

void main() {
  float size = 1.0 + floor(mouse.x * 19.0);          // cỡ ô pixel hoá, tính bằng pixel màn hình
  vec2 cells = resolution / size;                    // số ô theo mỗi chiều

  // Làm tròn toạ độ xuống ô, rồi lấy mẫu ở TÂM ô: mọi pixel trong một ô cùng thấy một màu.
  vec2 uv = (floor(fragTexCoord * cells) + 0.5) / cells;
  finalColor = vec4(texture(texture0, uv).rgb, 1.0);
}
