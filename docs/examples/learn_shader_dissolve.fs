#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 mouse;   // mouse.x: tiến độ tan biến, 0 là còn nguyên, 1 là biến mất
out vec4 finalColor;

// Số ngẫu nhiên giả từ một toạ độ: cùng đầu vào luôn cho cùng kết quả.
float hash(vec2 p) {
  return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
  vec4 c = texture(texture0, fragTexCoord) * fragColor;

  // Mỗi pixel của ảnh gốc có một số ngẫu nhiên riêng (làm tròn về ô pixel để các mảnh vuông vức).
  vec2 cell = floor(fragTexCoord * vec2(textureSize(texture0, 0)));
  float n = hash(cell);

  float t = mouse.x * 1.2 - 0.1;             // đi từ -0.1 đến 1.1 để chạy hết dải 0..1 của n
  if (n < t) discard;                        // pixel này đã tan: không vẽ gì cả
  float edge = 1.0 - smoothstep(0.0, 0.08, n - t);   // dải mép vừa sắp tan
  vec3 glow = vec3(1.0, 0.6, 0.1);
  finalColor = vec4(mix(c.rgb, glow, edge), c.a);
}
