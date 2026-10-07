#version 330

in vec2 fragTexCoord;
uniform vec2 resolution;
uniform vec2 mouse;        // mouse.x plays the "health amount" and the "cooldown progress", 0..1
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

  // --- Health bar: rounded frame, dark background, the filled part cut by a straight line ---
  vec2 bar_center = vec2(0.0, -0.30);
  vec2 bar_half = vec2(0.40, 0.045);
  vec2 q = p - bar_center;

  float frame = sd_round_box(q, bar_half, 0.03);
  float inner = sd_round_box(q, bar_half - 0.012, 0.02);
  float cut = q.x - (-bar_half.x + 2.0 * bar_half.x * amount);   // negative left of the cut line
  float filled = max(inner, cut);                                 // intersection: inside the frame AND left of the cut line

  col = mix(col, vec3(0.30, 0.30, 0.38), fill(frame));            // frame border
  col = mix(col, vec3(0.05, 0.05, 0.08), fill(inner));            // inside of the bar
  col = mix(col, vec3(0.90, 0.25, 0.30), fill(filled));           // the health part
  // Glow: falls off with the distance out of the frame. exp(-k * d) is the cheapest way to make a "glow".
  // Add it only OUTSIDE the frame (1 - fill(frame)); inside, d is negative, exp(...) would exceed 1 and wash out the whole bar.
  col += vec3(0.9, 0.2, 0.25) * 0.25 * exp(-40.0 * max(frame, 0.0)) * (1.0 - fill(frame));

  // --- Cooldown ring: a thick ring, only the recovered part filled, clockwise from the top ---
  vec2 c = p - vec2(0.0, 0.10);
  float ring = abs(length(c) - 0.17) - 0.028;                     // a ring 0.056 thick
  float turn = fract(atan(c.x, -c.y) / 6.2831853);                // 0 at the top, increasing clockwise
  float ready = 1.0 - step(amount, turn);                         // 1 if this angle has recovered

  col = mix(col, vec3(0.20, 0.22, 0.30), fill(ring));             // background ring
  col = mix(col, vec3(0.35, 0.80, 1.00), fill(ring) * ready);     // recovered part
  finalColor = vec4(col, 1.0);
}
