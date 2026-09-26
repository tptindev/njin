#version 330

in vec2 fragTexCoord;
uniform vec2 resolution;
uniform vec2 mouse;        // mouse.x đóng vai "lượng máu" và "tiến độ hồi chiêu", 0..1
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
  float amount = clamp(mouse.x, 0.0, 1.0);

  vec3 col = vec3(0.10, 0.10, 0.15);

  // --- Thanh máu: khung bo góc, nền tối, phần đầy cắt bằng một đường thẳng ---
  vec2 bar_center = vec2(0.0, -0.30);
  vec2 bar_half = vec2(0.40, 0.045);
  vec2 q = p - bar_center;

  float frame = sd_round_box(q, bar_half, 0.03);
  float inner = sd_round_box(q, bar_half - 0.012, 0.02);
  float cut = q.x - (-bar_half.x + 2.0 * bar_half.x * amount);   // âm bên trái đường cắt
  float filled = max(inner, cut);                                 // giao: trong khung VÀ bên trái đường cắt

  col = mix(col, vec3(0.30, 0.30, 0.38), fill(frame));            // viền khung
  col = mix(col, vec3(0.05, 0.05, 0.08), fill(inner));            // lòng thanh
  col = mix(col, vec3(0.90, 0.25, 0.30), fill(filled));           // phần máu
  // Quầng sáng: giảm theo khoảng cách ra khỏi khung. exp(-k * d) là cách rẻ nhất để làm "phát sáng".
  // Chỉ cộng ở BÊN NGOÀI khung (1 - fill(frame)); bên trong d âm, exp(...) sẽ lớn hơn 1 và làm cả thanh sáng bệch.
  col += vec3(0.9, 0.2, 0.25) * 0.25 * exp(-40.0 * max(frame, 0.0)) * (1.0 - fill(frame));

  // --- Vòng hồi chiêu: một vòng tròn dày, chỉ tô phần đã hồi, đi theo chiều kim đồng hồ từ đỉnh ---
  vec2 c = p - vec2(0.0, 0.10);
  float ring = abs(length(c) - 0.17) - 0.028;                     // vòng tròn dày 0.056
  float turn = fract(atan(c.x, -c.y) / 6.2831853);                // 0 ở đỉnh, tăng theo chiều kim đồng hồ
  float ready = 1.0 - step(amount, turn);                         // 1 nếu góc này đã hồi xong

  col = mix(col, vec3(0.20, 0.22, 0.30), fill(ring));             // vòng nền
  col = mix(col, vec3(0.35, 0.80, 1.00), fill(ring) * ready);     // phần đã hồi
  finalColor = vec4(col, 1.0);
}
