#pragma once
#include "_types.h"

namespace njin {
struct context;

/// @addtogroup grp_post
/// @{

/// Các hiệu ứng post-processing dựng sẵn, áp lên thế giới (không lên UI).
///
/// Mỗi hiệu ứng tắt khi ở giá trị mặc định; bật cái nào thì chỉnh trường của
/// cái đó. Đặt bằng post_fx_set(), sửa được mỗi frame (ví dụ tăng vignette đỏ
/// khi máu thấp). Thứ tự áp: blur, độ sâu trường ảnh, bloom, rồi một lượt gồm cong CRT, pixelate,
/// tách màu, chỉnh màu, scanline, vignette, nhiễu hạt. Nếu game có shader
/// riêng (camera_set_post_shader()), shader đó chạy **sau cùng**.
///
/// Bloom và blur tốn thêm vài lượt vẽ toàn màn hình; các hiệu ứng còn lại
/// gộp trong một lượt nên gần như miễn phí.
struct post_fx {
  /// @name Chỉnh màu
  /// @{
  f32 brightness = 0.0f; ///< Độ sáng cộng thêm, -1..1. 0 là tắt.
  f32 contrast = 1.0f;   ///< Độ tương phản. 1 là giữ nguyên.
  f32 saturation = 1.0f; ///< Độ bão hòa. 0 là đen trắng, 1 là giữ nguyên.
  f32 sepia = 0.0f;      ///< Mức ngả màu ảnh cũ, 0..1.
  rgba tint{1.0f, 1.0f, 1.0f, 1.0f}; ///< Màu nhân vào cả khung hình.
  /// @}

  /// @name Vignette (tối viền)
  /// @{
  f32 vignette = 0.0f;         ///< Độ đậm, 0..1. 0 là tắt.
  f32 vignette_radius = 0.75f; ///< Bán kính vùng sáng, tính từ tâm (0.7 gần tới cạnh).
  f32 vignette_softness = 0.45f; ///< Độ mềm của viền.
  rgba vignette_color{0.0f, 0.0f, 0.0f, 1.0f}; ///< Màu viền. Đỏ cho hiệu ứng máu thấp.
  /// @}

  /// @name Bloom (quầng sáng)
  /// @{
  f32 bloom = 0.0f;           ///< Độ mạnh. 0 là tắt, 0.5–1.5 là thường dùng.
  f32 bloom_threshold = 0.7f; ///< Chỉ vùng sáng hơn mức này (0..1) mới tỏa sáng.
  f32 bloom_radius = 1.0f;    ///< Độ tỏa. 1 là mặc định, 2 là rộng gấp đôi.
  /// @}

  /// @name Làm mờ và máy cũ
  /// @{
  f32 blur = 0.0f;       ///< Làm mờ cả khung hình, bán kính pixel. 0 là tắt. Cho menu pause.
  f32 chromatic = 0.0f;  ///< Tách màu đỏ/xanh ở rìa, pixel. 0 là tắt. 2–6 cho cú đánh mạnh.
  f32 scanlines = 0.0f;  ///< Độ đậm của sọc ngang kiểu màn CRT, 0..1. 0 là tắt.
  f32 scanline_size = 3.0f; ///< Khoảng cách giữa các sọc, pixel màn hình.
  f32 crt_curve = 0.0f;  ///< Độ cong kiểu màn CRT, 0..0.3. 0 là tắt.
  f32 pixelate = 0.0f;   ///< Cỡ ô vuông pixel hóa, pixel màn hình. Dưới 2 là tắt.
  f32 grain = 0.0f;      ///< Nhiễu hạt kiểu phim, 0..0.3. 0 là tắt.
  /// @}

  /// @name Độ sâu trường ảnh (depth of field, cho 3D)
  /// Nét quanh một khoảng cách, mờ dần ở gần hơn và xa hơn, như ống kính máy
  /// ảnh lấy nét. Đọc độ sâu của những gì vẽ giữa begin_3d() và end_3d() trong
  /// frame; không có 3D thì không áp. Khoảng cách đo theo hướng nhìn của camera,
  /// bằng đơn vị 3D. Vd. lấy nét vào một vật: `dof_focus` là khoảng cách từ camera
  /// tới nó.
  /// @{
  f32 dof = 0.0f;           ///< Độ mờ tối đa của phần ngoài tiêu điểm, bán kính pixel. 0 là tắt.
  f32 dof_focus = 10.0f;    ///< Khoảng cách nét nhất.
  f32 dof_range = 2.0f;     ///< Nét hoàn toàn trong khoảng `dof_focus` ± `dof_range`.
  f32 dof_falloff = 10.0f;  ///< Quãng mờ dần từ nét đến mờ nhất, sau `dof_range`.
  /// @}
};

/// Đặt các hiệu ứng post-processing dựng sẵn. `post_fx{}` là tắt hết.
/// @param ctx Context của engine.
/// @param fx Các hiệu ứng.
void post_fx_set(context &ctx, const post_fx &fx);

/// Các hiệu ứng đang đặt. Sửa bản sao rồi post_fx_set() lại.
/// @param ctx Context của engine.
/// @return Các hiệu ứng hiện tại.
post_fx post_fx_get(const context &ctx);

/// Nội suy giữa hai bộ hiệu ứng, để chuyển mượt (ví dụ vào menu pause).
/// @param a Bộ ở `t = 0`.
/// @param b Bộ ở `t = 1`.
/// @param t Tiến độ, 0..1.
/// @return Bộ hiệu ứng ở giữa.
post_fx post_fx_lerp(const post_fx &a, const post_fx &b, f32 t);

/// Các bộ post_fx hay dùng. Là giá trị thường: sửa thoải mái trước khi đặt.
///
/// @code
/// njin::post_fx fx = njin::post::crt();
/// fx.bloom = 0.6f;
/// njin::post_fx_set(ctx, fx);
/// @endcode
namespace post {
/// Màn hình CRT cũ: cong, sọc, tách màu nhẹ, tối viền.
/// @return Bộ hiệu ứng.
inline post_fx crt() {
  post_fx p{};
  p.crt_curve = 0.12f;
  p.scanlines = 0.35f;
  p.chromatic = 1.5f;
  p.vignette = 0.45f;
  p.contrast = 1.1f;
  p.brightness = 0.03f;
  return p;
}

/// Phim đen trắng: không màu, tương phản cao, tối viền, nhiễu hạt.
/// @return Bộ hiệu ứng.
inline post_fx noir() {
  post_fx p{};
  p.saturation = 0.0f;
  p.contrast = 1.25f;
  p.vignette = 0.6f;
  p.vignette_radius = 0.65f;
  p.grain = 0.08f;
  return p;
}

/// Ảnh cũ: ngả nâu, hơi mờ nhạt, tối viền.
/// @return Bộ hiệu ứng.
inline post_fx vintage() {
  post_fx p{};
  p.sepia = 0.8f;
  p.contrast = 0.9f;
  p.vignette = 0.5f;
  p.grain = 0.05f;
  return p;
}

/// Mơ màng: quầng sáng rộng, màu tươi hơn, tối viền nhẹ.
/// @return Bộ hiệu ứng.
inline post_fx dream() {
  post_fx p{};
  p.bloom = 0.9f;
  p.bloom_threshold = 0.55f;
  p.bloom_radius = 1.6f;
  p.saturation = 1.15f;
  p.vignette = 0.25f;
  return p;
}

/// Sáng rực: bloom cho game neon, lửa, phép thuật.
/// @return Bộ hiệu ứng.
inline post_fx glow() {
  post_fx p{};
  p.bloom = 1.0f;
  p.bloom_threshold = 0.65f;
  return p;
}

/// Pixel thô kiểu máy 8-bit, kèm sọc nhẹ.
/// @return Bộ hiệu ứng.
inline post_fx retro() {
  post_fx p{};
  p.pixelate = 4.0f;
  p.scanlines = 0.2f;
  p.scanline_size = 4.0f;
  p.saturation = 1.1f;
  return p;
}

/// Bị thương hoặc máu thấp: viền đỏ, hơi nhạt màu. Nhân `vignette` theo mức
/// máu mất để tăng dần.
/// @return Bộ hiệu ứng.
inline post_fx hurt() {
  post_fx p{};
  p.vignette = 0.7f;
  p.vignette_radius = 0.6f;
  p.vignette_color = {0.6f, 0.0f, 0.0f, 1.0f};
  p.saturation = 0.7f;
  return p;
}

/// Menu pause: thế giới mờ và tối phía sau UI.
/// @return Bộ hiệu ứng.
inline post_fx paused() {
  post_fx p{};
  p.blur = 6.0f;
  p.brightness = -0.15f;
  p.saturation = 0.6f;
  return p;
}
} // namespace post
/// @}
} // namespace njin
