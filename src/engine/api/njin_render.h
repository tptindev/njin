#pragma once
#include "_types.h"

namespace njin {
// Opaque, see njin_ctx.h.
struct njin_ctx;

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

/// Đặt uniform kiểu `vec3`. Xem shader_set_i32() về uniform không tồn tại.
/// @param ctx Context của engine.
/// @param handle Shader cần đặt.
/// @param name Tên uniform trong shader.
/// @param value Giá trị.
void shader_set_vec3(njin_ctx &ctx, shader_handle handle, const char *name,
                     vec3 value);

/// Đặt uniform kiểu `vec4`. Xem shader_set_i32() về uniform không tồn tại.
/// @param ctx Context của engine.
/// @param handle Shader cần đặt.
/// @param name Tên uniform trong shader.
/// @param value Giá trị.
void shader_set_vec4(njin_ctx &ctx, shader_handle handle, const char *name,
                     vec4 value);

/// Đặt uniform kiểu mảng `vec4`, ví dụ `uniform vec4 lights[8];`.
///
/// `count` không được lớn hơn kích thước mảng khai báo trong shader. Muốn truyền
/// mảng `vec2` hoặc `vec3`, đóng vào `vec4` (hai `vec2` một phần tử). Xem
/// shader_set_i32() về uniform không tồn tại.
/// @param ctx Context của engine.
/// @param handle Shader cần đặt.
/// @param name Tên mảng trong shader, không kèm `[0]`.
/// @param values Các phần tử, nằm liền nhau trong bộ nhớ.
/// @param count Số phần tử. 0 thì không làm gì.
void shader_set_vec4_array(njin_ctx &ctx, shader_handle handle, const char *name,
                           const vec4 *values, u32 count);

/// Gắn một texture vào uniform `sampler2D` của shader, ngoài `texture0`.
///
/// Dùng cho ảnh phụ mà shader đọc: bảng màu (LUT), nhiễu, mặt nạ, normal map.
/// Mỗi shader nhận tối đa 4 ảnh phụ; đặt lại cùng tên thì thay ảnh. Ảnh được gắn
/// mỗi khi shader bật (shader_begin(), camera_set_post_shader(), draw_instanced()),
/// nên gọi hàm này một lần là đủ. Ảnh nạp lại khi hot reload thì shader thấy ảnh mới.
///
/// - Dùng ảnh riêng, từ texture_load(). Ảnh xếp trong atlas bị từ chối, vì shader
///   sẽ thấy cả trang atlas chứ không phải riêng ảnh đó.
/// - Ảnh phụ không có bộ lọc riêng cho từng shader: nó lấy mẫu theo texture_set_filter()
///   của chính ảnh.
/// - Đường đáng tin cậy nhất: shader hậu kỳ của camera_set_post_shader(), và
///   draw_instanced(). Qua shader_begin() thì ảnh phụ chỉ sống đến lần raylib đẩy
///   batch kế tiếp (đầy 8192 hình, hoặc 256 lần đổi texture, hoặc có draw_instanced()
///   hay đổi render texture xen vào); dùng cho vài lệnh vẽ, không cho hàng nghìn
///   sprite.
/// @param ctx Context của engine.
/// @param handle Shader cần đặt.
/// @param name Tên `uniform sampler2D` trong shader.
/// @param texture Ảnh cần gắn.
void shader_set_texture(njin_ctx &ctx, shader_handle handle, const char *name, texture_handle texture);

/// Như bản trên, với một render texture.
///
/// Đừng gắn render texture đang được vẽ vào (giữa render_texture_begin() và
/// render_texture_end()). Render texture lưu ngược trục dọc: shader cần lật `v`
/// (`1.0 - v`) để đọc đúng chiều.
/// @param ctx Context của engine.
/// @param handle Shader cần đặt.
/// @param name Tên `uniform sampler2D` trong shader.
/// @param texture Render texture cần gắn.
void shader_set_texture(njin_ctx &ctx, shader_handle handle, const char *name,
                        render_texture_handle texture);
/// @}

/// @addtogroup grp_instancing
/// @{

/// Máy này có vẽ instanced được không: cần OpenGL 3.3 trở lên (hoặc ES 3.0).
///
/// Khi `false`, mọi hàm instance_* trả về handle không hợp lệ hoặc không làm
/// gì; game nên có sẵn một cách vẽ khác (ví dụ texture_draw_ex() từng cái).
/// @param ctx Context của engine.
/// @return `true` nếu draw_instanced() vẽ được.
bool instancing_available(const njin_ctx &ctx);

/// Tạo một bộ đệm instance trên GPU: mỗi instance là `floats_per_instance`
/// số thực, do game tự quyết định ý nghĩa.
///
/// Shader đọc chúng thành các thuộc tính `vec4` theo instance, tên `instance0`,
/// `instance1`, `instance2`, `instance3` (4 số một thuộc tính, theo thứ tự).
/// Bộ đệm tự lớn lên khi instance_buffer_upload() cần thêm chỗ.
/// @param ctx Context của engine.
/// @param floats_per_instance 4, 8, 12 hoặc 16.
/// @return Handle, hoặc handle có id 0 nếu số không hợp lệ hay máy không hỗ trợ
/// (xem instancing_available()).
instance_buffer_handle instance_buffer_create(njin_ctx &ctx, u32 floats_per_instance);

/// Hủy bộ đệm instance. Handle không hợp lệ bị bỏ qua.
/// @param ctx Context của engine.
/// @param handle Bộ đệm cần hủy.
void instance_buffer_destroy(njin_ctx &ctx, instance_buffer_handle handle);

/// Ghi `count` instance vào bộ đệm, thay toàn bộ nội dung cũ.
///
/// `data` có `count * floats_per_instance` số, instance này nối tiếp instance
/// kia. Gọi mỗi frame với dữ liệu mới là cách dùng bình thường.
/// @param ctx Context của engine.
/// @param handle Bộ đệm cần ghi.
/// @param data Dữ liệu các instance.
/// @param count Số instance.
void instance_buffer_upload(njin_ctx &ctx, instance_buffer_handle handle, const f32 *data,
                            u32 count);

/// Vẽ `count` hình vuông, bắt đầu từ instance `first`, bằng **một** lệnh vẽ.
///
/// Mỗi hình vuông có 6 đỉnh (2 tam giác). Vertex shader của game nhận:
/// - `in vec3 vertexPosition`: góc của hình vuông đơn vị, từ `(0, 0)` đến
///   `(1, 1)`, y hướng xuống. Chỉ `xy` có nghĩa.
/// - `in vec4 instance0` ... `instance3`: dữ liệu của instance đang vẽ.
/// - `uniform mat4 mvp`: camera hiện tại, như khi vẽ bình thường.
///
/// Shader tự đặt hình vuông vào thế giới (vị trí, kích thước, góc xoay) từ dữ
/// liệu instance. Cách trộn màu hiện tại (blend_begin()) được giữ nguyên; thứ
/// đã vẽ trước đó được đẩy ra trước, nên thứ tự vẽ đúng như gọi. Instance vẽ
/// sau đè lên instance vẽ trước: sắp xếp dữ liệu (ví dụ theo y) trước khi ghi.
///
/// Phải gọi giữa lúc bắt đầu và kết thúc vẽ của frame, ngoài
/// shader_begin()/shader_end(). Uniform khác của shader đặt bằng shader_set_*()
/// như thường.
/// @param ctx Context của engine.
/// @param handle Bộ đệm đã ghi bằng instance_buffer_upload().
/// @param shader Shader có vertex shader đọc các thuộc tính trên.
/// @param first Instance đầu tiên được vẽ.
/// @param count Số instance, bị cắt bớt nếu vượt quá số đã ghi.
void draw_instanced(njin_ctx &ctx, instance_buffer_handle handle, shader_handle shader, u32 first,
                    u32 count);

/// Như bản trên, và gắn `texture` vào uniform `sampler2D texture0` của shader.
///
/// Dùng khi mỗi instance là một khung trong một sprite sheet: vertex shader chọn
/// khung từ dữ liệu instance, fragment shader đọc nó. Với texture nằm trong atlas
/// (atlas_load()), toạ độ là của cả trang atlas.
/// @param ctx Context của engine.
/// @param handle Bộ đệm đã ghi bằng instance_buffer_upload().
/// @param shader Shader có vertex shader đọc các thuộc tính trên.
/// @param first Instance đầu tiên được vẽ.
/// @param count Số instance, bị cắt bớt nếu vượt quá số đã ghi.
/// @param texture Texture gắn vào `texture0`. Handle không hợp lệ thì không vẽ gì.
void draw_instanced(njin_ctx &ctx, instance_buffer_handle handle, shader_handle shader, u32 first,
                    u32 count, texture_handle texture);

/// Như bản trên, với một render texture, ví dụ sprite sheet vừa vẽ ra (bake) một lần
/// bằng render_texture_begin().
///
/// Toạ độ texture có gốc ở **góc dưới trái** của hình đã vẽ (`v = 1 - y / chiều
/// cao`), vì framebuffer OpenGL lưu từ dưới lên. Render texture lấy mẫu kiểu
/// `filter_nearest` trừ khi đổi bằng render_texture_set_filter().
/// @param ctx Context của engine.
/// @param handle Bộ đệm đã ghi bằng instance_buffer_upload().
/// @param shader Shader có vertex shader đọc các thuộc tính trên.
/// @param first Instance đầu tiên được vẽ.
/// @param count Số instance, bị cắt bớt nếu vượt quá số đã ghi.
/// @param texture Render texture gắn vào `texture0`. Handle không hợp lệ thì không vẽ gì.
void draw_instanced(njin_ctx &ctx, instance_buffer_handle handle, shader_handle shader, u32 first,
                    u32 count, render_texture_handle texture);
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

/// Lưu nội dung render texture ra file ảnh, ngay lập tức.
///
/// Ảnh theo đúng chiều đã vẽ (góc trên trái của hình vẽ là góc trên trái của
/// file) và giữ **nguyên từng byte**, kể cả kênh alpha: không nhân hay chia
/// alpha, nên một render texture dùng để chứa dữ liệu (sprite sheet đã bake, xem
/// draw_instanced()) lưu ra được đúng như nó chứa.
///
/// Định dạng theo đuôi file: `.png` (nên dùng), `.bmp`, `.tga`, `.qoi`. Thư
/// mục cha được tạo nếu chưa có. Không gọi giữa render_texture_begin() và
/// render_texture_end(). Đọc lại từ GPU nên chậm hơn nhiều so với một lệnh vẽ:
/// dùng khi xuất file, không phải mỗi frame.
/// @param ctx Context của engine.
/// @param handle Render texture cần lưu. Handle không hợp lệ thì trả về `false`.
/// @param path Đường dẫn file. Muốn lưu vào thư mục lưu game thì dùng save_path().
/// @return `true` nếu đã lưu. Lý do thất bại (đuôi file lạ, không ghi được) ghi vào log.
bool render_texture_save(njin_ctx &ctx, render_texture_handle handle, const char *path);

/// Vẽ nội dung render texture theo đúng chiều, góc trên trái tại `pos`.
/// @param ctx Context của engine.
/// @param handle Render texture cần vẽ.
/// @param pos Vị trí góc trên trái.
/// @param tint Màu nhân vào ảnh. Màu trắng `{1, 1, 1, 1}` giữ nguyên ảnh.
void render_texture_draw(const njin_ctx &ctx, render_texture_handle handle,
                         vec2 pos, rgba tint);
/// @}
} // namespace njin
