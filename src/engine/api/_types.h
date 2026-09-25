#pragma once

#include <cstddef>
#include <cstdint>

namespace njin {
/// @addtogroup grp_types
/// @{

/// @name Số nguyên có dấu
/// @{
using i8 = std::int8_t; ///< Số nguyên có dấu 8 bit.
using i16 = std::int16_t; ///< Số nguyên có dấu 16 bit.
using i32 = std::int32_t; ///< Số nguyên có dấu 32 bit.
using i64 = std::int64_t; ///< Số nguyên có dấu 64 bit.
/// @}

/// @name Số nguyên không dấu
/// @{
using u8 = std::uint8_t; ///< Số nguyên không dấu 8 bit.
using u16 = std::uint16_t; ///< Số nguyên không dấu 16 bit.
using u32 = std::uint32_t; ///< Số nguyên không dấu 32 bit.
using u64 = std::uint64_t; ///< Số nguyên không dấu 64 bit.
/// @}

/// @name Số thực
/// @{
using f32 = float; ///< Số thực 32 bit.
using f64 = double; ///< Số thực 64 bit.
/// @}

/// @name Số nguyên cỡ con trỏ
/// @{
using usize = std::size_t; ///< Số nguyên không dấu cỡ con trỏ, dùng cho kích thước và chỉ số.
using isize = std::ptrdiff_t; ///< Số nguyên có dấu cỡ con trỏ, dùng cho hiệu của hai chỉ số.
/// @}

/// Vector 2 chiều. Dùng cho vị trí, kích thước, độ dời.
struct vec2 {
  f32 x; ///< Thành phần ngang.
  f32 y; ///< Thành phần dọc (trong không gian màn hình, y tăng khi đi xuống).
};

/// Vector 4 chiều. Dùng để truyền giá trị `vec4` vào uniform của shader.
struct vec4 {
  f32 x; ///< Thành phần thứ nhất.
  f32 y; ///< Thành phần thứ hai.
  f32 z; ///< Thành phần thứ ba.
  f32 w; ///< Thành phần thứ tư.
};

/// Màu gồm đỏ, xanh lá, xanh dương và alpha. Mỗi kênh nằm trong khoảng 0..1.
///
/// `{1, 1, 1, 1}` là trắng đục, `{0, 0, 0, 0}` là trong suốt hoàn toàn.
struct rgba {
  f32 r; ///< Đỏ.
  f32 g; ///< Xanh lá.
  f32 b; ///< Xanh dương.
  f32 a; ///< Độ đục (0 = trong suốt, 1 = đục hoàn toàn).
};

/// Góc nhìn 2D đang được dùng để vẽ, lấy từ camera_active().
///
/// Điểm `target` trong thế giới được vẽ tại vị trí `offset` trên màn hình.
struct camera_view {
  f32 zoom;     ///< Độ phóng đại. 1 là không phóng.
  f32 rotation; ///< Góc xoay tính bằng độ, theo chiều kim đồng hồ.
  vec2 offset;  ///< Vị trí trên màn hình (pixel) nơi `target` được vẽ.
  vec2 target;  ///< Điểm trong thế giới mà camera nhìn vào.
};

/// Định danh của một action nhập liệu, tạo bởi action_register().
///
/// `id == 0` là handle không hợp lệ. Mọi hàm nhận handle không hợp lệ đều
/// không làm gì và trả về `false`.
struct action_handle {
  u32 id = 0; ///< 0 nghĩa là không hợp lệ.
};

/// Định danh của một axis nhập liệu, tạo bởi axis_register().
///
/// `id == 0` là handle không hợp lệ. Mọi hàm nhận handle không hợp lệ đều
/// không làm gì và axis_value() trả về 0.
struct axis_handle {
  u32 id = 0; ///< 0 nghĩa là không hợp lệ.
};

/// Định danh của một shader, tạo bởi shader_load().
///
/// `id == 0` là handle không hợp lệ (ví dụ file thiếu hoặc lỗi biên dịch).
/// Handle đã unload cũng bị bỏ qua, không bao giờ trỏ nhầm sang shader mới.
struct shader_handle {
  u32 id = 0; ///< 0 nghĩa là không hợp lệ.
};

/// Định danh của một texture, tạo bởi texture_load().
///
/// `id == 0` là handle không hợp lệ. Handle đã unload cũng bị bỏ qua.
struct texture_handle {
  u32 id = 0; ///< 0 nghĩa là không hợp lệ.
};

/// Định danh của một render texture, tạo bởi render_texture_load().
///
/// `id == 0` là handle không hợp lệ. Handle đã unload cũng bị bỏ qua.
struct render_texture_handle {
  u32 id = 0; ///< 0 nghĩa là không hợp lệ.
};
/// @}


/// @addtogroup grp_input
/// @{

/// Mã phím bàn phím, dùng với key_pressed(), action_bind_key() và các hàm liên quan.
enum key_code {
  key_none = 0, ///< Không có phím. Không hợp lệ khi bind hay truy vấn.

  // Letters
  key_a, ///< Phím A
  key_b, ///< Phím B
  key_c, ///< Phím C
  key_d, ///< Phím D
  key_e, ///< Phím E
  key_f, ///< Phím F
  key_g, ///< Phím G
  key_h, ///< Phím H
  key_i, ///< Phím I
  key_j, ///< Phím J
  key_k, ///< Phím K
  key_l, ///< Phím L
  key_m, ///< Phím M
  key_n, ///< Phím N
  key_o, ///< Phím O
  key_p, ///< Phím P
  key_q, ///< Phím Q
  key_r, ///< Phím R
  key_s, ///< Phím S
  key_t, ///< Phím T
  key_u, ///< Phím U
  key_v, ///< Phím V
  key_w, ///< Phím W
  key_x, ///< Phím X
  key_y, ///< Phím Y
  key_z, ///< Phím Z

  // Numbers
  key_0, ///< Phím 0 (hàng số)
  key_1, ///< Phím 1 (hàng số)
  key_2, ///< Phím 2 (hàng số)
  key_3, ///< Phím 3 (hàng số)
  key_4, ///< Phím 4 (hàng số)
  key_5, ///< Phím 5 (hàng số)
  key_6, ///< Phím 6 (hàng số)
  key_7, ///< Phím 7 (hàng số)
  key_8, ///< Phím 8 (hàng số)
  key_9, ///< Phím 9 (hàng số)

  // Function keys
  key_f1, ///< Phím F1
  key_f2, ///< Phím F2
  key_f3, ///< Phím F3
  key_f4, ///< Phím F4
  key_f5, ///< Phím F5
  key_f6, ///< Phím F6
  key_f7, ///< Phím F7
  key_f8, ///< Phím F8
  key_f9, ///< Phím F9
  key_f10, ///< Phím F10
  key_f11, ///< Phím F11
  key_f12, ///< Phím F12

  // Navigation
  key_up, ///< Mũi tên lên
  key_down, ///< Mũi tên xuống
  key_left, ///< Mũi tên trái
  key_right, ///< Mũi tên phải

  key_home, ///< Home
  key_end, ///< End
  key_page_up, ///< Page Up
  key_page_down, ///< Page Down
  key_insert, ///< Insert
  key_delete, ///< Delete

  // General
  key_space, ///< Phím cách
  key_enter, ///< Enter
  key_tab, ///< Tab
  key_escape, ///< Esc
  key_backspace, ///< Backspace

  key_caps_lock, ///< Caps Lock
  key_scroll_lock, ///< Scroll Lock
  key_num_lock, ///< Num Lock
  key_print_screen, ///< Print Screen
  key_pause, ///< Pause/Break

  // Symbols
  key_apostrophe, ///< Dấu nháy đơn
  key_comma, ///< Dấu phẩy
  key_minus, ///< Dấu trừ
  key_period, ///< Dấu chấm
  key_slash, ///< Dấu gạch chéo
  key_semicolon, ///< Dấu chấm phẩy
  key_equal, ///< Dấu bằng
  key_left_bracket, ///< Ngoặc vuông trái
  key_backslash, ///< Dấu gạch chéo ngược
  key_right_bracket, ///< Ngoặc vuông phải
  key_grave, ///< Dấu huyền

  // Modifiers
  key_left_shift, ///< Shift trái
  key_left_control, ///< Ctrl trái
  key_left_alt, ///< Alt trái
  key_left_super, ///< Windows/Super trái

  key_right_shift, ///< Shift phải
  key_right_control, ///< Ctrl phải
  key_right_alt, ///< Alt phải
  key_right_super, ///< Windows/Super phải

  // Keypad
  key_kp_0, ///< Bàn phím số: 0
  key_kp_1, ///< Bàn phím số: 1
  key_kp_2, ///< Bàn phím số: 2
  key_kp_3, ///< Bàn phím số: 3
  key_kp_4, ///< Bàn phím số: 4
  key_kp_5, ///< Bàn phím số: 5
  key_kp_6, ///< Bàn phím số: 6
  key_kp_7, ///< Bàn phím số: 7
  key_kp_8, ///< Bàn phím số: 8
  key_kp_9, ///< Bàn phím số: 9

  key_kp_decimal, ///< Bàn phím số: dấu thập phân
  key_kp_divide, ///< Bàn phím số: chia
  key_kp_multiply, ///< Bàn phím số: nhân
  key_kp_subtract, ///< Bàn phím số: trừ
  key_kp_add, ///< Bàn phím số: cộng
  key_kp_enter, ///< Bàn phím số: Enter
  key_kp_equal, ///< Bàn phím số: bằng

  key_count ///< Số lượng mã phím. Không phải một phím thật.
};

/// Số tay cầm tối đa được theo dõi. Chỉ số tay cầm hợp lệ là `0..gamepad_max-1`.
inline constexpr i32 gamepad_max = 4;

/// Nút chuột, dùng với mouse_pressed(), action_bind_mouse() và các hàm liên quan.
enum mouse_button {
  mouse_left,    ///< Chuột trái.
  mouse_right,   ///< Chuột phải.
  mouse_middle,  ///< Chuột giữa (nhấn bánh xe).
  mouse_side,    ///< Nút bên.
  mouse_extra,   ///< Nút phụ.
  mouse_forward, ///< Nút tiến.
  mouse_back,    ///< Nút lùi.
  mouse_button_count ///< Số lượng nút chuột. Không phải một nút thật.
};

/// Nút tay cầm, dùng với pad_pressed(), action_bind_pad() và các hàm liên quan.
///
/// Tên nút mặt theo vị trí, không theo nhãn: `pad_face_down` là A trên Xbox và
/// Cross (X) trên PlayStation.
enum gamepad_button {
  pad_none, ///< Không có nút. Không hợp lệ khi bind hay truy vấn.
  pad_dpad_up,    ///< D-pad lên.
  pad_dpad_right, ///< D-pad phải.
  pad_dpad_down,  ///< D-pad xuống.
  pad_dpad_left,  ///< D-pad trái.
  pad_face_up,    ///< Nút mặt trên (Y trên Xbox, Tam giác trên PlayStation).
  pad_face_right, ///< Nút mặt phải (B trên Xbox, Tròn trên PlayStation).
  pad_face_down,  ///< Nút mặt dưới (A trên Xbox, Chéo trên PlayStation).
  pad_face_left,  ///< Nút mặt trái (X trên Xbox, Vuông trên PlayStation).
  pad_l1,         ///< Nút vai trái.
  pad_l2,         ///< Cò trái, đọc như nút.
  pad_r1,         ///< Nút vai phải.
  pad_r2,         ///< Cò phải, đọc như nút.
  pad_select,     ///< Nút Select / Back / Share.
  pad_guide,      ///< Nút Guide / Home.
  pad_start,      ///< Nút Start / Menu.
  pad_left_thumb, ///< Nhấn cần trái.
  pad_right_thumb, ///< Nhấn cần phải.
  gamepad_button_count ///< Số lượng nút tay cầm. Không phải một nút thật.
};

/// Trục analog của tay cầm, dùng với pad_axis() và axis_bind_pad().
enum gamepad_axis {
  pad_axis_left_x,        ///< Cần trái, trục ngang (-1 là trái, 1 là phải).
  pad_axis_left_y,        ///< Cần trái, trục dọc (-1 là lên, 1 là xuống).
  pad_axis_right_x,       ///< Cần phải, trục ngang.
  pad_axis_right_y,       ///< Cần phải, trục dọc.
  pad_axis_left_trigger,  ///< Cò trái. Nghỉ ở -1, nhấn hết đọc 1.
  pad_axis_right_trigger, ///< Cò phải. Nghỉ ở -1, nhấn hết đọc 1.
  gamepad_axis_count      ///< Số lượng trục. Không phải một trục thật.
};
/// @}
} // namespace njin
