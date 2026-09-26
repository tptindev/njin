#version 330

in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 resolution;
out vec4 finalColor;

void main() {
  vec3 c = texture(texture0, fragTexCoord).rgb;

  // Xám rồi nhuộm xanh lục, như màn hình đơn sắc kiểu cũ.
  float g = dot(c, vec3(0.299, 0.587, 0.114));
  vec3 col = g * vec3(0.4, 1.0, 0.5);

  // Sọc ngang: một sóng sin theo hàng pixel, cứ hai hàng thì một hàng tối đi một chút.
  float row = fragTexCoord.y * resolution.y;
  float stripe = 0.85 + 0.15 * sin(row * 3.14159);
  finalColor = vec4(col * stripe, 1.0);
}
