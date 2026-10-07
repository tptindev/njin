#version 330

in vec2 fragTexCoord;   // image coordinates: (0, 0) at the top left, (1, 1) at the bottom right
out vec4 finalColor;

void main() {
  // Red channel follows the horizontal axis, green the vertical one. Blue stays 0.
  finalColor = vec4(fragTexCoord.x, fragTexCoord.y, 0.0, 1.0);
}
