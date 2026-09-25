#pragma once
#include "_math.h"

namespace njin {
struct njin_ctx;

/// @addtogroup grp_draw
/// @{

/// Vẽ hình chữ nhật đặc.
///
/// Mọi hàm vẽ phải gọi giữa lúc bắt đầu và kết thúc vẽ của frame, tức là trong
/// `phase_pre_render`, `phase_render` hoặc `phase_post_render`. Không gian (thế
/// giới hay màn hình) phụ thuộc phase: xem trang Vòng lặp game.
/// @param ctx Context của engine.
/// @param r Hình chữ nhật.
/// @param color Màu.
void draw_rect(const njin_ctx &ctx, rect r, rgba color);

/// Vẽ viền hình chữ nhật. Viền nằm bên trong `r`.
/// @param ctx Context của engine.
/// @param r Hình chữ nhật.
/// @param thickness Độ dày viền.
/// @param color Màu.
void draw_rect_lines(const njin_ctx &ctx, rect r, f32 thickness, rgba color);

/// Vẽ hình chữ nhật đặc xoay quanh tâm của nó.
/// @param ctx Context của engine.
/// @param center Tâm.
/// @param size Kích thước.
/// @param rotation Góc xoay tính bằng độ, theo chiều kim đồng hồ.
/// @param color Màu.
void draw_rect_rotated(const njin_ctx &ctx, vec2 center, vec2 size,
                       f32 rotation, rgba color);

/// Vẽ hình tròn đặc.
/// @param ctx Context của engine.
/// @param center Tâm.
/// @param radius Bán kính.
/// @param color Màu.
void draw_circle(const njin_ctx &ctx, vec2 center, f32 radius, rgba color);

/// Vẽ viền hình tròn. Viền nằm bên trong bán kính.
/// @param ctx Context của engine.
/// @param center Tâm.
/// @param radius Bán kính.
/// @param thickness Độ dày viền.
/// @param color Màu.
void draw_circle_lines(const njin_ctx &ctx, vec2 center, f32 radius,
                       f32 thickness, rgba color);

/// Vẽ đoạn thẳng.
/// @param ctx Context của engine.
/// @param a Điểm đầu.
/// @param b Điểm cuối.
/// @param thickness Độ dày.
/// @param color Màu.
void draw_line(const njin_ctx &ctx, vec2 a, vec2 b, f32 thickness, rgba color);

/// Vẽ tam giác đặc. Thứ tự ba đỉnh không quan trọng.
/// @param ctx Context của engine.
/// @param a Đỉnh thứ nhất.
/// @param b Đỉnh thứ hai.
/// @param c Đỉnh thứ ba.
/// @param color Màu.
void draw_triangle(const njin_ctx &ctx, vec2 a, vec2 b, vec2 c, rgba color);
/// @}

/// @addtogroup grp_text
/// @{

/// Nạp một font TrueType/OpenType, kèm sẵn các ký tự tiếng Việt.
///
/// Font được dựng thành ảnh ở cỡ `size` pixel. Vẽ ở đúng cỡ đó thì nét sắc
/// nhất; vẽ ở cỡ khác vẫn được nhưng chữ bị co giãn. Cần nhiều cỡ khác nhau
/// thì nạp một font cho mỗi cỡ.
/// @param ctx Context của engine.
/// @param path Đường dẫn file font (ttf, otf).
/// @param size Cỡ chữ để dựng font, tính bằng pixel.
/// @return Handle của font, hoặc handle id 0 (font mặc định) nếu nạp thất bại.
font_handle font_load(njin_ctx &ctx, const char *path, i32 size);

/// Giải phóng font. Handle không hợp lệ bị bỏ qua. Vẽ bằng handle đã giải
/// phóng thì dùng font mặc định.
/// @param ctx Context của engine.
/// @param font Font cần giải phóng.
void font_unload(njin_ctx &ctx, font_handle font);

/// Vẽ chữ với góc trên trái tại `pos`. Hỗ trợ xuống dòng bằng `\n`.
///
/// Chuỗi là UTF-8. Font mặc định (handle id 0) chỉ có ký tự ASCII: muốn viết
/// tiếng Việt có dấu thì nạp một font bằng font_load().
/// @param ctx Context của engine.
/// @param text Chuỗi UTF-8.
/// @param pos Vị trí góc trên trái.
/// @param size Cỡ chữ, tính bằng pixel.
/// @param color Màu.
/// @param font Font, mặc định là font của engine.
void draw_text(const njin_ctx &ctx, const char *text, vec2 pos, f32 size,
               rgba color, font_handle font = {});

/// Kích thước một chuỗi khi vẽ bằng draw_text() với cùng tham số.
///
/// Dùng để căn giữa hoặc căn phải: `pos.x = center.x - text_measure(...).x / 2`.
/// @param ctx Context của engine.
/// @param text Chuỗi UTF-8.
/// @param size Cỡ chữ, tính bằng pixel.
/// @param font Font, mặc định là font của engine.
/// @return Chiều rộng và chiều cao, tính bằng pixel.
vec2 text_measure(const njin_ctx &ctx, const char *text, f32 size,
                  font_handle font = {});
/// @}

/// @addtogroup grp_texture
/// @{

/// Cách lấy mẫu khi texture bị co giãn.
enum texture_filter {
  filter_nearest, ///< Giữ nguyên từng pixel, sắc cạnh. Dùng cho pixel art.
  filter_linear,  ///< Làm mượt. Mặc định.
};

/// Đổi cách lấy mẫu của texture.
/// @param ctx Context của engine.
/// @param handle Texture cần đổi.
/// @param filter Cách lấy mẫu.
void texture_set_filter(njin_ctx &ctx, texture_handle handle,
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
void texture_draw_ex(const njin_ctx &ctx, texture_handle handle,
                     const texture_draw_desc &desc);
/// @}

/// @addtogroup grp_draw
/// @{

/// Cách trộn màu của những gì vẽ sau blend_begin().
enum blend_mode {
  blend_alpha,    ///< Trộn theo độ trong suốt. Mặc định.
  blend_additive, ///< Cộng màu: sáng lên. Dùng cho lửa, ánh sáng, tia lửa.
  blend_multiply, ///< Nhân màu: tối đi. Dùng cho bóng đổ, lớp phủ màu.
};

/// Đổi cách trộn màu cho mọi thứ vẽ sau đó, cho đến blend_end().
/// @param ctx Context của engine.
/// @param mode Cách trộn.
void blend_begin(const njin_ctx &ctx, blend_mode mode);

/// Trở lại cách trộn mặc định.
/// @param ctx Context của engine.
void blend_end(const njin_ctx &ctx);

/// Chỉ vẽ bên trong một vùng của màn hình, cho đến clip_end().
///
/// `area` tính bằng pixel **màn hình**, không qua camera. Dùng cho khung cuộn
/// trong UI.
/// @param ctx Context của engine.
/// @param area Vùng được vẽ, pixel màn hình.
void clip_begin(const njin_ctx &ctx, rect area);

/// Bỏ giới hạn vùng vẽ.
/// @param ctx Context của engine.
void clip_end(const njin_ctx &ctx);
/// @}
} // namespace njin
