#version 330

in vec2 uv;
in vec4 color;
out vec4 finalColor;

void main() {
  // Một hình tròn mịn trong hình vuông.
  float d = length(uv * 2.0 - 1.0);
  float edge = fwidth(d);
  finalColor = vec4(color.rgb, color.a * (1.0 - smoothstep(1.0 - edge, 1.0, d)));
}
