#pragma once
#include "_types.h"
#include <string>
#include <string_view>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_file
/// @{

/// File có tồn tại không.
/// @param path Đường dẫn.
/// @return `true` nếu là một file tồn tại (không phải thư mục).
bool file_exists(const char *path);

/// Đọc toàn bộ một file.
/// @param path Đường dẫn.
/// @param out Nhận nội dung file. Không đổi nếu đọc thất bại.
/// @return `true` nếu đọc được.
bool file_read(const char *path, std::string &out);

/// Ghi toàn bộ một file, tạo thư mục cha nếu chưa có.
///
/// Ghi vào một file tạm rồi đổi tên, nên nếu game tắt giữa chừng thì file cũ
/// vẫn còn nguyên, không bị ghi dở. Nên dùng cho file lưu game.
/// @param path Đường dẫn.
/// @param data Nội dung, có thể là dữ liệu nhị phân.
/// @return `true` nếu ghi được.
bool file_write(const char *path, std::string_view data);

/// Đường dẫn đầy đủ của một file lưu game, trong thư mục riêng của người dùng.
///
/// Thư mục là `%APPDATA%/<tên game>` trên Windows,
/// `~/Library/Application Support/<tên game>` trên macOS và
/// `~/.local/share/<tên game>` trên Linux. Tên game lấy từ `njin_cfg::app_name`,
/// hoặc từ tiêu đề cửa sổ nếu để trống. Thư mục được tạo nếu chưa có.
/// @param ctx Context của engine.
/// @param file_name Tên file, ví dụ `"save.txt"`.
/// @return Đường dẫn đầy đủ.
std::string save_path(const njin_ctx &ctx, const char *file_name);
/// @}
} // namespace njin
