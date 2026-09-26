#version 330

in vec2 fragTexCoord;
uniform vec2 resolution;
out vec4 finalColor;

float sd_box(vec2 p, vec2 b) {
  vec2 q = abs(p) - b;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

float sd_round_box(vec2 p, vec2 b, float r) {
  return sd_box(p, b - r) - r;
}

float fill(float d) {
  float aa = fwidth(d);
  return 1.0 - smoothstep(-aa, aa, d);
}

void main() {
  vec2 p = (fragTexCoord - 0.5) * vec2(resolution.x / resolution.y, 1.0);
  vec2 half_size = vec2(0.22, 0.10);

  float d = sd_round_box(p, half_size, 0.05);

  // Bóng đổ = CÙNG hình, dời xuống dưới bên phải, nhưng làm mờ bằng một dải smoothstep rộng thay vì một pixel.
  float shadow_d = sd_round_box(p - vec2(0.025, 0.035), half_size, 0.05);
  float shadow = 1.0 - smoothstep(-0.03, 0.06, shadow_d);

  vec3 col = vec3(0.55, 0.60, 0.70);                  // nền sáng để thấy bóng
  col *= 1.0 - 0.55 * shadow;                         // tối đi theo độ đậm của bóng
  col = mix(col, vec3(0.95, 0.95, 1.0), fill(d));     // hình vẽ đè lên bóng
  finalColor = vec4(col, 1.0);
}
