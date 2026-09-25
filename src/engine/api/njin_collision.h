#pragma once
#include "_comps.h"
#include "_math.h"
#include "_types.h"
#include <entt/entity/entity.hpp>
#include <vector>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_collision
/// @{

/// Mọi lớp va chạm. Giá trị mặc định của `collider::mask`.
inline constexpr u32 layer_all = 0xFFFFFFFFu;

/// Bit của lớp va chạm thứ `n` (0..31), để đặt tên lớp cho dễ đọc:
/// `constexpr u32 layer_enemy = njin::layer_bit(2);`.
/// @param n Số thứ tự lớp, 0..31.
/// @return `1 << n`.
constexpr u32 layer_bit(i32 n) { return 1u << (u32)n; }

/// Hình của một collider.
enum collider_shape {
  collider_box,    ///< Hình chữ nhật thẳng trục, cỡ `size`. Không xoay theo transform.
  collider_circle, ///< Hình tròn bán kính `radius`.
  /// Mọi ô không trống của njin::tilemap trên cùng entity là vật cản. Dùng cho
  /// collision_move(), truy vấn và raycast; không sinh event va chạm.
  collider_tiles,
};

/// Hình va chạm của một entity. Cần một transform trên cùng entity.
///
/// Module collision của engine, trong `phase_post_update` (sau khi transform
/// của entity con đã được tính), tìm mọi cặp collider đang chồng nhau và gửi
/// event njin::collision_enter, njin::collision_stay, njin::collision_exit qua
/// events(). Game nhận chúng bằng `events(ctx).sink<...>().connect<...>()`.
///
/// Hai collider A và B chỉ được xét khi **cả hai chiều** đều cho phép:
/// `(A.mask & B.layer) != 0` và `(B.mask & A.layer) != 0`, giống Box2D.
///
/// Tâm của hình là `transform.pos` cộng `offset`; `offset`, `size` và
/// `radius` nhân với `transform.scale`, và `offset` xoay theo `transform.rot`
/// (nên hitbox gắn ở đầu kiếm đi theo lưỡi kiếm), nhưng bản thân hình hộp
/// luôn thẳng trục.
struct collider {
  collider_shape shape = collider_box; ///< Hình.
  vec2 size{16.0f, 16.0f}; ///< Kích thước hình hộp.
  f32 radius = 8.0f;       ///< Bán kính hình tròn.
  vec2 offset{};           ///< Độ lệch của tâm so với `transform.pos`.
  u32 layer = layer_bit(0); ///< Các lớp collider này thuộc về.
  u32 mask = layer_all;     ///< Các lớp collider này va chạm với.
  /// Chỉ báo chồng nhau, không chặn đường: vùng nhặt đồ, vùng sát thương, cửa
  /// chuyển màn. collision_move() đi xuyên qua trigger; raycast mặc định bỏ qua.
  bool trigger = false;
  bool enabled = true; ///< Tắt tạm mà không gỡ component. Tắt thì có `exit`.
};

/// Hai collider bắt đầu chồng nhau.
///
/// Mỗi cặp gửi **hai** event, một cho mỗi bên, nên handler chỉ cần xét `self`:
/// @code
/// void on_enter(njin::collision_enter &e) {
///   if (reg.all_of<bullet>(e.self) && reg.all_of<enemy>(e.other)) ...
/// }
/// @endcode
/// Event được phát sau `phase_post_update`, nên hủy entity trong handler là an
/// toàn. Nhưng event của cả frame đã xếp hàng sẵn: một handler trước có thể đã
/// hủy `self` hoặc `other`, nên kiểm tra `registry.valid()` cả hai trước.
struct collision_enter {
  entt::entity self = entt::null;  ///< Entity nhận event.
  entt::entity other = entt::null; ///< Entity kia.
  bool trigger = false; ///< Một trong hai là trigger.
};

/// Hai collider vẫn đang chồng nhau, gửi mỗi frame sau frame `enter`.
struct collision_stay {
  entt::entity self = entt::null;  ///< Entity nhận event.
  entt::entity other = entt::null; ///< Entity kia.
  bool trigger = false; ///< Một trong hai là trigger.
};

/// Hai collider thôi chồng nhau: tách ra, bị tắt, bị gỡ, hoặc bị hủy.
///
/// Chỉ gửi cho bên còn sống; `other` có thể đã bị hủy, kiểm tra bằng
/// `registry.valid(e.other)` trước khi đọc component của nó.
struct collision_exit {
  entt::entity self = entt::null;  ///< Entity nhận event.
  entt::entity other = entt::null; ///< Entity kia, có thể đã bị hủy.
  bool trigger = false; ///< Một trong hai là trigger.
};

/// Hình chữ nhật bao quanh một collider hộp hoặc tròn, trong thế giới.
/// @param tr Transform của entity.
/// @param col Collider.
/// @return Hình chữ nhật bao. Với `collider_tiles` là hình rỗng tại `tr.pos`.
inline rect collider_bounds(const transform &tr, const collider &col) {
  const f32 s = tr.scale < 0.0f ? -tr.scale : tr.scale;
  const vec2 center = tr.pos + rotate(col.offset * s, tr.rot);
  if (col.shape == collider_circle)
    return rect_from_center(center, vec2{col.radius, col.radius} * (2.0f * s));
  if (col.shape == collider_box)
    return rect_from_center(center, col.size * s);
  return rect{tr.pos, {}};
}

/// Kết quả của collision_move().
struct collision_move_result {
  vec2 moved{};  ///< Độ dời thật sự đã áp dụng.
  bool hit_x = false; ///< Bị chặn trên trục ngang.
  bool hit_y = false; ///< Bị chặn trên trục dọc. `hit_y && delta.y > 0` là đang đứng trên đất.
  entt::entity other_x = entt::null; ///< Vật chặn trên trục ngang.
  entt::entity other_y = entt::null; ///< Vật chặn trên trục dọc.
};

/// Di chuyển một entity có collider, dừng lại khi đụng collider **không phải
/// trigger** (kể cả ô của `collider_tiles`), rồi ghi vị trí mới vào transform.
///
/// Giống tilemap_move(): đi trục ngang trước rồi trục dọc, nên trượt dọc theo
/// tường. Hình tròn được di chuyển như hình hộp bao quanh nó. Chỉ chặn bởi
/// collider mà cả hai chiều layer/mask cho phép, và bỏ qua collider của chính
/// các entity con trực tiếp (njin::child_of) để vũ khí không chặn chủ.
///
/// Mỗi trục được quét liên tục, nên bước dài đến đâu cũng không xuyên qua tường
/// mỏng. Vật cản đang chồng lên entity từ trước bị bỏ qua, để entity bị kẹt
/// vẫn đi ra được. Chạy trong `phase_fixed_update` cho ổn định.
/// @param ctx Context của engine.
/// @param entity Entity có transform và collider hộp hoặc tròn.
/// @param delta Độ dời mong muốn.
/// @return Độ dời thật và vật chặn trên từng trục.
collision_move_result collision_move(njin_ctx &ctx, entt::entity entity, vec2 delta);

/// Tìm mọi entity có collider chồng lên hình chữ nhật `area`.
/// @param ctx Context của engine.
/// @param area Vùng cần tìm, trong thế giới.
/// @param out Nhận các entity tìm thấy (được thêm vào cuối, không xóa trước). Có thể null.
/// @param mask Chỉ xét collider có `layer & mask != 0`.
/// @param include_triggers Có xét collider trigger không.
/// @return Số entity tìm thấy.
i32 collision_overlap_rect(const njin_ctx &ctx, rect area,
                           std::vector<entt::entity> *out = nullptr,
                           u32 mask = layer_all, bool include_triggers = true);

/// Như collision_overlap_rect() với một hình tròn: vùng nổ, tầm nhìn.
/// @param ctx Context của engine.
/// @param area Vùng cần tìm.
/// @param out Nhận các entity tìm thấy. Có thể null.
/// @param mask Chỉ xét collider có `layer & mask != 0`.
/// @param include_triggers Có xét collider trigger không.
/// @return Số entity tìm thấy.
i32 collision_overlap_circle(const njin_ctx &ctx, circle area,
                             std::vector<entt::entity> *out = nullptr,
                             u32 mask = layer_all, bool include_triggers = true);

/// Như collision_overlap_rect() với một điểm: chuột đang chỉ vào gì.
/// @param ctx Context của engine.
/// @param point Điểm, trong thế giới (dùng scr2w() cho vị trí chuột).
/// @param out Nhận các entity tìm thấy. Có thể null.
/// @param mask Chỉ xét collider có `layer & mask != 0`.
/// @param include_triggers Có xét collider trigger không.
/// @return Số entity tìm thấy.
i32 collision_overlap_point(const njin_ctx &ctx, vec2 point,
                            std::vector<entt::entity> *out = nullptr,
                            u32 mask = layer_all, bool include_triggers = true);

/// Kết quả của collision_raycast().
struct raycast_hit {
  bool hit = false;   ///< Có trúng gì không.
  entt::entity entity = entt::null; ///< Entity bị trúng (tilemap nếu trúng ô).
  vec2 point{};       ///< Điểm trúng, trong thế giới.
  vec2 normal{};      ///< Pháp tuyến bề mặt tại điểm trúng, độ dài 1.
  f32 distance = 0.0f; ///< Khoảng cách từ `from` đến điểm trúng.
};

/// Bắn một tia từ `from` đến `to`, trả về vật trúng **gần nhất**: tầm nhìn của
/// quái, đạn tức thời, laser, kiểm tra đứng trên đất.
///
/// Tia bắt đầu bên trong một collider thì trúng ngay tại `from`.
/// @param ctx Context của engine.
/// @param from Điểm đầu.
/// @param to Điểm cuối.
/// @param mask Chỉ xét collider có `layer & mask != 0`.
/// @param include_triggers Có xét collider trigger không. Mặc định không.
/// @param ignore Entity bỏ qua, thường là chính người bắn.
/// @return Vật trúng, hoặc `hit == false`.
raycast_hit collision_raycast(const njin_ctx &ctx, vec2 from, vec2 to,
                              u32 mask = layer_all, bool include_triggers = false,
                              entt::entity ignore = entt::null);

/// Vẽ khung mọi collider hộp và tròn trong `phase_render`, trên sprite: xanh
/// lá cho vật cản, vàng cho trigger. Để dò lỗi hitbox.
/// @param ctx Context của engine.
/// @param on Bật hay tắt.
void collision_set_debug(njin_ctx &ctx, bool on);

/// Cỡ ô của lưới tìm cặp va chạm, đơn vị thế giới. Mặc định 64. Nên gần bằng
/// cỡ collider phổ biến trong game; chỉ ảnh hưởng tốc độ, không đổi kết quả.
/// @param ctx Context của engine.
/// @param size Cỡ ô, lớn hơn 0.
void collision_set_cell_size(njin_ctx &ctx, f32 size);
/// @}
} // namespace njin
