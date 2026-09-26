#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
out vec4 finalColor;

void main() {
  vec4 c = texture(texture0, fragTexCoord) * fragColor;
  // Âm bản: mỗi kênh màu thành 1 - giá trị cũ. Chỉ đảo màu, không đảo độ đục.
  finalColor = vec4(vec3(1.0) - c.rgb, c.a);
}
