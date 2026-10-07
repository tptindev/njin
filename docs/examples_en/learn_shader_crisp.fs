#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
out vec4 finalColor;

void main() {
  vec2 size = vec2(textureSize(texture0, 0));

  // Snap the coordinate to the center of a source pixel: even with a smooth (bilinear) filter, every sample
  // lands exactly in the middle of a pixel, so there is nothing to blend, and the enlarged image stays sharp.
  vec2 uv = (floor(fragTexCoord * size) + 0.5) / size;
  finalColor = texture(texture0, uv) * fragColor;
}
