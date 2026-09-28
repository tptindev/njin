# UI editor: dựng menu bằng kéo thả {#ui_editor}

`njin_ui_editor` là công cụ dựng giao diện theo kiểu Godot: kéo node vào cây, kéo để sắp xếp, xem
ngay kết quả. Nó lưu một file `.ui.json`; game nạp file đó bằng njin::ui_layout_load() rồi vẽ bằng
njin::ui_draw_layout(), không phải gõ từng dòng njin::ui_button().

@image html ui_editor.png "njin_ui_editor: cây Scene và bảng Nodes bên trái, Viewport ở giữa, Inspector bên phải. Viewport được vẽ bằng chính code UI của engine"

Editor chỉ cần cho những menu có nhiều widget hoặc hay đổi. Một menu ba nút vẫn viết thẳng bằng
@ref ui nhanh hơn.

## Chạy editor

```
cmake --build build --target njin_ui_editor --parallel
build\bin\njin_ui_editor.exe [duong_dan_file.ui.json]
```

Editor không có trong bản game: nó là công cụ của người làm game, như `njin_inspector`.

## Các cửa sổ

Các cửa sổ ghép (dock) được: kéo tab để đổi chỗ, tách hoặc xếp chồng. **Cửa sổ > Bố cục mặc định**
đặt lại về dạng ban đầu. Bố cục được nhớ trong `njin_ui_editor.ini`.

| Cửa sổ | Làm gì |
|---|---|
| Scene | Cây `Layout > Panel > Widget` và các Popup |
| Nodes | Danh sách loại node để kéo ra |
| Viewport | Xem layout như trong game; chọn, kéo, thả |
| Inspector | Thuộc tính của node đang chọn |
| Theme | Style riêng của layout |
| C++, JSON, Output | Code mẫu, nội dung file, sự kiện khi chạy thử |

### Scene

Mỗi dòng là một node. Kéo một dòng thả lên dòng khác: thả vào nửa trên thì chèn trước, nửa dưới
thì chèn sau, thả vào panel thì thành widget cuối của panel đó. Widget có thể chuyển sang panel
khác. Chuột phải để thêm node con, đổi loại widget, nhân đôi, đổi thứ tự, xóa. Ô tick bên phải
là `visible` của panel hoặc `open` của popup. Widget sau một `Row` được thụt vào: đó là các cột
của hàng.

### Nodes

Panel, Popup, Row, Space và các điều khiển (Label, Button, Toggle, Slider, Choice, Progress, Circle,
Image, Keybind). Kéo vào Viewport hoặc Scene để tạo, hoặc bấm đúp để thêm vào node đang chọn.

### Viewport

Ảnh trong Viewport không phải hình vẽ giả: editor chạy đúng code của njin::ui_begin(),
njin::ui_button()... và vẽ bằng raylib vào một texture, nên phông, màu, bo góc và kích thước khớp
với game.

| Thao tác | Kết quả |
|---|---|
| Bấm | Chọn panel hoặc widget |
| Kéo panel | Đổi `offset`. Hít lưới; giữ `Alt` để tạm tắt |
| Kéo mép trái hoặc phải của panel | Đổi `width` |
| Kéo một widget | Đổi thứ tự, hoặc sang panel khác. Vạch xanh là chỗ sẽ thả |
| Thả node từ Nodes vào panel | Chèn widget tại vạch xanh |
| Thả node từ Nodes ra chỗ trống | Tạo panel mới ở đó |
| Chuột phải | Menu thêm node, nhân đôi, xóa |
| Chuột giữa hoặc chuột phải kéo | Di chuyển khung nhìn |
| Cuộn | Phóng to, thu nhỏ |
| Mũi tên (panel đang chọn) | Dịch 1 px, giữ `Shift` thì 10 px |

Điểm xanh lá là `anchor` trên màn hình, điểm xanh lam là `pivot` của panel.

### Inspector và Theme

Inspector có bảng 3x3 chọn nhanh cặp `anchor` và `pivot` (như preset neo của Godot), rồi `offset`
chỉnh thêm. Muốn neo và tâm khác nhau thì mở mục "Neo & tâm riêng".

Theme quyết định style của file:

- **Tắt** `custom_style` (mặc định): file không lưu style, game tự gọi njin::ui_style_set(). Viewport
  xem trước bằng style Mặc định hoặc Pixel, chọn ở thanh công cụ.
- **Bật**: file lưu cỡ chữ, khoảng cách, bo góc và các màu, và áp chúng khi vẽ.

## Chạy thử

`F5` chạy một *bản sao* của layout: bấm nút, kéo slider, đổi công tắc trong Viewport; mũi tên, Enter
và Space cũng dùng được khi Viewport đang được chọn. Mỗi sự kiện hiện ở Output, đúng như game sẽ nhận.
Dừng lại thì layout trở về như trước lúc chạy thử.

## Phím tắt

| Phím | Việc |
|---|---|
| `Ctrl+N`, `Ctrl+O`, `Ctrl+S`, `Ctrl+Shift+S` | Mới, mở, lưu, lưu thành |
| `Ctrl+Z`, `Ctrl+Y` | Hoàn tác, làm lại. Một lần kéo hoặc sửa chữ là một bước |
| `F5` | Chạy thử, dừng |
| `Del`, `Ctrl+D` | Xóa, nhân đôi node (khi Scene hoặc Viewport đang được chọn) |
| `Ctrl+A`, `Ctrl+Up`, `Ctrl+Down` | Thêm node con, đổi thứ tự (trong Scene) |
| `F` | Vừa khung (trong Viewport) |

## Dùng file trong game

@include ui_layout.cpp

njin::ui_layout_load() nạp file vào một njin::ui_layout. njin::ui_draw_layout() gọi trong
`phase_post_render` vẽ mọi panel có `visible` và mọi popup có `open`; njin::ui_draw_panel() vẽ một
panel theo `id`. Callback nhận njin::ui_layout_event: `button_clicked` khi bấm nút, `value_changed`
khi đổi toggle, slider hoặc choice, `popup_dismissed` khi đóng popup.

Không cần callback thì đọc trạng thái bất kỳ lúc nào theo `id` của widget:
njin::ui_layout_is_clicked(), njin::ui_layout_get_bool(), njin::ui_layout_get_float(),
njin::ui_layout_get_int(), njin::ui_layout_get_text() và các hàm `set` tương ứng. Mở hoặc ẩn menu
bằng cách đặt `visible` của panel (njin::ui_layout::find_panel()) hoặc `open` của popup
(njin::ui_layout::find_popup()).

Editor còn sinh code C++ gọi trực tiếp njin::ui_begin(), njin::ui_button()... (tab **C++**), dùng
được khi không muốn giữ file JSON. Cùng chức năng có sẵn trong game qua njin::ui_layout_generate_cpp()
và njin::ui_panel_generate_cpp().

### Những điều cần biết

- `ev.widget_id` là `const char *`. `ev.widget_id == "btn_play"` chỉ so hai con trỏ và luôn sai:
  đổi sang `std::string_view` như ví dụ trên.
- Layout có `custom_style` sẽ gọi njin::ui_style_set() khi vẽ và **không trả lại** style cũ. Game
  dùng lẫn style riêng của layout và style của mình thì đặt lại style sau khi vẽ.
- Widget `image` chỉ lưu đường dẫn; njin::ui_layout_load() không nạp ảnh. Game phải tự gán
  `texture` của widget đó (njin::ui_layout::find_widget()) thì ảnh mới hiện.
- Widget `circle` (njin::ui_progress_circle()) chỉnh được đường kính, độ dày, góc bắt đầu, chiều chạy, đầu bo
  tròn, màu và chữ giữa vòng ngay trong Inspector. Bỏ chọn "màu riêng" thì vòng lấy màu từ style của game.
- Widget `keybind` được vẽ như một dòng chữ; việc gán phím game tự làm bằng njin::ui_keybind().
- Chiều cao panel do nội dung quyết định, không lưu trong file.
- Phần cây và toàn bộ layout chỉ chứa panel và popup. Không có node lồng nhau ngoài `Row`.

## Định dạng file

```json
{
  "version": 1,
  "design_resolution": [1280.0, 720.0],
  "panels": [
    {
      "id": "main_menu", "title": "Menu chính",
      "anchor": [0.5, 0.5], "pivot": [0.5, 0.5], "offset": [0.0, 0.0],
      "width": 340.0, "visible": true,
      "widgets": [
        { "type": "label", "id": "lbl_1", "text": "Chào mừng!" },
        { "type": "button", "id": "btn_play", "label": "Chơi" },
        { "type": "slider", "id": "sld_volume", "label": "Âm lượng",
          "value": 0.8, "min": 0.0, "max": 1.0, "step": 0.05, "percent": true },
        { "type": "row", "columns": 2 },
        { "type": "toggle", "id": "tog_a", "label": "A", "value": false },
        { "type": "toggle", "id": "tog_b", "label": "B", "value": true }
      ]
    }
  ],
  "popups": [
    { "id": "quit", "title": "Thoát?", "message": "Chưa lưu sẽ mất.",
      "buttons": ["Ở lại", "Thoát"], "cancel_button": 0 }
  ]
}
```

Loại widget: `label`, `space`, `button`, `toggle`, `slider`, `choice`, `progress`, `circle`, `image`,
`row`, `keybind`. Trường không có trong file lấy giá trị mặc định. Thêm khối `"style"` khi bật
`custom_style`.

`design_resolution` chỉ là khung mà editor dùng để xem: game vẽ theo cỡ màn hình thật, và `anchor`
với `offset` quyết định vị trí panel trên đó.
