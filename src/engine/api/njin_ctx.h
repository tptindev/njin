#pragma once
#include "_mod.h"
#include "_types.h"
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>

namespace njin {
// Opaque: created with njin_create (njin.h), only accessed through the
// functions below.
struct njin_ctx;

/// @addtogroup grp_module
/// @{

/// Trả về registry của EnTT chứa mọi entity và component của game.
///
/// EnTT là một phần của API module, nên system có thể tự định nghĩa component
/// và truy vấn registry trực tiếp.
/// @param ctx Context của engine.
/// @return Registry của game.
entt::registry &world(njin_ctx &ctx);

/// Trả về dispatcher của EnTT để gửi và nhận event giữa các system.
///
/// Event đã `enqueue` được phát ngay sau `phase_post_update`.
/// @param ctx Context của engine.
/// @return Dispatcher của game.
entt::dispatcher &events(njin_ctx &ctx);

/// Thêm một system vào lịch chạy của một phase.
///
/// Chỉ hợp lệ bên trong callback `setup` của module. Gọi ở nơi khác sẽ bị bỏ
/// qua và ghi cảnh báo. Các module chạy theo thứ tự đăng ký.
/// @param ctx Context của engine.
/// @param phase Phase mà system chạy trong đó.
/// @param fnc Hàm system.
void ecs_register(njin_ctx &ctx, sys_phase phase, sys_fnc fnc);

/// Thêm một system kèm ràng buộc thứ tự vào lịch chạy của một phase.
///
/// Trong cùng một module và phase, các system được sắp theo sys_desc (xem
/// _mod.h): ràng buộc `after`/`before` được đảm bảo trước, sau đó system có
/// `order` nhỏ hơn chạy trước, còn bằng nhau thì theo thứ tự đăng ký.
/// @param ctx Context của engine.
/// @param phase Phase mà system chạy trong đó.
/// @param desc Mô tả system và thứ tự của nó.
void ecs_register(njin_ctx &ctx, sys_phase phase, const sys_desc &desc);

/// Chạy `setup` của module và đưa các system của nó vào lịch chạy.
///
/// Phải gọi trước njin_run(). Tên module chỉ được đăng ký một lần.
/// @param ctx Context của engine.
/// @param desc Mô tả module.
void njin_mod_register(njin_ctx &ctx, const mod_desc &desc);
/// @}

/// @addtogroup grp_time
/// @{

/// Thời gian của frame trước, tính bằng giây.
///
/// Nhân vận tốc với giá trị này để chuyển động không phụ thuộc FPS.
/// @param ctx Context của engine.
/// @return Thời gian của frame trước, tính bằng giây.
f32 delta(const njin_ctx &ctx);

/// Thời gian đã trôi qua kể từ lúc mở cửa sổ, tính bằng giây.
/// @param ctx Context của engine.
/// @return Thời gian đã chạy, tính bằng giây.
f32 elapsed(const njin_ctx &ctx);
/// @}

/// @addtogroup grp_camera
/// @{

/// Trả về góc nhìn dùng cho frame này.
///
/// Đó là entity có camera_on, camera_2d và transform (xem _comps.h), hoặc góc
/// nhìn đồng nhất (thế giới trùng với pixel màn hình) nếu không có entity nào.
/// Được đọc trực tiếp từ registry nên thay đổi có hiệu lực ngay.
/// @param ctx Context của engine.
/// @return Góc nhìn đang dùng.
camera_view camera_active(const njin_ctx &ctx);

/// Đổi một điểm từ thế giới sang pixel màn hình, qua camera_active().
/// @param ctx Context của engine.
/// @param pos Điểm trong thế giới.
/// @return Vị trí tương ứng trên màn hình.
vec2 w2scr(const njin_ctx &ctx, vec2 pos);

/// Đổi một điểm từ pixel màn hình sang thế giới, qua camera_active().
///
/// Thường dùng để đổi vị trí chuột, từ mouse_pos(), thành vị trí trong thế giới.
/// @param ctx Context của engine.
/// @param pos Điểm trên màn hình (pixel).
/// @return Vị trí tương ứng trong thế giới.
vec2 scr2w(const njin_ctx &ctx, vec2 pos);
/// @}

/// @addtogroup grp_input
/// @{

/// Đúng ở frame mà phím vừa được nhấn xuống (chỉ một frame).
/// @param ctx Context của engine.
/// @param key Phím cần kiểm tra.
/// @return `true` ở frame phím vừa được nhấn.
bool key_pressed(const njin_ctx &ctx, key_code key);

/// Đúng khi phím đã được giữ từ frame trước và vẫn đang giữ.
///
/// Lưu ý: ở frame nhấn đầu tiên hàm này trả về false (lúc đó key_pressed() trả
/// về true). Muốn biết phím có đang được ấn hay không thì dùng
/// `key_pressed(...) || key_held(...)`.
/// @param ctx Context của engine.
/// @param key Phím cần kiểm tra.
/// @return `true` nếu phím đã được giữ từ frame trước và vẫn đang giữ.
bool key_held(const njin_ctx &ctx, key_code key);

/// Đúng ở frame mà phím vừa được thả ra (chỉ một frame).
/// @param ctx Context của engine.
/// @param key Phím cần kiểm tra.
/// @return `true` ở frame phím vừa được thả.
bool key_released(const njin_ctx &ctx, key_code key);

/// Đăng ký một action có tên. Nếu tên đã tồn tại thì trả về handle cũ.
///
/// Action là một tên logic (ví dụ "jump") có thể gắn với một hoặc nhiều phím.
/// @param ctx Context của engine.
/// @param name Tên action.
/// @return Handle của action, hoặc handle không hợp lệ (id 0) nếu `name` là null.
action_handle action_register(njin_ctx &ctx, const char *name);

/// Tìm action theo tên.
/// @param ctx Context của engine.
/// @param name Tên action.
/// @return Handle của action, hoặc handle không hợp lệ (id 0) nếu không có.
action_handle action_find(const njin_ctx &ctx, const char *name);

/// Gắn một phím vào action. Có thể gắn nhiều phím vào cùng một action.
/// @param ctx Context của engine.
/// @param handle Action cần gắn.
/// @param key Phím cần gắn. Phím không hợp lệ bị bỏ qua.
void action_bind_key(njin_ctx &ctx, action_handle handle, key_code key);

/// Gắn một nút chuột vào action. Có thể gắn lẫn phím, nút chuột và nút tay cầm
/// vào cùng một action: nguồn nào thỏa cũng làm action thỏa.
/// @param ctx Context của engine.
/// @param handle Action cần gắn.
/// @param button Nút chuột cần gắn. Nút không hợp lệ bị bỏ qua.
void action_bind_mouse(njin_ctx &ctx, action_handle handle, mouse_button button);

/// Gắn một nút tay cầm vào action. Bất kỳ tay cầm nào đang cắm đều thỏa.
/// @param ctx Context của engine.
/// @param handle Action cần gắn.
/// @param button Nút tay cầm cần gắn. Nút không hợp lệ bị bỏ qua.
void action_bind_pad(njin_ctx &ctx, action_handle handle, gamepad_button button);

/// Xóa mọi phím, nút chuột và nút tay cầm đã gắn vào action.
///
/// Đổi phím trong lúc chạy là gọi hàm này rồi bind lại. Dùng được ở mọi lúc,
/// không chỉ trong `setup`.
/// @param ctx Context của engine.
/// @param handle Action cần xóa.
void action_clear_binds(njin_ctx &ctx, action_handle handle);

/// Đúng ở frame mà một trong các nguồn của action (phím, nút chuột, nút tay cầm)
/// vừa được nhấn.
/// @param ctx Context của engine.
/// @param handle Action cần kiểm tra.
/// @return `true` ở frame một nguồn của action vừa được nhấn. `false` nếu handle không hợp lệ.
bool action_pressed(const njin_ctx &ctx, action_handle handle);

/// Đúng khi một trong các nguồn của action đã được giữ từ frame trước và vẫn
/// đang giữ. Cùng quy tắc với key_held().
/// @param ctx Context của engine.
/// @param handle Action cần kiểm tra.
/// @return `true` nếu một nguồn của action đang được giữ. `false` nếu handle không hợp lệ.
bool action_held(const njin_ctx &ctx, action_handle handle);

/// Đúng ở frame mà một trong các nguồn của action vừa được thả.
/// @param ctx Context của engine.
/// @param handle Action cần kiểm tra.
/// @return `true` ở frame một nguồn của action vừa được thả. `false` nếu handle không hợp lệ.
bool action_released(const njin_ctx &ctx, action_handle handle);

/// Vị trí con trỏ chuột, tính bằng pixel màn hình.
///
/// Không qua camera. Dùng scr2w() để đổi sang vị trí trong thế giới.
/// @param ctx Context của engine.
/// @return Vị trí con trỏ chuột.
vec2 mouse_pos(const njin_ctx &ctx);

/// Độ dời của chuột so với frame trước, tính bằng pixel.
/// @param ctx Context của engine.
/// @return Độ dời của chuột.
vec2 mouse_delta(const njin_ctx &ctx);

/// Số nấc cuộn bánh xe chuột trong frame này. Dương là cuộn ra xa người dùng.
/// Đa số frame trả về 0.
/// @param ctx Context của engine.
/// @return Số nấc cuộn.
f32 mouse_wheel(const njin_ctx &ctx);

/// Đúng ở frame mà nút chuột vừa được nhấn xuống (chỉ một frame).
/// @param ctx Context của engine.
/// @param button Nút cần kiểm tra.
/// @return `true` ở frame nút vừa được nhấn.
bool mouse_pressed(const njin_ctx &ctx, mouse_button button);

/// Đúng khi nút chuột đã được giữ từ frame trước và vẫn đang giữ. Cùng quy tắc
/// với key_held(): ở frame nhấn đầu tiên trả về false.
/// @param ctx Context của engine.
/// @param button Nút cần kiểm tra.
/// @return `true` nếu nút đã được giữ từ frame trước và vẫn đang giữ.
bool mouse_held(const njin_ctx &ctx, mouse_button button);

/// Đúng ở frame mà nút chuột vừa được thả (chỉ một frame).
/// @param ctx Context của engine.
/// @param button Nút cần kiểm tra.
/// @return `true` ở frame nút vừa được thả.
bool mouse_released(const njin_ctx &ctx, mouse_button button);

/// Tay cầm số `pad` có đang được cắm không.
///
/// Chưa cắm thì mọi hàm pad_* đọc như không nhấn, không báo lỗi.
/// @param ctx Context của engine.
/// @param pad Chỉ số tay cầm, từ 0 đến gamepad_max - 1.
/// @return `true` nếu tay cầm đang cắm.
bool pad_available(const njin_ctx &ctx, i32 pad);

/// Đúng ở frame mà nút tay cầm vừa được nhấn xuống (chỉ một frame).
/// @param ctx Context của engine.
/// @param pad Chỉ số tay cầm, từ 0 đến gamepad_max - 1.
/// @param button Nút cần kiểm tra.
/// @return `true` ở frame nút vừa được nhấn. `false` nếu tay cầm chưa cắm.
bool pad_pressed(const njin_ctx &ctx, i32 pad, gamepad_button button);

/// Đúng khi nút tay cầm đã được giữ từ frame trước và vẫn đang giữ. Cùng quy
/// tắc với key_held().
/// @param ctx Context của engine.
/// @param pad Chỉ số tay cầm, từ 0 đến gamepad_max - 1.
/// @param button Nút cần kiểm tra.
/// @return `true` nếu nút đã được giữ từ frame trước và vẫn đang giữ.
bool pad_held(const njin_ctx &ctx, i32 pad, gamepad_button button);

/// Đúng ở frame mà nút tay cầm vừa được thả (chỉ một frame).
/// @param ctx Context của engine.
/// @param pad Chỉ số tay cầm, từ 0 đến gamepad_max - 1.
/// @param button Nút cần kiểm tra.
/// @return `true` ở frame nút vừa được thả.
bool pad_released(const njin_ctx &ctx, i32 pad, gamepad_button button);

/// Giá trị một trục analog của tay cầm.
///
/// Đã qua vùng chết và được co giãn lại, nên rời khỏi 0 một cách mượt thay vì
/// nhảy bậc. Cò nghỉ ở -1 và đọc 1 khi nhấn hết (theo quy ước của raylib, không
/// đổi lại).
/// @param ctx Context của engine.
/// @param pad Chỉ số tay cầm, từ 0 đến gamepad_max - 1.
/// @param axis Trục cần đọc.
/// @return Giá trị từ -1 đến 1, hoặc 0 nếu tay cầm chưa cắm.
f32 pad_axis(const njin_ctx &ctx, i32 pad, gamepad_axis axis);

/// Đặt vùng chết cho mọi cần trên mọi tay cầm. Mặc định 0.15.
/// @param ctx Context của engine.
/// @param deadzone Vùng chết, được giới hạn trong khoảng 0 đến 0.95.
void pad_set_deadzone(njin_ctx &ctx, f32 deadzone);

/// Số ký tự đã gõ trong frame này.
///
/// Ký tự đã qua bố cục bàn phím của hệ điều hành và phím chết. Đây là thứ một ô
/// nhập văn bản cần; còn key_pressed() là thứ một phím tắt cần, hai câu hỏi
/// khác nhau.
/// @param ctx Context của engine.
/// @return Số ký tự, tối đa 32 mỗi frame.
i32 text_count(const njin_ctx &ctx);

/// Mã Unicode của ký tự thứ `index` đã gõ trong frame này, theo thứ tự gõ.
/// @param ctx Context của engine.
/// @param index Chỉ số, từ 0 đến text_count() - 1.
/// @return Mã Unicode, hoặc 0 nếu `index` ngoài phạm vi.
i32 text_char(const njin_ctx &ctx, i32 index);

/// Xóa trạng thái của một phím trong phần còn lại của frame.
///
/// System chạy sau đó không thấy phím này được nhấn hay giữ. Đây là cách một
/// menu nuốt phím trước khi thế giới bên dưới xử lý cùng phím đó. Trạng thái
/// được đọc lại ở frame sau, nên hiệu lực không kéo dài quá frame gọi nó.
/// @param ctx Context của engine.
/// @param key Phím cần xóa.
void key_consume(njin_ctx &ctx, key_code key);

/// Xóa trạng thái của một nút chuột trong phần còn lại của frame.
/// Xem key_consume().
/// @param ctx Context của engine.
/// @param button Nút cần xóa.
void mouse_consume(njin_ctx &ctx, mouse_button button);

/// Xóa cuộn bánh xe trong phần còn lại của frame. Xem key_consume().
/// @param ctx Context của engine.
void mouse_wheel_consume(njin_ctx &ctx);

/// Đăng ký một axis có tên. Nếu tên đã tồn tại thì trả về handle cũ.
///
/// Axis đọc giá trị từ -1 đến 1, khác action là đúng hoặc sai. Muốn một vector
/// hai chiều thì đọc hai axis rồi ghép lại: chỉ người gọi biết có cần chuẩn hóa
/// vector đó hay không.
/// @param ctx Context của engine.
/// @param name Tên axis.
/// @return Handle của axis, hoặc handle không hợp lệ (id 0) nếu `name` là null.
axis_handle axis_register(njin_ctx &ctx, const char *name);

/// Tìm axis theo tên.
/// @param ctx Context của engine.
/// @param name Tên axis.
/// @return Handle của axis, hoặc handle không hợp lệ (id 0) nếu không có.
axis_handle axis_find(const njin_ctx &ctx, const char *name);

/// Gắn một cặp phím vào axis, đóng vai một cần analog.
///
/// Chỉ phím âm thì đọc -1, chỉ phím dương thì đọc 1, cả hai hoặc không phím nào
/// thì đọc 0.
/// @param ctx Context của engine.
/// @param handle Axis cần gắn.
/// @param negative Phím cho chiều âm.
/// @param positive Phím cho chiều dương.
void axis_bind_keys(njin_ctx &ctx, axis_handle handle, key_code negative,
                    key_code positive);

/// Gắn một trục tay cầm vào axis. Bất kỳ tay cầm nào đang cắm đều được đọc.
/// @param ctx Context của engine.
/// @param handle Axis cần gắn.
/// @param axis Trục tay cầm cần gắn. Trục không hợp lệ bị bỏ qua.
void axis_bind_pad(njin_ctx &ctx, axis_handle handle, gamepad_axis axis);

/// Xóa mọi cặp phím và trục tay cầm đã gắn vào axis.
/// @param ctx Context của engine.
/// @param handle Axis cần xóa.
void axis_clear_binds(njin_ctx &ctx, axis_handle handle);

/// Giá trị hiện tại của axis.
///
/// Nguồn nào lệch xa vị trí nghỉ nhất thì thắng, nên cần đẩy nửa chừng không bị
/// cặp phím đang đứng yên ở 0 làm phẳng, và ngược lại.
/// @param ctx Context của engine.
/// @param handle Axis cần đọc.
/// @return Giá trị từ -1 đến 1, hoặc 0 nếu handle không hợp lệ.
f32 axis_value(const njin_ctx &ctx, axis_handle handle);
/// @}

/// @addtogroup grp_shader
/// @{

/// Nạp và biên dịch một shader.
///
/// Một trong hai đường dẫn có thể là nullptr để giữ stage mặc định của raylib.
/// Handle không hợp lệ hoặc đã unload bị mọi hàm shader bỏ qua.
/// @param ctx Context của engine.
/// @param vspath Đường dẫn vertex shader, hoặc nullptr.
/// @param fspath Đường dẫn fragment shader, hoặc nullptr.
/// @return Handle của shader, hoặc handle có id 0 nếu file thiếu hoặc biên dịch lỗi.
shader_handle shader_load(njin_ctx &ctx, const char *vspath,
                          const char *fspath);

/// Giải phóng shader. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Shader cần giải phóng.
void shader_unload(njin_ctx &ctx, shader_handle handle);

/// Bật shader cho mọi thứ vẽ sau đó, cho đến shader_end().
///
/// Phải gọi giữa lúc bắt đầu và kết thúc vẽ của frame. Đặt uniform trước khi
/// gọi hàm này.
/// @param ctx Context của engine.
/// @param handle Shader cần bật.
void shader_begin(const njin_ctx &ctx, shader_handle handle);

/// Tắt shader đã bật bằng shader_begin().
/// @param ctx Context của engine.
void shader_end(const njin_ctx &ctx);

/// Đặt uniform kiểu `int`. Uniform không tồn tại chỉ được ghi cảnh báo một lần
/// rồi bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Shader cần đặt.
/// @param name Tên uniform trong shader.
/// @param value Giá trị.
void shader_set_i32(njin_ctx &ctx, shader_handle handle, const char *name,
                    i32 value);

/// Đặt uniform kiểu `float`. Xem shader_set_i32() về uniform không tồn tại.
/// @param ctx Context của engine.
/// @param handle Shader cần đặt.
/// @param name Tên uniform trong shader.
/// @param value Giá trị.
void shader_set_f32(njin_ctx &ctx, shader_handle handle, const char *name,
                    f32 value);

/// Đặt uniform kiểu `vec2`. Xem shader_set_i32() về uniform không tồn tại.
/// @param ctx Context của engine.
/// @param handle Shader cần đặt.
/// @param name Tên uniform trong shader.
/// @param value Giá trị.
void shader_set_vec2(njin_ctx &ctx, shader_handle handle, const char *name,
                     vec2 value);

/// Đặt uniform kiểu `vec4`. Xem shader_set_i32() về uniform không tồn tại.
/// @param ctx Context của engine.
/// @param handle Shader cần đặt.
/// @param name Tên uniform trong shader.
/// @param value Giá trị.
void shader_set_vec4(njin_ctx &ctx, shader_handle handle, const char *name,
                     vec4 value);
/// @}

/// @addtogroup grp_texture
/// @{

/// Nạp một texture từ file ảnh.
///
/// Handle không hợp lệ hoặc đã unload bị mọi hàm texture bỏ qua.
/// @param ctx Context của engine.
/// @param path Đường dẫn file ảnh.
/// @return Handle của texture, hoặc handle có id 0 nếu file thiếu hoặc không giải mã được.
texture_handle texture_load(njin_ctx &ctx, const char *path);

/// Giải phóng texture. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Texture cần giải phóng.
void texture_unload(njin_ctx &ctx, texture_handle handle);

/// Kích thước texture (pixel).
/// @param ctx Context của engine.
/// @param handle Texture cần hỏi.
/// @return Kích thước, hoặc `{0, 0}` nếu handle không hợp lệ.
vec2 texture_size(const njin_ctx &ctx, texture_handle handle);

/// Vẽ texture với góc trên trái tại `pos`.
///
/// `tint` được nhân vào từng pixel, màu trắng `{1, 1, 1, 1}` giữ nguyên ảnh.
/// Phải gọi giữa lúc bắt đầu và kết thúc vẽ của frame.
/// @param ctx Context của engine.
/// @param handle Texture cần vẽ.
/// @param pos Vị trí góc trên trái.
/// @param tint Màu nhân vào ảnh.
void texture_draw(const njin_ctx &ctx, texture_handle handle, vec2 pos,
                  rgba tint);
/// @}

/// @addtogroup grp_render_texture
/// @{

/// Tạo một render texture: ảnh ngoài màn hình để vẽ vào, rồi vẽ lại như một
/// texture.
///
/// Dùng cho post-processing hoặc vẽ trước một cảnh phức tạp.
/// @param ctx Context của engine.
/// @param width Chiều rộng (pixel).
/// @param height Chiều cao (pixel).
/// @return Handle, hoặc handle có id 0 nếu kích thước bằng 0 hoặc tạo thất bại.
render_texture_handle render_texture_load(njin_ctx &ctx, u32 width,
                                          u32 height);

/// Giải phóng render texture. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Render texture cần giải phóng.
void render_texture_unload(njin_ctx &ctx, render_texture_handle handle);

/// Kích thước render texture (pixel).
/// @param ctx Context của engine.
/// @param handle Render texture cần hỏi.
/// @return Kích thước, hoặc `{0, 0}` nếu handle không hợp lệ.
vec2 render_texture_size(const njin_ctx &ctx, render_texture_handle handle);

/// Bắt đầu vẽ vào render texture, giữ nguyên nội dung cũ.
///
/// Mọi lệnh vẽ giữa render_texture_begin() và render_texture_end() đi vào
/// render texture, trong hệ pixel của chính nó (không qua camera).
///
/// Lệnh này đặt lại phép biến đổi của camera, nên không dùng trong
/// `phase_pre_render` hay `phase_render`. Hãy vẽ vào render texture ở
/// `phase_post_update` hoặc `phase_post_render`.
/// @param ctx Context của engine.
/// @param handle Render texture cần vẽ vào. Handle không hợp lệ bị bỏ qua.
void render_texture_begin(const njin_ctx &ctx, render_texture_handle handle);

/// Giống bản trên nhưng xóa render texture bằng màu `clear` trước.
/// @param ctx Context của engine.
/// @param handle Render texture cần vẽ vào. Handle không hợp lệ bị bỏ qua.
/// @param clear Màu dùng để xóa.
void render_texture_begin(const njin_ctx &ctx, render_texture_handle handle,
                          rgba clear);

/// Kết thúc vẽ vào render texture.
/// @param ctx Context của engine.
void render_texture_end(const njin_ctx &ctx);

/// Vẽ nội dung render texture theo đúng chiều, góc trên trái tại `pos`.
/// @param ctx Context của engine.
/// @param handle Render texture cần vẽ.
/// @param pos Vị trí góc trên trái.
/// @param tint Màu nhân vào ảnh. Màu trắng `{1, 1, 1, 1}` giữ nguyên ảnh.
void render_texture_draw(const njin_ctx &ctx, render_texture_handle handle,
                         vec2 pos, rgba tint);
/// @}
} // namespace njin
