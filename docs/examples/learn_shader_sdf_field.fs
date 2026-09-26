#version 330

in vec2 fragTexCoord;
uniform vec2 resolution;
uniform vec2 mouse;        // 0..1, tâm hình tròn đi theo chuột
out vec4 finalColor;

// Khoảng cách CÓ DẤU từ điểm p đến hình tròn bán kính r quanh gốc:
// âm bên trong, 0 đúng trên đường biên, dương bên ngoài.
float sd_circle(vec2 p, float r) {
  return length(p) - r;
}

void main() {
  vec2 aspect = vec2(resolution.x / resolution.y, 1.0);
  vec2 p = (fragTexCoord - 0.5) * aspect;
  vec2 center = (mouse - 0.5) * aspect;

  float d = sd_circle(p - center, 0.2);

  // Hiện d thành màu: cam bên trong, xanh bên ngoài, càng xa biên càng sáng.
  vec3 col = d > 0.0 ? vec3(0.35, 0.55, 0.90) : vec3(0.95, 0.60, 0.20);
  col *= 1.0 - exp(-6.0 * abs(d));            // tối sát biên, sáng dần khi ra xa
  col *= 0.8 + 0.2 * cos(150.0 * d);          // các đường đồng mức: cùng khoảng cách thì cùng vòng
  col = mix(col, vec3(1.0), 1.0 - smoothstep(0.0, 0.01, abs(d)));   // vẽ trắng đúng chỗ d = 0
  finalColor = vec4(col, 1.0);
}
