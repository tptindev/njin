#pragma once
#include "_mod.h"
#include "_random.h"
#include "_types.h"
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>
#include <initializer_list>
#include <span>

namespace njin {
// Opaque: created with njin_create (njin.h), only accessed through the
// functions below.
struct njin_ctx;

/// @addtogroup grp_module
/// @{

/// Trả về registry của EnTT chứa mọi entity và component của game.
///
/// EnTT là một phần của API module, nên system có thể tự định nghĩa component
/// và truy vấn registry trực tiếp.
/// @param ctx Context của engine.
/// @return Registry của game.
entt::registry &world(njin_ctx &ctx);

/// Trả về dispatcher của EnTT để gửi và nhận event giữa các system.
///
/// Event đã `enqueue` được phát ngay sau `phase_post_update`.
/// @param ctx Context của engine.
/// @return Dispatcher của game.
entt::dispatcher &events(njin_ctx &ctx);

/// Thêm một system vào lịch chạy của một phase.
///
/// Chỉ hợp lệ bên trong callback `setup` của module. Gọi ở nơi khác sẽ bị bỏ
/// qua và ghi cảnh báo. Các module chạy theo thứ tự đăng ký.
/// @param ctx Context của engine.
/// @param phase Phase mà system chạy trong đó.
/// @param fnc Hàm system.
/// @param name Tên system, hiện trong njin_inspector. Có thể null.
void ecs_register(njin_ctx &ctx, sys_phase phase, sys_fnc fnc, const char *name = nullptr);

/// Thêm một system kèm ràng buộc thứ tự vào lịch chạy của một phase.
///
/// Trong cùng một module và phase, các system được sắp theo sys_desc (xem
/// _mod.h): ràng buộc `after`/`before` được đảm bảo trước, sau đó system có
/// `order` nhỏ hơn chạy trước, còn bằng nhau thì theo thứ tự đăng ký.
/// @param ctx Context của engine.
/// @param phase Phase mà system chạy trong đó.
/// @param desc Mô tả system và thứ tự của nó.
void ecs_register(njin_ctx &ctx, sys_phase phase, const sys_desc &desc);

/// Chạy `setup` của module và đưa các system của nó vào lịch chạy.
///
/// Phải gọi trước njin_run(). Tên module chỉ được đăng ký một lần.
/// @param ctx Context của engine.
/// @param desc Mô tả module.
void njin_mod_register(njin_ctx &ctx, const mod_desc &desc);

/// Đăng ký nhiều module một lần, theo đúng thứ tự trong danh sách.
///
/// Giống hệt gọi njin_mod_register() cho từng module lần lượt: module đứng
/// trước chạy trước trong cùng phase, và một module lỗi (trùng tên, đăng ký
/// sau njin_run()) chỉ bị bỏ qua riêng nó.
/// @code
/// njin::njin_mod_register(*ctx, {input_module(), physics_module(), ui_module()});
/// @endcode
/// @param ctx Context của engine.
/// @param mods Các module, theo thứ tự đăng ký.
void njin_mod_register(njin_ctx &ctx, std::initializer_list<mod_desc> mods);

/// Đăng ký nhiều module từ một danh sách tạo lúc chạy, ví dụ một
/// `std::vector<mod_desc>` hay `std::array`. Cùng quy tắc với bản nhận danh
/// sách trực tiếp.
/// @param ctx Context của engine.
/// @param mods Các module, theo thứ tự đăng ký.
void njin_mod_register(njin_ctx &ctx, std::span<const mod_desc> mods);
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
f32 delta(const njin_ctx &ctx);

/// Thời gian thật của frame trước, không bị tốc độ thời gian hay tạm dừng ảnh
/// hưởng. Dùng cho thứ vẫn phải chạy khi game dừng, như menu tạm dừng.
/// @param ctx Context của engine.
/// @return Thời gian thật của frame, tính bằng giây.
f32 delta_real(const njin_ctx &ctx);

/// Đặt tốc độ thời gian của game. 1 là bình thường, 0.5 là chậm một nửa.
///
/// Ảnh hưởng delta(), `phase_fixed_update` và animation của sprite. Giá trị âm
/// được coi là 0.
/// @param ctx Context của engine.
/// @param scale Tốc độ thời gian.
void time_set_scale(njin_ctx &ctx, f32 scale);

/// Tốc độ thời gian hiện tại.
/// @param ctx Context của engine.
/// @return Tốc độ thời gian, mặc định 1.
f32 time_scale(const njin_ctx &ctx);

/// Tạm dừng hoặc chạy tiếp thời gian của game.
///
/// Khi dừng, delta() trả về 0 và `phase_fixed_update` không chạy, nhưng các
/// phase khác vẫn chạy mỗi frame: input vẫn đọc được, menu vẫn vẽ được. Giữ
/// nguyên tốc độ thời gian đã đặt.
/// @param ctx Context của engine.
/// @param paused `true` để tạm dừng.
void time_set_paused(njin_ctx &ctx, bool paused);

/// Game có đang tạm dừng không.
/// @param ctx Context của engine.
/// @return `true` nếu đang tạm dừng.
bool time_paused(const njin_ctx &ctx);

/// Độ dài một nhịp của `phase_fixed_update`, bằng `1 / njin_cfg::fixed_hz`.
/// @param ctx Context của engine.
/// @return Độ dài một nhịp, tính bằng giây.
f32 fixed_delta(const njin_ctx &ctx);

/// Phần nhịp cố định còn dư sau `phase_fixed_update` của frame này, từ 0 đến 1.
///
/// Dùng để nội suy vị trí khi vẽ, cho chuyển động mượt dù FPS khác nhịp vật lý:
/// `draw_pos = lerp(prev_pos, pos, fixed_alpha(ctx))`.
/// @param ctx Context của engine.
/// @return Tỉ lệ từ 0 đến 1.
f32 fixed_alpha(const njin_ctx &ctx);

/// Thời gian đã trôi qua kể từ lúc mở cửa sổ, tính bằng giây.
/// @param ctx Context của engine.
/// @return Thời gian đã chạy, tính bằng giây.
f32 elapsed(const njin_ctx &ctx);
/// @}

/// @addtogroup grp_random
/// @{

/// Bộ sinh số ngẫu nhiên dùng chung của engine.
///
/// Được gieo hạt giống từ thời gian lúc mở game, nên mỗi lần chơi mỗi khác.
/// Gọi `random(ctx).reseed(n)` để có dãy số lặp lại được, ví dụ khi thử lỗi.
/// @param ctx Context của engine.
/// @return Bộ sinh số ngẫu nhiên.
rng &random(njin_ctx &ctx);
/// @}
} // namespace njin
