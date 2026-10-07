#version 330

in vec2 fragTexCoord;
uniform vec2 resolution;
out vec4 finalColor;

// --- Four basic shapes: each function returns the signed distance to the shape, around the origin ---

float sd_circle(vec2 p, float r) {
  return length(p) - r;
}

// Box with half size b. Outside, it measures to the nearest corner or side; inside, it is negative.
float sd_box(vec2 p, vec2 b) {
  vec2 q = abs(p) - b;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
}

// Rounded box: a box smaller by r on each side, then "inflated" by r (subtract r from the distance).
float sd_round_box(vec2 p, vec2 b, float r) {
  return sd_box(p, b - r) - r;
}

// Segment a-b, thickness w: measure to the nearest point ON the segment, then subtract half the thickness.
float sd_segment(vec2 p, vec2 a, vec2 b, float w) {
  vec2 pa = p - a;
  vec2 ba = b - a;
  float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
  return length(pa - ba * h) - w;
}

// Turn a distance into 0..1 coverage, with an edge smooth over exactly ONE pixel at any zoom.
// fwidth(d) is how much d changes when you step to the next pixel.
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
