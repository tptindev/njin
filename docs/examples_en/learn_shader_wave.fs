#version 330

in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform float time;
uniform vec2 mouse;   // mouse.x: wave amplitude
out vec4 finalColor;

void main() {
  vec2 uv = fragTexCoord;
  // Shift the horizontal coordinate by a sine wave running down the image, moving over time.
  uv.x += sin(uv.y * 40.0 + time * 3.0) * 0.02 * mouse.x;
  finalColor = vec4(texture(texture0, uv).rgb, 1.0);
}
