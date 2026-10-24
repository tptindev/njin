#pragma once
#include "_math.h"
#include <string>
#include <vector>

// Phần chữ của njin_draw.h: font và vẽ chữ.

namespace njin {
struct context;

/// @addtogroup grp_text
/// @{

/// Kiểu dựng chữ, xem font_set_style().
enum font_style : u8 {
  font_smooth, ///< Có khử răng cưa, lọc mượt. Hợp font vector, chữ đọc nhiều.
  font_pixel,  ///< Không khử răng cưa, lọc nearest: từng texel của glyph hoặc bật
               ///< hoặc tắt. Hợp game pixel art.
};

/// Nạp một font TrueType/OpenType, kèm sẵn các ký tự tiếng Việt.
///
/// Mỗi cỡ chữ được vẽ có một ảnh glyph riêng, dựng đúng cỡ đó ở lần vẽ đầu
/// tiên (cỡ làm tròn thành số nguyên pixel, từ 6 đến 256), nên chữ nét ở mọi
/// cỡ và một font dùng được cho cả chữ nhỏ lẫn chữ to.
/// @param ctx Context của engine.
/// @param path Đường dẫn file font (ttf, otf, hoặc ttc: font đầu tiên của bộ).
/// @param size Cỡ dựng sẵn ngay lúc nạp, tính bằng pixel, để lần vẽ đầu không
/// phải chờ. Bỏ qua hoặc 0 thì dựng thử ở cỡ 16 để biết file có đọc được không.
/// @param style Kiểu dựng chữ, mặc định là font_smooth.
/// @return Handle của font, hoặc handle id 0 (font mặc định) nếu nạp thất bại.
font_handle font_load(context &ctx, const char *path, i32 size = 0,
                      font_style style = font_smooth);

/// Đổi kiểu dựng chữ của một font, kể cả font mặc định (handle id 0). Các ảnh
/// glyph đã dựng bị bỏ và dựng lại ở lần vẽ kế tiếp.
///
/// Kiểu font_pixel tắt khử răng cưa và dùng lọc nearest, nên **chỉ nét khi vẽ
/// đúng cỡ font được thiết kế**: một font pixel như Press Start 2P thiết kế ở 8
/// pixel thì vẽ ở 8, 16, 24. Font vector thường (như JetBrains Mono) dựng ở cỡ
/// nhỏ theo kiểu này sẽ răng cưa. Chữ kiểu này luôn vẽ trong ảnh ảo, phóng bằng
/// cùng bộ lọc nearest với sprite, và không đi qua lớp chữ nét của
/// config::crisp_text. Nên bật `integer_scale` cho độ phân giải ảo để mọi
/// pixel chữ to bằng nhau.
/// @param ctx Context của engine.
/// @param font Font cần đổi, handle id 0 là font mặc định.
/// @param style Kiểu mới.
void font_set_style(context &ctx, font_handle font, font_style style);

/// Chọn font vẽ khi không truyền font (handle rỗng `{}`), cho chữ của cả game,
/// UI lẫn draw_text(), đổi một lần thay vì truyền font ở từng lời gọi. Handle
/// không hợp lệ, `{}`, hoặc font đã giải phóng thì quay về font của engine
/// (JetBrains Mono). Không gọi hàm này thì chữ vẽ y như trước.
///
/// njin::font_set_style() với handle rỗng vẫn đổi kiểu của font của engine,
/// không phải font chọn ở đây: đổi kiểu font này thì truyền chính handle của nó.
/// @param ctx Context của engine.
/// @param font Font dùng làm mặc định, `{}` để quay về font của engine.
void font_set_default(context &ctx, font_handle font);

/// Cho font dựng thêm các ký tự có trong `text`, ngoài bộ ký tự Latin và
/// tiếng Việt có sẵn: chữ Nga, Thái, Trung, Nhật, Hàn... Mỗi cỡ chữ được dựng
/// lại ở lần vẽ kế tiếp. Ký tự font không có thì vẫn không vẽ được: chữ Trung
/// cần một font có chữ Trung. Thường gọi một lần với cả file ngôn ngữ.
/// @code
/// const njin::font_handle cjk = njin::font_load(ctx, "C:/Windows/Fonts/msyh.ttc");
/// std::string zh;
/// if (njin::file_read("assets/lang/zh.json", zh))
///   njin::font_add_chars(ctx, cjk, zh.c_str());
/// njin::font_set_default(ctx, cjk);
/// @endcode
/// @param ctx Context của engine.
/// @param font Font cần thêm, handle id 0 là font của engine.
/// @param text Chữ (UTF-8) có các ký tự cần dựng.
void font_add_chars(context &ctx, font_handle font, const char *text);

/// Giải phóng font. Handle không hợp lệ bị bỏ qua. Vẽ bằng handle đã giải
/// phóng thì dùng font mặc định.
/// @param ctx Context của engine.
/// @param font Font cần giải phóng.
void font_unload(context &ctx, font_handle font);

/// Vẽ chữ với góc trên trái tại `pos`. Hỗ trợ xuống dòng bằng `\n`.
///
/// Chuỗi là UTF-8. Font mặc định (handle id 0) là JetBrains Mono, đã có chữ
/// tiếng Việt. Vị trí được làm tròn về pixel để chữ không bị nhòe.
///
/// Với độ phân giải ảo và GPU thật, chữ trên màn hình (ngoài world) được vẽ ở
/// độ phân giải cửa sổ, xem config::crisp_text.
/// @param ctx Context của engine.
/// @param text Chuỗi UTF-8.
/// @param pos Vị trí góc trên trái.
/// @param size Cỡ chữ, tính bằng pixel.
/// @param color Màu.
/// @param font Font, mặc định là font của engine.
void draw_text(const context &ctx, const char *text, vec2 pos, f32 size,
               rgba color, font_handle font = {});

/// Kích thước một chuỗi khi vẽ bằng draw_text() với cùng tham số.
///
/// Dùng để căn giữa hoặc căn phải: `pos.x = center.x - text_measure(...).x / 2`.
/// @param ctx Context của engine.
/// @param text Chuỗi UTF-8.
/// @param size Cỡ chữ, tính bằng pixel.
/// @param font Font, mặc định là font của engine.
/// @return Chiều rộng và chiều cao, tính bằng pixel.
vec2 text_measure(const context &ctx, const char *text, f32 size,
                  font_handle font = {});

/// Chia một đoạn chữ thành các dòng không rộng quá `max_width` khi vẽ bằng
/// draw_text() với cùng cỡ và font.
///
/// Chỉ xuống dòng ở dấu cách và ở `\n`, nên không bao giờ cắt đôi một chữ
/// (hay một ký tự UTF-8); một từ dài hơn cả dòng đứng riêng một dòng.
/// @param ctx Context của engine.
/// @param text Chuỗi UTF-8.
/// @param size Cỡ chữ, pixel.
/// @param max_width Chiều rộng tối đa của một dòng, pixel.
/// @param font Font.
/// @return Các dòng, theo thứ tự.
std::vector<std::string> text_wrap(const context &ctx, const char *text, f32 size,
                                   f32 max_width, font_handle font = {});

/// Vẽ một đoạn chữ tự xuống dòng trong bề rộng `max_width`, góc trên trái tại
/// `pos`.
/// @param ctx Context của engine.
/// @param text Chuỗi UTF-8.
/// @param pos Góc trên trái.
/// @param size Cỡ chữ, pixel.
/// @param max_width Chiều rộng tối đa, pixel.
/// @param color Màu.
/// @param font Font.
/// @param line_spacing Khoảng cách dòng, nhân với chiều cao dòng. 1 là sát nhau.
/// @return Kích thước của khối chữ đã vẽ.
vec2 draw_text_wrapped(const context &ctx, const char *text, vec2 pos, f32 size,
                       f32 max_width, rgba color, font_handle font = {},
                       f32 line_spacing = 1.1f);
/// @}
} // namespace njin
