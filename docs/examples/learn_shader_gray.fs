#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;   // ảnh đang vẽ, raylib gán
uniform vec2 mouse;           // chuột, 0..1: kéo sang phải thì xám nhiều hơn
out vec4 finalColor;

void main() {
  vec4 c = texture(texture0, fragTexCoord) * fragColor;      // lấy màu của ảnh tại vị trí này
  float g = dot(c.rgb, vec3(0.299, 0.587, 0.114));           // độ sáng mắt người cảm nhận
  finalColor = vec4(mix(c.rgb, vec3(g), mouse.x), c.a);      // mix(a, b, t): t = 0 là a, t = 1 là b
}
