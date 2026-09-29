# njin UI Editor

Công cụ trực quan (WYSIWYG) để thiết kế, chỉnh sửa, thử nghiệm và xuất giao diện (UI) cho game trên nền tảng **njin**.

Editor được xây dựng bằng **raylib** và **Dear ImGui** (tương tự như `njin_inspector`), xuất ra file dữ liệu định dạng **JSON** (`.ui.json`) chuẩn hóa và hỗ trợ sinh mã nguồn C++ tự động.

---

## 1. Cách làm việc (giống Godot)

Các cửa sổ dock được (kéo tab để ghép, tách, đổi chỗ; **Cửa sổ > Bố cục mặc định** để đặt lại):

- **Scene**: cây node `Layout > Panel > Widget`, và các Popup. Kéo thả để đổi thứ tự hoặc chuyển
  widget sang panel khác; thả vào nửa trên/dưới một dòng để chèn trước/sau. Chuột phải: thêm node con,
  đổi loại, nhân đôi, xóa. Ô tick bên phải: hiện/ẩn panel, mở/đóng popup. Widget sau một `Row`
  được thụt vào: đó là các cột của hàng.
- **Nodes**: danh sách loại node (Panel, Popup, Row, Space, Label, Button, Toggle, Slider, Choice,
  Progress, Circle, Image, Keybind). Kéo vào Viewport hoặc Scene để tạo, bấm đúp để thêm vào node đang chọn.
- **Viewport**: layout được vẽ bằng **chính code UI của engine** (`ui_begin`, `ui_button`...) qua raylib
  vào một render texture, nên font, bo góc, màu và kích thước đúng như trong game. Trên đó:
  - bấm để chọn, kéo panel để dời (hít lưới, giữ Alt để tắt), kéo mép trái/phải để đổi chiều rộng;
  - kéo một widget để đổi chỗ trong panel hoặc sang panel khác (hoặc sang cây Scene);
  - thả node từ Nodes: vào panel thì chèn tại vạch xanh, ra chỗ trống thì tạo panel mới ở đó;
  - chuột phải: menu thêm node; chuột giữa hoặc chuột phải kéo: di chuyển; cuộn: phóng to.
- **Inspector**: thuộc tính node đang chọn (preset neo 3x3 như Godot, offset, rộng, nhãn, giá trị...).
- **Theme**: style riêng của layout (`custom_style`); tắt thì layout kế thừa style của game, Viewport
  xem trước bằng style Mặc định hoặc Pixel chọn trên thanh công cụ.
- **C++ / JSON / Output**: code mẫu, nội dung file, và sự kiện `ui_layout_event` khi chạy thử.

**Chạy thử (F5)** chạy một bản sao của layout: bấm nút, kéo slider, phím mũi tên/Enter (khi Viewport
đang focus) đi thẳng vào UI của engine; dừng thì layout trở lại như trước.

Mọi thay đổi đều hoàn tác được; một lần kéo hoặc một lần sửa chữ là một bước.

---

## 2. Khởi chạy

```bash
cmake --build build --target njin_ui_editor --parallel
build\bin\njin_ui_editor.exe [duong_dan_file.ui.json]
```

| Phím | Việc |
| --- | --- |
| `Ctrl+N` / `Ctrl+O` / `Ctrl+S` / `Ctrl+Shift+S` | Mới / Mở / Lưu / Lưu thành |
| `Ctrl+Z` / `Ctrl+Y` | Hoàn tác / Làm lại |
| `F5` | Chạy thử / Dừng |
| `Del`, `Ctrl+D` (Scene hoặc Viewport đang focus) | Xóa, nhân đôi node |
| `Ctrl+A` (Scene) | Thêm node con |
| `Ctrl+Up/Down` (Scene) | Đổi thứ tự |
| Mũi tên (Viewport, panel đang chọn) | Dời panel 1 px, `Shift` 10 px |
| `F` (Viewport) | Vừa khung |

---

## 3. Cách Tích Hợp UI Vào Game njin

### Cách 1: Nạp trực tiếp từ file JSON (Khuyên dùng)

Định dạng JSON được nạp bằng `njin::ui_layout_load` và vẽ trong `phase_post_render`:

```cpp
#include "njin.h"

struct game_state {
  njin::ui_layout menu_ui;
  bool settings_open = false;
};

void setup(njin::context &ctx, game_state &g) {
  // Nạp layout từ thư mục assets
  if (!njin::ui_layout_load(ctx, "assets/ui/menu.ui.json", g.menu_ui)) {
    NJIN_ERROR("Không thể nạp file UI!");
  }
}

void render_ui(njin::context &ctx, game_state &g) {
  // Vẽ panel menu chính và lắng nghe sự kiện
  njin::ui_draw_panel(ctx, g.menu_ui, "main_menu", [&](const njin::ui_layout_event &ev) {
    // widget_id là const char*: so bằng string_view, `ev.widget_id == "..."` chỉ so con trỏ.
    const std::string_view id = ev.widget_id;
    if (id == "btn_play") {
      // Người chơi bấm Chơi
    } else if (id == "btn_settings") {
      g.settings_open = true;
    } else if (id == "btn_quit") {
      njin::quit(ctx);
    }
  });

  // Hoặc kiểm tra trạng thái widget bằng các hàm tiện ích:
  // if (njin::ui_layout_is_clicked(g.menu_ui, "btn_play")) { ... }
  // bool fs = njin::ui_layout_get_bool(g.menu_ui, "tog_fullscreen");
  // float vol = njin::ui_layout_get_float(g.menu_ui, "slider_vol");
}
```

### Cách 2: Sử dụng Mã C++ Sinh Ra (Code Generator)

Trong tab **Xuất Code C++** của Editor, copy đoạn code đã sinh ra và dán vào file mã nguồn:

```cpp
void draw_main_menu(njin::context &ctx) {
  njin::ui_begin(ctx, {
    .id = "main_menu",
    .title = "Menu Chính",
    .anchor = {0.5f, 0.5f},
    .pivot = {0.5f, 0.5f},
    .offset = {0.0f, 0.0f},
    .width = 340.0f,
    .background = true,
  });

  njin::ui_label(ctx, "Chào mừng dũng sĩ đến với njin!");
  njin::ui_space(ctx, 10.0f);

  if (njin::ui_button(ctx, "Bắt đầu chơi")) {
    // Xử lý chơi
  }
  if (njin::ui_button(ctx, "Cài đặt")) {
    // Xử lý cài đặt
  }
  if (njin::ui_button(ctx, "Thoát")) {
    njin::quit(ctx);
  }

  njin::ui_end(ctx);
}
```

---

## 4. Cấu Trúc File UI Data (JSON Schema)

```json
{
  "version": 1,
  "design_resolution": [1280.0, 720.0],
  "panels": [
    {
      "id": "main_menu",
      "title": "Menu Chính",
      "anchor": [0.5, 0.5],
      "pivot": [0.5, 0.5],
      "offset": [0.0, 0.0],
      "width": 340.0,
      "background": true,
      "visible": true,
      "widgets": [
        { "type": "label", "id": "lbl_sub", "text": "Nội dung" },
        { "type": "space", "height": 10.0 },
        { "type": "button", "id": "btn_play", "label": "Chơi ngay", "enabled": true },
        { "type": "toggle", "id": "tog_fs", "label": "Toàn màn hình", "value": false },
        { "type": "slider", "id": "sld_vol", "label": "Âm lượng", "value": 0.8, "min": 0.0, "max": 1.0, "step": 0.05, "percent": true },
        { "type": "choice", "id": "cho_diff", "label": "Độ khó", "index": 1, "options": ["Dễ", "Vừa", "Khó"] },
        { "type": "progress", "id": "prg_hp", "value": 0.75, "text": "HP: 75/100" },
        { "type": "row", "columns": 2 }
      ]
    }
  ],
  "popups": [
    {
      "id": "quit_confirm",
      "title": "Thoát?",
      "message": "Bạn có chắc chắn muốn thoát?",
      "buttons": ["Ở lại", "Thoát"],
      "default_button": 0,
      "cancel_button": 0,
      "width": 320.0,
      "open": false
    }
  ]
}
```
