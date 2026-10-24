#pragma once
#include "_draw_types.h"
#include "_math.h"

// Phần hình của njin_draw.h: hình, đường, tam giác, độ mờ và độ dời, cách trộn màu,
// vùng vẽ. njin_draw.h include cả ba phần; engine include đúng phần nó dùng.

namespace njin {
struct context;

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
void draw_rect(const context &ctx, rect r, rgba color);

/// Vẽ viền hình chữ nhật. Viền nằm bên trong `r`.
/// @param ctx Context của engine.
/// @param r Hình chữ nhật.
/// @param thickness Độ dày viền.
/// @param color Màu.
void draw_rect_lines(const context &ctx, rect r, f32 thickness, rgba color);

/// Vẽ hình chữ nhật đặc bo tròn bốn góc.
/// @param ctx Context của engine.
/// @param r Hình chữ nhật.
/// @param radius Bán kính góc, pixel; lớn quá nửa cạnh ngắn thì lấy nửa cạnh
///        ngắn (thành hình viên thuốc). 0 là góc vuông như draw_rect().
/// @param color Màu.
void draw_rect_rounded(const context &ctx, rect r, f32 radius, rgba color);

/// Vẽ viền hình chữ nhật bo tròn bốn góc. Viền nằm bên trong `r`, như
/// draw_rect_lines(), nên vẽ cùng `r` và `radius` với draw_rect_rounded() thì
/// viền ôm khít nền.
/// @param ctx Context của engine.
/// @param r Hình chữ nhật.
/// @param radius Bán kính góc ngoài của viền, pixel.
/// @param thickness Độ dày viền.
/// @param color Màu.
void draw_rect_rounded_lines(const context &ctx, rect r, f32 radius, f32 thickness, rgba color);

/// Làm mờ những gì đã vẽ dưới `r` (kiểu kính mờ): nền của một popup, để nó
/// tách khỏi những gì sau nó mà vẫn thấy lờ mờ. Phần đã vẽ trong `r` được thay
/// bằng chính nó đã làm mờ, cắt theo góc bo `radius`; vẽ popup lên trên sau đó,
/// nền hơi trong để lớp mờ hiện qua.
///
/// `r` tính theo màn hình, như UI (kể cả canvas của ui_canvas_set()). Không
/// làm gì khi: `blur` <= 0, đang trong camera thế giới, đang vẽ vào render
/// texture, hoặc máy vẽ bằng phần mềm. Không gọi giữa draw_offset_push() và
/// draw_offset_pop(). Chữ đã xếp hàng để vẽ sắc nét ở độ phân giải cửa sổ
/// (config::crisp_text) mà nằm dưới `r` thì bị giấu, vì nó chưa có trong ảnh để làm mờ.
/// Mỗi lần gọi tốn một lần chép vùng đó và vài lượt vẽ ở nửa kích thước.
/// ui_skin::blur làm việc này cho nền của widget và panel.
/// @param ctx Context của engine.
/// @param r Vùng cần làm mờ.
/// @param blur Độ mờ: bán kính, pixel.
/// @param radius Bán kính góc bo của vùng, pixel. 0 là góc vuông.
/// @param tint Màu nhân vào lớp mờ; alpha nhỏ hơn 1 thì lớp mờ hòa với ảnh rõ dưới nó.
void draw_backdrop_blur(const context &ctx, rect r, f32 blur, f32 radius = 0.0f,
                        rgba tint = {1.0f, 1.0f, 1.0f, 1.0f});

/// Bật/tắt vẽ hình bo góc bằng SDF (trường khoảng cách có dấu): mỗi hình là
/// một quad, độ phủ của từng pixel tính trong shader, nên mép và góc bo được
/// khử răng cưa mịn ở mọi tỉ lệ (độ phân giải ảo, UI mịn). Áp dụng cho
/// draw_rect_rounded(), draw_rect_rounded_lines() và mặt màu phẳng của UI
/// (ui_skin không ảnh). Mặc định tắt: hình vẽ bằng tam giác của raylib như
/// trước. Mỗi hình đổi uniform nên tốn một lần vẽ riêng; đáng dùng cho UI, ở
/// mức đồ họa vừa trở lên. Khi đang có shader khác (shader_begin(),
/// draw_dissolve_begin()) hình vẫn vẽ kiểu cũ dưới shader đó.
/// @param ctx Context của engine.
/// @param on Bật hay tắt.
void draw_set_smooth_shapes(context &ctx, bool on);

/// Hình bo góc có đang vẽ bằng SDF không (draw_set_smooth_shapes()).
/// @param ctx Context của engine.
/// @return `true` khi đang bật.
bool draw_smooth_shapes(const context &ctx);

/// Vẽ hình chữ nhật đặc xoay quanh tâm của nó.
/// @param ctx Context của engine.
/// @param center Tâm.
/// @param size Kích thước.
/// @param rotation Góc xoay tính bằng độ, theo chiều kim đồng hồ.
/// @param color Màu.
void draw_rect_rotated(const context &ctx, vec2 center, vec2 size,
                       f32 rotation, rgba color);

/// Vẽ hình tròn đặc.
/// @param ctx Context của engine.
/// @param center Tâm.
/// @param radius Bán kính.
/// @param color Màu.
void draw_circle(const context &ctx, vec2 center, f32 radius, rgba color);

/// Vẽ viền hình tròn. Viền nằm bên trong bán kính.
/// @param ctx Context của engine.
/// @param center Tâm.
/// @param radius Bán kính.
/// @param thickness Độ dày viền.
/// @param color Màu.
void draw_circle_lines(const context &ctx, vec2 center, f32 radius,
                       f32 thickness, rgba color);

/// Vẽ đoạn thẳng.
/// @param ctx Context của engine.
/// @param a Điểm đầu.
/// @param b Điểm cuối.
/// @param thickness Độ dày.
/// @param color Màu.
void draw_line(const context &ctx, vec2 a, vec2 b, f32 thickness, rgba color);

/// Vẽ tam giác đặc. Thứ tự ba đỉnh không quan trọng.
/// @param ctx Context của engine.
/// @param a Đỉnh thứ nhất.
/// @param b Đỉnh thứ hai.
/// @param c Đỉnh thứ ba.
/// @param color Màu.
void draw_triangle(const context &ctx, vec2 a, vec2 b, vec2 c, rgba color);

/// Vẽ tam giác phủ một phần của texture: mỗi đỉnh lấy ảnh ở toạ độ `uv` của
/// nó (0..1 trên ảnh, `{0, 0}` là góc trên trái, `{1, 1}` góc dưới phải), phần
/// giữa nội suy theo, rồi nhân với `tint`. Thứ tự ba đỉnh không quan trọng.
/// Dùng khi ảnh phải nằm trong một hình không phải chữ nhật: một lát quạt, một
/// dải cong, một đa giác cắt thành tam giác. Texture trong atlas cũng dùng được,
/// `uv` vẫn tính trên riêng ảnh đó.
/// @code
/// // Một lát quạt có hoa văn, tâm quạt ở góc dưới phải của ảnh.
/// const auto uv_of = [&](njin::vec2 p) { return njin::vec2{1, 1} + (p - corner) / radius; };
/// njin::draw_triangle_textured(ctx, g.pattern, a, b, c, uv_of(a), uv_of(b), uv_of(c));
/// @endcode
/// @param ctx Context của engine.
/// @param texture Ảnh phủ lên tam giác.
/// @param a Đỉnh thứ nhất.
/// @param b Đỉnh thứ hai.
/// @param c Đỉnh thứ ba.
/// @param uv_a Toạ độ trên ảnh của đỉnh `a`.
/// @param uv_b Toạ độ trên ảnh của đỉnh `b`.
/// @param uv_c Toạ độ trên ảnh của đỉnh `c`.
/// @param tint Màu nhân vào ảnh; trắng là giữ nguyên.
void draw_triangle_textured(const context &ctx, texture_handle texture, vec2 a, vec2 b, vec2 c, vec2 uv_a,
                            vec2 uv_b, vec2 uv_c, rgba tint = {1.0f, 1.0f, 1.0f, 1.0f});

/// Như bản trên, phủ một phần của render texture (`uv` tính như ảnh thường:
/// `{0, 0}` góc trên trái của những gì đã vẽ vào nó). Dùng để vẽ lại một thứ đã
/// vẽ sẵn vào render texture mà biến dạng nó: uốn, bóp, thu vào một điểm.
/// @param ctx Context của engine.
/// @param texture Render texture phủ lên tam giác.
/// @param a Đỉnh thứ nhất.
/// @param b Đỉnh thứ hai.
/// @param c Đỉnh thứ ba.
/// @param uv_a Toạ độ trên ảnh của đỉnh `a`.
/// @param uv_b Toạ độ trên ảnh của đỉnh `b`.
/// @param uv_c Toạ độ trên ảnh của đỉnh `c`.
/// @param tint Màu nhân vào ảnh; trắng là giữ nguyên.
void draw_triangle_textured(const context &ctx, render_texture_handle texture, vec2 a, vec2 b, vec2 c,
                            vec2 uv_a, vec2 uv_b, vec2 uv_c, rgba tint = {1.0f, 1.0f, 1.0f, 1.0f});

/// Làm mờ mọi thứ vẽ sau đó (hình, chữ, ảnh, UI): độ đục của chúng nhân với
/// `alpha`, cho đến draw_alpha_pop(). Lồng được, các lớp nhân dồn với nhau;
/// mỗi lần push cần một lần pop trong cùng frame. Dùng để một nhóm hình vẽ
/// hiện dần hay tắt dần cùng nhau (một bảng HUD đang đóng, màn hình loading).
/// @code
/// njin::draw_alpha_push(ctx, 1.0f - leaving);
/// draw_card(ctx);   // nền, chữ, icon của nó đều mờ theo
/// njin::draw_alpha_pop(ctx);
/// @endcode
/// @param ctx Context của engine.
/// @param alpha Độ đục, 0 (trong suốt) đến 1 (giữ nguyên).
void draw_alpha_push(const context &ctx, f32 alpha);

/// Bỏ lớp mờ draw_alpha_push() gần nhất. @param ctx Context của engine.
void draw_alpha_pop(const context &ctx);

/// Phóng mọi hình vẽ sau đó theo `scale` rồi dời đi `offset` (đơn vị thế
/// giới) cho tới draw_offset_pop(): điểm `p` được vẽ ở `p * scale + offset`.
/// Dùng để vẽ một phần của thế giới có tọa độ riêng (một khu bản đồ được sinh
/// riêng) đúng chỗ của nó, hoặc vẽ nó thu nhỏ vào một render texture rồi vẽ
/// texture đó phóng to lại. Lồng được; mỗi lần push cần một lần pop trong
/// cùng phase. Chữ pixel (draw_text với font pixel) không bị dời.
/// @code
/// njin::draw_offset_push(ctx, ward_origin);
/// draw_ward(ctx, ward); // tọa độ riêng của khu
/// njin::draw_offset_pop(ctx);
/// @endcode
/// @param ctx Context của engine.
/// @param offset Độ dời.
/// @param scale Hệ số phóng, 1 là giữ nguyên cỡ.
void draw_offset_push(const context &ctx, vec2 offset, f32 scale = 1.0f);

/// Bỏ độ dời của lần draw_offset_push() gần nhất.
/// @param ctx Context của engine.
void draw_offset_pop(const context &ctx);
/// @}

/// @addtogroup grp_draw
/// @{

/// Đổi cách trộn màu cho mọi thứ vẽ sau đó, cho đến blend_end().
/// @param ctx Context của engine.
/// @param mode Cách trộn.
void blend_begin(const context &ctx, blend_mode mode);

/// Bật sắp xếp theo Y cho một lớp vẽ: trong lớp đó, sprite và particle có
/// `y` lớn hơn (thấp hơn trên màn hình) được vẽ sau, nên đè lên thứ đứng phía
/// sau nó. Đây là cách game top-down cho nhân vật đi vòng ra sau cây, sau nhà.
///
/// `y` là `transform.pos.y + sprite::sort_offset`. Đặt `sprite::origin` ở
/// chân (`{0.5, 1}`) thì không cần `sort_offset`. Tilemap trong lớp đó vẫn vẽ
/// trước mọi sprite (làm nền). Hai thứ cùng `y` giữ thứ tự ổn định giữa các
/// frame.
/// @param ctx Context của engine.
/// @param layer Lớp vẽ (njin::sprite::layer).
/// @param on `true` để bật.
void draw_set_y_sort(context &ctx, i32 layer, bool on);

/// Trở lại cách trộn mặc định.
/// @param ctx Context của engine.
void blend_end(const context &ctx);

/// Chỉ vẽ bên trong một vùng của màn hình, cho đến clip_end().
///
/// `area` tính bằng pixel **màn hình**, không qua camera. Dùng cho khung cuộn
/// trong UI.
/// @param ctx Context của engine.
/// @param area Vùng được vẽ, pixel màn hình.
void clip_begin(const context &ctx, rect area);

/// Bỏ giới hạn vùng vẽ.
/// @param ctx Context của engine.
void clip_end(const context &ctx);
/// @}
} // namespace njin
