#version 330

in vec2 fragTexCoord;
uniform float time;   // giây kể từ lúc chạy, chương trình đưa vào mỗi frame
out vec4 finalColor;

void main() {
  // cos cho giá trị từ -1 đến 1; nhân 0.5 rồi cộng 0.5 đưa về 0..1.
  // Ba kênh lệch pha nhau (0, 2, 4) nên màu chạy qua nhiều sắc, và đổi theo vị trí lẫn thời gian.
  vec3 col = 0.5 + 0.5 * cos(time + fragTexCoord.xyx * 3.0 + vec3(0.0, 2.0, 4.0));
  finalColor = vec4(col, 1.0);
}
