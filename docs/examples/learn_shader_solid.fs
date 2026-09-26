#version 330

// Shader ngắn nhất có ích: tô mọi pixel cùng một màu (đỏ, xanh lục, xanh lam, độ đục), mỗi số từ 0 đến 1.
out vec4 finalColor;

void main() {
  finalColor = vec4(1.0, 0.5, 0.2, 1.0);
}
