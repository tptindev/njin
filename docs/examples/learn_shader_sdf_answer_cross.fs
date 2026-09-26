#version 330

in vec2 fragTexCoord;
uniform vec2 resolution;
out vec4 finalColor;

float sd_box(vec2 p, vec2 b) {
  vec2 q = abs(p) - b;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

float fill(float d) {
  float aa = fwidth(d);
  return 1.0 - smoothstep(-aa, aa, d);
}

// Dấu cộng = hợp của một hộp nằm ngang và một hộp đứng.
float sd_cross(vec2 p) {
  return min(sd_box(p, vec2(0.20, 0.06)), sd_box(p, vec2(0.06, 0.20)));
}

void main() {
  vec2 p = (fragTexCoord - 0.5) * vec2(resolution.x / resolution.y, 1.0);
  vec3 col = mix(vec3(0.10, 0.10, 0.15), vec3(0.95, 0.35, 0.40), fill(sd_cross(p)));
  finalColor = vec4(col, 1.0);
}
