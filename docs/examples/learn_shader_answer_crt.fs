#version 330

in vec2 fragTexCoord;
uniform sampler2D texture0;
uniform vec2 resolution;
out vec4 finalColor;

void main() {
  vec3 c = texture(texture0, fragTexCoord).rgb;

  // Vignette
  float d = distance(fragTexCoord, vec2(0.5));
  float light = 1.0 - smoothstep(0.30, 0.80, d);

  // Sọc ngang
  float stripe = 0.85 + 0.15 * sin(fragTexCoord.y * resolution.y * 3.14159);

  // Cả hai nhân vào cùng một pixel trong MỘT lượt vẽ: không cần hai render texture liên tiếp.
  finalColor = vec4(c * light * stripe, 1.0);
}
