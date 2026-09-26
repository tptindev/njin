#pragma once
#include "_math.h"
#include "_tween.h"
#include "_types.h"
#include <entt/entity/entity.hpp>
#include <functional>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_timer
/// @{

/// Định danh của một hẹn giờ, từ timer_after() hoặc timer_every().
struct timer_handle {
  u32 id = 0; ///< 0 nghĩa là không hợp lệ.
};

/// Định danh của một tween chạy trên entity, từ tween_move() và các hàm cùng họ.
struct tween_handle {
  u32 id = 0; ///< 0 nghĩa là không hợp lệ.
};

/// Tùy chọn của một hẹn giờ.
struct timer_desc {
  /// Gắn với entity này: entity bị hủy thì hẹn giờ tự hủy, không bao giờ gọi
  /// hàm với một entity đã chết.
  entt::entity owner = entt::null;
  /// Tính theo giờ thật: vẫn chạy khi game pause, hitstop hay chạy chậm (menu,
  /// UI). Mặc định theo giờ của game.
  bool real_time = false;
  /// Vẫn chạy khi đổi scene. Mặc định hẹn giờ thuộc về scene đang chạy lúc tạo
  /// và bị hủy khi rời scene đó.
  bool keep_across_scenes = false;
};

/// Gọi `fn` một lần sau `seconds` giây.
/// @code
/// njin::timer_after(ctx, 0.4f, [](njin::njin_ctx &c) { njin::scene_fade(c, next); });
/// njin::timer_after(ctx, 1.5f, respawn, {.owner = player});
/// @endcode
/// Hàm chạy trong `phase_update` (trước system của game), nên được thêm, hủy
/// entity và tạo hẹn giờ mới. Thời gian tính từ frame gọi hàm.
/// @param ctx Context của engine.
/// @param seconds Thời gian chờ, giây. 0 là frame sau.
/// @param fn Hàm cần gọi.
/// @param desc Tùy chọn.
/// @return Handle để hủy bằng timer_cancel().
timer_handle timer_after(njin_ctx &ctx, f32 seconds, std::function<void(njin_ctx &)> fn,
                         const timer_desc &desc = {});

/// Gọi `fn` mỗi `interval` giây: sinh quái theo nhịp, hồi máu từ từ.
/// @param ctx Context của engine.
/// @param interval Khoảng cách giữa hai lần gọi, giây. Lớn hơn 0.
/// @param fn Hàm cần gọi.
/// @param count Số lần gọi, -1 là mãi mãi (đến khi timer_cancel()).
/// @param desc Tùy chọn.
/// @return Handle để hủy bằng timer_cancel().
timer_handle timer_every(njin_ctx &ctx, f32 interval, std::function<void(njin_ctx &)> fn,
                         i32 count = -1, const timer_desc &desc = {});

/// Hủy một hẹn giờ. Handle đã hết hạn hoặc không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param timer Hẹn giờ.
void timer_cancel(njin_ctx &ctx, timer_handle timer);

/// Hẹn giờ còn đang chờ không.
/// @param ctx Context của engine.
/// @param timer Hẹn giờ.
/// @return `true` nếu nó chưa chạy xong và chưa bị hủy.
bool timer_active(const njin_ctx &ctx, timer_handle timer);

/// Tùy chọn của một tween.
struct tween_desc {
  f32 delay = 0.0f;       ///< Chờ bao lâu trước khi bắt đầu, giây.
  /// Số lần lặp thêm sau lần đầu. -1 là mãi mãi.
  i32 repeat = 0;
  /// Mỗi lần lặp thì đi ngược lại (tới rồi lui), thay vì nhảy về đầu.
  bool yoyo = false;
  bool real_time = false; ///< Tính theo giờ thật, như timer_desc::real_time.
  /// Gọi khi tween chạy xong (không gọi khi bị hủy).
  std::function<void(njin_ctx &)> done{};
};

/// Dời `transform.pos` của entity tới `to` trong `seconds` giây.
///
/// Mọi tween trên entity: bắt đầu từ giá trị hiện tại (lúc hết `delay`), tự
/// hủy khi entity bị hủy, và thay thế tween cũ **cùng loại** trên cùng
/// entity (gọi tween_move hai lần thì cái sau thắng). Chạy trong
/// `phase_update`.
/// @code
/// njin::tween_move(ctx, door, door_pos + njin::vec2{0, -32}, 0.6f, njin::ease::out_cubic);
/// njin::tween_scale(ctx, coin, 1.4f, 0.1f, njin::ease::out_quad, {.repeat = 1, .yoyo = true});
/// @endcode
/// @param ctx Context của engine.
/// @param entity Entity có transform.
/// @param to Vị trí đích.
/// @param seconds Thời lượng.
/// @param curve Đường cong.
/// @param desc Tùy chọn.
/// @return Handle để hủy bằng tween_cancel().
tween_handle tween_move(njin_ctx &ctx, entt::entity entity, vec2 to, f32 seconds,
                        ease curve = ease::out_quad, const tween_desc &desc = {});

/// Đổi `transform.scale` của entity tới `to`. Xem tween_move().
/// @param ctx Context của engine. @param entity Entity có transform.
/// @param to Tỉ lệ đích. @param seconds Thời lượng. @param curve Đường cong.
/// @param desc Tùy chọn. @return Handle.
tween_handle tween_scale(njin_ctx &ctx, entt::entity entity, f32 to, f32 seconds,
                         ease curve = ease::out_quad, const tween_desc &desc = {});

/// Xoay `transform.rot` của entity tới `to` độ. Xem tween_move().
/// @param ctx Context của engine. @param entity Entity có transform.
/// @param to Góc đích, độ. @param seconds Thời lượng. @param curve Đường cong.
/// @param desc Tùy chọn. @return Handle.
tween_handle tween_rotate(njin_ctx &ctx, entt::entity entity, f32 to, f32 seconds,
                          ease curve = ease::out_quad, const tween_desc &desc = {});

/// Đổi `sprite.tint` của entity tới `to` (cả độ trong suốt). Xem tween_move().
/// @param ctx Context của engine. @param entity Entity có njin::sprite.
/// @param to Màu đích. @param seconds Thời lượng. @param curve Đường cong.
/// @param desc Tùy chọn. @return Handle.
tween_handle tween_tint(njin_ctx &ctx, entt::entity entity, rgba to, f32 seconds,
                        ease curve = ease::linear, const tween_desc &desc = {});

/// Chạy một giá trị từ `from` tới `to` và đưa nó cho `apply` mỗi frame: tween
/// cho bất cứ thứ gì (âm lượng, thanh máu hiện chậm, bán kính vòng nổ).
/// @param ctx Context của engine.
/// @param from Giá trị đầu.
/// @param to Giá trị cuối.
/// @param seconds Thời lượng.
/// @param apply Nhận giá trị mỗi frame, kể cả giá trị cuối.
/// @param curve Đường cong.
/// @param desc Tùy chọn.
/// @param owner Entity sở hữu: bị hủy thì tween dừng. Có thể để null.
/// @return Handle.
tween_handle tween_value(njin_ctx &ctx, f32 from, f32 to, f32 seconds,
                         std::function<void(njin_ctx &, f32)> apply, ease curve = ease::linear,
                         const tween_desc &desc = {}, entt::entity owner = entt::null);

/// Hủy một tween, giữ nguyên giá trị đang có. Không gọi `done`.
/// @param ctx Context của engine.
/// @param tween Tween.
void tween_cancel(njin_ctx &ctx, tween_handle tween);

/// Hủy mọi tween đang chạy trên một entity.
/// @param ctx Context của engine.
/// @param entity Entity.
void tween_cancel_all(njin_ctx &ctx, entt::entity entity);

/// Tween còn đang chạy (kể cả đang chờ `delay`) không.
/// @param ctx Context của engine.
/// @param tween Tween.
/// @return `true` nếu còn chạy.
bool tween_active(const njin_ctx &ctx, tween_handle tween);
/// @}
} // namespace njin
