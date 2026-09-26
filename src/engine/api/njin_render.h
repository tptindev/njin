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
