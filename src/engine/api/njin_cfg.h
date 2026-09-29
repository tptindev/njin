#pragma once
#include "_types.h"

namespace njin {
struct context;

/// @addtogroup grp_core
/// @{

/// Cấu hình cửa sổ và vòng lặp, truyền vào create().
struct config {
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
  /// Độ phân giải ảo, ví dụ `{320, 180}` cho pixel art. `{0, 0}` là tắt. Xem
  /// window_set_virtual_size().
  vec2 virtual_size{};
  /// Với độ phân giải ảo: chỉ phóng theo bội số nguyên (mọi pixel ảo to bằng
  /// nhau), phần thừa của cửa sổ là viền.
  bool integer_scale = true;
  /// Đồng bộ dọc: mỗi frame chờ màn hình làm tươi xong mới hiện, nên không xé
  /// hình và GPU, CPU nghỉ giữa các frame (đỡ tốn pin). Mặc định tắt. Bật thì
  /// `target_fps` vẫn còn tác dụng như một mức trần thêm. Xem window_set_vsync().
  bool vsync = false;
  /// Vẽ chữ ở độ phân giải thật của cửa sổ khi máy có GPU thật, để chữ nét ở mọi
  /// cỡ cửa sổ (mặc định bật). Với độ phân giải ảo (`virtual_size`), cảnh được
  /// vẽ nhỏ rồi phóng lên và chữ vẽ trong đó sẽ bị mờ; bật thì chữ trên màn hình
  /// (UI, HUD) được vẽ sau khi phóng, từ font dựng đúng cỡ trên màn hình. Xem
  /// trang Vẽ và chữ. Máy chỉ có renderer phần mềm luôn vẽ chữ trong ảnh ảo.
  bool crisp_text = true;
  /// Vẽ giao diện ở độ phân giải thật của cửa sổ, cho UI mịn. Với độ phân giải ảo
  /// (`virtual_size`) mặc định cả frame vẽ vào ảnh nhỏ rồi phóng lên bằng lọc
  /// nearest, nên panel bo góc, nút và thanh trượt bị vỡ hạt theo mức phóng. Bật
  /// thì world vẫn vẽ trong ảnh ảo (pixel art), còn `phase_post_render` (UI, HUD),
  /// hội thoại, toast, flash và fade vẽ sau khi ảnh đã phóng, thẳng vào cửa sổ:
  /// hình khối và chữ đều mịn ở mọi cỡ cửa sổ, tọa độ vẫn tính theo pixel ảo.
  /// Mặc định tắt: UI giữ nguyên kiểu pixel. Bật thì `crisp_text` không còn cần.
  /// Xem trang Vẽ và chữ.
  bool smooth_ui = false;
  /// Khử răng cưa bằng supersampling: thế giới và giao diện được vẽ ở độ phân
  /// giải gấp `render_scale` lần (theo mỗi chiều) rồi thu nhỏ lại bằng lọc
  /// mượt khi lên màn hình. `1` là tắt (mặc định). `2`, `4` hay `8` cho cạnh
  /// mượt hơn, tốn thêm bấy nhiêu lần pixel GPU phải vẽ (4 lần ở mức 2, 16 lần
  /// ở mức 4...).
  ///
  /// Đây không phải MSAA của cửa sổ (raylib chỉ có đúng một mức 4x qua GLFW,
  /// không chọn được 2x/8x); cách này chạy trên mọi GPU giống nhau và cho
  /// đúng số mức đã đặt. Không đổi được lúc đang chạy: `create()` tạo
  /// cửa sổ và các render texture theo đúng giá trị này một lần, nên đổi mức
  /// cần khởi động lại game (đọc giá trị người chơi chọn từ file cài đặt của
  /// bạn, trước khi gọi create() ở lần chạy sau). Xem render_scale().
  ///
  /// Không đổi tọa độ nào cả: screen_size(), chuột, camera vẫn tính như
  /// `virtual_size` (nếu có) hay kích thước cửa sổ (nếu không), không biết gì
  /// về `render_scale`. Pixel art dùng `virtual_size` với `filter_nearest` nên
  /// thường không cần; hợp game vẽ hình khối, sprite xoay hay chữ vector hơn.
  i32 render_scale = 1;
};

/// Trả về FPS mục tiêu đã cấu hình (`target_fps`).
///
/// Đây là giá trị cấu hình, không phải FPS đo được. Muốn thời gian của frame
/// thì dùng delta().
/// @param ctx Context của engine.
/// @return FPS mục tiêu.
f32 fps(const context &ctx);

/// Trả về kích thước màn hình mà game vẽ lên, tính bằng pixel.
///
/// Không có độ phân giải ảo thì đó là cửa sổ: lúc đầu bằng `width`, `height`
/// đã cấu hình, và thay đổi khi người dùng kéo cửa sổ (nếu `resizable`) hoặc
/// khi bật toàn màn hình. Có độ phân giải ảo (window_set_virtual_size()) thì
/// đó là kích thước ảo, không đổi theo cửa sổ; cỡ thật của cửa sổ là
/// window_size().
/// @param ctx Context của engine.
/// @return Kích thước cửa sổ: `x` là rộng, `y` là cao.
vec2 screen_size(const context &ctx);
/// @}
}
