#version 330

in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform float time;
uniform vec2 mouse;   // mouse.x: biên độ sóng
out vec4 finalColor;

void main() {
  vec2 uv = fragTexCoord;
  // Dịch toạ độ ngang theo một sóng sin chạy dọc, chuyển theo thời gian.
  uv.x += sin(uv.y * 40.0 + time * 3.0) * 0.02 * mouse.x;
  finalColor = vec4(texture(texture0, uv).rgb, 1.0);
}
