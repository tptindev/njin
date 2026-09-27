#version 330

// Cold-press watercolour paper, multiplied over the finished scene: a field of
// small rounded bumps (cellular noise), lit from the top left, plus a faint
// large-scale blotchiness where the sheet took up more or less water. The
// output is a factor near 1.0: blend_multiply turns it into grain on the paper
// and the paint alike.

// Positions are world pixels, from paper.vs: the grain belongs to the sheet,
// so it pans and zooms with the camera like everything drawn on it.
in vec2 world_pos;
out vec4 finalColor;

vec2 hash2(vec2 p) {
  p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
  return fract(sin(p) * 43758.5453);
}

float hash1(vec2 p) {
  return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

float value_noise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  float a = hash1(i);
  float b = hash1(i + vec2(1.0, 0.0));
  float c = hash1(i + vec2(0.0, 1.0));
  float d = hash1(i + vec2(1.0, 1.0));
  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Height of the paper surface: 1 on top of a bump, 0 in the dips between.
float bumps(vec2 p) {
  vec2 cell = floor(p);
  vec2 f = fract(p);
  float best = 8.0;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      vec2 o = vec2(float(x), float(y));
      vec2 r = o + hash2(cell + o) * 0.85 - f;
      best = min(best, dot(r, r));
    }
  }
  return 1.0 - smoothstep(0.0, 0.62, sqrt(best));
}

float height(vec2 px) {
  // Stretch the cells a little so they read as woven, not as dots.
  vec2 p = px / vec2(5.2, 4.3);
  return bumps(p) * 0.75 + bumps(p * 2.1 + 7.3) * 0.25;
}

void main() {
  vec2 px = world_pos;
  float e = 0.6;
  float h = height(px);
  float hx = height(px + vec2(e, 0.0)) - h;
  float hy = height(px + vec2(0.0, e)) - h;
  vec3 n = normalize(vec3(-hx * 2.2, -hy * 2.2, 1.0));
  // World y points down the screen, so -y is toward the top: lit from top left.
  vec3 light = normalize(vec3(-0.55, -0.65, 0.55));
  float lit = dot(n, light) - light.z; // 0 on flat paper

  float grain = 1.0 + lit * 0.11 - (1.0 - h) * 0.025;
  float blotch = value_noise(px / 170.0) * 0.6 + value_noise(px / 60.0 + 3.1) * 0.4;
  grain *= 1.0 - blotch * 0.035;
  // Tiny dark fibres here and there.
  grain *= 1.0 - step(0.9985, hash1(floor(px * 0.5))) * 0.12;

  // Multiplying can only darken, so keep the brightest ridges at 1.0.
  finalColor = vec4(vec3(min(grain, 1.0)), 1.0);
}
