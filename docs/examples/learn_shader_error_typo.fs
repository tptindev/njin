#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

void main() {
  finalColor = vec4(fragTexCoords.x, fragTexCoord.y, 0.0, 1.0);
}
