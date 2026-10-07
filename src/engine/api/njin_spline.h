#pragma once
#include "_math.h"
#include "_types.h"
#include <vector>

namespace njin {
struct context;

/// @addtogroup grp_spline
/// @{

/// Loại đường cong của một spline.
enum spline_kind {
  /// Catmull-Rom: đường đi **qua** mọi điểm. Dễ đặt nhất: chấm các điểm đường ray,
  /// đường bay, lối tuần tra. Dạng `alpha` = 0.5 (centripetal) không tạo nút thắt
  /// hay vòng xoắn khi các điểm cách nhau không đều.
  spline_catmull_rom,
  /// Bezier bậc ba: điểm 0, 3, 6... nằm trên đường, hai điểm giữa mỗi cặp là tay
  /// cầm kéo đường cong (như công cụ pen của trình vẽ). Đường mở cần `3k + 1` điểm,
  /// đường kín `3k` điểm; điểm thừa bị bỏ.
  spline_bezier,
};

/// Một đường cong 3D: đường ray của camera, bục di chuyển, lối bay, đường tuần tra.
///
/// Sửa `points` (hay `kind`, `closed`, `alpha`) xong thì gọi spline_bake() một lần:
/// nó lập bảng độ dài để đi theo khoảng cách (spline_point_at(), spline_follow())
/// với tốc độ đều. Hàm theo tham số `t` (spline_point()) không cần bảng.
struct spline3d {
  std::vector<vec3> points;              ///< Các điểm điều khiển.
  spline_kind kind = spline_catmull_rom; ///< Loại đường cong.
  bool closed = false;                   ///< Đường kín: điểm cuối nối về điểm đầu.
  /// Chỉ cho Catmull-Rom: 0 là đều (uniform), 0.5 là centripetal (mặc định, không
  /// thắt nút), 1 là theo dây cung (chordal).
  f32 alpha = 0.5f;
  /// @name Bảng độ dài (spline_bake() điền, đừng sửa)
  /// @{
  std::vector<f32> table; ///< Độ dài từ đầu đường tới từng mẫu.
  i32 steps = 0;          ///< Số mẫu mỗi đoạn.
  /// @}
};

/// Như njin::spline3d, trên mặt phẳng: đường đi của quái 2D, quỹ đạo đạn cong.
struct spline2d {
  std::vector<vec2> points;              ///< Các điểm điều khiển.
  spline_kind kind = spline_catmull_rom; ///< Loại đường cong.
  bool closed = false;                   ///< Đường kín.
  f32 alpha = 0.5f;                      ///< Như spline3d::alpha.
  /// @name Bảng độ dài (spline_bake() điền, đừng sửa)
  /// @{
  std::vector<f32> table; ///< Độ dài từ đầu đường tới từng mẫu.
  i32 steps = 0;          ///< Số mẫu mỗi đoạn.
  /// @}
};

/// Lập bảng độ dài sau khi sửa điểm. Thiếu bảng thì các hàm theo khoảng cách tự
/// lập một bảng tạm mỗi lần gọi (đúng nhưng chậm).
/// @param spline Đường cong.
/// @param steps Số mẫu mỗi đoạn, 2..1024: nhiều thì khoảng cách chính xác hơn. 64 là
/// đủ cho tốc độ đều trong 1%.
void spline_bake(spline3d &spline, i32 steps = 64);
/// @copydoc spline_bake(spline3d&, i32)
void spline_bake(spline2d &spline, i32 steps = 64);

/// Số đoạn: `t` của spline_point() chạy từ 0 đến số này.
/// @param spline Đường cong.
/// @return Số đoạn, 0 nếu không đủ điểm (cần 2 điểm cho Catmull-Rom, 4 cho Bezier mở).
i32 spline_segment_count(const spline3d &spline);
/// @copydoc spline_segment_count(const spline3d&)
i32 spline_segment_count(const spline2d &spline);

/// Điểm trên đường ở tham số `t`: phần nguyên là đoạn, phần lẻ là vị trí trong
/// đoạn. Với Catmull-Rom, `t` nguyên là đúng điểm điều khiển thứ `t`. Tốc độ theo
/// `t` không đều (đoạn dài đi nhanh hơn): đi theo khoảng cách thì dùng
/// spline_point_at().
/// @param spline Đường cong.
/// @param t 0..spline_segment_count(); ngoài khoảng bị kẹp (đường kín thì quay vòng).
/// @return Điểm, hoặc điểm đầu nếu không đủ điểm.
vec3 spline_point(const spline3d &spline, f32 t);
/// @copydoc spline_point(const spline3d&, f32)
vec2 spline_point(const spline2d &spline, f32 t);

/// Hướng của đường ở tham số `t`, độ dài 1.
/// @param spline Đường cong.
/// @param t Như spline_point().
/// @return Hướng, hoặc 0 nếu đường suy biến.
vec3 spline_tangent(const spline3d &spline, f32 t);
/// @copydoc spline_tangent(const spline3d&, f32)
vec2 spline_tangent(const spline2d &spline, f32 t);

/// Độ dài cả đường.
/// @param spline Đường cong.
/// @return Độ dài.
f32 spline_length(const spline3d &spline);
/// @copydoc spline_length(const spline3d&)
f32 spline_length(const spline2d &spline);

/// Tham số `t` ở khoảng cách `distance` tính từ đầu đường.
/// @param spline Đường cong.
/// @param distance 0..spline_length(); ngoài khoảng bị kẹp (đường kín thì quay vòng).
/// @return Tham số `t`.
f32 spline_t_at(const spline3d &spline, f32 distance);
/// @copydoc spline_t_at(const spline3d&, f32)
f32 spline_t_at(const spline2d &spline, f32 distance);

/// Điểm ở khoảng cách `distance` tính từ đầu đường: đi đều theo khoảng cách.
/// @param spline Đường cong.
/// @param distance Như spline_t_at().
/// @return Điểm.
vec3 spline_point_at(const spline3d &spline, f32 distance);
/// @copydoc spline_point_at(const spline3d&, f32)
vec2 spline_point_at(const spline2d &spline, f32 distance);

/// Hướng ở khoảng cách `distance`, độ dài 1.
/// @param spline Đường cong.
/// @param distance Như spline_t_at().
/// @return Hướng.
vec3 spline_tangent_at(const spline3d &spline, f32 distance);
/// @copydoc spline_tangent_at(const spline3d&, f32)
vec2 spline_tangent_at(const spline2d &spline, f32 distance);

/// Chỗ gần `p` nhất trên đường: để bám vào đường ray, biết người chơi đã đi được
/// bao xa trên đường đua.
/// @param spline Đường cong.
/// @param p Điểm.
/// @param out_point Nhận điểm gần nhất, hoặc nullptr.
/// @return Khoảng cách dọc đường tới điểm đó.
f32 spline_nearest(const spline3d &spline, vec3 p, vec3 *out_point = nullptr);
/// @copydoc spline_nearest(const spline3d&, vec3, vec3*)
f32 spline_nearest(const spline2d &spline, vec2 p, vec2 *out_point = nullptr);

/// Khi đến cuối đường thì làm gì.
enum spline_end {
  spline_stop,      ///< Dừng ở cuối, `finished` thành true.
  spline_loop,      ///< Quay về đầu (đường kín thì đi tiếp vòng mới).
  spline_ping_pong, ///< Quay đầu đi ngược lại, rồi lại quay đầu ở đầu đường.
};

/// Trạng thái của một vật đi dọc đường với tốc độ đều: camera trên ray, bục di
/// chuyển, quái tuần tra. Giữ trong component của game, mỗi frame gọi spline_follow().
struct spline_follower {
  f32 distance = 0.0f;        ///< Đã đi được bao xa từ đầu đường.
  f32 speed = 1.0f;           ///< Đơn vị mỗi giây. Âm là đi ngược.
  spline_end end = spline_stop; ///< Khi đến cuối đường.
  bool finished = false;      ///< Đã dừng ở cuối (chỉ với spline_stop).
};

/// Đi tiếp `dt` giây dọc đường và trả về chỗ mới. Hướng ở đó là
/// `spline_tangent_at(spline, follower.distance)` (nhân -1 nếu đang đi ngược).
/// @param spline Đường cong.
/// @param follower Trạng thái, được cập nhật.
/// @param dt Thời gian, giây (thường là delta()).
/// @return Chỗ mới.
vec3 spline_follow(const spline3d &spline, spline_follower &follower, f32 dt);
/// @copydoc spline_follow(const spline3d&, spline_follower&, f32)
vec2 spline_follow(const spline2d &spline, spline_follower &follower, f32 dt);

/// Vẽ đường và các điểm điều khiển bằng gizmo (njin_gizmo.h) trong frame này.
/// @param ctx Context của engine.
/// @param spline Đường cong.
/// @param color Màu.
void spline_draw_debug(context &ctx, const spline3d &spline, rgba color = {1.0f, 0.8f, 0.2f, 1.0f});
/// @copydoc spline_draw_debug(context&, const spline3d&, rgba)
void spline_draw_debug(context &ctx, const spline2d &spline, rgba color = {1.0f, 0.8f, 0.2f, 1.0f});
/// @}
} // namespace njin
