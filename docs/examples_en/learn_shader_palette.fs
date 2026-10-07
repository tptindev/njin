#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
out vec4 finalColor;

// A three-step palette: t = 0 is dark, 0.5 is medium, 1 is light.
vec3 ramp(float t) {
  vec3 dark = vec3(0.10, 0.05, 0.25);
  vec3 mid = vec3(0.80, 0.25, 0.55);
  vec3 light = vec3(1.00, 0.90, 0.60);
  return t < 0.5 ? mix(dark, mid, t * 2.0) : mix(mid, light, (t - 0.5) * 2.0);
}

void main() {
  vec4 c = texture(texture0, fragTexCoord) * fragColor;
  float g = dot(c.rgb, vec3(0.299, 0.587, 0.114));   // brightness of the source pixel
  finalColor = vec4(ramp(g), c.a);                   // the new color is picked by brightness
}
