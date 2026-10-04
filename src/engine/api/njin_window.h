#pragma once
#include "_math.h"
#include "_types.h"

namespace njin {
struct context;

/// @addtogroup grp_window
/// @{

/// Yêu cầu thoát game. Vòng lặp dừng ở cuối frame hiện tại, `phase_shutdown`
/// vẫn chạy như bình thường.
/// @param ctx Context của engine.
void quit(context &ctx);

/// Chặn nút đóng cửa sổ: nút [x], Alt+F4, và phím thoát (config::exit_key).
///
/// Khi bật, đóng cửa sổ không thoát game nữa mà chỉ làm
/// window_close_requested() trả về `true` trong một frame. Game tự quyết làm
/// gì (hỏi lại người chơi, lưu, dọn tài nguyên) rồi gọi quit(). Mặc định tắt.
/// @param ctx Context của engine.
/// @param on `true` để chặn.
void window_set_close_intercept(context &ctx, bool on);

/// Người chơi vừa đóng cửa sổ trong lúc window_set_close_intercept() đang bật.
/// @param ctx Context của engine.
/// @return `true` trong đúng frame có yêu cầu đóng.
bool window_close_requested(const context &ctx);

/// Đổi kích thước cửa sổ. Không có tác dụng khi đang toàn màn hình.
/// @param ctx Context của engine.
/// @param size Kích thước mới, tính bằng pixel.
void window_set_size(context &ctx, vec2 size);

/// Đổi tiêu đề cửa sổ.
/// @param ctx Context của engine.
/// @param title Tiêu đề mới.
void window_set_title(context &ctx, const char *title);

/// Bật hoặc tắt toàn màn hình.
///
/// Dùng chế độ cửa sổ không viền phủ kín màn hình, nên chuyển qua lại nhanh và
/// không đổi độ phân giải của màn hình.
/// @param ctx Context của engine.
/// @param fullscreen `true` để bật.
void window_set_fullscreen(context &ctx, bool fullscreen);

/// Cửa sổ có đang toàn màn hình không.
/// @param ctx Context của engine.
/// @return `true` nếu đang toàn màn hình.
bool window_fullscreen(const context &ctx);

/// Bật hoặc tắt đồng bộ dọc lúc đang chạy, xem config::vsync.
/// @param ctx Context của engine.
/// @param vsync `true` để bật.
void window_set_vsync(context &ctx, bool vsync);

/// Đồng bộ dọc có đang bật không.
/// @param ctx Context của engine.
/// @return `true` nếu đang bật.
bool window_vsync(const context &ctx);

/// Cửa sổ có vừa đổi kích thước ở frame này không (người dùng kéo cửa sổ, hoặc
/// bật tắt toàn màn hình).
/// @param ctx Context của engine.
/// @return `true` nếu kích thước vừa đổi.
bool window_resized(const context &ctx);

/// Hiện hoặc ẩn con trỏ chuột khi nó nằm trong cửa sổ.
/// @param ctx Context của engine.
/// @param visible `true` để hiện.
void cursor_set_visible(context &ctx, bool visible);

/// Chụp màn hình và lưu ra file ảnh.
///
/// Ảnh được chụp ở **cuối frame hiện tại**, sau `phase_post_render`, nên có
/// đủ mọi thứ của frame kể cả UI, dù gọi hàm này ở phase nào. Gọi nhiều lần
/// trong một frame thì lưu nhiều file cùng một ảnh.
///
/// Định dạng theo đuôi file: `.png` (nên dùng), `.bmp`, `.tga`, `.qoi`. Thư
/// mục cha được tạo nếu chưa có. Kết quả được ghi vào log.
/// @param ctx Context của engine.
/// @param path Đường dẫn file. Để nullptr thì lưu vào thư mục `screenshots`
/// trong thư mục lưu game (xem save_path()), tên theo ngày giờ.
void screenshot(context &ctx, const char *path = nullptr);

/// Bật độ phân giải ảo: game vẽ lên một màn hình cố định `size` pixel (ví dụ
/// 320 x 180), rồi engine phóng nó ra cửa sổ, giữ tỉ lệ, phần thừa là viền.
///
/// Mọi thứ đi qua màn hình ảo: thế giới, UI, toast, chuyển scene. screen_size()
/// trả về `size`, mouse_pos() và mouse_delta() tính theo pixel ảo, nên code
/// của game không cần biết cửa sổ thật to bao nhiêu. Ảnh được phóng không làm
/// mượt, nên pixel art sắc nét ở mọi cỡ cửa sổ.
///
/// Với `integer_scale`, chỉ phóng 1, 2, 3... lần: mọi pixel ảo to bằng nhau,
/// viền có thể dày hơn. Không thì phóng vừa khít cửa sổ.
/// @param ctx Context của engine.
/// @param size Kích thước ảo, pixel. `{0, 0}` để tắt và vẽ thẳng lên cửa sổ.
/// @param integer_scale Chỉ phóng theo bội số nguyên.
void window_set_virtual_size(context &ctx, vec2 size, bool integer_scale = true);

/// Màu viền quanh màn hình ảo. Mặc định là đen.
/// @param ctx Context của engine.
/// @param color Màu.
void window_set_bar_color(context &ctx, rgba color);

/// Kích thước thật của cửa sổ, pixel, kể cả khi có độ phân giải ảo.
/// @param ctx Context của engine.
/// @return Kích thước cửa sổ.
vec2 window_size(const context &ctx);

/// Vùng của cửa sổ mà màn hình ảo đang chiếm, pixel cửa sổ. Không có độ phân
/// giải ảo thì là cả cửa sổ.
/// @param ctx Context của engine.
/// @return Vùng ảnh trong cửa sổ.
rect window_viewport(const context &ctx);

/// Khóa con trỏ chuột trong cửa sổ và ẩn nó, như game bắn súng góc nhìn thứ
/// nhất. Khi khóa, dùng mouse_delta() thay cho mouse_pos().
/// @param ctx Context của engine.
/// @param locked `true` để khóa.
void cursor_set_locked(context &ctx, bool locked);
/// @}
} // namespace njin
