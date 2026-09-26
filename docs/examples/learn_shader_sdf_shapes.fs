#version 330

in vec2 fragTexCoord;
uniform vec2 resolution;
out vec4 finalColor;

// --- Bốn hình cơ bản: mỗi hàm trả về khoảng cách có dấu đến hình, quanh gốc toạ độ ---

float sd_circle(vec2 p, float r) {
  return length(p) - r;
}

// Hộp nửa kích thước b. Phía ngoài đo đến góc hoặc cạnh gần nhất, phía trong là số âm.
float sd_box(vec2 p, vec2 b) {
  vec2 q = abs(p) - b;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

// Hộp bo góc: hộp nhỏ hơn r ở mỗi phía, rồi "phồng" ra r (trừ r khỏi khoảng cách).
float sd_round_box(vec2 p, vec2 b, float r) {
  return sd_box(p, b - r) - r;
}

// Đoạn thẳng a-b, dày w: đo đến điểm gần nhất TRÊN đoạn, rồi trừ nửa bề dày.
float sd_segment(vec2 p, vec2 a, vec2 b, float w) {
  vec2 pa = p - a;
  vec2 ba = b - a;
  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
  return length(pa - ba * h) - w;
}

// Đổi khoảng cách thành độ phủ 0..1, mép mượt đúng MỘT pixel bất kể phóng to hay nhỏ.
// fwidth(d) là d đổi bao nhiêu khi đi sang pixel bên cạnh.
float fill(float d) {
  float aa = fwidth(d);
  return 1.0 - smoothstep(-aa, aa, d);
}

void main() {
  vec2 p = (fragTexCoord - 0.5) * vec2(resolution.x / resolution.y, 1.0);

  vec3 col = vec3(0.10, 0.10, 0.15);
  col = mix(col, vec3(1.00, 0.80, 0.20), fill(sd_circle(p - vec2(-0.55, 0.0), 0.16)));
  col = mix(col, vec3(0.35, 0.70, 1.00), fill(sd_box(p - vec2(-0.18, 0.0), vec2(0.14, 0.10))));
  col = mix(col, vec3(0.50, 0.90, 0.50), fill(sd_round_box(p - vec2(0.22, 0.0), vec2(0.15, 0.11), 0.07)));
  col = mix(col, vec3(1.00, 0.45, 0.55), fill(sd_segment(p, vec2(0.50, -0.12), vec2(0.72, 0.12), 0.04)));

  finalColor = vec4(col, 1.0);
}
