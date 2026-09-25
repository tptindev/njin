# UI: menu, nút, thanh trượt {#ui}

UI của njin là kiểu *immediate mode*: không tạo đối tượng nút, chỉ gọi hàm mỗi frame trong
`phase_post_render`. Nút nào được gọi thì hiện, và hàm trả về `true` ở frame nó được bấm.

@include ui_menu.cpp

## Widget

| Hàm | Là gì | Trả về `true` khi |
|---|---|---|
| njin::ui_begin(), njin::ui_end() | Một panel: nền, tiêu đề, xếp widget từ trên xuống | |
| njin::ui_button() | Nút bấm, có thể tắt (`enabled = false`) | Được bấm |
| njin::ui_toggle() | Công tắc bật/tắt | Giá trị đổi |
| njin::ui_slider() | Thanh trượt, hiện số hoặc phần trăm | Giá trị đổi |
| njin::ui_choice() | Chọn một trong nhiều mục bằng trái/phải | Lựa chọn đổi |
| njin::ui_progress() | Thanh tiến độ: máu, thời gian nạp | |
| njin::ui_label(), njin::ui_image(), njin::ui_space() | Chữ, ảnh, khoảng trống | |
| njin::ui_row() | Xếp vài widget tiếp theo thành một hàng ngang | |
| njin::ui_back() | | Người chơi bấm quay lại |

Nhãn là định danh của widget trong panel. Hai widget cùng chữ thì thêm hậu tố ẩn sau `##`:
`"Xóa##1"`, `"Xóa##2"` (phần sau `##` không được vẽ).

Panel đặt theo `anchor` (điểm trên màn hình, theo tỉ lệ) và `pivot` (điểm của panel), nên
`{.anchor = {1, 0}, .pivot = {1, 0}}` là góc trên phải. Chiều cao tự tính theo nội dung.

## Chuột, bàn phím, tay cầm

Mọi widget dùng được bằng cả ba, không cần viết thêm gì:

| Việc | Chuột | Bàn phím | Tay cầm |
|---|---|---|---|
| Chọn | Trỏ vào | Mũi tên lên/xuống (trái/phải trong hàng) | D-pad hoặc cần trái |
| Bấm | Click | Enter, Space | A (nút mặt dưới) |
| Đổi slider, choice | Kéo, click | Trái/phải | Trái/phải |
| Quay lại | | Esc, Backspace | B (nút mặt phải) |

Giữ một hướng thì lựa chọn tự lặp. Đi lên từ widget trên cùng thì vòng xuống dưới cùng.
Widget bị tắt được bỏ qua. Vừa mở menu, widget đầu tiên được chọn sẵn cho tay cầm; muốn chọn
nút khác thì gọi njin::ui_focus() lúc mở.

**Trong lúc có panel đang hiện**, UI giữ các phím điều hướng (mũi tên, Enter, Space, Esc,
Backspace) và click chuột lên panel: game không thấy chúng, nên nhân vật không chạy khi người
chơi đang chọn menu. Kiểm tra bằng njin::ui_active().

@note Esc mặc định đóng cửa sổ (`njin_cfg::exit_key`). Menu dùng Esc để quay lại thì đặt
`.exit_key = njin::key_none` trong njin_cfg và thoát bằng một nút "Thoát".

## Tùy biến giao diện

Toàn bộ giao diện nằm trong njin::ui_style: phông, cỡ chữ, `scale`, khoảng cách, và diện mạo
(njin::ui_look) của từng loại widget: `panel`, `label`, `button`, `track`, `fill`, `knob`. Lấy
bản mặc định bằng njin::ui_default_style(), sửa, rồi njin::ui_style_set(). Đổi style giữa hai
panel được, ví dụ một hộp thoại cảnh báo màu đỏ.

Mỗi njin::ui_look có bốn mặt (njin::ui_skin) cho bốn trạng thái: `normal`, `focused`,
`pressed`, `disabled`, và màu chữ tương ứng. Một mặt vẽ bằng:

- **màu phẳng**: `color`, bo góc `roundness`, viền `outline`;
- **ảnh**: `texture` (và `source` nếu dùng một vùng trong atlas), nhân màu `color`;
- **ảnh 9-slice**: thêm `border`: bốn góc giữ nguyên, cạnh và giữa kéo giãn, nên một khung vẽ
  nhỏ dùng được cho nút mọi kích thước.

```cpp
njin::ui_style s = njin::ui_default_style();
const njin::texture_handle frame = njin::texture_load(ctx, "assets/ui/frame.png");
s.button.normal = {.texture = frame, .border = 6};
s.button.focused = {.texture = frame, .border = 6, .color = {1.0f, 0.9f, 0.6f, 1.0f}};
s.button.shader = njin::shader_load(ctx, nullptr, "assets/ui/glow.fs");
njin::ui_style_set(ctx, s);
```

**Shader.** `ui_look::shader` được bật khi vẽ các mặt của widget. Engine tự đặt các uniform
sau nếu shader khai báo chúng (không khai báo thì bỏ qua, không cảnh báo):

| Uniform | Kiểu | Giá trị |
|---|---|---|
| `uiState` | float | 0 thường, 1 đang chọn, 2 đang nhấn, 3 bị tắt |
| `uiTime` | float | Giây từ lúc chạy, cho hiệu ứng động |
| `uiRect` | vec4 | x, y, rộng, cao của widget, pixel màn hình |
| `uiValue` | float | Tiến độ 0..1 của slider hay thanh tiến độ |

Ví dụ nút phát sáng nhấp nháy khi được chọn:

```glsl
#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float uiState;
uniform float uiTime;
out vec4 finalColor;
void main() {
  vec4 c = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
  float glow = uiState >= 1.0 ? 0.35 + 0.1 * sin(uiTime * 6.0) : 0.0;
  finalColor = vec4(c.rgb + vec3(glow, glow * 0.6, 0.0), c.a);
}
```

**Âm thanh.** `sound_move`, `sound_accept`, `sound_back` trong style phát khi chuyển lựa chọn,
khi bấm hoặc đổi giá trị, và khi quay lại.

**Phông.** Phông mặc định của engine chỉ có chữ ASCII. Menu tiếng Việt cần nạp một phông có
dấu bằng njin::font_load() và đặt vào `ui_style::font` (xem @ref drawing).

UI vẽ trong không gian màn hình, sau post-processing, nên hiệu ứng như blur hay CRT không làm
mờ menu (xem @ref post_processing; njin::post::paused() hợp với menu pause).
