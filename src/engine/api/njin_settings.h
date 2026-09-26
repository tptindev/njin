#pragma once
#include "njin_json.h"

namespace njin {
struct njin_ctx;

/// @addtogroup grp_settings
/// @{

/// Cài đặt của người chơi dạng JSON: âm lượng và tắt tiếng từng kênh, phím
/// đã gắn, toàn màn hình, ngôn ngữ.
/// @code{.json}
/// { "audio": { "master": 0.8, "music": 0.5, "sfx": 1, "ui": 1, "voice": 1,
///              "muted": ["music"] },
///   "input": { "actions": {...}, "axes": {...} },
///   "fullscreen": false, "language": "vi" }
/// @endcode
/// @param ctx Context của engine.
/// @return Object JSON.
json_value settings_to_json(const njin_ctx &ctx);

/// Áp dụng cài đặt từ JSON của settings_to_json(). Phần nào thiếu thì giữ
/// nguyên; ngôn ngữ chưa nạp bị bỏ qua.
/// @param ctx Context của engine.
/// @param json Dữ liệu.
void settings_apply(njin_ctx &ctx, const json_value &json);

/// Lưu cài đặt vào file trong thư mục lưu game (save_path()), kèm dữ liệu
/// riêng của game nếu có (độ khó, độ sáng...), dưới khóa `"game"`.
/// @code
/// njin::settings_save(ctx); // khi rời menu cài đặt
/// @endcode
/// @param ctx Context của engine.
/// @param file Tên file.
/// @param game Dữ liệu riêng của game, hoặc null.
/// @return `true` nếu ghi được.
bool settings_save(const njin_ctx &ctx, const char *file = "settings.json",
                   const json_value *game = nullptr);

/// Nạp và áp dụng cài đặt đã lưu. Gọi sau khi đã đăng ký action, axis và nạp
/// ngôn ngữ, thường trong `phase_startup`, để phím người chơi đã đổi thay phím
/// mặc định. Chưa có file (lần chạy đầu) thì không làm gì.
/// @param ctx Context của engine.
/// @param file Tên file.
/// @param game Nhận dữ liệu riêng của game đã lưu, hoặc null.
/// @return `true` nếu đã đọc được file.
bool settings_load(njin_ctx &ctx, const char *file = "settings.json", json_value *game = nullptr);
/// @}
} // namespace njin
