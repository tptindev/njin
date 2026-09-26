#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 mouse;   // mouse.x: mức nháy, 0 là bình thường, 1 là trắng hoàn toàn
out vec4 finalColor;

void main() {
  vec4 c = texture(texture0, fragTexCoord) * fragColor;
  // Trộn màu của pixel về trắng, giữ nguyên độ đục: phần trong suốt vẫn trong suốt,
  // nên sprite sáng lên đúng theo hình dáng của nó.
  finalColor = vec4(mix(c.rgb, vec3(1.0), mouse.x), c.a);
}
