#pragma once
#include "_math.h"
#include "_types.h"
#include <initializer_list>
#include <span>
#include <string>

namespace njin {
struct context;

/// @addtogroup grp_ui
/// @{

/// Cách vẽ một mặt của widget: màu phẳng, ảnh (có thể 9-slice), hoặc cả hai.
///
/// Không có ảnh thì vẽ hình chữ nhật `color` (bo góc theo `radius`, hoặc
/// `roundness` khi `radius` là 0). Có ảnh
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
  /// Bán kính bo góc khi không có ảnh, pixel (nhân `ui_style::scale` như mọi
  /// kích thước khác). Lớn hơn 0 thì thay `roundness`: góc tròn bằng nhau dù
  /// widget cao hay thấp, còn `roundness` tính theo cạnh ngắn của widget.
  f32 radius = 0.0f;
  /// Làm mờ những gì dưới nó trước khi vẽ (draw_backdrop_blur()), bán kính
  /// pixel (nhân `ui_style::scale`). 0 là tắt. Chỉ khi không có ảnh; muốn thấy
  /// lớp mờ thì `color` nên hơi trong (alpha dưới 1).
  f32 blur = 0.0f;
  /// Góc 9-slice co theo chiều cao: có `border`, các góc vẽ bằng `border` nhân
  /// tỉ lệ chiều cao widget trên chiều cao vùng ảnh, thay vì đúng `border`
  /// pixel. Dùng cho ảnh nút vẽ to hơn chỗ nó chiếm (HUD theo canvas, vẽ ra
  /// màn hình 4K): góc thu lại cùng nút, ảnh chỉ bị thu nhỏ nên vẫn nét.
  /// `false` (mặc định) là như trước.
  bool border_fit = false;
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

  /// Bề rộng thanh cuộn của vùng cuộn (ui_scroll_begin()), pixel (trước
  /// `scale`), ở mép phải trong vùng. 0 (mặc định) là không có thanh, như
  /// trước. Lớn hơn 0 thì widget trong vùng chừa bên phải chừng ấy cộng nửa
  /// `spacing`, và thanh hiện khi vùng chứa nhiều hơn chiều cao của nó: kéo
  /// con trượt để cuộn, bấm vào rãnh thì con trượt nhảy tới đó.
  f32 scrollbar_width = 0.0f;
  /// Con trượt của thanh cuộn: `normal`, `focused` (chuột trỏ vào),
  /// `pressed` (đang kéo).
  ui_look scrollbar{};
  /// Rãnh của thanh cuộn, dưới con trượt. Alpha 0 thì không vẽ.
  ui_skin scrollbar_rail{};

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
  /// Thời gian (giây, giờ thật) ui_button() chuyển dần màu từ mặt thường sang
  /// mặt đang chọn khi chuột vào, và ngược lại khi chuột ra. 0 là đổi ngay.
  f32 hover_seconds = 0.0f;
  /// ui_button() đang bị nhấn giữ được vẽ thu lại theo tỉ lệ này quanh tâm,
  /// như bị ấn lún xuống (ví dụ `0.96`). 1 là giữ nguyên cỡ.
  f32 press_scale = 1.0f;

  /// Một tab của thanh tab (ui_tabs()) khi nó không mở: `normal`, `focused`
  /// (chuột trỏ vào), `pressed` (đang bị nhấn giữ); `text` và `text_focused`
  /// là màu chữ của nó. `disabled` không dùng. Mặt có alpha 0 thì không vẽ.
  ui_look tab{};
  /// Mặt của tab đang mở. Đổi tab thì nó trượt sang tab mới
  /// (`tab_slide_seconds`). Thanh đang được chọn bằng phím thì nó lấy viền
  /// của `tab.focused`.
  ui_skin tab_open{};
  rgba tab_open_text{1.0f, 1.0f, 1.0f, 1.0f}; ///< Màu chữ của tab đang mở.
  /// Nền sau cả thanh tab. Alpha 0 là không vẽ; có nền thì các tab lùi vào trong 3 pixel.
  ui_skin tab_bar{.color = {0.0f, 0.0f, 0.0f, 0.0f}};
  /// Vạch dưới tab đang mở, trượt theo nó. Alpha 0 là không vẽ.
  ui_skin tab_line{.color = {0.0f, 0.0f, 0.0f, 0.0f}};
  f32 tab_line_height = 3.0f;    ///< Bề dày vạch đó, pixel (trước `scale`).
  f32 tab_spacing = 4.0f;        ///< Khoảng giữa hai tab, pixel (trước `scale`).
  f32 tab_slide_seconds = 0.15f; ///< Thời gian (giây, giờ thật) mặt tab mở trượt sang tab mới. 0 là nhảy ngay.

  /// Thân công tắc của ui_toggle() riêng, khác rãnh slider: bật thì công tắc
  /// vẽ thân bằng `toggle_off` khi tắt và `toggle_on` khi bật (theo trạng thái
  /// widget như mọi `ui_look`), núm vẫn là `knob`. Tắt (mặc định) thì như
  /// trước: thân là `track`, thêm `fill` khi bật.
  bool toggle_own = false;
  ui_look toggle_off{}; ///< Thân công tắc khi tắt (khi `toggle_own`).
  ui_look toggle_on{};  ///< Thân công tắc khi bật (khi `toggle_own`).
};

/// Giao diện mặc định: panel tối trong suốt, nút bo góc, màu nhấn xanh.
/// @return Style mặc định.
ui_style ui_default_style();

/// Đặt style cho mọi widget vẽ sau đó. Đổi giữa chừng được (ví dụ một panel
/// cảnh báo màu đỏ).
/// @param ctx Context của engine.
/// @param style Style mới.
void ui_style_set(context &ctx, const ui_style &style);

/// Style hiện tại. @param ctx Context của engine. @return Style.
ui_style ui_style_get(const context &ctx);

/// Khung vẽ (canvas) của UI: UI thiết kế ở một chiều cao cố định rồi phóng
/// theo cửa sổ. Xem ui_canvas_set().
struct ui_canvas_desc {
  /// Chiều cao thiết kế, pixel. Tỉ lệ là chiều cao cửa sổ chia số này. 0 là
  /// tắt canvas (mặc định): UI tính bằng pixel cửa sổ như trước.
  f32 height = 0.0f;
  /// Canvas rộng ít nhất bấy nhiêu pixel thiết kế: cửa sổ hẹp quá thì tỉ lệ
  /// giảm theo chiều ngang thay vì cắt mất mép UI. 0 là không giới hạn.
  f32 min_width = 0.0f;
  f32 min_scale = 0.5f; ///< Tỉ lệ nhỏ nhất.
  f32 max_scale = 4.0f; ///< Tỉ lệ lớn nhất.
};

/// Bật/tắt khung vẽ UI (canvas). Bật thì giữa ui_canvas_begin() và
/// ui_canvas_end(), UI và HUD được viết theo đơn vị thiết kế: screen_size() trả
/// về cửa sổ chia tỉ lệ (cao bằng `height`, rộng theo khung hình cửa sổ),
/// mouse_pos() và mouse_delta() tính theo đơn vị đó, mọi hình, ảnh và panel vẽ
/// ra được phóng theo tỉ lệ, chữ được dựng đúng cỡ trên màn hình nên vẫn nét,
/// clip_begin() nhận vùng theo đơn vị thiết kế. Một HUD viết cho 1600 x 900 nhờ
/// đó vừa khít mọi cửa sổ, nhỏ hay 4K, không phải sửa từng kích thước.
///
/// Tỉ lệ tính lại mỗi frame theo cửa sổ (ui_canvas_scale()). Khi bật, mọi panel
/// UI phải dựng trong ui_canvas_begin()/ui_canvas_end() (UI đọc chuột theo đơn
/// vị canvas); toast cũng vẽ trong canvas. Không dùng chung với độ phân giải
/// ảo (window_set_virtual_size()): khi đó tỉ lệ luôn là 1.
/// @code
/// njin::ui_canvas_set(ctx, {.height = 900.0f, .min_width = 1280.0f});
/// // Mỗi frame, trong phase_post_render:
/// njin::ui_canvas_begin(ctx);
/// draw_hud(ctx); // tính theo 900 pixel chiều cao
/// njin::ui_canvas_end(ctx);
/// @endcode
/// @param ctx Context của engine.
/// @param desc Chiều cao thiết kế và giới hạn tỉ lệ.
void ui_canvas_set(context &ctx, const ui_canvas_desc &desc);

/// Bắt đầu vẽ trong canvas (xem ui_canvas_set()). Lồng được; canvas tắt thì
/// không làm gì. Vẽ vào render texture giữa chừng thì ảnh đó không bị phóng
/// (tọa độ vẫn theo đơn vị canvas). @param ctx Context của engine.
void ui_canvas_begin(const context &ctx);

/// Kết thúc ui_canvas_begin(). @param ctx Context của engine.
void ui_canvas_end(const context &ctx);

/// Tỉ lệ canvas của frame này: pixel cửa sổ trên một đơn vị thiết kế; 1 khi
/// tắt. @param ctx Context của engine. @return Tỉ lệ.
f32 ui_canvas_scale(const context &ctx);

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
  /// Panel nhận phím điều hướng và tay cầm (mũi tên, Enter, Space, Esc). `false`
  /// cho HUD luôn hiện trong lúc chơi: nút của nó chỉ bấm bằng chuột, không bao
  /// giờ được chọn sẵn, và các phím đó vẫn thuộc về game (xem ui_active()).
  bool navigable = true;
  /// Có thì vẽ một nút đóng ở góc trên phải panel; bấm vào (chỉ bằng chuột)
  /// thì `*open` thành `false`. Panel vẫn được vẽ hết frame đó; game thôi gọi
  /// nó từ frame sau. Null là không có nút đóng.
  bool *open = nullptr;
  /// Thời gian (giây, giờ thật) panel trượt vào và hiện dần, tính từ frame nó
  /// bắt đầu được vẽ (frame trước không có nó). 0 là hiện ngay. Panel ẩn đi
  /// rồi vẽ lại thì trượt vào lại.
  f32 enter_seconds = 0.0f;
  /// Lúc bắt đầu trượt vào, panel lệch chỗ của nó chừng này pixel rồi về dần,
  /// chậm ở cuối (ease::out_cubic); `{0, 0}` là chỉ hiện dần tại chỗ. Mọi thứ
  /// xếp trong panel đi theo, kể cả ui_last_rect(). Không dùng khi
  /// `enter_seconds` là 0.
  /// @code
  /// // Bảng công cụ trồi lên từ thanh công cụ bên dưới.
  /// njin::ui_begin(ctx, {.id = "tools", .anchor = {0.5f, 1.0f}, .pivot = {0.5f, 1.0f},
  ///                      .enter_seconds = 0.18f, .enter_from = {0.0f, 32.0f}});
  /// @endcode
  vec2 enter_from{};
  /// Panel đang rút đi, 0..1: 0 là bình thường, 1 là đã đi hẳn. Game tự đếm
  /// thời gian và vẫn gọi panel trong lúc nó rút (UI vẽ tức thì nên panel
  /// không gọi là biến mất ngay). Panel mờ dần, trượt về phía `enter_from`
  /// (nhanh dần, ease::in_cubic), và không nhận chuột: không nút nào bấm
  /// được, ui_mouse_over() không tính nó. Những gì game tự vẽ đè lên panel thì
  /// game làm mờ theo bằng draw_alpha_push().
  /// @code
  /// // Đóng bảng: còn vẽ 0.15 giây nữa, rút xuống và mờ đi.
  /// if (g.closing > 0.0f) {
  ///   g.closing = std::max(0.0f, g.closing - njin::delta_real(ctx));
  ///   const njin::f32 leaving = 1.0f - g.closing / 0.15f;
  ///   njin::ui_begin(ctx, {.id = "tools", .enter_from = {0.0f, 32.0f}, .leaving = leaving});
  ///   // ... các widget như lúc mở ...
  ///   njin::ui_end(ctx);
  /// }
  /// @endcode
  f32 leaving = 0.0f;
  /// Panel chỉ để xem: vẽ như thường nhưng không nhận chuột, không nút nào bấm
  /// được, ui_mouse_over() không tính nó. Ví dụ một panel đang được game vẽ
  /// biến dạng (thu vào một icon) trong lúc nó đóng.
  bool inert = false;
};

/// Bắt đầu một panel. Các widget gọi sau đó xếp từ trên xuống bên trong nó,
/// cho đến ui_end(). Chiều cao tự tính theo nội dung. Panel cao hơn chỗ nó có
/// ở vị trí của nó (theo `anchor`, `pivot`, `offset`: panel treo trên mép dưới
/// chỉ có phần màn hình phía trên điểm đó) thì tự thu nhỏ (cả chữ, tối đa còn
/// một nửa) cho vừa, thay vì tràn ra ngoài màn hình. Thu hết cỡ vẫn không vừa
/// thì panel được dời xuống để đầu nó (tiêu đề, nút đóng) vẫn trên màn hình.
///
/// Gọi trong `phase_post_render` (không gian màn hình). Chỉ panel nào được
/// gọi trong frame mới hiện; muốn ẩn menu thì đừng gọi nó.
/// @code
/// void menu(njin::context &ctx) {
///   njin::ui_begin(ctx, {.id = "main", .title = "Tên game"});
///   if (njin::ui_button(ctx, "Chơi"))
///     njin::scene_fade(ctx, g.play);
///   if (njin::ui_button(ctx, "Cài đặt"))
///     g.settings_open = true;
///   if (njin::ui_button(ctx, "Thoát"))
///     njin::quit(ctx);
///   njin::ui_end(ctx);
/// }
/// @endcode
/// @param ctx Context của engine.
/// @param desc Vị trí và kích thước.
void ui_begin(context &ctx, const ui_panel_desc &desc = {});

/// Kết thúc panel và vẽ nó. @param ctx Context của engine.
void ui_end(context &ctx);

/// Xếp `columns` widget tiếp theo thành một hàng ngang, chia đều bề rộng.
/// Mũi tên trái/phải di chuyển giữa chúng.
/// @param ctx Context của engine.
/// @param columns Số widget trong hàng.
void ui_row(context &ctx, i32 columns);

/// Một dòng chữ. Trong một hàng (ui_row()) nó cao bằng một widget và chữ nằm giữa
/// theo chiều dọc, thẳng hàng với nút bên cạnh.
/// @param ctx Context của engine.
/// @param text Chữ (UTF-8).
void ui_label(context &ctx, const char *text);

/// Khoảng trống. @param ctx Context của engine. @param height Chiều cao, pixel (trước `scale`).
void ui_space(context &ctx, f32 height);

/// Một nút bấm.
///
/// Nhãn phải khác nhau trong cùng panel; hai nút cùng chữ thì thêm hậu tố ẩn
/// sau `##`: `"Xóa##slot1"`, `"Xóa##slot2"` (phần sau `##` không được vẽ).
/// @param ctx Context của engine.
/// @param label Nhãn (UTF-8).
/// @param enabled `false` thì nút xám và không bấm được.
/// @param selected `true` thì nút giữ mặt "đang nhấn" (ui_look::pressed) khi
/// không có gì khác: một tab đang mở, một công cụ đang bật, tốc độ đang chọn.
/// @return `true` ở frame nút được bấm (chuột, Enter, Space, nút A).
bool ui_button(context &ctx, const char *label, bool enabled = true, bool selected = false);

/// Một công tắc bật/tắt. Bấm để đổi.
/// @param ctx Context của engine.
/// @param label Nhãn.
/// @param value Giá trị, được sửa khi bấm.
/// @return `true` ở frame giá trị đổi.
bool ui_toggle(context &ctx, const char *label, bool &value);

/// Một thanh trượt. Kéo bằng chuột, hoặc trái/phải khi đang được chọn.
/// @param ctx Context của engine.
/// @param label Nhãn.
/// @param value Giá trị, được sửa khi kéo.
/// @param min Giá trị nhỏ nhất.
/// @param max Giá trị lớn nhất.
/// @param step Bước khi dùng phím hay tay cầm, và làm tròn khi kéo. 0 là 1/20 khoảng.
/// @param percent Hiện giá trị dạng phần trăm của khoảng thay vì con số.
/// @return `true` ở frame giá trị đổi.
bool ui_slider(context &ctx, const char *label, f32 &value, f32 min, f32 max,
               f32 step = 0.0f, bool percent = false);

/// Chọn một trong nhiều lựa chọn bằng trái/phải hoặc bấm: độ khó, độ phân
/// giải, ngôn ngữ.
/// @param ctx Context của engine.
/// @param label Nhãn.
/// @param index Lựa chọn hiện tại, được sửa khi đổi. Vòng lại ở hai đầu.
/// @param options Các lựa chọn.
/// @return `true` ở frame lựa chọn đổi.
bool ui_choice(context &ctx, const char *label, i32 &index,
               std::initializer_list<const char *> options);

/// Chọn một trong nhiều lựa chọn (danh sách động).
/// @param ctx Context của engine.
/// @param label Nhãn.
/// @param index Lựa chọn hiện tại.
/// @param options Mảng các lựa chọn dạng chuỗi.
/// @return `true` ở frame lựa chọn đổi.
bool ui_choice(context &ctx, const char *label, i32 &index,
               std::span<const std::string> options);

/// Cách vẽ một thanh tab bằng ui_tabs().
struct ui_tabs_desc {
  f32 height = 0.0f;    ///< Chiều cao, pixel (trước `scale`). 0 là ui_style::widget_height.
  f32 font_size = 0.0f; ///< Cỡ chữ, pixel (trước `scale`). 0 là ui_style::font_size.
  /// Chừa một ô vuông cạnh này (pixel, trước `scale`) bên trái chữ của mỗi
  /// tab, để game tự vẽ icon vào đó (lấy chỗ bằng ui_tabs_icon_rect()). 0 là
  /// không chừa.
  f32 icon_size = 0.0f;
};

/// Một thanh tab: các tab chia đều một dòng (hay một cột của ui_row()), đúng
/// một tab mở. Bấm một tab để mở nó; khi thanh được chọn bằng phím hay tay
/// cầm, trái/phải sang tab bên cạnh, Enter sang tab sau. Mặt tab mở
/// (ui_style::tab_open) trượt sang tab mới. Chữ rộng hơn tab thì được thu nhỏ
/// cho vừa. Dùng cho các trang của một bảng thay vì một hàng ui_button().
/// @code
/// static njin::i32 page = 0;
/// njin::ui_tabs(ctx, "bank_tabs", page, {"Tổng quan", "Sao kê", "Tiết kiệm"});
/// if (page == 0)
///   overview(ctx);
/// @endcode
/// @param ctx Context của engine.
/// @param id Tên của thanh, khác nhau trong cùng panel (không vẽ ra).
/// @param index Tab đang mở, được sửa khi đổi.
/// @param labels Chữ của các tab; phần sau `##` không vẽ.
/// @param desc Chiều cao, cỡ chữ, chỗ cho icon.
/// @return `true` ở frame tab mở đổi.
bool ui_tabs(context &ctx, const char *id, i32 &index, std::initializer_list<const char *> labels,
             const ui_tabs_desc &desc = {});

/// Một thanh tab với chữ là danh sách động (ví dụ một tab kèm số việc đang chờ).
/// @param ctx Context của engine.
/// @param id Tên của thanh.
/// @param index Tab đang mở.
/// @param labels Chữ của các tab.
/// @param desc Chiều cao, cỡ chữ, chỗ cho icon.
/// @return `true` ở frame tab mở đổi.
bool ui_tabs(context &ctx, const char *id, i32 &index, std::span<const std::string> labels,
             const ui_tabs_desc &desc = {});

/// Ô icon của tab `tab` trong thanh tab vẽ gần nhất (ui_tabs_desc::icon_size),
/// tọa độ màn hình như ui_last_rect(). Game vẽ icon vào đó, tô màu theo tab
/// nào đang mở.
/// @param ctx Context của engine.
/// @param tab Thứ tự của tab.
/// @return Ô icon; rỗng khi không có tab đó hay thanh không chừa icon.
rect ui_tabs_icon_rect(const context &ctx, i32 tab);

/// Một thanh tiến độ, không bấm được: máu, thời gian nạp.
/// @param ctx Context của engine.
/// @param value Tiến độ 0..1.
/// @param text Chữ vẽ giữa thanh. Có thể null.
/// @param fill Màu phần đầy, cho thanh mang nghĩa riêng (máu đỏ khi thấp).
/// Alpha 0: dùng ui_style::fill.
void ui_progress(context &ctx, f32 value, const char *text = nullptr, rgba fill = {0.0f, 0.0f, 0.0f, 0.0f});

/// Cách vẽ một dòng chữ bằng ui_text().
struct ui_text_desc {
  rgba color{0.0f, 0.0f, 0.0f, 0.0f}; ///< Màu. Alpha 0: màu của ui_label() (ui_style::label).
  f32 size = 0.0f;  ///< Cỡ chữ, pixel (trước `scale`). 0 là ui_style::font_size.
  f32 align = 0.0f; ///< Căn ngang trong ô: 0 trái, 0.5 giữa, 1 phải.
};

/// Một dòng chữ như ui_label(), có màu, cỡ và căn lề riêng: số tiền xanh hay
/// đỏ, một giá trị căn phải cạnh tên của nó trong một hàng.
/// @code
/// njin::ui_row(ctx, 2);
/// njin::ui_label(ctx, "Tiền");
/// njin::ui_text(ctx, "1500k", {.color = {0.5f, 0.8f, 0.4f, 1.0f}, .align = 1.0f});
/// @endcode
/// @param ctx Context của engine.
/// @param text Chữ (UTF-8).
/// @param desc Màu, cỡ, căn lề.
void ui_text(context &ctx, const char *text, const ui_text_desc &desc = {});

/// Một chú thích nhỏ cạnh con trỏ chuột, vẽ đè lên mọi panel ở cuối frame. Gọi
/// mỗi frame còn muốn hiện (thường khi chuột đang trỏ vào thứ cần giải thích);
/// không gọi nữa thì nó biến mất. Gọi được từ bất cứ đâu, không cần ui_begin.
/// @code
/// const bool pause = njin::ui_button(ctx, "||");
/// if (njin::point_in_rect(njin::mouse_pos(ctx), njin::ui_last_rect(ctx)))
///   njin::ui_tooltip(ctx, "Dừng (Space)");
/// @endcode
/// @param ctx Context của engine.
/// @param text Chữ (UTF-8).
void ui_tooltip(context &ctx, const char *text);

/// Bắt đầu một vùng cuộn cao `height` trong panel đang mở: các widget gọi sau
/// đó, đến ui_scroll_end(), xếp trong vùng này và chỉ hiện phần nằm trong nó.
/// Bánh xe chuột trên vùng cuộn nó (game không thấy bánh xe đó); một widget
/// được chọn bằng phím mà nằm ngoài khung nhìn thì vùng tự cuộn tới.
/// @code
/// njin::ui_scroll_begin(ctx, "list", 240.0f);
/// for (const shop &s : shops)
///   if (njin::ui_button(ctx, s.name.c_str()))
///     open_shop(s);
/// njin::ui_scroll_end(ctx);
/// @endcode
/// @param ctx Context của engine.
/// @param id Tên của vùng, duy nhất trong panel: nó nhớ đã cuộn tới đâu.
/// @param height Chiều cao, pixel (trước `scale`).
void ui_scroll_begin(context &ctx, const char *id, f32 height);

/// Như ui_scroll_begin(), nhưng vùng cao vừa đủ những gì nó chứa ở frame
/// trước, không quá `max_height`: danh sách ngắn thì panel thấp, dài thì
/// cuộn. Frame đầu tiên (chưa biết nó chứa gì) vùng cao `max_height`.
/// @code
/// // Cửa sổ thông tin không dài quá chỗ trống phía trên điện thoại.
/// njin::ui_scroll_begin_fit(ctx, "body", phone_top - njin::ui_last_rect(ctx).pos.y);
/// @endcode
/// @param ctx Context của engine.
/// @param id Tên của vùng, duy nhất trong panel.
/// @param max_height Chiều cao lớn nhất, pixel (trước `scale`).
void ui_scroll_begin_fit(context &ctx, const char *id, f32 max_height);

/// Kết thúc vùng cuộn bắt đầu bằng ui_scroll_begin(). @param ctx Context của engine.
void ui_scroll_end(context &ctx);

/// Cuộn vùng `id` xuống cuối, một lần: khung chat mở ra ở tin mới nhất, hay
/// khi có tin mới. Gọi trong panel chứa vùng đó, trước hoặc trong lúc nó mở;
/// người chơi vẫn cuộn lên được sau đó.
/// @param ctx Context của engine.
/// @param id Tên của vùng, như đã đưa cho ui_scroll_begin().
void ui_scroll_to_end(context &ctx, const char *id);

/// Mô tả một thanh tiến độ hình tròn, dùng với ui_progress_circle().
/// Mọi cỡ tính bằng pixel trước `ui_style::scale`. Màu có alpha 0 thì lấy từ ui_style.
struct ui_circle_desc {
  f32 value = 0.0f;       ///< Tiến độ 0..1.
  f32 diameter = 96.0f;   ///< Đường kính ngoài.
  f32 thickness = 10.0f;  ///< Độ dày của vòng. Từ nửa đường kính trở lên thì thành hình tròn đặc.
  /// Điểm bắt đầu, độ, tính từ đỉnh (12 giờ) theo chiều kim đồng hồ: 90 là 3 giờ, -90 là 9 giờ.
  f32 start_angle = 0.0f;
  bool clockwise = true;   ///< `false`: đầy dần theo chiều ngược kim đồng hồ.
  bool round_caps = false; ///< Hai đầu của phần đầy bo tròn.
  bool show_track = true;  ///< Vẽ vòng nền dưới phần đầy.
  rgba track{0.0f, 0.0f, 0.0f, 0.0f}; ///< Màu vòng nền. Alpha 0: dùng ui_style::track.
  rgba fill{0.0f, 0.0f, 0.0f, 0.0f};  ///< Màu phần đầy. Alpha 0: dùng ui_style::fill.
  const char *text = nullptr;         ///< Chữ giữa vòng. Null thì xem `percent`.
  bool percent = false;               ///< Khi `text` là null: vẽ tiến độ dạng "75%" giữa vòng.
  rgba text_color{0.0f, 0.0f, 0.0f, 0.0f}; ///< Màu chữ. Alpha 0: dùng ui_style::panel.text.
};

/// Một thanh tiến độ hình tròn, không bấm được: hồi chiêu, nạp đạn, máu dạng vòng.
/// Chiếm một dòng cao bằng đường kính, căn giữa trong panel (hoặc trong cột của ui_row()).
/// @code
/// njin::ui_progress_circle(ctx, {.value = cooldown, .diameter = 64, .thickness = 8,
///                                .round_caps = true, .fill = {0.9f, 0.3f, 0.2f, 1.0f}});
/// @endcode
/// @param ctx Context của engine.
/// @param desc Giá trị và kiểu dáng.
void ui_progress_circle(context &ctx, const ui_circle_desc &desc);

/// Một ảnh, căn giữa trong panel.
/// @param ctx Context của engine.
/// @param texture Ảnh.
/// @param size Kích thước vẽ, pixel (trước `scale`).
/// @param source Vùng trong ảnh. Kích thước 0 là cả ảnh.
void ui_image(context &ctx, texture_handle texture, vec2 size, rect source = {});

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
bool ui_keybind(context &ctx, const char *label, action_handle action, bool pad = false);

/// Có dòng ui_keybind() nào đang chờ phím không. Trong lúc đó đừng coi Esc là
/// "đóng menu".
/// @param ctx Context của engine.
/// @return `true` nếu đang chờ.
bool ui_keybind_listening(const context &ctx);

/// Cách một ô nhập chữ hiện và giới hạn, dùng với ui_input().
struct ui_input_desc {
  /// Chữ mờ hiện khi ô trống và chưa gõ. Null: phần nhãn trước `##`.
  const char *hint = nullptr;
  /// Số ký tự (không phải byte) tối đa. 0 là không giới hạn.
  i32 max_chars = 64;
};

/// Một ô nhập chữ một dòng: ô tìm kiếm, tên nhân vật, tên file lưu.
///
/// Bấm vào ô (hay Enter khi đang chọn nó) thì bắt đầu gõ. Trong lúc gõ, bàn
/// phím thuộc về ô này: game không thấy phím nào được nhấn hay giữ (kể cả
/// WASD và Esc), ui_typing() trả về `true`. Chữ đến từ text_char(), tức đã qua
/// bố cục bàn phím và bộ gõ của hệ điều hành, nên gõ được tiếng Việt có dấu.
/// Backspace (giữ thì lặp) xóa từng ký tự cuối. Enter, Esc hay bấm chuột ra
/// ngoài ô thì thôi gõ, chữ vẫn giữ. Ô không được vẽ nữa (menu đóng) cũng thôi.
/// Chữ dài hơn ô thì chỉ hiện phần cuối.
/// @code
/// static std::string search;
/// njin::ui_begin(ctx, {.id = "shop"});
/// if (njin::ui_input(ctx, "Tìm##search", search, {.hint = "Tên món đồ"}))
///   filter(search);
/// njin::ui_end(ctx);
/// @endcode
/// @param ctx Context của engine.
/// @param label Nhãn (UTF-8), là định danh của ô; phần trước `##` là chữ gợi ý
/// mặc định.
/// @param text Chữ trong ô, UTF-8; ô sửa thẳng vào đây.
/// @param desc Chữ gợi ý và giới hạn.
/// @return `true` ở frame chữ đổi.
bool ui_input(context &ctx, const char *label, std::string &text, const ui_input_desc &desc = {});

/// Có ô ui_input() nào đang được gõ không. Trong lúc đó game không nhận phím
/// nào; dùng hàm này để không coi phím là phím tắt khi đọc chúng theo cách khác.
/// @param ctx Context của engine.
/// @return `true` nếu đang gõ.
bool ui_typing(const context &ctx);

/// `true` ở frame người chơi bấm quay lại (Esc, Backspace, nút B) trong lúc
/// có panel đang hiện. Dùng để đóng menu con hay quay về màn trước.
/// @param ctx Context của engine.
/// @return `true` nếu vừa bấm quay lại.
bool ui_back(context &ctx);

/// Chọn sẵn widget có nhãn `label` (trong panel đang mở), thường gọi khi vừa
/// mở menu để nút mặc định được chọn cho tay cầm.
/// @param ctx Context của engine.
/// @param label Nhãn đầy đủ, kể cả phần `##`.
void ui_focus(context &ctx, const char *label);

/// Có panel nào nhận phím được vẽ ở frame trước không (panel HUD với
/// `navigable = false` không tính). Trong lúc đó UI nhận các phím điều hướng
/// (mũi tên, Enter, Space, Esc) và game không thấy chúng, nên nhân vật không
/// chạy khi người chơi đang chọn menu.
/// @param ctx Context của engine.
/// @return `true` nếu UI đang hiện.
bool ui_active(const context &ctx);

/// Khung của widget vừa đặt trong panel đang mở (ui_label(), ui_button(),
/// ui_space()...), pixel màn hình. Dùng để vẽ thêm lên một widget: hình trong
/// nút, bản đồ nhỏ trong một khoảng ui_space(). Panel chỉ được vẽ ở ui_end(),
/// nên phần vẽ thêm phải đặt sau ui_end(), không thì nền panel đè lên.
/// @code
/// njin::ui_begin(ctx, {.id = "shop"});
/// const bool buy = njin::ui_button(ctx, "##sword");
/// const njin::rect icon = njin::ui_last_rect(ctx);
/// njin::ui_end(ctx);
/// njin::texture_draw(ctx, g.sword, icon.pos);
/// @endcode
/// @param ctx Context của engine.
/// @return Khung của widget đặt gần nhất.
rect ui_last_rect(const context &ctx);

/// Khung của panel vừa đóng bằng ui_end() gần nhất: vị trí và kích thước thật
/// của nó, cả chiều cao nội dung. Dùng khi game cần vẽ thêm theo cả panel,
/// như hiệu ứng mở panel từ một icon.
/// @param ctx Context của engine.
/// @return Khung của panel, pixel màn hình.
rect ui_panel_rect(const context &ctx);

/// Khung của panel `id` ở frame trước (các panel đã vẽ xong, như ui_on_top()).
/// Dùng khi game cần biết các cửa sổ của nó có chồng lên nhau không, như làm
/// mờ cửa sổ bị một cửa sổ khác đè lên.
/// @param ctx Context của engine.
/// @param id Tên panel, như ui_panel_desc::id.
/// @return Khung của panel, pixel màn hình; kích thước 0 nếu frame trước nó không được vẽ.
rect ui_panel_rect(const context &ctx, const char *id);

/// Chuột có đang nằm trên một panel không (các panel của frame trước). Ở đó UI
/// đã nuốt nút chuột trái; hàm này để game bỏ qua cả nút phải, bánh xe hay con
/// trỏ riêng của nó, ví dụ không ra lệnh cho quân khi bấm chuột phải lên HUD.
/// @param ctx Context của engine.
/// @return `true` nếu chuột đang trên một panel.
bool ui_mouse_over(const context &ctx);

/// Các panel chồng lên nhau theo thứ tự vẽ: panel vẽ sau nằm trên. Ở chỗ con
/// trỏ, chỉ panel trên cùng nhận chuột (nút, nút đóng, bánh xe cuộn, vùng
/// ui_let_through() đặt trong nó); panel bị che bên dưới thì không. Muốn đổi
/// panel nào nằm trên, game đổi thứ tự gọi chúng.
///
/// Hàm này cho biết chuột có đang trên panel `id` và panel đó nằm trên cùng
/// ở chỗ ấy không (theo các panel của frame trước). Dùng cho chuột game tự xử
/// lý quanh một panel, như kéo panel đi bằng mép của nó.
/// @param ctx Context của engine.
/// @param id Tên panel, như ui_panel_desc::id.
/// @return `true` nếu panel `id` nằm trên cùng dưới con trỏ.
bool ui_on_top(const context &ctx, const char *id);

/// Nút chuột trái vừa bấm xuống frame này lên panel `id`, ở chỗ nó nằm trên
/// cùng (xem ui_on_top()). Dùng để đưa một cửa sổ game tự sắp lên trên khi
/// người chơi bấm vào nó.
/// @code
/// if (njin::ui_pressed_on(ctx, "inventory"))
///   g.inventory_on_top = true; // từ frame sau gọi nó sau cùng
/// @endcode
/// @param ctx Context của engine.
/// @param id Tên panel, như ui_panel_desc::id.
/// @return `true` nếu vừa bấm lên panel `id`.
bool ui_pressed_on(const context &ctx, const char *id);

/// Panel đang dựng (giữa ui_begin() và ui_end()), hoặc panel vừa đóng nếu
/// không có panel nào mở, đang bị một panel vẽ sau nó che ở chỗ con trỏ: các
/// widget của nó không nhận chuột. Dùng cho thứ game tự vẽ và tự đọc chuột
/// trong panel (`point_in_rect(mouse_pos(ctx), ...)`), để nó cũng không phản
/// ứng qua một panel đang che.
/// @param ctx Context của engine.
/// @return `true` nếu panel bị che ở chỗ con trỏ.
bool ui_covered(const context &ctx);

/// Để nút chuột trái lọt qua UI ở vùng `area` (pixel màn hình), dù vùng đó
/// nằm trong một panel: game vẫn đọc được mouse_pressed(), mouse_held(),
/// mouse_released() ở đó. Dùng cho thứ game tự vẽ và tự xử lý chuột bên trong
/// một panel, như một bản đồ nhỏ kéo bằng chuột.
///
/// Gọi mỗi frame còn muốn để lọt (thường ngay khi đặt vùng đó trong panel,
/// lấy khung bằng ui_last_rect()); có hiệu lực từ frame sau, như việc panel
/// nuốt chuột. ui_mouse_over() vẫn trả về `true` ở đó, nên phần game bỏ qua
/// chuột trên HUD (chọn quân, ra lệnh) vẫn bỏ qua. Chuột ra khỏi vùng thì UI
/// lại nuốt nút trái: game thấy nút như đã thả.
/// @code
/// njin::ui_space(ctx, 200.0f);
/// const njin::rect map = njin::ui_last_rect(ctx);
/// njin::ui_let_through(ctx, map);
/// njin::ui_end(ctx);
/// if (njin::mouse_held(ctx, njin::mouse_left) && njin::point_in_rect(njin::mouse_pos(ctx), map))
///   g.map_centre -= njin::mouse_delta(ctx) / g.map_zoom;
/// @endcode
/// @param ctx Context của engine.
/// @param area Vùng để chuột lọt qua, pixel màn hình.
void ui_let_through(context &ctx, rect area);

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
void ui_toast(context &ctx, const char *text, const ui_toast_desc &desc = {});

/// Xóa mọi toast đang hiện, ví dụ khi đổi scene. @param ctx Context của engine.
void ui_toast_clear(context &ctx);

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
///   if (pick == 1) njin::quit(ctx);
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
i32 ui_popup(context &ctx, const ui_popup_desc &desc, bool &open);

/// Bắt đầu một popup có nội dung tùy ý: làm tối nền, mở panel modal. Gọi các
/// widget bình thường (ui_button, ui_slider...), rồi ui_popup_end(). Đóng popup
/// bằng cách không gọi nữa; dùng njin::ui_back() để bắt phím quay lại.
///
/// Chỉ dùng `id`, `title` và `width` của `desc`.
/// @param ctx Context của engine.
/// @param desc Mô tả popup.
void ui_popup_begin(context &ctx, const ui_popup_desc &desc);

/// Kết thúc popup bắt đầu bằng ui_popup_begin(). @param ctx Context của engine.
void ui_popup_end(context &ctx);
/// @}
} // namespace njin
