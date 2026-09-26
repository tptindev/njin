#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
out vec4 finalColor;

void main() {
  vec2 size = vec2(textureSize(texture0, 0));

  // Đưa toạ độ về tâm pixel của ảnh gốc: dù bộ lọc là mượt (bilinear), mỗi lần lấy mẫu
  // đều rơi đúng giữa một pixel nên không còn gì để trộn, và ảnh phóng to vẫn sắc nét.
  vec2 uv = (floor(fragTexCoord * size) + 0.5) / size;
  finalColor = texture(texture0, uv) * fragColor;
}
