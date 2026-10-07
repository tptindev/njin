#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
out vec4 finalColor;

void main() {
  vec4 c = texture(texture0, fragTexCoord) * fragColor;
  // Negative: each color channel becomes 1 - its old value. Only the color is inverted, not the opacity.
  finalColor = vec4(vec3(1.0) - c.rgb, c.a);
}
