#pragma once
#include "_math.h"
#include "_types.h"

namespace njin {
struct context;

/// @addtogroup grp_post3d
/// @{

/// Hiệu ứng màn hình của cảnh 3D, tính từ độ sâu của lần vẽ 3D: che khuất môi
/// trường (SSAO), phản chiếu (SSR), mờ chuyển động, tia nắng và lóa ống kính.
///
/// Mọi hiệu ứng tắt ở giá trị mặc định: `post3d{}` vẽ y như khi không có nó. Đặt
/// bằng post3d_set(), sửa được mỗi frame. Chỉ áp cho lần vẽ 3D vào thế giới
/// (begin_3d() không có render texture), không áp cho lần vẽ vào render texture.
/// Thứ tự trong end_3d(): sau các hình đục là decal, SSAO, SSR; rồi kính, nước và
/// hạt 3D; rồi tia nắng, lóa ống kính và mờ chuyển động. post_fx (njin_post.h) và
/// shader riêng của game chạy sau cùng, trên cả ảnh.
struct post3d {
  /// @name Che khuất môi trường (SSAO)
  /// Góc tường, khe giữa hai vật và chân vật trên sàn tối đi, như ánh sáng nền
  /// khó lọt vào. Tính từ độ sâu nên mọi hình đục đều có, kể cả model của game.
  /// @{
  f32 ssao = 0.0f;           ///< Độ đậm, 0..1. 0 là tắt.
  f32 ssao_radius = 0.6f;    ///< Tầm xét quanh mỗi điểm, đơn vị 3D. Cỡ của khe được tối.
  i32 ssao_samples = 12;     ///< Số mẫu mỗi pixel, 4..32. Nhiều thì mịn hơn, tốn hơn.
  bool ssao_half = true;     ///< Tính ở nửa độ phân giải rồi phóng lên theo độ sâu: rẻ gấp bốn.
  /// @}

  /// @name Phản chiếu (SSR)
  /// Bề mặt có `material3d::reflect` lớn hơn 0 (sàn bóng, kim loại, vũng nước) phản
  /// chiếu những gì đang thấy trên màn hình, tìm bằng cách dò tia trên ảnh độ sâu.
  /// Thứ ngoài màn hình hay bị che không phản chiếu được: chỗ đó mờ dần về màu sương
  /// (`light3d::fog_color`, tức màu chân trời khi có draw_sky3d()).
  /// @{
  f32 ssr = 0.0f;            ///< Độ mạnh, 0..1, nhân với `reflect` của mỗi bề mặt. 0 là tắt.
  f32 ssr_distance = 20.0f;  ///< Tia dài nhất, đơn vị 3D.
  i32 ssr_steps = 48;        ///< Số bước dò mỗi tia, 8..128.
  f32 ssr_thickness = 0.5f;  ///< Một bề mặt được coi là dày bấy nhiêu khi tia đi qua phía sau nó.
  f32 ssr_sky = 1.0f;        ///< Tia không trúng gì thì phản chiếu bấy nhiêu phần màu sương, 0..1.
  /// @}

  /// @name Mờ chuyển động
  /// Khi camera di chuyển hay quay, ảnh nhòe theo hướng mỗi pixel trượt trên màn
  /// hình giữa hai frame. Camera đứng yên thì không nhòe (vật đang chạy cũng không:
  /// chỉ có chuyển động của camera).
  /// @{
  f32 motion_blur = 0.0f;       ///< Phần chuyển động của một frame được nhòe, 0..1 (0.5 như máy quay phim). 0 là tắt.
  i32 motion_blur_samples = 8;  ///< Số mẫu dọc vệt nhòe, 2..32.
  /// @}

  /// @name Tia nắng
  /// Tia sáng tỏa từ mặt trời qua khe giữa các vật, khi mặt trời ở trong hoặc gần
  /// khung hình. Mặt trời là hướng của `light3d::direction` (draw_sky3d() đặt nó
  /// theo giờ trong ngày); chỗ trời là chỗ không có hình 3D nào.
  /// @{
  f32 shafts = 0.0f;            ///< Độ sáng, 0..2. 0 là tắt.
  f32 shafts_length = 0.8f;     ///< Tia dài bao nhiêu phần đường từ mỗi điểm tới mặt trời, 0..1.
  rgba shafts_color{1.0f, 0.95f, 0.85f, 1.0f}; ///< Màu nhân với màu nắng.
  /// @}

  /// @name Lóa ống kính
  /// Quầng và các đốm sáng dọc đường từ mặt trời qua tâm màn hình, như ánh sáng dội
  /// trong ống kính. Mờ dần khi mặt trời bị vật che hay ra khỏi khung hình.
  /// @{
  f32 flare = 0.0f;             ///< Độ sáng, 0..2. 0 là tắt.
  f32 flare_halo = 0.5f;        ///< Độ sáng của vòng quầng quanh tâm, nhân với `flare`.
  /// @}

  /// @name Khử răng cưa theo thời gian (TAA)
  /// Mỗi frame, hình chiếu 3D bị dịch đi một phần nhỏ của pixel (mỗi frame một chỗ
  /// khác), rồi ảnh được trộn với ảnh của các frame trước, đưa về đúng chỗ theo độ
  /// sâu và chuyển động của camera: cạnh xiên hết răng cưa, cạnh mảnh hết nhấp nháy.
  /// Chỉ lần vẽ 3D đầu tiên vào thế giới của mỗi frame được khử; 2D vẽ sau end_3d()
  /// không bị đụng tới. Vật đang chạy không có vận tốc riêng: chỗ nào ảnh cũ không
  /// còn khớp (độ sâu khác hẳn, màu nằm ngoài màu xung quanh) thì ảnh cũ bị bỏ, nên
  /// không để lại vệt, nhưng mép vật đang chạy còn răng cưa.
  /// @{
  bool taa = false;           ///< Bật TAA.
  f32 taa_sharpen = 0.25f;    ///< Làm nét lại ảnh sau khi trộn, 0..1. 0 là không làm nét.
  /// @}
};

/// Đặt hiệu ứng màn hình 3D, cho mọi lần vẽ 3D từ frame này. `post3d{}` là tắt hết.
/// @param ctx Context của engine.
/// @param fx Các hiệu ứng.
void post3d_set(context &ctx, const post3d &fx);

/// Các hiệu ứng đang đặt.
/// @param ctx Context của engine.
/// @return Giá trị đặt bởi post3d_set(), hoặc mặc định.
post3d post3d_get(const context &ctx);

/// Cách một decal phủ lên bề mặt.
enum decal3d_blend {
  /// Nhân màu decal vào bề mặt: chỉ làm tối, giữ nguyên ánh sáng và bóng đổ của
  /// bề mặt (vết đạn, vết cháy, máu, vết chân, bùn).
  decal3d_multiply,
  /// Phủ màu decal lên như sơn, chiếu sáng theo nắng và ánh sáng nền (không có bóng
  /// đổ): sơn, phấn, giấy dán, ký hiệu sáng màu trên nền tối.
  decal3d_paint,
};

/// Một decal: ảnh chiếu lên mọi hình đục nằm trong một hình hộp (tường, sàn, model,
/// địa hình), theo đúng hình của bề mặt.
///
/// Ảnh nằm trên mặt xz của hộp và chiếu theo trục y của hộp: mặc định (không xoay)
/// là chiếu từ trên xuống sàn. Để dán lên tường, xoay hộp cho trục y chĩa ra khỏi
/// tường (ví dụ `rotation = {90, 0, 0}` cho tường quay về +z). Bề mặt càng nghiêng
/// so với trục chiếu thì decal càng mờ (xem `angle_fade`), nên vết không bị kéo dài
/// trên mặt bên.
struct decal3d_desc {
  vec3 position{0.0f, 0.0f, 0.0f}; ///< Tâm hộp.
  vec3 rotation{0.0f, 0.0f, 0.0f}; ///< Góc xoay, độ, như njin::transform3d::rotation.
  /// Cỡ hộp: `x` và `z` là bề rộng và bề dài của ảnh, `y` là độ sâu mà decal phủ tới
  /// (phủ lên mọi bề mặt trong khoảng đó; mỏng thì không lan sang vật bên cạnh).
  vec3 size{1.0f, 0.5f, 1.0f};
  /// Ảnh. Không hợp lệ là một vết tròn mềm mép bằng `color`. Ảnh đã xếp vào atlas
  /// bị bỏ qua (như material3d::texture).
  texture_handle texture{};
  rect source{};                  ///< Vùng trong ảnh, pixel. Kích thước 0 là cả ảnh.
  rgba color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Màu nhân vào ảnh; `a` là độ đậm.
  decal3d_blend blend = decal3d_multiply; ///< Cách phủ.
  f32 lifetime = 0.0f;            ///< Sống bao lâu, giây (thời gian của game, dừng khi pause). 0 là mãi mãi.
  f32 fade = 1.0f;                ///< Mờ dần trong bấy nhiêu giây cuối của `lifetime`.
  /// Bề mặt mà pháp tuyến lệch khỏi trục chiếu nhiều hơn mức này không có decal:
  /// cos của góc, 0..1. 0.3 (mặc định) là mờ dần từ khoảng 70 độ.
  f32 angle_fade = 0.3f;
};

/// Góc xoay cho `decal3d_desc::rotation` để trục chiếu của decal trùng với pháp
/// tuyến của bề mặt: decal nằm phẳng trên bề mặt đó. Dùng với pháp tuyến của một
/// tia chạm (ray3d_hit::normal, physics3d_raycast()) để đặt vết đạn đúng chỗ trúng.
/// @code
/// const njin::ray3d_hit hit = njin::physics3d_raycast(ctx, shot, 100.0f);
/// if (hit.hit)
///   njin::decal3d_add(ctx, {.position = hit.point, .rotation = njin::decal3d_rotation(hit.normal),
///                           .size = {0.2f, 0.2f, 0.2f}, .texture = bullet_hole});
/// @endcode
/// @param normal Pháp tuyến (không cần độ dài 1).
/// @return Góc xoay, độ.
vec3 decal3d_rotation(vec3 normal);

/// Thêm một decal. Decal có ở mọi lần vẽ 3D vào thế giới cho đến khi hết
/// `lifetime`, bị xóa, hay bị decal mới thay chỗ (xem decal3d_set_max()).
/// @param ctx Context của engine.
/// @param desc Decal.
/// @return Handle, hoặc handle không hợp lệ nếu vị trí, góc hay cỡ không phải số hữu hạn.
decal3d_handle decal3d_add(context &ctx, const decal3d_desc &desc);

/// Xóa một decal. Handle không hợp lệ hay decal đã hết bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Decal.
void decal3d_remove(context &ctx, decal3d_handle handle);

/// Xóa mọi decal.
/// @param ctx Context của engine.
void decal3d_clear(context &ctx);

/// Số decal đang có.
/// @param ctx Context của engine.
/// @return Số decal.
i32 decal3d_count(const context &ctx);

/// Số decal tối đa, mặc định 256. Thêm quá số này thì decal cũ nhất bị thay; giảm
/// xuống dưới số đang có thì các decal cũ nhất bị xóa.
/// @param ctx Context của engine.
/// @param max Số tối đa, 1..4096.
void decal3d_set_max(context &ctx, i32 max);
/// @}
} // namespace njin
