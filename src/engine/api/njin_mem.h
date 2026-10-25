#pragma once
#include "_types.h"
#include <functional>

namespace njin {
struct context;

/// @addtogroup grp_core
/// @{

/// Số byte game đang giữ, theo bộ cấp phát của njin. Mọi `new`/`delete` của C++
/// (engine, thư viện chuẩn và code của game) và mọi cấp phát của raylib (ảnh,
/// âm thanh, mesh phía CPU) đều đi qua nó, nên con số này là phần RAM mà game
/// tự cấp phát. Không gồm bộ nhớ GPU, bộ nhớ của driver, và các thư viện cấp
/// phát riêng (Jolt, Lua).
struct mem_stats {
  usize used = 0;          ///< Số byte đang giữ.
  usize peak = 0;          ///< `used` cao nhất từ lúc chạy (hoặc từ lần mem_reset_peak()).
  usize soft_limit = 0;    ///< Ngưỡng mềm, byte. 0 là không có.
  usize hard_limit = 0;    ///< Trần cứng, byte. 0 là không có.
  usize new_bytes = 0;     ///< Phần của `used` do `new` của C++ giữ.
  usize raylib_bytes = 0;  ///< Phần của `used` do raylib giữ.
  u64 allocs = 0;          ///< Số lần cấp phát từ lúc chạy.
  u64 frees = 0;           ///< Số lần giải phóng từ lúc chạy.
  u64 refused = 0;         ///< Số lần trần cứng từ chối một cấp phát.
};

/// Số liệu bộ nhớ hiện tại.
/// @return Ảnh chụp tại lúc gọi; gọi được từ mọi luồng.
mem_stats mem_info();

/// Đặt ngưỡng mềm và trần cứng cho vùng RAM của game, tính bằng byte.
///
/// - **Ngưỡng mềm**: vượt qua thì đầu frame sau engine ghi một cảnh báo và gọi
///   các hàm đăng ký bằng mem_on_pressure(): chỗ để game bỏ cache, hạ chất
///   lượng, dỡ phần level đã đi qua. Hàm chỉ gọi lại khi `used` đã xuống dưới
///   90% ngưỡng rồi lại vượt lên.
/// - **Trần cứng**: cấp phát nào làm `used` vượt trần thì bị từ chối. `new` ném
///   `std::bad_alloc`, còn raylib thì dừng game kèm một dòng giải thích (thay vì
///   ăn hết RAM của máy rồi bị hệ điều hành giết không rõ lý do). Đặt ngưỡng
///   mềm thấp hơn trần, ví dụ 80%, để hàm giải phóng kịp chạy trước.
///
/// Có thể đặt từ đầu bằng `config::mem_budget_mb` và `config::mem_limit_mb`.
/// @param soft_bytes Ngưỡng mềm, 0 là tắt.
/// @param hard_bytes Trần cứng, 0 là tắt. Nên không nhỏ hơn mức đang dùng.
void mem_set_limit(usize soft_bytes, usize hard_bytes);

/// Hàm được gọi khi game vượt ngưỡng mềm. Nhận số liệu lúc đó.
using mem_pressure_fn = std::function<void(context &, const mem_stats &)>;

/// Đăng ký hàm giải phóng bộ nhớ, gọi khi vượt ngưỡng mềm (xem mem_set_limit())
/// hoặc khi mem_pressure() được gọi, kể cả từ njin_inspector.
/// @code
/// njin::mem_on_pressure([](njin::context &c, const njin::mem_stats &) { drop_unused_textures(c); });
/// @endcode
/// @param fn Hàm cần gọi. Chạy trong luồng chính, ở đầu frame, nên được tạo và hủy
///           entity, nạp và bỏ tài nguyên.
/// @return Số định danh (lớn hơn 0) để mem_off_pressure().
u32 mem_on_pressure(mem_pressure_fn fn);

/// Bỏ một hàm đã đăng ký bằng mem_on_pressure().
/// @param id Giá trị mem_on_pressure() trả về.
void mem_off_pressure(u32 id);

/// Gọi ngay (ở đầu frame sau) các hàm của mem_on_pressure(), không chờ vượt
/// ngưỡng. Dùng để thử xem game có giải phóng đúng không, hoặc khi game tự biết
/// sắp cần nhiều chỗ, ví dụ trước khi nạp level lớn.
/// @param ctx Context của engine.
void mem_pressure(context &ctx);

/// Đặt lại `peak` về mức đang dùng, để đo đỉnh của riêng một đoạn: nạp một
/// level, chơi một màn.
void mem_reset_peak();

/// @}
} // namespace njin
