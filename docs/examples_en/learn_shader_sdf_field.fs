#version 330

in vec2 fragTexCoord;
uniform vec2 resolution;
uniform vec2 mouse;        // 0..1, the circle's center follows the mouse
out vec4 finalColor;

// SIGNED distance from point p to the circle of radius r around the origin:
// negative inside, 0 exactly on the boundary, positive outside.
float sd_circle(vec2 p, float r) {
  return length(p) - r;
}

void main() {
  vec2 aspect = vec2(resolution.x / resolution.y, 1.0);
  vec2 p = (fragTexCoord - 0.5) * aspect;
  vec2 center = (mouse - 0.5) * aspect;

  float d = sd_circle(p - center, 0.2);

  // Show d as a color: orange inside, blue outside, brighter the farther from the boundary.
  vec3 col = d > 0.0 ? vec3(0.35, 0.55, 0.90) : vec3(0.95, 0.60, 0.20);
  col *= 1.0 - exp(-6.0 * abs(d));            // dark near the boundary, brighter farther out
  col *= 0.8 + 0.2 * cos(150.0 * d);          // contour lines: the same distance is the same ring
  col = mix(col, vec3(1.0), 1.0 - smoothstep(0.0, 0.01, abs(d)));   // paint white exactly where d = 0
  finalColor = vec4(col, 1.0);
}
