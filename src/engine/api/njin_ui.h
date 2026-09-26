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
  ui_look toast{};  ///< Nền và màu chữ của toast (chỉ dùng `normal` và `text`).

  /// Màu phủ làm tối nền phía sau popup.
  rgba dim{0.0f, 0.0f, 0.0f, 0.6f};
  /// Vạch màu bên trái toast theo loại: info, success, warning, error.
  rgba toast_accent[4] = {{0.26f, 0.56f, 0.98f, 1.0f}, {0.25f, 0.75f, 0.42f, 1.0f},
                          {0.96f, 0.72f, 0.20f, 1.0f}, {0.92f, 0.32f, 0.34f, 1.0f}};
  /// Góc màn hình toast bám vào, theo tỉ lệ: `{1, 1}` là góc dưới phải, `{0.5, 0}`
  /// là giữa cạnh trên. Toast xếp từ góc đó vào trong.
  vec2 toast_anchor{1.0f, 1.0f};
  vec2 toast_margin{24.0f, 24.0f}; ///< Khoảng từ mép màn hình, pixel (trước `scale`).
  f32 toast_seconds = 2.5f;        ///< Thời gian hiện mặc định.
  i32 toast_max = 5;               ///< Số toast tối đa cùng lúc. Cái cũ nhất bị bỏ.
  f32 toast_width = 420.0f;        ///< Chiều rộng tối đa, pixel (trước `scale`). Chữ dài tự xuống dòng.

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
/// cho đến ui_end(). Chiều cao tự tính theo nội dung. Panel cao hơn màn hình thì
/// tự thu nhỏ (cả chữ, tối đa còn một nửa) cho vừa, thay vì tràn ra trên và dưới.
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

/// Một dòng đổi phím cho màn hình cài đặt: bên trái là tên, bên phải là phím
/// đang gắn vào `action`. Bấm vào thì dòng chờ phím mới ("..."); phím (hoặc
/// nút chuột) bấm tiếp theo được gắn thay phím cũ bằng action_rebind(). Esc
/// hủy. Trong lúc chờ, UI không điều hướng.
/// @code
/// njin::ui_keybind(ctx, "Nhảy", g.jump);            // bàn phím
/// njin::ui_keybind(ctx, "Nhảy##pad", g.jump, true); // tay cầm
/// @endcode
/// Lưu phím mới bằng settings_save() (hoặc input_bindings_save()).
/// @param ctx Context của engine.
/// @param label Nhãn.
/// @param action Action cần đổi phím.
/// @param pad `true` để đổi nút tay cầm thay vì phím.
/// @return `true` ở frame phím vừa được đổi.
bool ui_keybind(njin_ctx &ctx, const char *label, action_handle action, bool pad = false);

/// Có dòng ui_keybind() nào đang chờ phím không. Trong lúc đó đừng coi Esc là
/// "đóng menu".
/// @param ctx Context của engine.
/// @return `true` nếu đang chờ.
bool ui_keybind_listening(const njin_ctx &ctx);

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

/// Loại toast: quyết định màu vạch bên trái (xem ui_style::toast_accent).
enum ui_toast_kind {
  ui_toast_info,    ///< Thông tin trung tính.
  ui_toast_success, ///< Việc vừa làm thành công.
  ui_toast_warning, ///< Cảnh báo.
  ui_toast_error,   ///< Lỗi.
};

/// Cách hiện một toast.
struct ui_toast_desc {
  ui_toast_kind kind = ui_toast_info; ///< Loại.
  f32 seconds = 0.0f; ///< Thời gian hiện. 0 là `ui_style::toast_seconds`.
};

/// Hiện một thông báo nhỏ ở góc màn hình, tự biến mất: "Đã lưu game", "Nhặt được
/// 5 vàng".
///
/// Gọi từ bất cứ đâu, bất cứ phase nào, **không cần** ui_begin. Engine tự xếp
/// hàng, trượt vào, mờ dần và vẽ đè lên mọi thứ trừ hiệu ứng chuyển scene. Chữ
/// dài tự xuống dòng.
///
/// Toast chỉ để đọc, **không nuốt phím hay chuột** của game, khác với panel của
/// UI. Thời gian tính theo giờ thật: vẫn chạy khi game đang pause hay hitstop.
/// Diện mạo lấy từ ui_style::toast, nên có thể dùng ảnh, 9-slice và shader như
/// mọi widget khác.
/// @code
/// njin::ui_toast(ctx, "Đã lưu game", {.kind = njin::ui_toast_success});
/// @endcode
/// @param ctx Context của engine.
/// @param text Nội dung (UTF-8).
/// @param desc Loại và thời gian hiện.
void ui_toast(njin_ctx &ctx, const char *text, const ui_toast_desc &desc = {});

/// Xóa mọi toast đang hiện, ví dụ khi đổi scene. @param ctx Context của engine.
void ui_toast_clear(njin_ctx &ctx);

/// Mô tả một popup, dùng với ui_popup() và ui_popup_begin().
struct ui_popup_desc {
  const char *id = "popup";     ///< Tên duy nhất của popup.
  const char *title = nullptr;  ///< Tiêu đề. Có thể null.
  const char *message = nullptr; ///< Nội dung, tự xuống dòng. Chỉ dùng với ui_popup().
  /// Nhãn các nút, tối đa 4, phần còn lại để null. Chỉ dùng với ui_popup().
  /// Từ 1 đến 3 nút xếp thành một hàng, 4 nút xếp dọc.
  const char *buttons[4] = {"OK", nullptr, nullptr, nullptr};
  i32 default_button = 0; ///< Nút được chọn sẵn cho bàn phím và tay cầm. Nên là nút an toàn.
  /// Số thứ tự trả về khi người chơi bấm quay lại (Esc, Backspace, B). -1 là
  /// đóng popup mà không báo nút nào.
  i32 cancel_button = -1;
  f32 width = 0.0f; ///< Chiều rộng, pixel. 0 là `ui_style::width`.
};

/// Một popup xác nhận: nền tối, tiêu đề, nội dung và vài nút.
///
/// Gọi mỗi frame trong `phase_post_render` khi popup đang mở; `open` do game
/// giữ. Popup tự đóng (đặt `open = false`) khi bấm một nút hay quay lại.
/// @code
/// if (want_quit) {
///   const njin::i32 pick = njin::ui_popup(ctx, {.id = "quit", .title = "Thoát game?",
///       .message = "Tiến trình chưa lưu sẽ mất.", .buttons = {"Ở lại", "Thoát"},
///       .cancel_button = 0}, want_quit);
///   if (pick == 1) njin::njin_quit(ctx);
/// }
/// @endcode
/// Popup là **modal**: các panel khác vẫn được vẽ nhưng không nhận chuột, phím
/// hay tay cầm cho đến khi popup đóng, và njin::ui_back() chỉ báo cho popup.
/// Khi mở, nút `default_button` được chọn sẵn; khi đóng, lựa chọn trở lại chỗ
/// cũ. Frame popup vừa mở bỏ qua nút quay lại, để cùng cú nhấn Esc vừa mở nó
/// không đóng nó ngay.
/// @param ctx Context của engine.
/// @param desc Mô tả popup.
/// @param open Popup đang mở. Được đặt `false` khi đóng.
/// @return Số thứ tự nút vừa được bấm ở frame này (theo `desc.buttons`,
/// `desc.cancel_button` nếu bấm quay lại), hoặc -1 nếu chưa có gì.
i32 ui_popup(njin_ctx &ctx, const ui_popup_desc &desc, bool &open);

/// Bắt đầu một popup có nội dung tùy ý: làm tối nền, mở panel modal. Gọi các
/// widget bình thường (ui_button, ui_slider...), rồi ui_popup_end(). Đóng popup
/// bằng cách không gọi nữa; dùng njin::ui_back() để bắt phím quay lại.
///
/// Chỉ dùng `id`, `title` và `width` của `desc`.
/// @param ctx Context của engine.
/// @param desc Mô tả popup.
void ui_popup_begin(njin_ctx &ctx, const ui_popup_desc &desc);

/// Kết thúc popup bắt đầu bằng ui_popup_begin(). @param ctx Context của engine.
void ui_popup_end(njin_ctx &ctx);
/// @}
} // namespace njin
