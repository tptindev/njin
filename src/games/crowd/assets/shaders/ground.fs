#version 330

// Nền sân dạng vải canvas mộc: dệt chéo mờ nhạt (hai lớp sợi vuông góc, xoay
// 45 độ), cộng một chút không đều màu trên diện rộng và hạt mịn. Sợi dùng
// sóng sin (mượt, không có cạnh cứng) và mờ dần theo đạo hàm màn hình
// (fwidth) khi camera lùi xa, để không tạo hoa văn moiré khi sợi nhỏ hơn một
// pixel. Toàn bộ sinh bằng nhiễu thủ tục, vẽ qua draw_rect() nên không cần
// ảnh nào.

in vec2 fragTexCoord;
uniform vec2 resolution; // kích thước hình chữ nhật đang vẽ, đơn vị thế giới
out vec4 finalColor;

float hash(vec2 p) {
  p = fract(p * vec2(123.34, 456.21));
  p += dot(p, p + 45.32);
  return fract(p.x * p.y);
}

float value_noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  float a = hash(i), b = hash(i + vec2(1.0, 0.0));
  float c = hash(i + vec2(0.0, 1.0)), d = hash(i + vec2(1.0, 1.0));
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

void main() {
  vec2 px = fragTexCoord * resolution; // toạ độ thế giới thực trên nền

  // Dệt chéo: xoay 45 độ rồi lấy hai hướng vuông góc, giống vải lanh dệt chéo.
  // Sóng sin mượt thay vì vạch cạnh cứng, ít bị răng cưa/moiré hơn khi thu nhỏ.
  const mat2 rot = mat2(0.70710678, -0.70710678, 0.70710678, 0.70710678);
  const float period = 9.0; // độ rộng một sợi, đơn vị thế giới
  vec2 wp = rot * px * (6.2831853 / period);
  float warp = sin(wp.x);
  float weft = sin(wp.y);

  // Sợi nhỏ hơn một pixel màn hình thì tắt dần, tránh hoa văn moiré khi camera lùi xa.
  float aa = fwidth(wp.x) + fwidth(wp.y);
  float fade = clamp(1.0 - aa * (1.0 / 3.0), 0.0, 1.0);
  float weave = (warp + weft) * 0.5 * fade;

  // Không đều màu rất nhạt trên diện rộng, tránh trông như in máy đồng nhất.
  float uneven = value_noise(px * 0.004);

  // Hạt mịn: nhiễu tần số rất cao, đổi mỗi pixel. Không có cạnh thẳng nên
  // không tạo moiré, chỉ là hạt lấm tấm kể cả khi thu nhỏ.
  float grain = hash(floor(px * 1.4));

  vec3 canvas = vec3(0.87, 0.87, 0.84);
  canvas *= 1.0 + weave * 0.035;
  canvas *= 0.97 + uneven * 0.06;
  canvas += (grain - 0.5) * 0.018;

  // Vignette rất nhẹ về phía mép sân.
  vec2 uv = fragTexCoord * 2.0 - 1.0;
  canvas *= 1.0 - dot(uv, uv) * 0.08;

  finalColor = vec4(canvas, 1.0);
}
