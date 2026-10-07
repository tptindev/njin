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

// Combine two shapes by combining two DISTANCES:
float op_union(float a, float b)     { return min(a, b); }       // inside a OR inside b
float op_intersect(float a, float b) { return max(a, b); }       // inside a AND inside b
float op_subtract(float a, float b)  { return max(a, -b); }      // inside a, minus what is inside b

// Smooth union: two shapes stick together like water drops when they get close. k is the "stickiness".
float op_smooth_union(float a, float b, float k) {
  float h = max(k - abs(a - b), 0.0) / k;
  return min(a, b) - h * h * k * 0.25;
}

void main() {
  vec2 p = (fragTexCoord - 0.5) * vec2(resolution.x / resolution.y, 1.0);

  // Two circles move closer, then apart; each of the four cells uses this same pair around the cell's center.
  // The distance between the two centers is 2 * offset, always less than the sum of the radii, so they always overlap.
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
