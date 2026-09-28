#pragma once
#include "_math.h"
#include "_types.h"
#include "njin_json.h"
#include "njin_ui.h"
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace njin {
struct njin_ctx;

/// @addtogroup grp_ui
/// @{

/// Loại widget trong layout UI khai báo.
enum class ui_widget_kind {
  label,    ///< Dòng chữ.
  space,    ///< Khoảng trống dọc.
  button,   ///< Nút bấm.
  toggle,   ///< Công tắc bật/tắt.
  slider,   ///< Thanh trượt giá trị.
  choice,   ///< Danh sách lựa chọn.
  progress, ///< Thanh tiến độ.
  image,    ///< Ảnh tĩnh.
  row,      ///< Xếp hàng ngang cho các widget tiếp theo.
  keybind,  ///< Dòng gán phím điều khiển.
};

/// Dữ liệu cấu hình và trạng thái của một widget.
struct ui_widget_data {
  ui_widget_kind kind = ui_widget_kind::label; ///< Loại widget.
  std::string id;       ///< Mã định danh duy nhất (dùng để truy vấn sự kiện/giá trị).
  std::string label;    ///< Nhãn hiển thị trên widget (hỗ trợ ## để ẩn id).
  std::string text;     ///< Chuỗi phụ hoặc chữ vẽ trên progress bar.
  bool enabled = true;  ///< Trạng thái kích hoạt.

  // Giá trị tương tác
  bool bool_val = false;       ///< Trạng thái toggle.
  f32 float_val = 0.0f;        ///< Giá trị slider hoặc progress (0..1).
  f32 min_val = 0.0f;          ///< Giá trị nhỏ nhất của slider.
  f32 max_val = 1.0f;          ///< Giá trị lớn nhất của slider.
  f32 step = 0.0f;             ///< Bước nhảy của slider.
  bool percent = false;        ///< Hiển thị dạng % thay vì con số.

  i32 int_val = 0;                     ///< Lựa chọn hiện tại của choice (index).
  std::vector<std::string> options;    ///< Các lựa chọn cho choice.

  // Kích thước & bố cục
  f32 height = 10.0f;          ///< Chiều cao (dùng cho kind == space).
  i32 columns = 2;             ///< Số cột (dùng cho kind == row).

  // Ảnh (kind == image)
  std::string texture_path;    ///< Đường dẫn file ảnh.
  texture_handle texture{};    ///< Handle ảnh khi đã nạp.
  vec2 size{64.0f, 64.0f};     ///< Kích thước vẽ ảnh.
  rect source{};               ///< Vùng cắt trong ảnh.

  // Keybind (kind == keybind)
  std::string action_name;     ///< Tên action cần rebind.
  bool pad = false;            ///< Rebind nút tay cầm thay vì phím.

  // Trạng thái frame vừa qua
  bool clicked = false;        ///< Vừa được bấm trong frame này (đối với button).
  bool changed = false;        ///< Giá trị vừa thay đổi trong frame này (slider, toggle, choice).
};

/// Dữ liệu của một panel chứa danh sách widget.
struct ui_panel_data {
  std::string id = "panel";    ///< Tên duy nhất của panel.
  std::string title;           ///< Tiêu đề ở đầu panel.
  vec2 anchor{0.5f, 0.5f};     ///< Tỉ lệ neo trên màn hình ({0.5, 0.5} là giữa).
  vec2 pivot{0.5f, 0.5f};      ///< Điểm đặt của panel vào anchor ({0.5, 0.5} là tâm).
  vec2 offset{0.0f, 0.0f};     ///< Độ dời pixel từ anchor.
  f32 width = 0.0f;            ///< Chiều rộng panel (0 là dùng ui_style::width).
  bool background = true;      ///< Có vẽ nền panel hay không.
  bool visible = true;         ///< Hiển thị panel khi vẽ layout.
  std::vector<ui_widget_data> widgets; ///< Danh sách các widget trong panel.

  /// Tìm widget theo ID trong panel này.
  /// @param widget_id ID của widget.
  /// @return Con trỏ tới widget, hoặc `nullptr` nếu không có.
  ui_widget_data *find_widget(std::string_view widget_id);
  /// @copydoc find_widget(std::string_view)
  const ui_widget_data *find_widget(std::string_view widget_id) const;
};

/// Dữ liệu của một popup thông báo hoặc xác nhận.
struct ui_popup_data {
  std::string id = "popup";                ///< Tên duy nhất của popup.
  std::string title;                       ///< Tiêu đề.
  std::string message;                     ///< Nội dung, tự xuống dòng.
  std::vector<std::string> buttons{"OK"};  ///< Nhãn các nút, tối đa 4.
  i32 default_button = 0;                  ///< Nút được chọn sẵn khi mở.
  i32 cancel_button = -1;                  ///< Nút trả về khi bấm quay lại, -1 là không có.
  f32 width = 0.0f;                        ///< Chiều rộng (0 là dùng ui_style::width).
  bool open = false;           ///< Trạng thái đang mở hay đóng.
};

/// Toàn bộ bố cục UI nạp từ file JSON hoặc xuất từ editor.
struct ui_layout {
  i32 version = 1;                         ///< Phiên bản cấu trúc data UI.
  vec2 design_resolution{1280.0f, 720.0f}; ///< Độ phân giải thiết kế chuẩn.
  ui_style style = ui_default_style();     ///< Style giao diện mặc định.
  bool custom_style = false;               ///< Có áp dụng custom style trong layout không.
  std::vector<ui_panel_data> panels;       ///< Danh sách các panel.
  std::vector<ui_popup_data> popups;       ///< Danh sách các popup.

  /// Tìm panel theo ID.
  /// @param panel_id ID của panel.
  /// @return Con trỏ tới panel, hoặc `nullptr` nếu không có.
  ui_panel_data *find_panel(std::string_view panel_id);
  /// @copydoc find_panel(std::string_view)
  const ui_panel_data *find_panel(std::string_view panel_id) const;

  /// Tìm widget theo ID trên toàn bộ các panel.
  /// @param widget_id ID của widget.
  /// @return Con trỏ tới widget đầu tiên có ID đó, hoặc `nullptr` nếu không có.
  ui_widget_data *find_widget(std::string_view widget_id);
  /// @copydoc find_widget(std::string_view)
  const ui_widget_data *find_widget(std::string_view widget_id) const;

  /// Tìm popup theo ID.
  /// @param popup_id ID của popup.
  /// @return Con trỏ tới popup, hoặc `nullptr` nếu không có.
  ui_popup_data *find_popup(std::string_view popup_id);
  /// @copydoc find_popup(std::string_view)
  const ui_popup_data *find_popup(std::string_view popup_id) const;
};

/// Sự kiện UI phát ra khi người dùng tương tác với widget.
struct ui_layout_event {
  /// Loại sự kiện.
  enum kind_t {
    none,
    button_clicked,   ///< Nút được bấm.
    value_changed,    ///< Giá trị đổi (toggle, slider, choice).
    popup_dismissed,  ///< Popup đóng (chọn một nút hoặc bấm Back).
  } kind = none; ///< Loại sự kiện.

  const char *panel_id = "";  ///< ID của panel chứa widget. Rỗng với popup_dismissed.
  /// ID của widget, hoặc của popup với popup_dismissed. Là `const char *`: so sánh
  /// bằng `std::string_view`, không bằng `==` (chỉ so con trỏ).
  const char *widget_id = "";
  bool bool_val = false;      ///< Giá trị mới của toggle.
  f32 float_val = 0.0f;       ///< Giá trị mới của slider.
  i32 int_val = 0;            ///< Lựa chọn mới của choice, hoặc nút đã bấm của popup.
};

/// Kiểu hàm callback khi có sự kiện UI.
using ui_event_callback = std::function<void(const ui_layout_event &)>;

/// Style pixel-art tiêu chuẩn của njin (góc vuông phẳng, kích thước nhỏ gọn 16px).
/// @return Style pixel.
ui_style ui_pixel_style();

/// Nạp bố cục UI từ file JSON.
/// @param ctx Context của engine.
/// @param path Đường dẫn file JSON.
/// @param out Nhận layout đọc được.
/// @return `true` nếu nạp thành công.
bool ui_layout_load(njin_ctx &ctx, const char *path, ui_layout &out);

/// Phân tích dữ liệu JSON thành ui_layout.
/// @param json Đối tượng json_value gốc.
/// @param out Nhận layout đọc được.
/// @return `true` nếu hợp lệ.
bool ui_layout_parse(const json_value &json, ui_layout &out);

/// Chuyển đổi ui_layout thành json_value để lưu file.
/// @param layout Bố cục UI.
/// @return Đối tượng JSON.
json_value ui_layout_to_json(const ui_layout &layout);

/// Ghi bố cục UI ra file JSON.
/// @param path Đường dẫn lưu file.
/// @param layout Bố cục UI.
/// @param pretty Xuống dòng và thụt lề cho dễ đọc.
/// @return `true` nếu lưu thành công.
bool ui_layout_save(const char *path, const ui_layout &layout, bool pretty = true);

/// Vẽ một panel cụ thể trong layout.
/// Gọi trong phase_post_render. Nếu `layout.custom_style` bật thì gọi ui_style_set() với
/// style của layout và không trả lại style cũ.
/// @param ctx Context engine.
/// @param layout Layout chứa panel.
/// @param panel_id ID của panel cần vẽ.
/// @param on_event Callback tùy chọn khi có tương tác widget.
/// @return `true` nếu có bất kỳ tương tác nào xảy ra ở frame này.
bool ui_draw_panel(njin_ctx &ctx, ui_layout &layout, const char *panel_id,
                   const ui_event_callback &on_event = nullptr);

/// Vẽ tất cả các panel có cờ `visible == true` và các popup đang `open`.
/// Gọi trong phase_post_render. Có cùng lưu ý về style với ui_draw_panel().
/// @param ctx Context engine.
/// @param layout Layout cần vẽ.
/// @param on_event Callback tùy chọn khi có tương tác widget.
void ui_draw_layout(njin_ctx &ctx, ui_layout &layout,
                    const ui_event_callback &on_event = nullptr);

// --- Các hàm tiện ích đọc / ghi trạng thái widget ---

/// Kiểm tra nút có vừa được bấm trong lần vẽ gần nhất hay không.
/// @param layout Layout chứa widget.
/// @param widget_id ID của nút.
/// @return `true` nếu nút được bấm trong lần vẽ gần nhất; `false` nếu không hoặc không có widget.
bool ui_layout_is_clicked(const ui_layout &layout, const char *widget_id);

/// Đọc giá trị bool của toggle.
/// @param layout Layout chứa widget.
/// @param widget_id ID của toggle.
/// @param fallback Giá trị trả về khi không có widget.
/// @return Giá trị hiện tại của toggle, hoặc `fallback`.
bool ui_layout_get_bool(const ui_layout &layout, const char *widget_id, bool fallback = false);

/// Gán giá trị bool cho toggle. Không làm gì nếu không có widget.
/// @param layout Layout chứa widget.
/// @param widget_id ID của toggle.
/// @param value Giá trị mới.
void ui_layout_set_bool(ui_layout &layout, const char *widget_id, bool value);

/// Đọc giá trị float của slider hoặc progress.
/// @param layout Layout chứa widget.
/// @param widget_id ID của slider hoặc progress.
/// @param fallback Giá trị trả về khi không có widget.
/// @return Giá trị hiện tại, hoặc `fallback`.
f32 ui_layout_get_float(const ui_layout &layout, const char *widget_id, f32 fallback = 0.0f);

/// Gán giá trị float cho slider hoặc progress. Không làm gì nếu không có widget.
/// @param layout Layout chứa widget.
/// @param widget_id ID của slider hoặc progress.
/// @param value Giá trị mới.
void ui_layout_set_float(ui_layout &layout, const char *widget_id, f32 value);

/// Đọc index của choice.
/// @param layout Layout chứa widget.
/// @param widget_id ID của choice.
/// @param fallback Giá trị trả về khi không có widget.
/// @return Index đang chọn, hoặc `fallback`.
i32 ui_layout_get_int(const ui_layout &layout, const char *widget_id, i32 fallback = 0);

/// Gán index cho choice. Không làm gì nếu không có widget.
/// @param layout Layout chứa widget.
/// @param widget_id ID của choice.
/// @param value Index mới.
void ui_layout_set_int(ui_layout &layout, const char *widget_id, i32 value);

/// Đọc nhãn/văn bản của widget.
/// @param layout Layout chứa widget.
/// @param widget_id ID của widget.
/// @param fallback Chuỗi trả về khi không có widget.
/// @return Nhãn của widget, hoặc `fallback`. Con trỏ hết hạn khi nhãn bị đổi.
const char *ui_layout_get_text(const ui_layout &layout, const char *widget_id, const char *fallback = "");

/// Gán nhãn/văn bản cho widget. Không làm gì nếu không có widget.
/// @param layout Layout chứa widget.
/// @param widget_id ID của widget.
/// @param text Nhãn mới.
void ui_layout_set_text(ui_layout &layout, const char *widget_id, const char *text);

/// Sinh mã nguồn C++ gọi njin::ui_* từ ui_layout hoặc ui_panel_data.
/// @param layout Layout cần sinh code.
/// @param func_name Tên hàm C++ sẽ được tạo ra.
/// @return Chuỗi mã nguồn C++.
std::string ui_layout_generate_cpp(const ui_layout &layout, const char *func_name = "draw_ui");

/// Sinh mã nguồn C++ cho một panel.
/// @param panel Panel cần sinh code.
/// @param func_name Tên hàm C++ sẽ được tạo ra.
/// @return Chuỗi mã nguồn C++.
std::string ui_panel_generate_cpp(const ui_panel_data &panel, const char *func_name = "draw_panel");

/// @}
} // namespace njin
