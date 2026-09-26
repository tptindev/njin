#pragma once
#include "_types.h"
#include "njin_json.h"
#include <vector>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_input
/// @{

/// Một nguồn nhập liệu: một phím, một nút chuột, hoặc một nút tay cầm. Dùng để
/// đổi phím trong game và để hiện phím đang gắn.
struct input_source {
  /// Loại nguồn.
  enum kind_t : u8 {
    none,  ///< Không có.
    key,   ///< Phím, `code` là njin::key_code.
    mouse, ///< Nút chuột, `code` là njin::mouse_button.
    pad,   ///< Nút tay cầm, `code` là njin::gamepad_button.
  };
  kind_t kind = none; ///< Loại.
  i32 code = 0;       ///< Mã theo loại.

  /// So sánh hai nguồn. @param o Nguồn kia. @return `true` nếu cùng loại và mã.
  bool operator==(const input_source &o) const { return kind == o.kind && code == o.code; }
};

/// Tên hiển thị của một nguồn, bằng tiếng Anh ngắn như trên bàn phím: "Space",
/// "A", "Left Shift", "Mouse Left", "Pad A" (theo vị trí nút của Xbox).
/// @param source Nguồn.
/// @return Chuỗi tĩnh, không bao giờ null ("?" nếu không hợp lệ).
const char *input_source_name(input_source source);

/// Tên hiển thị của một phím. Xem input_source_name().
/// @param key Phím.
/// @return Chuỗi tĩnh.
const char *key_name(key_code key);

/// Nguồn vừa được nhấn ở frame này, nếu có: dùng cho màn hình "bấm phím mới".
/// Xét bàn phím trước, rồi chuột, rồi mọi tay cầm.
/// @param ctx Context của engine.
/// @param out Nhận nguồn vừa nhấn.
/// @return `true` nếu có một nguồn vừa được nhấn.
bool input_any_pressed(const njin_ctx &ctx, input_source &out);

/// Các nguồn đang gắn vào một action, theo thứ tự: phím, chuột, tay cầm.
/// @param ctx Context của engine.
/// @param action Action.
/// @return Danh sách nguồn (rỗng nếu handle không hợp lệ).
std::vector<input_source> action_sources(const njin_ctx &ctx, action_handle action);

/// Gắn một nguồn vào action (như action_bind_key(), action_bind_mouse(),
/// action_bind_pad() tùy loại).
/// @param ctx Context của engine.
/// @param action Action.
/// @param source Nguồn.
void action_bind(njin_ctx &ctx, action_handle action, input_source source);

/// Gỡ một nguồn khỏi action.
/// @param ctx Context của engine.
/// @param action Action.
/// @param source Nguồn.
void action_unbind(njin_ctx &ctx, action_handle action, input_source source);

/// Gắn `source` vào `action` thay cho nguồn **cùng loại** đang có (phím thay
/// phím, nút tay cầm thay nút tay cầm), và gỡ nó khỏi mọi action khác để một
/// phím không làm hai việc. Đây là việc một màn hình đổi phím cần.
/// @param ctx Context của engine.
/// @param action Action.
/// @param source Nguồn mới.
void action_rebind(njin_ctx &ctx, action_handle action, input_source source);

/// Mọi phím đã gắn (action và axis) dạng JSON, để lưu vào file cài đặt:
/// `{"actions": {"jump": ["key:space", "pad:face_down"]}, "axes": {"move":
/// {"keys": [["left", "right"]], "pad": ["left_x"]}}}`.
/// @param ctx Context của engine.
/// @return Object JSON.
json_value input_bindings_save(const njin_ctx &ctx);

/// Nạp phím từ JSON của input_bindings_save(). Action và axis có trong JSON
/// được **thay** toàn bộ phím; cái không có giữ nguyên, nên thêm action mới
/// trong bản cập nhật game vẫn có phím mặc định. Tên lạ bị bỏ qua (có log).
/// @param ctx Context của engine.
/// @param json Dữ liệu.
/// @return `false` nếu `json` không phải object.
bool input_bindings_load(njin_ctx &ctx, const json_value &json);

/// Rung tay cầm.
/// @param ctx Context của engine.
/// @param pad Chỉ số tay cầm.
/// @param low Độ mạnh mô-tơ trái (rung trầm), 0..1.
/// @param high Độ mạnh mô-tơ phải (rung nhanh), 0..1.
/// @param seconds Thời gian rung.
void pad_rumble(njin_ctx &ctx, i32 pad, f32 low, f32 high, f32 seconds);
/// @}
} // namespace njin
