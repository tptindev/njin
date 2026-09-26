#pragma once
#include "_comps.h"
#include "_types.h"
#include <entt/entity/entity.hpp>

namespace njin {
struct njin_ctx;
struct level_handle;

/// @addtogroup grp_camera
/// @{

/// Cho camera bám theo một entity: mượt, có vùng chết, nhìn trước theo hướng
/// chạy, và không bao giờ lộ ra ngoài khung level.
///
/// Gắn vào entity camera (cùng transform, njin::camera_2d, njin::camera_on).
/// Module camera_follow của engine, trong `phase_post_update` (sau khi nhân
/// vật đã di chuyển, trước khi tilemap được chuẩn bị để vẽ), dời
/// `transform.pos` của camera.
/// @code
/// const entt::entity cam = njin::camera_spawn(ctx, 3.0f);
/// reg.emplace<njin::camera_follow>(cam, njin::camera_follow{
///     .target = player,
///     .deadzone = {24, 32},
///     .lookahead = {40, 0},
///     .bounds = njin::level_bounds(ctx, level)});
/// @endcode
///
/// Mọi khoảng cách là đơn vị thế giới (pixel của thế giới trước khi phóng).
/// Rung màn hình của njin::fx không bị ảnh hưởng: nó được cộng vào khi vẽ.
struct camera_follow {
  entt::entity target = entt::null; ///< Entity cần bám (có transform). null là đứng yên.
  vec2 offset{}; ///< Cộng vào vị trí mục tiêu: `{0, -16}` để nhìn cao hơn đầu nhân vật.
  /// Kích thước hình chữ nhật quanh tâm màn hình mà mục tiêu đi trong đó thì
  /// camera đứng yên. `{0, 0}` là luôn bám.
  vec2 deadzone{};
  /// Độ trễ, giây: mất khoảng chừng này để đi được 63% quãng đến vị trí mới.
  /// 0 là bám chặt.
  f32 smoothing = 0.12f;
  /// Nhìn trước theo hướng mục tiêu đang đi, từng trục: `{48, 0}` cho
  /// platformer (chỉ ngang), `{32, 32}` cho top-down.
  vec2 lookahead{};
  /// Độ trễ của phần nhìn trước, giây. Lớn hơn `smoothing` để quay đầu không giật.
  f32 lookahead_smoothing = 0.5f;
  /// Vùng thế giới mà khung nhìn không được vượt ra. Kích thước 0 là không giới
  /// hạn. Level nhỏ hơn màn hình thì nằm giữa màn hình. Xem level_bounds().
  rect bounds{};
  /// Giữ `camera_2d::offset` ở giữa màn hình (cả khi đổi cỡ cửa sổ hay độ
  /// phân giải ảo). `false` để tự đặt offset.
  bool center = true;
  /// Làm tròn vị trí camera theo pixel màn hình. Bật cho pixel art để các ô
  /// không bị rung một pixel khi camera trôi chậm.
  bool pixel_snap = false;

  vec2 look{};        ///< Phần nhìn trước hiện tại, engine ghi.
  vec2 last_target{}; ///< Vị trí mục tiêu ở frame trước, engine ghi.
  /// Đã đặt vị trí lần đầu. Đặt lại `false` để camera nhảy thẳng tới mục tiêu,
  /// ví dụ sau khi nhân vật dịch chuyển.
  bool started = false;
};

/// Tạo một entity camera đang dùng (transform, njin::camera_2d, njin::camera_on)
/// nhìn vào `pos`, với offset ở giữa màn hình.
/// @param ctx Context của engine.
/// @param zoom Độ phóng. 2 là mọi thứ to gấp đôi.
/// @param pos Điểm nhìn ban đầu trong thế giới.
/// @return Entity camera. Thêm njin::camera_follow để nó bám theo nhân vật.
entt::entity camera_spawn(njin_ctx &ctx, f32 zoom = 1.0f, vec2 pos = {});

/// Khung của một level trong thế giới, dùng cho camera_follow::bounds.
/// @param ctx Context của engine.
/// @param level Level đã nạp.
/// @return `{level_origin, level_size}`, hoặc hình rỗng nếu handle không hợp lệ.
rect level_bounds(const njin_ctx &ctx, level_handle level);

/// Giới hạn một vị trí camera để khung nhìn nằm trong `bounds`.
/// @param ctx Context của engine (để biết cỡ màn hình).
/// @param pos Vị trí camera (`transform.pos`).
/// @param cam Camera.
/// @param bounds Vùng thế giới. Kích thước 0 là không giới hạn.
/// @return Vị trí đã giới hạn.
vec2 camera_clamp(const njin_ctx &ctx, vec2 pos, const camera_2d &cam, rect bounds);

/// Trả về góc nhìn dùng cho frame này.
///
/// Đó là entity có camera_on, camera_2d và transform (xem _comps.h), hoặc góc
/// nhìn đồng nhất (thế giới trùng với pixel màn hình) nếu không có entity nào.
/// Được đọc trực tiếp từ registry nên thay đổi có hiệu lực ngay.
/// @param ctx Context của engine.
/// @return Góc nhìn đang dùng.
camera_view camera_active(const njin_ctx &ctx);

/// Đổi một điểm từ thế giới sang pixel màn hình, qua camera_active().
/// @param ctx Context của engine.
/// @param pos Điểm trong thế giới.
/// @return Vị trí tương ứng trên màn hình.
vec2 w2scr(const njin_ctx &ctx, vec2 pos);

/// Đổi một điểm từ pixel màn hình sang thế giới, qua camera_active().
///
/// Thường dùng để đổi vị trí chuột, từ mouse_pos(), thành vị trí trong thế giới.
/// @param ctx Context của engine.
/// @param pos Điểm trên màn hình (pixel).
/// @return Vị trí tương ứng trong thế giới.
vec2 scr2w(const njin_ctx &ctx, vec2 pos);

/// Vùng thế giới đang hiện trên màn hình.
///
/// Khi camera xoay, đây là hình chữ nhật thẳng trục bao quanh vùng nhìn thấy.
/// Dùng để bỏ qua việc vẽ những thứ nằm ngoài màn hình.
/// @param ctx Context của engine.
/// @return Hình chữ nhật trong thế giới.
rect camera_bounds(const njin_ctx &ctx);

/// Áp một shader hậu kỳ lên toàn bộ thế giới đi qua camera.
///
/// Khi bật, mọi thứ vẽ trong `phase_pre_render` và `phase_render` (kể cả sprite
/// và tilemap) được vẽ vào một ảnh ngoài màn hình, rồi vẽ ra màn hình qua
/// shader này. UI vẽ trong `phase_post_render` không bị ảnh hưởng. Đặt uniform
/// cho shader như bình thường bằng các hàm shader_set_*().
/// @param ctx Context của engine.
/// @param shader Shader hậu kỳ. Handle id 0 để tắt.
void camera_set_post_shader(njin_ctx &ctx, shader_handle shader);
/// @}
} // namespace njin
