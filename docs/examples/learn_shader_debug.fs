#version 330

in vec2 fragTexCoord;
uniform sampler2D texture0;
out vec4 finalColor;

void main() {
  // Muốn biết một số trong shader là bao nhiêu thì hiện nó thành màu: không có printf trên GPU.
  // Ở đây: độ đục của ảnh thành màu xám. Vùng đen là trong suốt, vùng trắng là đục hẳn.
  float a = texture(texture0, fragTexCoord).a;
  finalColor = vec4(vec3(a), 1.0);
}
