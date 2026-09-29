#pragma once
#include "_types.h"
#include "njin_input.h"
#include "njin_json.h"
#include <initializer_list>
#include <vector>

namespace njin {
struct context;

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
bool input_any_pressed(const context &ctx, input_source &out);

/// Các nguồn đang gắn vào một action, theo thứ tự: phím, chuột, tay cầm.
/// @param ctx Context của engine.
/// @param action Action.
/// @return Danh sách nguồn (rỗng nếu handle không hợp lệ).
std::vector<input_source> action_sources(const context &ctx, action_handle action);

/// Gắn một nguồn vào action (như action_bind_key(), action_bind_mouse(),
/// action_bind_pad() tùy loại).
/// @param ctx Context của engine.
/// @param action Action.
/// @param source Nguồn.
void action_bind(context &ctx, action_handle action, input_source source);

/// Gỡ một nguồn khỏi action.
/// @param ctx Context của engine.
/// @param action Action.
/// @param source Nguồn.
void action_unbind(context &ctx, action_handle action, input_source source);

/// Gắn `source` vào `action` thay cho nguồn **cùng loại** đang có (phím thay
/// phím, nút tay cầm thay nút tay cầm), và gỡ nó khỏi mọi action khác để một
/// phím không làm hai việc. Đây là việc một màn hình đổi phím cần.
/// @param ctx Context của engine.
/// @param action Action.
/// @param source Nguồn mới.
void action_rebind(context &ctx, action_handle action, input_source source);

/// Một nguồn viết gọn cho action_define(): truyền thẳng một phím, một nút chuột
/// hoặc một nút tay cầm, không cần dựng njin::input_source.
struct binding {
  input_source source; ///< Nguồn đã đổi sang njin::input_source.
  /// @param key Phím.
  binding(key_code key) : source{input_source::key, (i32)key} {}
  /// @param button Nút chuột.
  binding(mouse_button button) : source{input_source::mouse, (i32)button} {}
  /// @param button Nút tay cầm.
  binding(gamepad_button button) : source{input_source::pad, (i32)button} {}
};

/// Đăng ký một action và gắn mọi nguồn của nó trong một lời gọi.
///
/// Thay cho action_register() rồi action_bind_key(), action_bind_mouse(),
/// action_bind_pad() lần lượt:
/// @code
/// g.jump = njin::action_define(ctx, "jump", {njin::key_space, njin::key_w, njin::pad_face_down});
/// @endcode
/// Nếu tên đã có thì các nguồn được **thêm** vào action đó, như action_bind().
/// Gọi trước settings_load() hay input_bindings_load(): phím người chơi đã đổi
/// sẽ thay các phím mặc định này.
/// @param ctx Context của engine.
/// @param name Tên action.
/// @param sources Các phím, nút chuột và nút tay cầm, theo bất kỳ thứ tự nào.
/// @return Handle của action, hoặc handle không hợp lệ (id 0) nếu `name` là null.
inline action_handle action_define(context &ctx, const char *name, std::initializer_list<binding> sources) {
  const action_handle handle = action_register(ctx, name);
  for (const binding &b : sources)
    action_bind(ctx, handle, b.source);
  return handle;
}

/// Một cặp phím của axis: phím cho chiều âm và phím cho chiều dương.
struct axis_keys {
  key_code negative; ///< Phím cho chiều âm (trái, lên).
  key_code positive; ///< Phím cho chiều dương (phải, xuống).
};

/// Đăng ký một axis và gắn mọi cặp phím và trục tay cầm của nó trong một lời gọi.
///
/// Thay cho axis_register() rồi axis_bind_keys(), axis_bind_pad() lần lượt:
/// @code
/// g.move = njin::axis_define(ctx, "move",
///                            {{njin::key_left, njin::key_right}, {njin::key_a, njin::key_d}},
///                            {njin::pad_axis_left_x});
/// @endcode
/// Nếu tên đã có thì các nguồn được thêm vào axis đó. Gọi trước settings_load().
/// @param ctx Context của engine.
/// @param name Tên axis.
/// @param keys Các cặp phím (âm, dương).
/// @param pads Các trục tay cầm. Có thể bỏ trống.
/// @return Handle của axis, hoặc handle không hợp lệ (id 0) nếu `name` là null.
inline axis_handle axis_define(context &ctx, const char *name, std::initializer_list<axis_keys> keys,
                               std::initializer_list<gamepad_axis> pads = {}) {
  const axis_handle handle = axis_register(ctx, name);
  for (const axis_keys &k : keys)
    axis_bind_keys(ctx, handle, k.negative, k.positive);
  for (const gamepad_axis a : pads)
    axis_bind_pad(ctx, handle, a);
  return handle;
}

/// Mọi phím đã gắn (action và axis) dạng JSON, để lưu vào file cài đặt:
/// `{"actions": {"jump": ["key:space", "pad:face_down"]}, "axes": {"move":
/// {"keys": [["left", "right"]], "pad": ["left_x"]}}}`.
/// @param ctx Context của engine.
/// @return Object JSON.
json_value input_bindings_save(const context &ctx);

/// Nạp phím từ JSON của input_bindings_save(). Action và axis có trong JSON
/// được **thay** toàn bộ phím; cái không có giữ nguyên, nên thêm action mới
/// trong bản cập nhật game vẫn có phím mặc định. Tên lạ bị bỏ qua (có log).
/// @param ctx Context của engine.
/// @param json Dữ liệu.
/// @return `false` nếu `json` không phải object.
bool input_bindings_load(context &ctx, const json_value &json);

/// Rung tay cầm.
/// @param ctx Context của engine.
/// @param pad Chỉ số tay cầm.
/// @param low Độ mạnh mô-tơ trái (rung trầm), 0..1.
/// @param high Độ mạnh mô-tơ phải (rung nhanh), 0..1.
/// @param seconds Thời gian rung.
void pad_rumble(context &ctx, i32 pad, f32 low, f32 high, f32 seconds);
/// @}
} // namespace njin
