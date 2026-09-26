#version 330

in vec2 fragTexCoord;   // toạ độ ảnh: (0, 0) ở góc trên trái, (1, 1) ở góc dưới phải
out vec4 finalColor;

void main() {
  // Kênh đỏ theo chiều ngang, kênh xanh lục theo chiều dọc. Kênh xanh lam để 0.
  finalColor = vec4(fragTexCoord.x, fragTexCoord.y, 0.0, 1.0);
}
