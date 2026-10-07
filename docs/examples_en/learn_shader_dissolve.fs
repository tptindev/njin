#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 mouse;   // mouse.x: dissolve progress, 0 is intact, 1 is gone
out vec4 finalColor;

// A pseudo-random number from a coordinate: the same input always gives the same result.
float hash(vec2 p) {
  return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
  vec4 c = texture(texture0, fragTexCoord) * fragColor;

  // Each source pixel has its own random number (snapped to the pixel cell so the pieces are square).
  vec2 cell = floor(fragTexCoord * vec2(textureSize(texture0, 0)));
  float n = hash(cell);

  float t = mouse.x * 1.2 - 0.1;             // goes from -0.1 to 1.1 to cover the whole 0..1 range of n
  if (n < t) discard;                        // this pixel has dissolved: draw nothing
  float edge = 1.0 - smoothstep(0.0, 0.08, n - t);   // the band about to dissolve
  vec3 glow = vec3(1.0, 0.6, 0.1);
  finalColor = vec4(mix(c.rgb, glow, edge), c.a);
}
