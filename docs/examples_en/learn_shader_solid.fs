#version 330

// The shortest useful shader: paint every pixel the same color (red, green, blue, opacity), each number from 0 to 1.
out vec4 finalColor;

void main() {
  finalColor = vec4(1.0, 0.5, 0.2, 1.0);
}
