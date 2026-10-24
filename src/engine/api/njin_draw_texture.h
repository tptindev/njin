#pragma once
#include "_draw_types.h"
#include "_math.h"

// Phần texture của njin_draw.h: cách lấy mẫu, vẽ texture, shader của texture.

namespace njin {
struct context;

/// @addtogroup grp_texture
/// @{

/// Đổi cách lấy mẫu của texture.
/// @param ctx Context của engine.
/// @param handle Texture cần đổi.
/// @param filter Cách lấy mẫu.
void texture_set_filter(context &ctx, texture_handle handle,
                        texture_filter filter);

/// Đổi cách lấy mẫu của render texture.
///
/// Render texture mới tạo lấy mẫu kiểu `filter_nearest`. Đặt `filter_linear` khi
/// shader của game (xem draw_instanced()) đọc nó ở kích thước khác kích thước
/// đã vẽ.
/// @param ctx Context của engine.
/// @param handle Render texture cần đổi.
/// @param filter Cách lấy mẫu.
void render_texture_set_filter(context &ctx, render_texture_handle handle,
                               texture_filter filter);

/// Tham số đầy đủ cho texture_draw_ex().
struct texture_draw_desc {
  vec2 pos{};     ///< Vị trí của điểm neo `origin`.
  /// Vùng trong ảnh, tính bằng pixel. Kích thước 0 là cả ảnh. Dùng cho sprite
  /// sheet.
  rect source{};
  vec2 scale{1.0f, 1.0f}; ///< Tỉ lệ theo từng trục. Giá trị âm là lật.
  /// Điểm neo, tính theo tỉ lệ kích thước: `{0, 0}` là góc trên trái, `{0.5,
  /// 0.5}` là tâm. `pos` rơi đúng vào điểm này, và ảnh xoay quanh nó.
  vec2 origin{};
  f32 rotation = 0.0f; ///< Góc xoay tính bằng độ, theo chiều kim đồng hồ.
  bool flip_x = false; ///< Lật ngang.
  bool flip_y = false; ///< Lật dọc.
  rgba tint{1.0f, 1.0f, 1.0f, 1.0f}; ///< Màu nhân vào ảnh.
};

/// Vẽ texture với vùng nguồn, tỉ lệ, điểm neo, góc xoay và lật.
/// @param ctx Context của engine.
/// @param handle Texture cần vẽ.
/// @param desc Tham số vẽ.
void texture_draw_ex(const context &ctx, texture_handle handle,
                     const texture_draw_desc &desc);

/// Gắn một shader "vật liệu" cố định vào texture: không cần bọc
/// shader_begin()/shader_end() quanh mỗi lần vẽ nữa.
///
/// Từ lần vẽ kế tiếp, mọi texture_draw() và texture_draw_ex() vẽ `handle` tự
/// bật shader này (kể cả các ảnh phụ đã gắn bằng shader_set_texture()), rồi tắt
/// lại ngay sau khi vẽ xong. Cách cũ vẫn dùng được nguyên vẹn và luôn thắng: nếu
/// đang ở giữa một shader_begin() khác (kể cả của một texture có vật liệu khác),
/// texture_draw()/texture_draw_ex() giữ nguyên shader đó, không thay bằng vật
/// liệu của mình. Đặt uniform bằng shader_set_*() trước khi vẽ, như
/// shader_begin().
///
/// Không áp dụng cho sprite/tilemap/particle vẽ qua ECS: những hệ đó tự quản
/// shader riêng (ví dụ flash/dissolve của njin_fx.h).
/// @param ctx Context của engine.
/// @param handle Texture cần gắn.
/// @param shader Shader cần gắn, hoặc handle có id 0 để gỡ vật liệu.
void texture_set_shader(context &ctx, texture_handle handle,
                        shader_handle shader);
/// @}
} // namespace njin
