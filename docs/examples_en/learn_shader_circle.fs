#version 330

in vec2 fragTexCoord;
uniform vec2 resolution;   // window size, passed in by the program
out vec4 finalColor;

void main() {
  // Move the center to (0, 0) and measure in "window height" units, multiplied by the aspect ratio
  // so the circle is not squashed into an ellipse.
  vec2 p = (fragTexCoord - 0.5) * vec2(resolution.x / resolution.y, 1.0);

  float d = length(p);                        // distance from the center
  float inside = 1.0 - smoothstep(0.28, 0.30, d);   // 1 inside the circle, 0 outside, slightly soft at the edge

  vec3 background = vec3(0.10, 0.10, 0.15);
  vec3 disc = vec3(1.0, 0.8, 0.2);
  finalColor = vec4(mix(background, disc, inside), 1.0);
}
