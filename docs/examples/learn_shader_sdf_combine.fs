#version 330

in vec2 fragTexCoord;
uniform vec2 resolution;
uniform float time;
out vec4 finalColor;

float sd_circle(vec2 p, float r) {
  return length(p) - r;
}

float fill(float d) {
  float aa = fwidth(d);
  return 1.0 - smoothstep(-aa, aa, d);
}

// Kết hợp hai hình bằng cách kết hợp hai KHOẢNG CÁCH:
float op_union(float a, float b)     { return min(a, b); }       // trong a HOẶC trong b
float op_intersect(float a, float b) { return max(a, b); }       // trong a VÀ trong b
float op_subtract(float a, float b)  { return max(a, -b); }      // trong a, trừ phần trong b

// Hợp mềm: hai hình dính lại như giọt nước khi lại gần nhau. k là độ "dính".
float op_smooth_union(float a, float b, float k) {
  float h = max(k - abs(a - b), 0.0) / k;
  return min(a, b) - h * h * k * 0.25;
}

void main() {
  vec2 p = (fragTexCoord - 0.5) * vec2(resolution.x / resolution.y, 1.0);

  // Hai vòng tròn chạy lại gần rồi ra xa nhau; mỗi ô trong bốn ô dùng đúng cặp này quanh tâm của ô.
  // Khoảng cách hai tâm là 2 * offset, luôn nhỏ hơn tổng hai bán kính nên chúng luôn chồng lên nhau.
  vec2 offset = vec2(0.06 + 0.04 * sin(time * 2.0), 0.0);

  vec3 col = vec3(0.10, 0.10, 0.15);
  float d;

  d = op_union(sd_circle(p - vec2(-0.60, 0.0) - offset, 0.12), sd_circle(p - vec2(-0.60, 0.0) + offset, 0.12));
  col = mix(col, vec3(1.00, 0.80, 0.20), fill(d));

  d = op_intersect(sd_circle(p - vec2(-0.20, 0.0) - offset, 0.15), sd_circle(p - vec2(-0.20, 0.0) + offset, 0.15));
  col = mix(col, vec3(0.35, 0.70, 1.00), fill(d));

  d = op_subtract(sd_circle(p - vec2(0.20, 0.0), 0.15), sd_circle(p - vec2(0.20, 0.0) - offset, 0.12));
  col = mix(col, vec3(0.50, 0.90, 0.50), fill(d));

  d = op_smooth_union(sd_circle(p - vec2(0.60, 0.0) - offset, 0.10), sd_circle(p - vec2(0.60, 0.0) + offset, 0.10), 0.15);
  col = mix(col, vec3(1.00, 0.45, 0.55), fill(d));

  finalColor = vec4(col, 1.0);
}
