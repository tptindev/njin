#pragma once
#include "_tilemap.h"
#include "_types.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace njin {
// Opaque, see njin_ctx.h.
struct context;

/// @addtogroup grp_light
/// @{

/// Loại đèn.
enum light_kind : i32 {
  light_point,       ///< Đèn điểm: tỏa đều mọi hướng từ `transform.pos`, tối dần theo khoảng cách.
  light_spot,        ///< Đèn nón (đèn pin, đèn sân khấu): chỉ sáng trong một góc quanh hướng `angle`.
  light_directional, ///< Đèn hướng (mặt trời, trăng): sáng đều cả cảnh, từ một hướng, không có vị trí.
};

/// Đường cong tối dần theo khoảng cách của đèn điểm và đèn nón.
enum light_falloff : i32 {
  /// Theo vật lý: độ sáng giảm theo nghịch đảo bình phương khoảng cách,
  /// `1 / (1 + (d / size)^2)`, rồi cắt mượt về 0 ở `radius` để đèn có vùng sáng
  /// hữu hạn. `size` là kích thước nguồn sáng: đúng bằng khoảng cách mà độ
  /// sáng còn một nửa. Lõi đèn sáng gắt, rìa tối nhanh, như đèn thật.
  falloff_physical,
  falloff_linear, ///< Giảm tuyến tính từ tâm đến `radius`. Dễ đoán nhất.
  falloff_smooth, ///< Giảm mượt, phẳng ở tâm, tắt hẳn ở `radius`. Kiểu quầng sáng cổ điển.
  falloff_none,   ///< Sáng đều trong `radius` rồi tắt hẳn ở rìa. Hợp với vùng sáng cố định.
};

/// Một nguồn sáng 2D. Cần transform trên cùng entity; `transform.pos` là vị
/// trí đèn, `transform.rot` cộng vào `angle`. Chỉ có tác dụng khi ánh sáng đã
/// được bật bằng lighting_set().
///
/// Ánh sáng là PBR (physically based rendering): mỗi pixel của cảnh có màu gốc
/// (albedo, chính là ảnh sprite), pháp tuyến (normal map), độ kim loại, độ nhám
/// và độ che khuất (bản đồ vật liệu), có thể thêm phát sáng, và mỗi đèn được tính
/// bằng mô hình Cook-Torrance (GGX, Smith, Fresnel-Schlick, như pbr.fs trong ví dụ
/// của raylib) trong không gian tuyến tính HDR, rồi tonemap. Xem trang Ánh sáng 2D trong docs.
struct light_2d {
  light_kind kind = light_point; ///< Loại đèn.
  /// Màu đèn, kiểu sRGB như mọi màu khác trong njin (engine tự đổi sang tuyến
  /// tính). Kênh alpha bị bỏ qua.
  rgba color{1.0f, 1.0f, 1.0f, 1.0f};
  /// Nhiệt độ màu (kelvin). Lớn hơn 0 thì màu đèn được nhân thêm màu của một
  /// vật đen phát sáng ở nhiệt độ đó: nến 1900, đèn dây tóc 2700, ban ngày
  /// 6500, trời xanh 10000. 0 là tắt.
  f32 temperature = 0.0f;
  /// Cường độ bức xạ (không gian tuyến tính). Bề mặt nhám màu trắng ngay dưới
  /// đèn, đối diện đèn, sáng đúng bằng giá trị này; đèn theo vật lý giảm nhanh nên
  /// thường cần 4 đến 12. Có thể lớn hơn 1: ánh sáng là HDR, phần dư được tonemap.
  f32 intensity = 6.0f;
  f32 radius = 200.0f; ///< Bán kính tối đa (đơn vị thế giới): ngoài đó đèn không chiếu tới.
  /// Kích thước nguồn sáng (đơn vị thế giới). Với `falloff_physical` đây là
  /// khoảng cách độ sáng còn một nửa. Với mọi kiểu, nó là bán kính của nguồn
  /// sáng khi đổ bóng: nguồn càng lớn, bóng càng mờ ở xa vật chắn (vùng nửa tối).
  /// 0 cho bóng sắc nét.
  f32 size = 32.0f;
  light_falloff falloff = falloff_physical; ///< Đường cong tối dần. Bỏ qua với đèn hướng.
  /// Hướng đèn nón chiếu, hoặc hướng ánh sáng đi tới của đèn hướng, tính bằng
  /// độ: 0 là sang phải, 90 là xuống dưới (theo chiều kim đồng hồ). Cộng với
  /// `transform.rot`.
  f32 angle = 0.0f;
  f32 cone = 60.0f;      ///< Đèn nón: góc mở toàn phần, độ.
  f32 softness = 0.3f;   ///< Đèn nón: độ mềm của rìa nón, 0 (gắt) đến 1 (mờ từ tâm).
  /// Độ cao của đèn so với mặt phẳng cảnh (đơn vị thế giới). Đèn càng thấp,
  /// bề mặt nghiêng càng rõ và phản xạ gương càng lệch. Với đèn hướng thay bằng `elevation`.
  f32 height = 40.0f;
  f32 elevation = 45.0f; ///< Đèn hướng: góc của đèn so với mặt phẳng cảnh, độ (90 là thẳng từ trên xuống).
  bool cast_shadows = true; ///< Bị vật chắn (light_occluder) che và đổ bóng.
  bool enabled = true;      ///< Tắt mà không cần gỡ component.
};

/// Vật chắn sáng: một hình chắn ánh sáng và đổ bóng. Cần transform trên cùng
/// entity; các điểm tính từ `transform.pos`, xoay và co giãn theo transform, nên
/// vật chắn di chuyển, xoay, co giãn cùng entity. Sửa `points` mỗi frame cũng được:
/// bóng theo hình mới ngay.
///
/// Có ba cách có hình:
/// - Tự chỉ điểm: `points` là đa giác đóng (`closed` mặc định), hoặc đường
///   gấp khúc mở (một bức tường mỏng, `closed = false`). Các hàm dựng sẵn bên dưới
///   (light_occluder_box(), light_occluder_circle(), light_occluder_ellipse(),
///   light_occluder_capsule(), light_occluder_line()) cho các hình hay dùng.
/// - Theo hình sprite: gắn light_occluder_sprite() cạnh sprite.
/// - Theo tilemap: light_occluders_from_tiles().
///
/// Vật chắn đặc không tự che chính nó: điểm nằm trong hình không bị chính hình
/// đó đổ bóng, và đèn nằm trong hình (đuốc trên người nhân vật) không bị hình
/// đó chặn. Đa giác đóng viết theo chiều nào cũng được. Mỗi đèn chỉ xét tối đa 64 cạnh gần
/// nhất trong vùng của nó, nên đừng dùng quá nhiều cạnh cho vật nhỏ.
struct light_occluder {
  std::vector<vec2> points; ///< Các điểm theo thứ tự. Đóng vòng lại khi `closed`. Ít nhất 2.
  /// Đa giác đóng (đặc, chắn từ phía ngoài vào) hay đường mở (mỏng, chắn cả hai phía).
  bool closed = true;
  /// Đa giác đóng là một cái lỗ trong khối đặc: bên trong là khoảng trống, vật đặc
  /// nằm bên ngoài. Dùng cho vòng trong của một căn phòng có tường dày (vòng ngoài là
  /// đặc, vòng trong là lỗ). light_occluders_from_tiles() tự đặt cho các vòng lỗ.
  bool hole = false;
  /// Khoảng xa nhất từ điểm neo đến một điểm của hình, cho engine loại nhanh vật chắn
  /// nằm ngoài tầm nhìn mà không phải duyệt các điểm. Các hàm dựng sẵn tự đặt. Để 0
  /// thì engine tự đo mỗi frame (chậm hơn một chút khi có hàng nghìn vật chắn); nếu
  /// đặt thì phải đúng hoặc lớn hơn, không thì vật chắn có thể bị bỏ sót khi ở rìa.
  f32 reach = 0.0f;
};

/// Vật chắn hình chữ nhật.
/// @param size Kích thước (rộng, cao).
/// @param origin Điểm neo tính theo tỉ lệ kích thước, như sprite::origin: `{0.5, 1}` là giữa đáy, hợp với gốc cây.
/// @return Vật chắn hình chữ nhật.
inline light_occluder light_occluder_box(vec2 size, vec2 origin = {0.5f, 0.5f}) {
  const f32 x0 = -origin.x * size.x, y0 = -origin.y * size.y;
  const f32 reach = std::sqrt(std::max(x0 * x0, (x0 + size.x) * (x0 + size.x)) + std::max(y0 * y0, (y0 + size.y) * (y0 + size.y)));
  return light_occluder{{{x0, y0}, {x0 + size.x, y0}, {x0 + size.x, y0 + size.y}, {x0, y0 + size.y}}, true, false, reach};
}

/// Vật chắn hình elip (đa giác đều đủ nhiều cạnh). Hình tròn khi hai bán kính bằng nhau.
/// @param radii Bán kính theo hai trục.
/// @param center Tâm, tính từ điểm neo của entity.
/// @param segments Số cạnh, từ 3. Nhiều cạnh thì tròn hơn nhưng đèn tốn hơn.
/// @return Vật chắn.
inline light_occluder light_occluder_ellipse(vec2 radii, vec2 center = {0.0f, 0.0f}, i32 segments = 16) {
  light_occluder o;
  const i32 n = segments < 3 ? 3 : segments;
  for (i32 i = 0; i < n; i++) {
    const f32 a = 6.28318530718f * (f32)i / (f32)n;
    o.points.push_back({center.x + std::cos(a) * radii.x, center.y + std::sin(a) * radii.y});
  }
  o.reach = std::hypot(center.x, center.y) + std::max(radii.x, radii.y);
  return o;
}

/// Vật chắn hình tròn.
/// @param radius Bán kính.
/// @param center Tâm, tính từ điểm neo của entity.
/// @param segments Số cạnh, từ 3.
/// @return Vật chắn.
inline light_occluder light_occluder_circle(f32 radius, vec2 center = {0.0f, 0.0f}, i32 segments = 16) {
  return light_occluder_ellipse({radius, radius}, center, segments);
}

/// Vật chắn hình viên thuốc (capsule): đoạn thẳng từ `a` đến `b` phình ra `radius`.
/// Hợp với người đứng, thân cây, cột: dài mà tròn hai đầu.
/// @param a Tâm đầu thứ nhất, tính từ điểm neo của entity.
/// @param b Tâm đầu thứ hai.
/// @param radius Bán kính.
/// @param arc_segments Số cạnh mỗi nửa vòng tròn ở hai đầu.
/// @return Vật chắn.
inline light_occluder light_occluder_capsule(vec2 a, vec2 b, f32 radius, i32 arc_segments = 6) {
  light_occluder o;
  const f32 dx = b.x - a.x, dy = b.y - a.y;
  const f32 len = std::sqrt(dx * dx + dy * dy);
  const f32 base = len > 1e-6f ? std::atan2(dy, dx) : 0.0f;
  const i32 n = arc_segments < 1 ? 1 : arc_segments;
  // The half circle around `b`, from the left of the axis to the right, then the one around `a`.
  for (i32 i = 0; i <= n; i++) {
    const f32 t = base - 1.57079632679f + 3.14159265359f * (f32)i / (f32)n;
    o.points.push_back({b.x + std::cos(t) * radius, b.y + std::sin(t) * radius});
  }
  for (i32 i = 0; i <= n; i++) {
    const f32 t = base + 1.57079632679f + 3.14159265359f * (f32)i / (f32)n;
    o.points.push_back({a.x + std::cos(t) * radius, a.y + std::sin(t) * radius});
  }
  o.reach = std::max(std::hypot(a.x, a.y), std::hypot(b.x, b.y)) + radius;
  return o;
}

/// Vật chắn là một đường gấp khúc mỏng, chắn cả hai phía: tường, hàng rào, mép vách.
/// @param points Các điểm theo thứ tự, ít nhất 2.
/// @return Vật chắn mở.
inline light_occluder light_occluder_line(std::vector<vec2> points) {
  light_occluder o{std::move(points), false};
  for (const vec2 &p : o.points)
    o.reach = std::max(o.reach, std::hypot(p.x, p.y));
  return o;
}

/// Vật chắn theo hình sprite: viền của các pixel đủ đục trong frame đang hiện của
/// sprite trên cùng entity, nên bóng đúng hình cái cây, con quái, và đổi theo
/// từng frame animation, lật ngang, xoay và co giãn. Cần sprite và transform; thay
/// cho light_occluder trên entity đó.
///
/// Viền được tính một lần cho mỗi frame của mỗi ảnh rồi nhớ lại, nên chi phí chỉ
/// ở lần đầu một frame xuất hiện (đọc ảnh từ card đồ họa). Ảnh vẽ vào render
/// texture không dùng được. Ảnh xếp trong atlas thì dùng được.
struct light_occluder_sprite {
  /// Pixel có alpha từ ngưỡng này trở lên là vật đặc, 0..1.
  f32 alpha = 0.5f;
  /// Độ lệch tối đa (pixel ảnh) của viền so với viền pixel thật. 0 giữ nguyên
  /// từng bậc thang (nhiều cạnh); 1 đến 2 làm trơn còn vài cạnh, rẻ hơn nhiều.
  f32 simplify = 1.0f;
};

/// Vật chắn **từng pixel**: những pixel đủ đục của ảnh sprite (hoặc của `mask`) chắn sáng và
/// đổ bóng đúng từng pixel, không cần dựng hình. Cần sprite và transform trên cùng entity.
///
/// Cách làm theo bài "2D Pixel-Perfect Shadows" của mattdesl: các vật chắn được vẽ vào
/// một ảnh (bản đồ vật chắn), rồi với mỗi đèn, ray-march theo từng góc quanh đèn để biết
/// vật chắn đầu tiên ở khoảng cách nào (bản đồ bóng 1D). Chi phí phụ thuộc vào cỡ vùng
/// đèn chiếu tới, **không phụ thuộc số vật chắn**, nên hợp với hàng nghìn cây, cỏ, đá.
///
/// So với light_occluder và light_occluder_sprite (dựng bằng đa giác): bóng theo từng
/// pixel của ảnh, hết cả chi tiết nhỏ và lỗ hổng; nhưng chỉ có vật chắn nằm trong ảnh màn hình
/// mở rộng thêm `lighting_desc::occluder_margin` mới đổ bóng, và không có "đường mở" mỏng.
/// Có thể dùng cả hai cùng lúc. Bóng mềm theo `light_2d::size`, mềm dần từ chỗ vật chắn.
///
/// Vật chắn tự nó vẫn sáng (điểm nằm trong vật chắn đầu tiên trên tia không bị chính nó
/// che), và đèn nằm trong một vật chắn không bị vật chắn đó chặn.
struct light_occluder_pixels {
  /// Ảnh dùng làm vật chắn: pixel có alpha từ `lighting_desc::pixel_alpha` trở lên là vật
  /// đặc. Cùng kích thước và cách xếp frame với `sprite::texture`. Trống thì dùng chính ảnh
  /// của sprite. Dùng để cho chỉ thân cây chắn sáng chứ không phải cả tán lá.
  texture_handle mask{};
};

/// Viền các ô đặc của một tilemap thành các vật chắn, để tường và vách chặn sáng.
///
/// Chỉ những cạnh giữa ô đặc và ô không đặc mới thành viền, và các cạnh thẳng
/// hàng được nối lại, nên một khối tường lớn chỉ có vài cạnh. Kết quả tính từ gốc
/// của tilemap: gắn mỗi vật chắn vào một entity có transform của tilemap. Tính lại
/// khi tilemap đổi (đào tường, mở cửa).
/// @param map Tilemap.
/// @param solid Cho biết ô (số thứ tự trong tileset, không kèm bit lật) có chắn sáng không.
/// @param simplify Làm trơn, tính bằng ô; 0 là giữ nguyên.
/// @return Các vật chắn đóng (một vòng cho mỗi khối liền và mỗi lỗ trong khối).
std::vector<light_occluder> light_occluders_from_tiles(const tilemap &map, const std::function<bool(i32 tile)> &solid,
                                                       f32 simplify = 0.0f);

/// Cách nén ánh sáng HDR về màn hình. Đều chạy sau độ phơi sáng (lighting_desc::exposure).
enum light_tonemap : i32 {
  /// Giữ nguyên các tông dưới 0.6, chỉ ép mượt phần sáng hơn về 1. Màu pixel art ở chỗ
  /// vừa sáng không bị đổi, nên dễ dùng nhất. Mặc định.
  tonemap_shoulder,
  tonemap_reinhard, ///< `x / (1 + x)`: đơn giản, mềm, nhưng làm cả cảnh nhạt và tối đi.
  tonemap_aces,     ///< ACES filmic (xấp xỉ của Narkowicz): tương phản điện ảnh, bão hòa vừa. Nên đặt `exposure` cao hơn.
};

/// Cài đặt chung của ánh sáng. Đặt bằng lighting_set().
struct lighting_desc {
  /// Bật ánh sáng. Tắt thì cảnh vẽ như bình thường, không tốn gì. Bật làm cả
  /// thế giới đi qua các lượt ánh sáng và tonemap (trước post_fx và shader hậu
  /// kỳ của camera). Ánh sáng chỉ chạy khi có đèn, hoặc ambient làm tối cảnh.
  bool enabled = false;
  /// Ánh sáng nền, có ở mọi nơi kể cả chỗ không có đèn, nhân với màu gốc và độ
  /// che khuất (ambient occlusion) của bề mặt. Màu sRGB như mọi màu trong njin.
  /// Trắng là không tối đi chút nào, đen là tối hẳn. Ban đêm thường là một xanh sẫm.
  rgba ambient{0.12f, 0.14f, 0.24f, 1.0f};
  /// Hệ số nhân toàn bộ ánh sáng trước khi tonemap, gồm cả ambient. Để chỉnh độ
  /// sáng chung (ví dụ giảm dần lúc trời sáng), như độ phơi sáng của máy ảnh.
  f32 exposure = 1.0f;
  /// Cách nén ánh sáng HDR về màn hình.
  light_tonemap tonemap = tonemap_shoulder;
  /// Số cột của bản đồ bóng mỗi đèn dùng light_occluder_pixels: số góc quanh một đèn điểm (số dải
  /// của đèn hướng). 0 là tự chọn: đủ để mỗi tia cách nhau khoảng một pixel ở rìa đèn rộng nhất, nên
  /// vật chắn mảnh không lọt giữa hai tia (nếu ít hơn, bóng xa đèn vỡ thành các tia nan quạt). Đặt số
  /// cụ thể (256 đến 4096) để giới hạn chi phí.
  i32 shadow_columns = 0;
  /// Ngưỡng alpha của light_occluder_pixels, 0..1: pixel từ ngưỡng này trở lên là vật đặc.
  f32 pixel_alpha = 0.5f;
  /// Vật chắn light_occluder_pixels được đọc trong ảnh màn hình mở rộng thêm khoảng này về mọi phía
  /// (đơn vị thế giới): vật chắn xa hơn thế không đổ bóng. Lớn hơn thì tốn bộ nhớ và fill nhiều hơn.
  f32 occluder_margin = 96.0f;
  /// Kích thước ảnh ánh sáng so với ảnh thế giới, 0.25 đến 1. 1 là đầy đủ chi
  /// tiết (cần cho pixel art). Nhỏ hơn thì cả ảnh đã chiếu sáng được tính ở độ
  /// phân giải thấp rồi phóng lên: nhanh hơn nhiều, nhưng mờ, chỉ hợp máy yếu.
  f32 scale = 1.0f;
  /// Độ dài bóng của đèn hướng (đơn vị thế giới): vật chắn xa hơn thế phía sau
  /// không đổ bóng lên điểm đang xét. Một vật cao H bị mặt trời ở góc `elevation`
  /// chiếu thì bóng dài `H / tan(elevation)`: với cây cao 24 và mặt trời 30 độ là
  /// khoảng 40. Mặc định 600 là bóng gần như vô hạn, chỉ hợp khi vật chắn thưa; ở khu
  /// rừng dày nó làm cả bản đồ chìm trong bóng. Áp dụng cho cả bóng đa giác và từng pixel.
  f32 shadow_reach = 600.0f;
};

/// Đặt cài đặt ánh sáng. Đổi mỗi frame được (ví dụ ambient theo giờ trong ngày).
/// @param ctx Context của engine.
/// @param desc Cài đặt mới.
void lighting_set(context &ctx, const lighting_desc &desc);

/// Cài đặt ánh sáng đang dùng. Sửa bản sao rồi lighting_set() lại.
/// @param ctx Context của engine.
/// @return Cài đặt hiện tại.
lighting_desc lighting_get(const context &ctx);

/// Màu của một vật đen phát sáng ở nhiệt độ cho trước.
///
/// Dùng để đặt `light_2d::color` hoặc để tô màu ambient (bình minh 3500,
/// trưa 6500, trăng lạnh 9000). Xấp xỉ trong khoảng 1000 đến 40000 kelvin.
/// @param kelvin Nhiệt độ màu.
/// @return Màu (alpha là 1), đã chuẩn hóa để kênh sáng nhất bằng 1.
rgba light_color_kelvin(f32 kelvin);
/// @}
} // namespace njin
