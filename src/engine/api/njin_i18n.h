#pragma once
#include "_types.h"
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_i18n
/// @{

/// Nạp bảng chuỗi của một ngôn ngữ từ file JSON.
///
/// File là một object; object lồng nhau được nối tên bằng dấu chấm, nên hai
/// cách viết sau là một:
/// @code{.json}
/// { "menu": { "play": "Chơi", "quit": "Thoát" }, "_name": "Tiếng Việt" }
/// { "menu.play": "Chơi", "menu.quit": "Thoát", "_name": "Tiếng Việt" }
/// @endcode
/// Khóa `_name` là tên ngôn ngữ để hiện trong menu (i18n_language_name()).
/// Nạp lại cùng một ngôn ngữ thì chuỗi mới ghi đè chuỗi cũ, nên có thể chia
/// bảng thành nhiều file. Ngôn ngữ nạp đầu tiên thành ngôn ngữ đang dùng.
/// @param ctx Context của engine.
/// @param lang Mã ngôn ngữ, ví dụ `"vi"`, `"en"`.
/// @param path Đường dẫn file JSON.
/// @return `false` nếu không đọc được file (có ghi log).
bool i18n_load(njin_ctx &ctx, const char *lang, const char *path);

/// Chọn ngôn ngữ đang dùng. Ngôn ngữ chưa nạp bị bỏ qua (có ghi log).
/// @param ctx Context của engine.
/// @param lang Mã ngôn ngữ.
void i18n_set_language(njin_ctx &ctx, const char *lang);

/// Mã ngôn ngữ đang dùng, hoặc chuỗi rỗng nếu chưa nạp ngôn ngữ nào.
/// @param ctx Context của engine. @return Mã ngôn ngữ.
const char *i18n_language(const njin_ctx &ctx);

/// Ngôn ngữ dự phòng: khi ngôn ngữ đang dùng thiếu một khóa thì lấy ở đây
/// (thường là ngôn ngữ gốc của game). Mặc định là ngôn ngữ nạp đầu tiên.
/// @param ctx Context của engine.
/// @param lang Mã ngôn ngữ.
void i18n_set_fallback(njin_ctx &ctx, const char *lang);

/// Mã các ngôn ngữ đã nạp, theo thứ tự nạp. Dùng cho lựa chọn ngôn ngữ trong
/// menu cài đặt.
/// @param ctx Context của engine. @return Danh sách mã.
std::vector<std::string> i18n_languages(const njin_ctx &ctx);

/// Tên hiện của một ngôn ngữ: khóa `_name` trong file của nó, hoặc chính mã.
/// @param ctx Context của engine. @param lang Mã ngôn ngữ. @return Tên.
const char *i18n_language_name(const njin_ctx &ctx, const char *lang);

/// Chuỗi đã dịch của `key` trong ngôn ngữ đang dùng.
///
/// Thiếu thì lấy ở ngôn ngữ dự phòng; thiếu nữa thì trả về chính `key` (và ghi
/// log một lần), nên chữ thiếu dịch hiện ra rõ ràng thay vì mất hẳn.
/// @code
/// njin::ui_button(ctx, njin::tr(ctx, "menu.play"));
/// @endcode
/// Chuỗi trả về còn hợp lệ đến khi nạp thêm file hay đổi ngôn ngữ.
/// @param ctx Context của engine.
/// @param key Khóa.
/// @return Chuỗi UTF-8.
const char *tr(const njin_ctx &ctx, const char *key);

/// Như tr(), rồi thay `{0}`, `{1}`... bằng các tham số theo thứ tự: câu dịch
/// được đổi trật tự từ theo từng ngôn ngữ.
/// @code
/// // "hud.coins": "Vàng: {0}/{1}"
/// njin::trf(ctx, "hud.coins", {std::to_string(coins), std::to_string(total)});
/// @endcode
/// @param ctx Context của engine.
/// @param key Khóa.
/// @param args Giá trị thay vào.
/// @return Chuỗi đã thay.
std::string trf(const njin_ctx &ctx, const char *key, std::initializer_list<std::string_view> args);

/// Có khóa này trong ngôn ngữ đang dùng hoặc ngôn ngữ dự phòng không.
/// @param ctx Context của engine. @param key Khóa. @return `true` nếu có.
bool i18n_has(const njin_ctx &ctx, const char *key);
/// @}
} // namespace njin
