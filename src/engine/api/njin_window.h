#pragma once
#include "_types.h"

namespace njin {
struct njin_ctx;

/// @addtogroup grp_window
/// @{

/// Yêu cầu thoát game. Vòng lặp dừng ở cuối frame hiện tại, `phase_shutdown`
/// vẫn chạy như bình thường.
/// @param ctx Context của engine.
void njin_quit(njin_ctx &ctx);

/// Đổi kích thước cửa sổ. Không có tác dụng khi đang toàn màn hình.
/// @param ctx Context của engine.
/// @param size Kích thước mới, tính bằng pixel.
void window_set_size(njin_ctx &ctx, vec2 size);

/// Đổi tiêu đề cửa sổ.
/// @param ctx Context của engine.
/// @param title Tiêu đề mới.
void window_set_title(njin_ctx &ctx, const char *title);

/// Bật hoặc tắt toàn màn hình.
///
/// Dùng chế độ cửa sổ không viền phủ kín màn hình, nên chuyển qua lại nhanh và
/// không đổi độ phân giải của màn hình.
/// @param ctx Context của engine.
/// @param fullscreen `true` để bật.
void window_set_fullscreen(njin_ctx &ctx, bool fullscreen);

/// Cửa sổ có đang toàn màn hình không.
/// @param ctx Context của engine.
/// @return `true` nếu đang toàn màn hình.
bool window_fullscreen(const njin_ctx &ctx);

/// Cửa sổ có vừa đổi kích thước ở frame này không (người dùng kéo cửa sổ, hoặc
/// bật tắt toàn màn hình).
/// @param ctx Context của engine.
/// @return `true` nếu kích thước vừa đổi.
bool window_resized(const njin_ctx &ctx);

/// Hiện hoặc ẩn con trỏ chuột khi nó nằm trong cửa sổ.
/// @param ctx Context của engine.
/// @param visible `true` để hiện.
void cursor_set_visible(njin_ctx &ctx, bool visible);

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
void screenshot(njin_ctx &ctx, const char *path = nullptr);

/// Khóa con trỏ chuột trong cửa sổ và ẩn nó, như game bắn súng góc nhìn thứ
/// nhất. Khi khóa, dùng mouse_delta() thay cho mouse_pos().
/// @param ctx Context của engine.
/// @param locked `true` để khóa.
void cursor_set_locked(njin_ctx &ctx, bool locked);
/// @}
} // namespace njin
