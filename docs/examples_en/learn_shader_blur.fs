#version 330

in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 resolution;
uniform vec2 mouse;   // mouse.x: blur radius, 0..1 (at most 6 pixels)
out vec4 finalColor;

void main() {
  vec2 texel = 1.0 / resolution;       // one pixel in image coordinates
  float radius = mouse.x * 6.0;

  // Average of 9 points around this position (a 3 x 3 grid, `radius` pixels apart).
  vec3 sum = vec3(0.0);
  for (int y = -1; y <= 1; y++) {
    for (int x = -1; x <= 1; x++) {
      // Outside 0..1, raylib repeats the image and picks the wrong color from the opposite edge. clamp keeps it inside,
      // leaving half a pixel, because exactly 0 or 1 is still blended with the other edge by the smooth filter.
      vec2 p = clamp(fragTexCoord + vec2(x, y) * texel * radius, texel * 0.5, 1.0 - texel * 0.5);
      sum += texture(texture0, p).rgb;
    }
  }
  finalColor = vec4(sum / 9.0, 1.0);
}
