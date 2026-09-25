#pragma once
#include "_types.h"

namespace njin {
struct njin_ctx;

/// @addtogroup grp_core
/// @{

/// Cấu hình cửa sổ và vòng lặp, truyền vào njin_create().
struct njin_cfg {
  const char *title;  ///< Tiêu đề cửa sổ.
  f32 width;          ///< Chiều rộng cửa sổ (pixel).
  f32 height;         ///< Chiều cao cửa sổ (pixel).
  f32 target_fps;     ///< FPS mục tiêu của vòng lặp.
  /// Màu nền xóa mỗi frame. Mặc định là trắng.
  rgba clear_bg_color = { .r = 1.0, .g = 1.0, .b = 1.0, .a = 1.0 };
  /// Số lần `phase_fixed_update` chạy mỗi giây. Mặc định 60.
  f32 fixed_hz = 60.0f;
  /// Phím đóng game ngay lập tức. Mặc định là Esc. Đặt `key_none` để tắt, khi
  /// game cần dùng Esc cho việc khác (ví dụ mở menu tạm dừng).
  key_code exit_key = key_escape;
  /// Cho phép người dùng kéo đổi kích thước cửa sổ.
  bool resizable = false;
  /// Tên thư mục lưu game, xem save_path(). Để trống thì dùng `title`.
  const char *app_name = nullptr;
};

/// Trả về FPS mục tiêu đã cấu hình (`target_fps`).
///
/// Đây là giá trị cấu hình, không phải FPS đo được. Muốn thời gian của frame
/// thì dùng delta().
/// @param ctx Context của engine.
/// @return FPS mục tiêu.
f32 fps(const njin_ctx &ctx);

/// Trả về kích thước hiện tại của cửa sổ, tính bằng pixel.
///
/// Lúc đầu bằng `width`, `height` đã cấu hình; thay đổi khi người dùng kéo cửa
/// sổ (nếu `resizable`) hoặc khi bật toàn màn hình.
/// @param ctx Context của engine.
/// @return Kích thước cửa sổ: `x` là rộng, `y` là cao.
vec2 screen_size(const njin_ctx &ctx);
/// @}
}
