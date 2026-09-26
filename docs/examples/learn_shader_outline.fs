#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
out vec4 finalColor;

void main() {
  vec2 texel = 1.0 / vec2(textureSize(texture0, 0));   // một pixel của ảnh, theo toạ độ ảnh
  vec4 c = texture(texture0, fragTexCoord) * fragColor;

  // Độ đục lớn nhất trong bốn pixel kề (phải, trái, dưới, trên).
  float around = max(max(texture(texture0, fragTexCoord + vec2(texel.x, 0.0)).a,
                         texture(texture0, fragTexCoord - vec2(texel.x, 0.0)).a),
                     max(texture(texture0, fragTexCoord + vec2(0.0, texel.y)).a,
                         texture(texture0, fragTexCoord - vec2(0.0, texel.y)).a));

  // Pixel này trong suốt nhưng có pixel đục bên cạnh: nó nằm ngay ngoài mép, tô màu viền.
  vec4 outline = vec4(1.0, 1.0, 0.2, 1.0) * (1.0 - c.a) * around;
  finalColor = c + outline;
}
