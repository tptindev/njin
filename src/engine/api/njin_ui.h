#pragma once
#include "_math.h"
#include "_types.h"
#include <initializer_list>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_ui
/// @{

/// Cách vẽ một mặt của widget: màu phẳng, ảnh (có thể 9-slice), hoặc cả hai.
///
/// Không có ảnh thì vẽ hình chữ nhật `color` (bo góc theo `roundness`). Có ảnh
/// thì vẽ ảnh nhân màu `color`; `border > 0` cắt ảnh thành 9 mảnh (9-slice):
/// bốn góc giữ nguyên, cạnh và giữa được kéo giãn, nên khung vẽ một lần dùng
/// cho mọi kích thước nút.
struct ui_skin {
  texture_handle texture{};          ///< Ảnh. Không hợp lệ thì vẽ màu phẳng.
  rect source{};                     ///< Vùng trong ảnh, pixel. Kích thước 0 là cả ảnh.
  f32 border = 0.0f;                 ///< Bề dày viền 9-slice trong ảnh, pixel. 0 là kéo giãn cả ảnh.
  rgba color{1.0f, 1.0f, 1.0f, 1.0f}; ///< Màu nền (không ảnh) hoặc màu nhân vào ảnh.
  f32 roundness = 0.0f;              ///< Bo góc khi không có ảnh, 0..1.
  rgba outline{0.0f, 0.0f, 0.0f, 0.0f}; ///< Màu viền khi không có ảnh. Alpha 0 là không viền.
  f32 outline_width = 0.0f;          ///< Bề dày viền, pixel.
};

/// Diện mạo của một loại widget ở từng trạng thái.
///
/// `shader` (tùy chọn) được bật khi vẽ các mặt của widget. Engine tự đặt các
/// uniform sau nếu shader có khai báo (không có thì bỏ qua, không cảnh báo):
///
/// | Uniform | Kiểu | Giá trị |
/// |---|---|---|
/// | `uiState` | float | 0 thường, 1 đang chọn, 2 đang nhấn, 3 bị tắt |
/// | `uiTime` | float | Giây từ lúc chạy, cho hiệu ứng động |
/// | `uiRect` | vec4 | x, y, rộng, cao của widget, pixel màn hình |
/// | `uiValue` | float | Tiến độ 0..1 của slider hay thanh tiến độ, còn lại 0 |
struct ui_look {
  ui_skin normal{};   ///< Bình thường.
  ui_skin focused{};  ///< Đang được chọn (chuột trỏ vào, hoặc bàn phím / tay cầm chọn tới).
  ui_skin pressed{};  ///< Đang bị nhấn giữ.
  ui_skin disabled{}; ///< Bị tắt.
  rgba text{1.0f, 1.0f, 1.0f, 1.0f};          ///< Màu chữ bình thường.
  rgba text_focused{1.0f, 1.0f, 1.0f, 1.0f};  ///< Màu chữ khi được chọn.
  rgba text_disabled{0.5f, 0.5f, 0.5f, 1.0f}; ///< Màu chữ khi bị tắt.
  shader_handle shader{}; ///< Shader tùy chỉnh cho mặt widget. Có thể để trống.
};

/// Toàn bộ giao diện của UI: phông chữ, kích thước, diện mạo từng loại widget,
/// âm thanh. Lấy bản mặc định bằng ui_default_style(), sửa, rồi ui_style_set().
struct ui_style {
  font_handle font{};    ///< Phông chữ. Mặc định là phông của engine.
  f32 font_size = 26.0f; ///< Cỡ chữ, pixel.
  f32 scale = 1.0f;      ///< Nhân mọi kích thước. Đặt theo chiều cao màn hình để UI co giãn.
  f32 padding = 18.0f;   ///< Khoảng từ mép panel vào nội dung.
  f32 spacing = 10.0f;   ///< Khoảng giữa hai widget.
  f32 widget_height = 46.0f; ///< Chiều cao một dòng widget.
  f32 width = 380.0f;    ///< Chiều rộng mặc định của panel.

  ui_look panel{};  ///< Nền panel (chỉ dùng `normal`) và màu chữ tiêu đề (`text`).
  ui_look label{};  ///< Chữ thường (chỉ dùng màu chữ).
  ui_look button{}; ///< Nút; cũng là nền dòng của toggle, slider, choice.
  ui_look track{};  ///< Rãnh slider, thanh tiến độ, ô của toggle.
  ui_look fill{};   ///< Phần đã đầy của slider, thanh tiến độ, dấu tích của toggle.
  ui_look knob{};   ///< Núm kéo của slider.

  sound_handle sound_move{};   ///< Phát khi chuyển lựa chọn. Có thể để trống.
  sound_handle sound_accept{}; ///< Phát khi bấm nút hoặc đổi giá trị.
  sound_handle sound_back{};   ///< Phát khi ui_back() trả về `true`.
};

/// Giao diện mặc định: panel tối trong suốt, nút bo góc, màu nhấn xanh.
/// @return Style mặc định.
ui_style ui_default_style();

/// Đặt style cho mọi widget vẽ sau đó. Đổi giữa chừng được (ví dụ một panel
/// cảnh báo màu đỏ).
/// @param ctx Context của engine.
/// @param style Style mới.
void ui_style_set(njin_ctx &ctx, const ui_style &style);

/// Style hiện tại. @param ctx Context của engine. @return Style.
ui_style ui_style_get(const njin_ctx &ctx);

/// Vị trí và kích thước của một panel, dùng với ui_begin().
struct ui_panel_desc {
  /// Tên duy nhất của panel. Dùng để nhớ kích thước và lựa chọn giữa các frame.
  const char *id = "panel";
  /// Tiêu đề vẽ ở đầu panel. Có thể null.
  const char *title = nullptr;
  /// Điểm neo trên màn hình, theo tỉ lệ: `{0.5, 0.5}` là giữa, `{0, 1}` là góc dưới trái.
  vec2 anchor{0.5f, 0.5f};
  /// Điểm của panel đặt vào `anchor`, theo tỉ lệ kích thước panel. Mặc định bằng tâm.
  vec2 pivot{0.5f, 0.5f};
  vec2 offset{};          ///< Dời thêm, pixel.
  f32 width = 0.0f;       ///< Chiều rộng, pixel. 0 là `ui_style::width`.
  bool background = true; ///< Vẽ nền panel.
};

/// Bắt đầu một panel. Các widget gọi sau đó xếp từ trên xuống bên trong nó,
/// cho đến ui_end(). Chiều cao tự tính theo nội dung.
///
/// Gọi trong `phase_post_render` (không gian màn hình). Chỉ panel nào được
/// gọi trong frame mới hiện; muốn ẩn menu thì đừng gọi nó.
/// @code
/// void menu(njin::njin_ctx &ctx) {
///   njin::ui_begin(ctx, {.id = "main", .title = "Tên game"});
///   if (njin::ui_button(ctx, "Chơi"))
///     njin::scene_fade(ctx, g.play);
///   if (njin::ui_button(ctx, "Cài đặt"))
///     g.settings_open = true;
///   if (njin::ui_button(ctx, "Thoát"))
///     njin::njin_quit(ctx);
///   njin::ui_end(ctx);
/// }
/// @endcode
/// @param ctx Context của engine.
/// @param desc Vị trí và kích thước.
void ui_begin(njin_ctx &ctx, const ui_panel_desc &desc = {});

/// Kết thúc panel và vẽ nó. @param ctx Context của engine.
void ui_end(njin_ctx &ctx);

/// Xếp `columns` widget tiếp theo thành một hàng ngang, chia đều bề rộng.
/// Mũi tên trái/phải di chuyển giữa chúng.
/// @param ctx Context của engine.
/// @param columns Số widget trong hàng.
void ui_row(njin_ctx &ctx, i32 columns);

/// Một dòng chữ. @param ctx Context của engine. @param text Chữ (UTF-8).
void ui_label(njin_ctx &ctx, const char *text);

/// Khoảng trống. @param ctx Context của engine. @param height Chiều cao, pixel (trước `scale`).
void ui_space(njin_ctx &ctx, f32 height);

/// Một nút bấm.
///
/// Nhãn phải khác nhau trong cùng panel; hai nút cùng chữ thì thêm hậu tố ẩn
/// sau `##`: `"Xóa##slot1"`, `"Xóa##slot2"` (phần sau `##` không được vẽ).
/// @param ctx Context của engine.
/// @param label Nhãn (UTF-8).
/// @param enabled `false` thì nút xám và không bấm được.
/// @return `true` ở frame nút được bấm (chuột, Enter, Space, nút A).
bool ui_button(njin_ctx &ctx, const char *label, bool enabled = true);

/// Một công tắc bật/tắt. Bấm để đổi.
/// @param ctx Context của engine.
/// @param label Nhãn.
/// @param value Giá trị, được sửa khi bấm.
/// @return `true` ở frame giá trị đổi.
bool ui_toggle(njin_ctx &ctx, const char *label, bool &value);

/// Một thanh trượt. Kéo bằng chuột, hoặc trái/phải khi đang được chọn.
/// @param ctx Context của engine.
/// @param label Nhãn.
/// @param value Giá trị, được sửa khi kéo.
/// @param min Giá trị nhỏ nhất.
/// @param max Giá trị lớn nhất.
/// @param step Bước khi dùng phím hay tay cầm, và làm tròn khi kéo. 0 là 1/20 khoảng.
/// @param percent Hiện giá trị dạng phần trăm của khoảng thay vì con số.
/// @return `true` ở frame giá trị đổi.
bool ui_slider(njin_ctx &ctx, const char *label, f32 &value, f32 min, f32 max,
               f32 step = 0.0f, bool percent = false);

/// Chọn một trong nhiều lựa chọn bằng trái/phải hoặc bấm: độ khó, độ phân
/// giải, ngôn ngữ.
/// @param ctx Context của engine.
/// @param label Nhãn.
/// @param index Lựa chọn hiện tại, được sửa khi đổi. Vòng lại ở hai đầu.
/// @param options Các lựa chọn.
/// @return `true` ở frame lựa chọn đổi.
bool ui_choice(njin_ctx &ctx, const char *label, i32 &index,
               std::initializer_list<const char *> options);

/// Một thanh tiến độ, không bấm được: máu, thời gian nạp.
/// @param ctx Context của engine.
/// @param value Tiến độ 0..1.
/// @param text Chữ vẽ giữa thanh. Có thể null.
void ui_progress(njin_ctx &ctx, f32 value, const char *text = nullptr);

/// Một ảnh, căn giữa trong panel.
/// @param ctx Context của engine.
/// @param texture Ảnh.
/// @param size Kích thước vẽ, pixel (trước `scale`).
/// @param source Vùng trong ảnh. Kích thước 0 là cả ảnh.
void ui_image(njin_ctx &ctx, texture_handle texture, vec2 size, rect source = {});

/// `true` ở frame người chơi bấm quay lại (Esc, Backspace, nút B) trong lúc
/// có panel đang hiện. Dùng để đóng menu con hay quay về màn trước.
/// @param ctx Context của engine.
/// @return `true` nếu vừa bấm quay lại.
bool ui_back(njin_ctx &ctx);

/// Chọn sẵn widget có nhãn `label` (trong panel đang mở), thường gọi khi vừa
/// mở menu để nút mặc định được chọn cho tay cầm.
/// @param ctx Context của engine.
/// @param label Nhãn đầy đủ, kể cả phần `##`.
void ui_focus(njin_ctx &ctx, const char *label);

/// Có panel nào được vẽ ở frame trước không. Trong lúc đó UI nhận các phím
/// điều hướng (mũi tên, Enter, Space, Esc) và game không thấy chúng, nên nhân
/// vật không chạy khi người chơi đang chọn menu.
/// @param ctx Context của engine.
/// @return `true` nếu UI đang hiện.
bool ui_active(const njin_ctx &ctx);
/// @}
} // namespace njin
