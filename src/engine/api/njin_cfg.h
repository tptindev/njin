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
};

/// Trả về FPS mục tiêu đã cấu hình (`target_fps`).
///
/// Đây là giá trị cấu hình, không phải FPS đo được. Muốn thời gian của frame
/// thì dùng delta().
/// @param ctx Context của engine.
/// @return FPS mục tiêu.
f32 fps(const njin_ctx &ctx);

/// Trả về kích thước cửa sổ đã cấu hình (`width`, `height`), tính bằng pixel.
/// @param ctx Context của engine.
/// @return Kích thước cửa sổ: `x` là rộng, `y` là cao.
vec2 screen_size(const njin_ctx &ctx);
/// @}
}
