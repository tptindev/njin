#pragma once
#include "_types.h"

namespace njin {
// Opaque, see njin_ctx.h.
struct context;

/// @addtogroup grp_input
/// @{

/// Đúng ở frame mà phím vừa được nhấn xuống (chỉ một frame).
/// @param ctx Context của engine.
/// @param key Phím cần kiểm tra.
/// @return `true` ở frame phím vừa được nhấn.
bool key_pressed(const context &ctx, key_code key);

/// Đúng khi phím đã được giữ từ frame trước và vẫn đang giữ.
///
/// Lưu ý: ở frame nhấn đầu tiên hàm này trả về false (lúc đó key_pressed() trả
/// về true). Muốn biết phím có đang được ấn hay không thì dùng
/// `key_pressed(...) || key_held(...)`.
/// @param ctx Context của engine.
/// @param key Phím cần kiểm tra.
/// @return `true` nếu phím đã được giữ từ frame trước và vẫn đang giữ.
bool key_held(const context &ctx, key_code key);

/// Đúng ở frame mà phím vừa được thả ra (chỉ một frame).
/// @param ctx Context của engine.
/// @param key Phím cần kiểm tra.
/// @return `true` ở frame phím vừa được thả.
bool key_released(const context &ctx, key_code key);

/// Đăng ký một action có tên. Nếu tên đã tồn tại thì trả về handle cũ.
///
/// Action là một tên logic (ví dụ "jump") có thể gắn với một hoặc nhiều phím.
/// @param ctx Context của engine.
/// @param name Tên action.
/// @return Handle của action, hoặc handle không hợp lệ (id 0) nếu `name` là null.
action_handle action_register(context &ctx, const char *name);

/// Tìm action theo tên.
/// @param ctx Context của engine.
/// @param name Tên action.
/// @return Handle của action, hoặc handle không hợp lệ (id 0) nếu không có.
action_handle action_find(const context &ctx, const char *name);

/// Gắn một phím vào action. Có thể gắn nhiều phím vào cùng một action.
/// @param ctx Context của engine.
/// @param handle Action cần gắn.
/// @param key Phím cần gắn. Phím không hợp lệ bị bỏ qua.
void action_bind_key(context &ctx, action_handle handle, key_code key);

/// Gắn một nút chuột vào action. Có thể gắn lẫn phím, nút chuột và nút tay cầm
/// vào cùng một action: nguồn nào thỏa cũng làm action thỏa.
/// @param ctx Context của engine.
/// @param handle Action cần gắn.
/// @param button Nút chuột cần gắn. Nút không hợp lệ bị bỏ qua.
void action_bind_mouse(context &ctx, action_handle handle, mouse_button button);

/// Gắn một nút tay cầm vào action. Bất kỳ tay cầm nào đang cắm đều thỏa.
/// @param ctx Context của engine.
/// @param handle Action cần gắn.
/// @param button Nút tay cầm cần gắn. Nút không hợp lệ bị bỏ qua.
void action_bind_pad(context &ctx, action_handle handle, gamepad_button button);

/// Xóa mọi phím, nút chuột và nút tay cầm đã gắn vào action.
///
/// Đổi phím trong lúc chạy là gọi hàm này rồi bind lại. Dùng được ở mọi lúc,
/// không chỉ trong `setup`.
/// @param ctx Context của engine.
/// @param handle Action cần xóa.
void action_clear_binds(context &ctx, action_handle handle);

/// Đúng ở frame mà một trong các nguồn của action (phím, nút chuột, nút tay cầm)
/// vừa được nhấn.
/// @param ctx Context của engine.
/// @param handle Action cần kiểm tra.
/// @return `true` ở frame một nguồn của action vừa được nhấn. `false` nếu handle không hợp lệ.
bool action_pressed(const context &ctx, action_handle handle);

/// Đúng khi một trong các nguồn của action đã được giữ từ frame trước và vẫn
/// đang giữ. Cùng quy tắc với key_held().
/// @param ctx Context của engine.
/// @param handle Action cần kiểm tra.
/// @return `true` nếu một nguồn của action đang được giữ. `false` nếu handle không hợp lệ.
bool action_held(const context &ctx, action_handle handle);

/// Đúng ở frame mà một trong các nguồn của action vừa được thả.
/// @param ctx Context của engine.
/// @param handle Action cần kiểm tra.
/// @return `true` ở frame một nguồn của action vừa được thả. `false` nếu handle không hợp lệ.
bool action_released(const context &ctx, action_handle handle);

/// Vị trí con trỏ chuột, tính bằng pixel màn hình.
///
/// Không qua camera. Dùng scr2w() để đổi sang vị trí trong thế giới.
/// @param ctx Context của engine.
/// @return Vị trí con trỏ chuột.
vec2 mouse_pos(const context &ctx);

/// Độ dời của chuột so với frame trước, tính bằng pixel.
/// @param ctx Context của engine.
/// @return Độ dời của chuột.
vec2 mouse_delta(const context &ctx);

/// Số nấc cuộn bánh xe chuột trong frame này. Dương là cuộn ra xa người dùng.
/// Đa số frame trả về 0.
/// @param ctx Context của engine.
/// @return Số nấc cuộn.
f32 mouse_wheel(const context &ctx);

/// Đúng ở frame mà nút chuột vừa được nhấn xuống (chỉ một frame).
/// @param ctx Context của engine.
/// @param button Nút cần kiểm tra.
/// @return `true` ở frame nút vừa được nhấn.
bool mouse_pressed(const context &ctx, mouse_button button);

/// Đúng khi nút chuột đã được giữ từ frame trước và vẫn đang giữ. Cùng quy tắc
/// với key_held(): ở frame nhấn đầu tiên trả về false.
/// @param ctx Context của engine.
/// @param button Nút cần kiểm tra.
/// @return `true` nếu nút đã được giữ từ frame trước và vẫn đang giữ.
bool mouse_held(const context &ctx, mouse_button button);

/// Đúng ở frame mà nút chuột vừa được thả (chỉ một frame).
/// @param ctx Context của engine.
/// @param button Nút cần kiểm tra.
/// @return `true` ở frame nút vừa được thả.
bool mouse_released(const context &ctx, mouse_button button);

/// Tay cầm số `pad` có đang được cắm không.
///
/// Chưa cắm thì mọi hàm pad_* đọc như không nhấn, không báo lỗi.
/// @param ctx Context của engine.
/// @param pad Chỉ số tay cầm, từ 0 đến gamepad_max - 1.
/// @return `true` nếu tay cầm đang cắm.
bool pad_available(const context &ctx, i32 pad);

/// Đúng ở frame mà nút tay cầm vừa được nhấn xuống (chỉ một frame).
/// @param ctx Context của engine.
/// @param pad Chỉ số tay cầm, từ 0 đến gamepad_max - 1.
/// @param button Nút cần kiểm tra.
/// @return `true` ở frame nút vừa được nhấn. `false` nếu tay cầm chưa cắm.
bool pad_pressed(const context &ctx, i32 pad, gamepad_button button);

/// Đúng khi nút tay cầm đã được giữ từ frame trước và vẫn đang giữ. Cùng quy
/// tắc với key_held().
/// @param ctx Context của engine.
/// @param pad Chỉ số tay cầm, từ 0 đến gamepad_max - 1.
/// @param button Nút cần kiểm tra.
/// @return `true` nếu nút đã được giữ từ frame trước và vẫn đang giữ.
bool pad_held(const context &ctx, i32 pad, gamepad_button button);

/// Đúng ở frame mà nút tay cầm vừa được thả (chỉ một frame).
/// @param ctx Context của engine.
/// @param pad Chỉ số tay cầm, từ 0 đến gamepad_max - 1.
/// @param button Nút cần kiểm tra.
/// @return `true` ở frame nút vừa được thả.
bool pad_released(const context &ctx, i32 pad, gamepad_button button);

/// Giá trị một trục analog của tay cầm.
///
/// Đã qua vùng chết và được co giãn lại, nên rời khỏi 0 một cách mượt thay vì
/// nhảy bậc. Cò nghỉ ở -1 và đọc 1 khi nhấn hết (theo quy ước của raylib, không
/// đổi lại).
/// @param ctx Context của engine.
/// @param pad Chỉ số tay cầm, từ 0 đến gamepad_max - 1.
/// @param axis Trục cần đọc.
/// @return Giá trị từ -1 đến 1, hoặc 0 nếu tay cầm chưa cắm.
f32 pad_axis(const context &ctx, i32 pad, gamepad_axis axis);

/// Đặt vùng chết cho mọi cần trên mọi tay cầm. Mặc định 0.15.
/// @param ctx Context của engine.
/// @param deadzone Vùng chết, được giới hạn trong khoảng 0 đến 0.95.
void pad_set_deadzone(context &ctx, f32 deadzone);

/// Số ký tự đã gõ trong frame này.
///
/// Ký tự đã qua bố cục bàn phím của hệ điều hành và phím chết. Đây là thứ một ô
/// nhập văn bản cần; còn key_pressed() là thứ một phím tắt cần, hai câu hỏi
/// khác nhau.
/// @param ctx Context của engine.
/// @return Số ký tự, tối đa 32 mỗi frame.
i32 text_count(const context &ctx);

/// Mã Unicode của ký tự thứ `index` đã gõ trong frame này, theo thứ tự gõ.
/// @param ctx Context của engine.
/// @param index Chỉ số, từ 0 đến text_count() - 1.
/// @return Mã Unicode, hoặc 0 nếu `index` ngoài phạm vi.
i32 text_char(const context &ctx, i32 index);

/// Xóa trạng thái của một phím trong phần còn lại của frame.
///
/// System chạy sau đó không thấy phím này được nhấn hay giữ. Đây là cách một
/// menu nuốt phím trước khi thế giới bên dưới xử lý cùng phím đó. Trạng thái
/// được đọc lại ở frame sau, nên hiệu lực không kéo dài quá frame gọi nó.
/// @param ctx Context của engine.
/// @param key Phím cần xóa.
void key_consume(context &ctx, key_code key);

/// Xóa trạng thái của một nút chuột trong phần còn lại của frame.
/// Xem key_consume().
/// @param ctx Context của engine.
/// @param button Nút cần xóa.
void mouse_consume(context &ctx, mouse_button button);

/// Xóa cuộn bánh xe trong phần còn lại của frame. Xem key_consume().
/// @param ctx Context của engine.
void mouse_wheel_consume(context &ctx);

/// Đăng ký một axis có tên. Nếu tên đã tồn tại thì trả về handle cũ.
///
/// Axis đọc giá trị từ -1 đến 1, khác action là đúng hoặc sai. Muốn một vector
/// hai chiều thì đọc hai axis rồi ghép lại: chỉ người gọi biết có cần chuẩn hóa
/// vector đó hay không.
/// @param ctx Context của engine.
/// @param name Tên axis.
/// @return Handle của axis, hoặc handle không hợp lệ (id 0) nếu `name` là null.
axis_handle axis_register(context &ctx, const char *name);

/// Tìm axis theo tên.
/// @param ctx Context của engine.
/// @param name Tên axis.
/// @return Handle của axis, hoặc handle không hợp lệ (id 0) nếu không có.
axis_handle axis_find(const context &ctx, const char *name);

/// Gắn một cặp phím vào axis, đóng vai một cần analog.
///
/// Chỉ phím âm thì đọc -1, chỉ phím dương thì đọc 1, cả hai hoặc không phím nào
/// thì đọc 0.
/// @param ctx Context của engine.
/// @param handle Axis cần gắn.
/// @param negative Phím cho chiều âm.
/// @param positive Phím cho chiều dương.
void axis_bind_keys(context &ctx, axis_handle handle, key_code negative,
                    key_code positive);

/// Gắn một trục tay cầm vào axis. Bất kỳ tay cầm nào đang cắm đều được đọc.
/// @param ctx Context của engine.
/// @param handle Axis cần gắn.
/// @param axis Trục tay cầm cần gắn. Trục không hợp lệ bị bỏ qua.
void axis_bind_pad(context &ctx, axis_handle handle, gamepad_axis axis);

/// Xóa mọi cặp phím và trục tay cầm đã gắn vào axis.
/// @param ctx Context của engine.
/// @param handle Axis cần xóa.
void axis_clear_binds(context &ctx, axis_handle handle);

/// Giá trị hiện tại của axis.
///
/// Nguồn nào lệch xa vị trí nghỉ nhất thì thắng, nên cần đẩy nửa chừng không bị
/// cặp phím đang đứng yên ở 0 làm phẳng, và ngược lại.
/// @param ctx Context của engine.
/// @param handle Axis cần đọc.
/// @return Giá trị từ -1 đến 1, hoặc 0 nếu handle không hợp lệ.
f32 axis_value(const context &ctx, axis_handle handle);
/// @}
} // namespace njin
