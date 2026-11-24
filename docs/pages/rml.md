# Giao diện HTML/CSS với RmlUi {#rml}

Trang này dựng giao diện game bằng **tài liệu RmlUi**: HUD, menu, cửa hàng, túi đồ viết bằng RML (giống HTML) và
RCSS (giống CSS), giá trị của game gắn thẳng vào tài liệu. Mọi thứ khai báo trong `njin_rml.h`; RmlUi và FreeType
nằm trong engine, game không include header của chúng.

Cần biết trước: @ref ui (UI có sẵn của engine, đủ cho menu đơn giản) và @ref input.

## Khi nào dùng

| Dùng | Khi |
|---|---|
| @ref ui (`ui_*`) | Menu, cài đặt, popup đơn giản; chạy cả trên renderer cũ; điều khiển bằng tay cầm có sẵn |
| RmlUi (`rml_*`) | Giao diện nhiều bố cục: HUD có nhiều khối, cửa hàng, túi đồ, bảng điểm; người làm UI quen HTML/CSS; cần animation, transition, template |

Hai cách dùng chung được trong một game. RmlUi vẽ trên cùng, trên thế giới và UI của engine.

## Bật feature

RmlUi là feature `rmlui`, game nhỏ không mang theo nó:

```cmake
njin_use(my_game FEATURES rmlui)
```

Cần renderer SDL3/bgfx (mặc định). Trên renderer OpenGL cũ, hàm `rml_*` đầu tiên báo lỗi.

## Tài liệu và stylesheet

Một HUD: `ui/hud.rml`

```html
<rml>
<head><link type="text/rcss" href="hud.rcss"/></head>
<body data-model="hud">
  <div id="top">Xu: {{coins}} &#183; Máu: {{health * 100 | format(0)}}%</div>
  <div id="pause" data-if="paused">
    <h1>Tạm dừng</h1>
    <input type="text" data-value="player"/>
    <button data-event-click="resume">Chơi tiếp</button>
    <button id="quit">Thoát</button>
  </div>
</body>
</rml>
```

và `ui/hud.rcss`

```css
body { font-family: "Be Vietnam Pro"; font-size: 20dp; color: #fff; width: 100%; height: 100%;
       pointer-events: none; }
#top { position: absolute; left: 16dp; top: 12dp; }
#pause { pointer-events: auto; margin: 120dp auto; width: 320dp; padding: 24dp; background-color: #000c; }
button { display: block; margin-top: 12dp; padding: 8dp; background-color: #2a5ad8; }
button:hover { background-color: #3d6ef0; }
```

- Đường dẫn trong tài liệu (stylesheet, ảnh, template) tính **tương đối với file tài liệu**; file tìm như mọi asset.
- `dp` theo độ phân giải màn hình (Windows chỉnh 150% thì 1dp là 1.5 pixel); `px` là pixel của cửa sổ. RmlUi vẽ
  theo pixel của cửa sổ, không theo window_set_virtual_size().
- **`pointer-events: none` trên `body`** của một HUD phủ cả màn hình: không có nó, `body` che cả cửa sổ và mọi cú
  bấm chuột thuộc về UI. Bật lại `pointer-events: auto` cho khối nào cần bấm.
- Cú pháp RML/RCSS, data binding, animation: xem tài liệu của RmlUi (mikke89.github.io/RmlUiDoc).

## Font

```cpp
njin::rml_font_load(ctx, "ui/fonts/BeVietnamPro-Bold.ttf");
njin::rml_font_load(ctx, "ui/fonts/NotoEmoji.ttf", true); // fallback cho ký tự font chính không có
```

Font dùng chung cho mọi tài liệu, nạp được trước khi tài liệu nào mở. RCSS gọi font bằng **tên họ font ghi trong
file** (`font-family: "Be Vietnam Pro"`), không phải tên file. Mỗi độ đậm (Regular, Bold) là một file riêng. Chữ
tiếng Việt cần font có đủ dấu.

## Data model: giá trị của game trong tài liệu

```cpp
auto hud = njin::rml_model(ctx, "hud");                 // = data-model="hud"
njin::rml_bind_int(ctx, hud, "coins", [&] { return game.coins; });
njin::rml_bind_event(ctx, hud, "resume", [&](njin::context &) { game.paused = false; });
auto doc = njin::rml_document_load(ctx, "ui/hud.rml");  // sau khi gắn xong
```

**Tạo model và gắn mọi giá trị trước khi nạp tài liệu dùng nó**: tài liệu đọc model lúc nạp. Engine đọc lại mọi
getter mỗi frame, nên `{{coins}}` luôn theo đúng trạng thái của game, game không phải báo khi đổi.

| Hàm | Kiểu | Trong tài liệu |
|---|---|---|
| rml_bind_bool() | `bool` | `data-if`, `data-visible`, `data-class-tên`, `data-checked` |
| rml_bind_int() | `i64` | `{{tên}}`, `data-value` |
| rml_bind_float() | `f64` | `{{tên}}`, thanh trượt `data-value` |
| rml_bind_string() | `std::string` UTF-8 | `{{tên}}`, ô nhập `data-value` |
| rml_bind_event() | sự kiện | `data-event-click="tên"` (hay `data-event-change`...) |

Tham số `set` (tùy chọn) của các hàm `rml_bind_*` nhận giá trị khi tài liệu đổi nó: ô nhập chữ, ô đánh dấu, thanh
trượt có `data-value`. Để trống là chỉ đọc.

## Tài liệu, sự kiện, nội dung

| Hàm | Làm gì |
|---|---|
| rml_document_load() | Nạp và hiện một tài liệu |
| rml_document_show(), rml_document_visible() | Ẩn, hiện; tài liệu ẩn không vẽ và không nhận chuột, phím |
| rml_document_close() | Đóng |
| rml_on() | Gọi lại khi một phần tử (theo `id`) có sự kiện `"click"`, `"change"`, `"mouseover"`... |
| rml_set_inner() | Thay nội dung RML của một phần tử |
| rml_wants_mouse() | Chuột đang thuộc về UI (ở trên hay đang kéo một phần tử) |
| rml_debugger() | Bộ gỡ lỗi của RmlUi: cây phần tử, style tính ra, sự kiện |

## Chuột và phím: của UI hay của game

Chuột ở trên một phần tử của tài liệu đang hiện (hay đang kéo nó) thì mouse_pressed(), mouse_down() và bánh xe
**không tới game** trong frame đó; một ô nhập chữ đang được chọn thì phím cũng vậy. Game không phải tự kiểm tra:
bấm vào nút "Chơi tiếp" không bắn súng. rml_wants_mouse() cho biết chuột đang thuộc về UI khi game cần (đổi con trỏ,
ẩn tâm ngắm).

## Ví dụ đầy đủ

@include rml.cpp

## Giới hạn

- Chỉ renderer SDL3/bgfx (Vulkan, Direct3D 12, OpenGL qua bgfx).
- Bo góc có `overflow: hidden`, filter (`blur`, `drop-shadow`), mask và shader của RmlUi chưa được vẽ: phần tử vẫn
  hiện, không có hiệu ứng đó.
- RmlUi chạy cho một context tại một thời điểm.
- Điều khiển bằng tay cầm chưa có sẵn; @ref ui có.
