#pragma once
#include "_mod.h"
#include "_random.h"
#include "_types.h"
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>
#include <initializer_list>
#include <span>

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
/// @param name Tên system, hiện trong njin_inspector. Có thể null.
void ecs_register(njin_ctx &ctx, sys_phase phase, sys_fnc fnc, const char *name = nullptr);

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

/// Đăng ký nhiều module một lần, theo đúng thứ tự trong danh sách.
///
/// Giống hệt gọi njin_mod_register() cho từng module lần lượt: module đứng
/// trước chạy trước trong cùng phase, và một module lỗi (trùng tên, đăng ký
/// sau njin_run()) chỉ bị bỏ qua riêng nó.
/// @code
/// njin::njin_mod_register(*ctx, {input_module(), physics_module(), ui_module()});
/// @endcode
/// @param ctx Context của engine.
/// @param mods Các module, theo thứ tự đăng ký.
void njin_mod_register(njin_ctx &ctx, std::initializer_list<mod_desc> mods);

/// Đăng ký nhiều module từ một danh sách tạo lúc chạy, ví dụ một
/// `std::vector<mod_desc>` hay `std::array`. Cùng quy tắc với bản nhận danh
/// sách trực tiếp.
/// @param ctx Context của engine.
/// @param mods Các module, theo thứ tự đăng ký.
void njin_mod_register(njin_ctx &ctx, std::span<const mod_desc> mods);
/// @}

/// @addtogroup grp_time
/// @{

/// Thời gian của frame trước, tính bằng giây, đã nhân với tốc độ thời gian.
///
/// Nhân vận tốc với giá trị này để chuyển động không phụ thuộc FPS. Bằng 0 khi
/// đang tạm dừng (time_set_paused()). Trong `phase_fixed_update` hàm này trả
/// về đúng một nhịp cố định, xem fixed_delta().
/// @param ctx Context của engine.
/// @return Thời gian của frame, tính bằng giây.
f32 delta(const njin_ctx &ctx);

/// Thời gian thật của frame trước, không bị tốc độ thời gian hay tạm dừng ảnh
/// hưởng. Dùng cho thứ vẫn phải chạy khi game dừng, như menu tạm dừng.
/// @param ctx Context của engine.
/// @return Thời gian thật của frame, tính bằng giây.
f32 delta_real(const njin_ctx &ctx);

/// Đặt tốc độ thời gian của game. 1 là bình thường, 0.5 là chậm một nửa.
///
/// Ảnh hưởng delta(), `phase_fixed_update` và animation của sprite. Giá trị âm
/// được coi là 0.
/// @param ctx Context của engine.
/// @param scale Tốc độ thời gian.
void time_set_scale(njin_ctx &ctx, f32 scale);

/// Tốc độ thời gian hiện tại.
/// @param ctx Context của engine.
/// @return Tốc độ thời gian, mặc định 1.
f32 time_scale(const njin_ctx &ctx);

/// Tạm dừng hoặc chạy tiếp thời gian của game.
///
/// Khi dừng, delta() trả về 0 và `phase_fixed_update` không chạy, nhưng các
/// phase khác vẫn chạy mỗi frame: input vẫn đọc được, menu vẫn vẽ được. Giữ
/// nguyên tốc độ thời gian đã đặt.
/// @param ctx Context của engine.
/// @param paused `true` để tạm dừng.
void time_set_paused(njin_ctx &ctx, bool paused);

/// Game có đang tạm dừng không.
/// @param ctx Context của engine.
/// @return `true` nếu đang tạm dừng.
bool time_paused(const njin_ctx &ctx);

/// Độ dài một nhịp của `phase_fixed_update`, bằng `1 / njin_cfg::fixed_hz`.
/// @param ctx Context của engine.
/// @return Độ dài một nhịp, tính bằng giây.
f32 fixed_delta(const njin_ctx &ctx);

/// Phần nhịp cố định còn dư sau `phase_fixed_update` của frame này, từ 0 đến 1.
///
/// Dùng để nội suy vị trí khi vẽ, cho chuyển động mượt dù FPS khác nhịp vật lý:
/// `draw_pos = lerp(prev_pos, pos, fixed_alpha(ctx))`.
/// @param ctx Context của engine.
/// @return Tỉ lệ từ 0 đến 1.
f32 fixed_alpha(const njin_ctx &ctx);

/// Thời gian đã trôi qua kể từ lúc mở cửa sổ, tính bằng giây.
/// @param ctx Context của engine.
/// @return Thời gian đã chạy, tính bằng giây.
f32 elapsed(const njin_ctx &ctx);
/// @}

/// @addtogroup grp_random
/// @{

/// Bộ sinh số ngẫu nhiên dùng chung của engine.
///
/// Được gieo hạt giống từ thời gian lúc mở game, nên mỗi lần chơi mỗi khác.
/// Gọi `random(ctx).reseed(n)` để có dãy số lặp lại được, ví dụ khi thử lỗi.
/// @param ctx Context của engine.
/// @return Bộ sinh số ngẫu nhiên.
rng &random(njin_ctx &ctx);
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

/// Vùng thế giới đang hiện trên màn hình.
///
/// Khi camera xoay, đây là hình chữ nhật thẳng trục bao quanh vùng nhìn thấy.
/// Dùng để bỏ qua việc vẽ những thứ nằm ngoài màn hình.
/// @param ctx Context của engine.
/// @return Hình chữ nhật trong thế giới.
rect camera_bounds(const njin_ctx &ctx);

/// Áp một shader hậu kỳ lên toàn bộ thế giới đi qua camera.
///
/// Khi bật, mọi thứ vẽ trong `phase_pre_render` và `phase_render` (kể cả sprite
/// và tilemap) được vẽ vào một ảnh ngoài màn hình, rồi vẽ ra màn hình qua
/// shader này. UI vẽ trong `phase_post_render` không bị ảnh hưởng. Đặt uniform
/// cho shader như bình thường bằng các hàm shader_set_*().
/// @param ctx Context của engine.
/// @param shader Shader hậu kỳ. Handle id 0 để tắt.
void camera_set_post_shader(njin_ctx &ctx, shader_handle shader);
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

/// @addtogroup grp_sound
/// @{

/// Kênh trộn âm thanh, như thanh trượt trong menu cài đặt của game.
///
/// Âm lượng thật của một sound là âm lượng riêng của nó nhân âm lượng kênh của
/// nó nhân `bus_master`. Music luôn ở kênh `bus_music`; sound mặc định ở
/// `bus_sfx`, đổi bằng sound_set_bus().
enum audio_bus {
  bus_master, ///< Tổng: nhân vào mọi thứ.
  bus_music,  ///< Nhạc nền (mọi music).
  bus_sfx,    ///< Hiệu ứng trong game. Mặc định của sound.
  bus_ui,     ///< Tiếng giao diện.
  bus_voice,  ///< Lồng tiếng, tiếng thoại.
  audio_bus_count ///< Số kênh. Không phải một kênh thật.
};

/// Đặt âm lượng một kênh, từ 0 trở lên (1 là nguyên). Áp dụng ngay cho cả
/// những âm đang phát. Lưu cùng cài đặt bằng settings_save().
/// @param ctx Context của engine.
/// @param bus Kênh.
/// @param volume Âm lượng. Giá trị âm được coi là 0.
void audio_set_bus_volume(njin_ctx &ctx, audio_bus bus, f32 volume);

/// Âm lượng một kênh. @param ctx Context của engine. @param bus Kênh.
/// @return Âm lượng, mặc định 1.
f32 audio_bus_volume(const njin_ctx &ctx, audio_bus bus);

/// Tắt hoặc bật tiếng cả một kênh, không đụng đến âm lượng đã đặt.
/// @param ctx Context của engine.
/// @param bus Kênh.
/// @param muted `true` để tắt tiếng.
void audio_set_bus_muted(njin_ctx &ctx, audio_bus bus, bool muted);

/// Kênh có đang tắt tiếng không. @param ctx Context của engine. @param bus Kênh.
/// @return `true` nếu đang tắt.
bool audio_bus_muted(const njin_ctx &ctx, audio_bus bus);

/// Đưa một sound vào kênh khác, ví dụ `bus_ui` cho tiếng nhấp menu.
/// @param ctx Context của engine.
/// @param handle Sound.
/// @param bus Kênh.
void sound_set_bus(njin_ctx &ctx, sound_handle handle, audio_bus bus);

/// Nạp một âm thanh ngắn vào bộ nhớ.
///
/// Dùng cho hiệu ứng như tiếng bắn, tiếng nhấp. Nhạc nền dài thì dùng music_load().
/// Handle không hợp lệ hoặc đã unload bị mọi hàm sound bỏ qua. Nếu máy không có
/// thiết bị âm thanh thì việc nạp thất bại và ghi log, còn game vẫn chạy.
/// @param ctx Context của engine.
/// @param path Đường dẫn file âm thanh (wav, ogg, mp3, flac...).
/// @return Handle của sound, hoặc handle có id 0 nếu không có thiết bị, file thiếu
/// hoặc không giải mã được.
sound_handle sound_load(njin_ctx &ctx, const char *path);

/// Tạo một âm thanh từ các mẫu đã có trong bộ nhớ.
///
/// Dữ liệu là mono, số thực 32 bit trong khoảng -1..1. Nó được chép vào bộ đệm
/// riêng của sound và không được giữ lại, nên mảng của bạn có thể bị hủy ngay sau
/// khi hàm trả về. Dùng cho âm thanh game tự sinh ra lúc khởi động.
/// @param ctx Context của engine.
/// @param samples Mảng mẫu.
/// @param count Số mẫu.
/// @param sample_rate Tần số lấy mẫu, ví dụ 44100.
/// @return Handle của sound, hoặc handle có id 0 nếu tham số sai hoặc tạo thất bại.
sound_handle sound_load_samples(njin_ctx &ctx, const f32 *samples, i32 count,
                                i32 sample_rate);

/// Giải phóng sound. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Sound cần giải phóng.
void sound_unload(njin_ctx &ctx, sound_handle handle);

/// Đặt âm lượng của sound, từ 0 trở lên (1 là âm lượng gốc).
///
/// Áp dụng ngay cho cả những bản đang phát. Mặc định là 1. Giá trị âm được coi là 0.
/// @param ctx Context của engine.
/// @param handle Sound cần đặt.
/// @param volume Âm lượng.
void sound_set_volume(njin_ctx &ctx, sound_handle handle, f32 volume);

/// Tắt hoặc bật tiếng sound.
///
/// Tắt tiếng đưa âm lượng đang phát về 0 mà không đụng đến âm lượng đã lưu, nên
/// bật lại thì khôi phục đúng như cũ.
/// @param ctx Context của engine.
/// @param handle Sound cần đặt.
/// @param muted `true` để tắt tiếng.
void sound_set_muted(njin_ctx &ctx, sound_handle handle, bool muted);

/// Phát sound mà không cắt các bản đang phát.
///
/// Gọi liên tiếp thì các bản chồng lên nhau, tối đa 8 bản cùng lúc. Quá số đó
/// thì bản chạy lâu nhất bị cắt để lấy chỗ. Hợp với âm thanh mà nhiều thứ cùng
/// kích hoạt, để một đám đông được nghe như một đám đông.
/// @param ctx Context của engine.
/// @param handle Sound cần phát.
void sound_play_once(njin_ctx &ctx, sound_handle handle);

/// Giống sound_play_once() nhưng chỉnh cao độ và độ lớn cho riêng bản này.
///
/// `pitch` 1 là như bản ghi, 2 là cao gấp đôi. `gain` nhân vào âm lượng của
/// sound cho bản này. Cả hai không được lưu lại: lần phát thường sau đó trở về 1
/// và 1. Hợp với một âm thanh dùng cho nhiều vật có kích cỡ khác nhau: một bản ghi
/// dùng cho tất cả, vật lớn hơn nghe to hơn và trầm hơn.
/// @param ctx Context của engine.
/// @param handle Sound cần phát.
/// @param pitch Cao độ. Giá trị rất nhỏ được nâng lên mức tối thiểu.
/// @param gain Hệ số âm lượng cho bản này. Giá trị âm được coi là 0.
void sound_play_once_at(njin_ctx &ctx, sound_handle handle, f32 pitch, f32 gain);

/// Cắt mọi bản đang phát rồi phát lại từ đầu, nên lúc nào cũng chỉ nghe một bản.
///
/// Hợp với tiếng nhấp giao diện hoặc tiếng cảnh báo: bấm dồn thì nghe rõ từng
/// lần thay vì chồng thành một đống.
/// @param ctx Context của engine.
/// @param handle Sound cần phát.
void sound_play_restart(njin_ctx &ctx, sound_handle handle);

/// Đánh dấu sound là lặp lại và phát nếu nó chưa phát.
///
/// Module âm thanh của engine phát lại sound mỗi khi thấy nó vừa kết thúc, nên
/// đây là lời gọi duy nhất cần để giữ nó chạy. Vì được phát lại sau khi kết thúc,
/// chỗ nối có một quãng lặng cỡ một frame. Với nhạc nền dài, dùng music_load()
/// để không có quãng lặng. Gọi sound_stop() để dừng.
/// @param ctx Context của engine.
/// @param handle Sound cần phát lặp.
void sound_play_loop(njin_ctx &ctx, sound_handle handle);

/// Phát sound như sound_play_once(), kèm vị trí trong thế giới.
///
/// Âm lượng giảm dần theo khoảng cách tới điểm camera đang nhìn, và âm thanh
/// lệch sang loa trái hoặc phải theo vị trí trên màn hình. Xem
/// audio_set_range().
/// @param ctx Context của engine.
/// @param handle Sound cần phát.
/// @param world_pos Nơi phát ra tiếng, trong thế giới.
void sound_play_at(njin_ctx &ctx, sound_handle handle, vec2 world_pos);

/// Khoảng cách nghe được của sound_play_at().
///
/// Gần hơn `full_until` thì nghe đủ âm lượng, xa hơn `silent_from` thì im
/// lặng, ở giữa thì nhỏ dần đều. Mặc định là 200 và 1200 đơn vị thế giới.
/// @param ctx Context của engine.
/// @param full_until Khoảng cách bắt đầu nhỏ dần.
/// @param silent_from Khoảng cách im lặng hẳn. Nhỏ hơn `full_until` thì được
/// nâng bằng `full_until`.
void audio_set_range(njin_ctx &ctx, f32 full_until, f32 silent_from);

/// Dừng mọi bản đang phát của sound và bỏ chế độ lặp.
/// @param ctx Context của engine.
/// @param handle Sound cần dừng.
void sound_stop(njin_ctx &ctx, sound_handle handle);
/// @}

/// @addtogroup grp_music
/// @{

/// Nạp một bản nhạc để stream từ đĩa.
///
/// Nhạc không nằm hết trong bộ nhớ mà được giải mã từng đoạn, hợp với nhạc nền
/// hoặc âm thanh môi trường dài. Nó lặp ngay trong bộ giải mã nên không có quãng
/// lặng ở chỗ nối. Module âm thanh của engine cấp dữ liệu cho stream mỗi frame.
/// @param ctx Context của engine.
/// @param path Đường dẫn file nhạc (ogg, mp3, wav, flac...).
/// @return Handle của music, hoặc handle có id 0 nếu không có thiết bị, file thiếu
/// hoặc không giải mã được.
music_handle music_load(njin_ctx &ctx, const char *path);

/// Giải phóng music. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Music cần giải phóng.
void music_unload(njin_ctx &ctx, music_handle handle);

/// Đặt âm lượng của music, từ 0 trở lên (1 là âm lượng gốc). Mặc định là 1.
/// @param ctx Context của engine.
/// @param handle Music cần đặt.
/// @param volume Âm lượng. Giá trị âm được coi là 0.
void music_set_volume(njin_ctx &ctx, music_handle handle, f32 volume);

/// Tắt hoặc bật tiếng music, không đụng đến âm lượng đã lưu.
/// @param ctx Context của engine.
/// @param handle Music cần đặt.
/// @param muted `true` để tắt tiếng.
void music_set_muted(njin_ctx &ctx, music_handle handle, bool muted);

/// Bật hoặc tắt chế độ lặp. Mặc định là bật. Có hiệu lực ngay cả giữa bài.
/// @param ctx Context của engine.
/// @param handle Music cần đặt.
/// @param looping `true` để lặp.
void music_set_looping(njin_ctx &ctx, music_handle handle, bool looping);

/// Phát từ đầu, kể cả khi đang tạm dừng hoặc đang phát.
/// @param ctx Context của engine.
/// @param handle Music cần phát.
void music_play(njin_ctx &ctx, music_handle handle);

/// Dừng và đưa vị trí về đầu bài.
/// @param ctx Context của engine.
/// @param handle Music cần dừng.
void music_stop(njin_ctx &ctx, music_handle handle);

/// Tạm dừng nhưng giữ nguyên vị trí. Dùng music_resume() để phát tiếp.
/// @param ctx Context của engine.
/// @param handle Music cần tạm dừng.
void music_pause(njin_ctx &ctx, music_handle handle);

/// Phát tiếp từ chỗ music_pause() đã dừng.
/// @param ctx Context của engine.
/// @param handle Music cần phát tiếp.
void music_resume(njin_ctx &ctx, music_handle handle);

/// Music có đang phát không (không tính lúc tạm dừng).
/// @param ctx Context của engine.
/// @param handle Music.
/// @return `true` nếu đang phát.
bool music_playing(njin_ctx &ctx, music_handle handle);

/// Phát từ đầu, to dần từ im lặng trong `seconds` giây. Đang phát thì chỉ to
/// dần lên mức đầy từ mức hiện tại.
/// @param ctx Context của engine.
/// @param handle Music.
/// @param seconds Thời gian to dần, giây (giờ thật).
void music_fade_in(njin_ctx &ctx, music_handle handle, f32 seconds);

/// Nhỏ dần rồi dừng.
/// @param ctx Context của engine.
/// @param handle Music.
/// @param seconds Thời gian nhỏ dần, giây (giờ thật).
void music_fade_out(njin_ctx &ctx, music_handle handle, f32 seconds);

/// Chuyển nhạc: mọi music khác đang phát nhỏ dần rồi dừng, trong lúc `handle`
/// to dần. Gọi khi vào màn mới, khi gặp trùm. `handle` đang phát rồi thì nó
/// tiếp tục, không bị phát lại từ đầu.
/// @code
/// njin::music_crossfade(ctx, g.boss_theme, 1.5f);
/// @endcode
/// @param ctx Context của engine.
/// @param handle Music cần chuyển sang. Handle id 0 thì chỉ tắt dần mọi nhạc.
/// @param seconds Thời gian chuyển, giây (giờ thật).
void music_crossfade(njin_ctx &ctx, music_handle handle, f32 seconds);
/// @}
} // namespace njin
