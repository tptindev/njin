#pragma once
#include "_mod.h"
#include "_random.h"
#include "_types.h"
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>
#include <initializer_list>
#include <span>

namespace njin {
// Opaque: created with create (njin.h), only accessed through the
// functions below.
struct context;

/// @addtogroup grp_module
/// @{

/// Trả về registry của EnTT chứa mọi entity và component của game.
///
/// EnTT là một phần của API module, nên system có thể tự định nghĩa component
/// và truy vấn registry trực tiếp.
/// @param ctx Context của engine.
/// @return Registry của game.
entt::registry &world(context &ctx);

/// Trả về dispatcher của EnTT để gửi và nhận event giữa các system.
///
/// Event đã `enqueue` được phát ngay sau `phase_post_update`.
/// @param ctx Context của engine.
/// @return Dispatcher của game.
entt::dispatcher &events(context &ctx);

/// Thêm một system vào lịch chạy của một phase.
///
/// Chỉ hợp lệ bên trong callback `setup` của module. Gọi ở nơi khác sẽ bị bỏ
/// qua và ghi cảnh báo. Các module chạy theo thứ tự đăng ký.
/// @param ctx Context của engine.
/// @param phase Phase mà system chạy trong đó.
/// @param fnc Hàm system.
/// @param name Tên system, hiện trong njin_inspector. Có thể null.
void ecs_register(context &ctx, sys_phase phase, sys_fnc fnc, const char *name = nullptr);

/// Thêm một system kèm ràng buộc thứ tự vào lịch chạy của một phase.
///
/// Trong cùng một module và phase, các system được sắp theo sys_desc (xem
/// _mod.h): ràng buộc `after`/`before` được đảm bảo trước, sau đó system có
/// `order` nhỏ hơn chạy trước, còn bằng nhau thì theo thứ tự đăng ký.
/// @param ctx Context của engine.
/// @param phase Phase mà system chạy trong đó.
/// @param desc Mô tả system và thứ tự của nó.
void ecs_register(context &ctx, sys_phase phase, const sys_desc &desc);

/// Chạy `setup` của module và đưa các system của nó vào lịch chạy.
///
/// Phải gọi trước run(). Tên module chỉ được đăng ký một lần.
/// @param ctx Context của engine.
/// @param desc Mô tả module.
void mod_register(context &ctx, const mod_desc &desc);

/// Đăng ký nhiều module một lần, theo đúng thứ tự trong danh sách.
///
/// Giống hệt gọi mod_register() cho từng module lần lượt: module đứng
/// trước chạy trước trong cùng phase, và một module lỗi (trùng tên, đăng ký
/// sau run()) chỉ bị bỏ qua riêng nó.
/// @code
/// njin::mod_register(*ctx, {input_module(), physics_module(), ui_module()});
/// @endcode
/// @param ctx Context của engine.
/// @param mods Các module, theo thứ tự đăng ký.
void mod_register(context &ctx, std::initializer_list<mod_desc> mods);

/// Đăng ký nhiều module từ một danh sách tạo lúc chạy, ví dụ một
/// `std::vector<mod_desc>` hay `std::array`. Cùng quy tắc với bản nhận danh
/// sách trực tiếp.
/// @param ctx Context của engine.
/// @param mods Các module, theo thứ tự đăng ký.
void mod_register(context &ctx, std::span<const mod_desc> mods);
/// @}

/// @addtogroup grp_time
/// @{

/// Thời gian của frame trước, tính bằng giây, đã nhân với tốc độ thời gian.
///
/// Nhân vận tốc với giá trị này để chuyển động không phụ thuộc FPS. Bằng 0 khi
/// đang tạm dừng (time_set_paused()). Trong `phase_fixed_update` hàm này trả
/// về đúng một nhịp cố định, xem fixed_delta().
/// @param ctx Context của engine.
/// @return Thời gian của frame, tính bằng giây.
f32 delta(const context &ctx);

/// Thời gian thật của frame trước, không bị tốc độ thời gian hay tạm dừng ảnh
/// hưởng. Dùng cho thứ vẫn phải chạy khi game dừng, như menu tạm dừng.
/// @param ctx Context của engine.
/// @return Thời gian thật của frame, tính bằng giây.
f32 delta_real(const context &ctx);

/// Đặt tốc độ thời gian của game. 1 là bình thường, 0.5 là chậm một nửa.
///
/// Ảnh hưởng delta(), `phase_fixed_update` và animation của sprite. Giá trị âm
/// được coi là 0.
/// @param ctx Context của engine.
/// @param scale Tốc độ thời gian.
void time_set_scale(context &ctx, f32 scale);

/// Tốc độ thời gian hiện tại.
/// @param ctx Context của engine.
/// @return Tốc độ thời gian, mặc định 1.
f32 time_scale(const context &ctx);

/// Tạm dừng hoặc chạy tiếp thời gian của game.
///
/// Khi dừng, delta() trả về 0 và `phase_fixed_update` không chạy, nhưng các
/// phase khác vẫn chạy mỗi frame: input vẫn đọc được, menu vẫn vẽ được. Giữ
/// nguyên tốc độ thời gian đã đặt.
/// @param ctx Context của engine.
/// @param paused `true` để tạm dừng.
void time_set_paused(context &ctx, bool paused);

/// Game có đang tạm dừng không.
/// @param ctx Context của engine.
/// @return `true` nếu đang tạm dừng.
bool time_paused(const context &ctx);

/// Độ dài một nhịp của `phase_fixed_update`, bằng `1 / config::fixed_hz`.
/// @param ctx Context của engine.
/// @return Độ dài một nhịp, tính bằng giây.
f32 fixed_delta(const context &ctx);

/// Đặt nhịp chạy của `phase_fixed_update` so với thời gian thật, mà không đổi độ dài
/// một nhịp (fixed_delta()) hay delta(). 1.02 là chạy nhanh hơn 2%: thêm khoảng một
/// nhịp mỗi giây ở 60 Hz.
///
/// Dùng để giữ hai máy chơi mạng cùng nhịp: netcode tự đặt nó trên máy khách (xem
/// @ref multiplayer). Giá trị được kẹp trong 0.5..2.
/// @param ctx Context của engine.
/// @param rate Nhịp, mặc định 1.
void time_set_fixed_rate(context &ctx, f32 rate);

/// Nhịp chạy hiện tại của `phase_fixed_update`, xem time_set_fixed_rate().
/// @param ctx Context của engine.
/// @return Nhịp, mặc định 1.
f32 time_fixed_rate(const context &ctx);

/// Phần nhịp cố định còn dư sau `phase_fixed_update` của frame này, từ 0 đến 1.
///
/// Dùng để nội suy vị trí khi vẽ, cho chuyển động mượt dù FPS khác nhịp vật lý:
/// `draw_pos = lerp(prev_pos, pos, fixed_alpha(ctx))`.
/// @param ctx Context của engine.
/// @return Tỉ lệ từ 0 đến 1.
f32 fixed_alpha(const context &ctx);

/// Thời gian đã trôi qua kể từ lúc mở cửa sổ, tính bằng giây.
/// @param ctx Context của engine.
/// @return Thời gian đã chạy, tính bằng giây.
f32 elapsed(const context &ctx);

/// Giờ của máy: số giây từ 00:00 ngày 1/1/1970 theo giờ UTC (Unix time).
///
/// Khác elapsed() và các timer, giờ này vẫn trôi khi game tắt, nên dùng cho
/// việc tính theo giờ thật: lưu lúc thoát rồi lúc mở lại lấy hiệu để biết bao
/// lâu đã qua (công trình xây xong, mỏ sinh tài nguyên, năng lượng hồi), phần
/// thưởng mỗi ngày. Người chơi chỉnh được đồng hồ của máy; game offline thường
/// chấp nhận điều đó, hoặc bỏ qua khi giờ đi lùi so với lần lưu trước.
///
/// Không cần context: gọi được cả trước create().
/// @code
/// // Lúc lưu: save["saved_at"] = njin::time_unix();
/// const njin::i64 away = njin::time_unix() - saved_at;   // giây
/// if (away > 0) gold += std::min<njin::i64>(away, 8 * 3600) * gold_per_second;
/// @endcode
/// @return Số giây.
i64 time_unix();

/// Một thời điểm tách thành ngày, giờ. Dùng với time_date() và time_from_date().
struct date_time {
  i32 year = 1970;  ///< Năm, ví dụ 2026.
  i32 month = 1;    ///< Tháng, 1..12.
  i32 day = 1;      ///< Ngày trong tháng, 1..31.
  i32 hour = 0;     ///< Giờ, 0..23.
  i32 minute = 0;   ///< Phút, 0..59.
  i32 second = 0;   ///< Giây, 0..59.
  i32 weekday = 4;  ///< Thứ trong tuần, 0 là Chủ nhật, 1 là thứ Hai ... 6 là thứ Bảy. time_from_date() bỏ qua.
};

/// Tách một giờ Unix (time_unix()) thành ngày, giờ.
///
/// `local` là theo múi giờ của máy (đổi ngày lúc nửa đêm của người chơi, cho
/// phần thưởng mỗi ngày); `false` là theo UTC (mọi máy cùng đổi ngày).
/// @param unix_time Số giây từ 1/1/1970 UTC.
/// @param local Theo giờ của máy, hay theo UTC.
/// @return Ngày, giờ.
date_time time_date(i64 unix_time, bool local = true);

/// Ngược lại của time_date(): giờ Unix của một ngày, giờ.
///
/// Trường vượt khoảng được cộng dồn sang trường lớn hơn, nên `day + 1` của ngày
/// cuối tháng là ngày đầu tháng sau. `weekday` bị bỏ qua.
/// @code
/// // Lúc nửa đêm tới, khi phần thưởng mỗi ngày làm mới.
/// njin::date_time d = njin::time_date(njin::time_unix());
/// const njin::i64 reset = njin::time_from_date({.year = d.year, .month = d.month, .day = d.day + 1});
/// @endcode
/// @param date Ngày, giờ.
/// @param local `date` theo giờ của máy, hay theo UTC.
/// @return Số giây từ 1/1/1970 UTC.
i64 time_from_date(const date_time &date, bool local = true);
/// @}

/// @addtogroup grp_random
/// @{

/// Bộ sinh số ngẫu nhiên dùng chung của engine.
///
/// Được gieo hạt giống từ thời gian lúc mở game, nên mỗi lần chơi mỗi khác.
/// Gọi `random(ctx).reseed(n)` để có dãy số lặp lại được, ví dụ khi thử lỗi.
/// @param ctx Context của engine.
/// @return Bộ sinh số ngẫu nhiên.
rng &random(context &ctx);
/// @}
} // namespace njin
