# UI: menu, nút, thanh trượt {#ui}

UI của njin là kiểu *immediate mode*: không tạo đối tượng nút, chỉ gọi hàm mỗi frame trong
`phase_post_render`. Nút nào được gọi thì hiện, và hàm trả về `true` ở frame nó được bấm.

@include ui_menu.cpp

Menu có nhiều widget hoặc hay đổi thì dựng bằng kéo thả trong njin_ui_editor rồi nạp file vào game: xem @ref ui_editor.

@image html platformer_title.png "Menu chính của game mẫu Mầm Leo Núi, dựng bằng njin::ui_button(). Nút đang chọn có viền xanh; đổi nút bằng phím, chuột hoặc tay cầm"

## Widget

| Hàm | Là gì | Trả về `true` khi |
|---|---|---|
| njin::ui_begin(), njin::ui_end() | Một panel: nền, tiêu đề, xếp widget từ trên xuống | |
| njin::ui_button() | Nút bấm, có thể tắt (`enabled = false`) | Được bấm |
| njin::ui_toggle() | Công tắc bật/tắt | Giá trị đổi |
| njin::ui_slider() | Thanh trượt, hiện số hoặc phần trăm | Giá trị đổi |
| njin::ui_choice() | Chọn một trong nhiều mục bằng trái/phải | Lựa chọn đổi |
| njin::ui_progress() | Thanh tiến độ: máu, thời gian nạp | |
| njin::ui_progress_circle() | Vòng tiến độ: hồi chiêu, nạp đạn. Tùy chỉnh độ dày, màu, góc bắt đầu, chiều chạy, đầu bo tròn, chữ giữa | |
| njin::ui_label(), njin::ui_image(), njin::ui_space() | Chữ, ảnh, khoảng trống | |
| njin::ui_last_rect() | Khung của widget vừa đặt, để vẽ thêm lên nó (sau njin::ui_end()) | |
| njin::ui_row() | Xếp vài widget tiếp theo thành một hàng ngang | |
| njin::ui_back() | | Người chơi bấm quay lại |

Nhãn là định danh của widget trong panel. Hai widget cùng chữ thì thêm hậu tố ẩn sau `##`:
`"Xóa##1"`, `"Xóa##2"` (phần sau `##` không được vẽ).

Panel đặt theo `anchor` (điểm trên màn hình, theo tỉ lệ) và `pivot` (điểm của panel), nên
`{.anchor = {1, 0}, .pivot = {1, 0}}` là góc trên phải. Chiều cao tự tính theo nội dung.

HUD luôn hiện trong lúc chơi (thanh tài nguyên, dãy nút chiêu mộ) thì đặt `.navigable = false`:
nút của nó chỉ bấm bằng chuột, không bao giờ được chọn sẵn, và mũi tên, Enter, Space, Esc vẫn
thuộc về game. Muốn vẽ thêm lên một widget (hình trong nút, bản đồ nhỏ trong một khoảng
njin::ui_space()) thì lấy khung của nó bằng njin::ui_last_rect() và vẽ sau njin::ui_end().

### Vòng tiến độ

njin::ui_progress_circle() nhận một njin::ui_circle_desc. Mọi trường đều tùy chọn; màu có alpha 0 thì lấy từ
style của game (`track`, `fill`, `panel.text`), nên một vòng không chỉnh gì vẫn hợp với theme.

```cpp
njin::ui_progress_circle(ctx, {.value = cooldown, .diameter = 64.0f, .thickness = 8.0f,
                               .round_caps = true, .fill = {0.95f, 0.35f, 0.25f, 1.0f}, .percent = true});
```

| Trường | Tác dụng |
|---|---|
| `diameter`, `thickness` | Đường kính ngoài và độ dày vòng. Độ dày từ nửa đường kính trở lên thì thành hình tròn đặc |
| `start_angle`, `clockwise` | Vòng bắt đầu ở đâu (độ, 0 là đỉnh, 90 là 3 giờ) và đầy dần theo chiều nào |
| `round_caps` | Hai đầu phần đầy bo tròn |
| `show_track`, `track`, `fill` | Vòng nền có vẽ không, màu vòng nền, màu phần đầy |
| `text`, `percent`, `text_color` | Chữ giữa vòng; hoặc `percent` để hiện "75%" |

Widget chiếm một dòng cao bằng `diameter` và được căn giữa; trong njin::ui_row() nó thu nhỏ vừa cột.

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
chơi đang chọn menu. Kiểm tra bằng njin::ui_active(). UI chỉ nuốt nút trái; game bấm chuột phải hay
cuộn bánh xe vào thế giới thì hỏi njin::ui_mouse_over() trước, để cú bấm lên HUD không lọt xuống.

@note Esc mặc định đóng cửa sổ (`njin_cfg::exit_key`). Menu dùng Esc để quay lại thì đặt
`.exit_key = njin::key_none` trong njin_cfg và thoát bằng một nút "Thoát".

## Popup

njin::ui_popup() là hộp thoại xác nhận: nền tối, tiêu đề, nội dung tự xuống dòng và tối đa 4 nút.
Gọi nó mỗi frame khi popup đang mở; biến `open` do game giữ và popup tự đặt về `false` khi đóng.

@include ui_popup_toast.cpp

Hàm trả về số thứ tự nút vừa bấm (theo `buttons`), hoặc -1 nếu chưa có gì. Quay lại (Esc, Backspace,
nút B) đóng popup và trả về `cancel_button`, hoặc -1 nếu không đặt.

Popup là **modal**:

- Các panel khác vẫn được vẽ, nhưng không nhận chuột, phím hay tay cầm cho đến khi popup đóng.
  Menu phía sau không cần ẩn đi, và không cần `if/else` giữa các màn hình.
- njin::ui_back() chỉ báo cho popup, nên cùng một cú Esc không vừa đóng popup vừa quay lại menu.
- Khi mở, nút `default_button` được chọn sẵn: đặt nó là nút **an toàn** ("Ở lại", "Hủy") để bấm Enter
  nhầm không mất dữ liệu. Khi đóng, lựa chọn trở lại đúng widget cũ của menu phía sau.
- Frame popup vừa mở bỏ qua nút quay lại. Nhờ vậy menu mở popup bằng Esc (`if (ui_back(ctx))
  open = true;`) không bị popup đóng ngay bởi chính cú Esc đó.
- Cú bấm vừa đóng popup không rơi xuống widget phía sau.

Cần nội dung tùy ý (thanh trượt, công tắc...) thì dùng njin::ui_popup_begin() và njin::ui_popup_end()
bao quanh các widget bình thường, xem `settings_popup` trong ví dụ. Chỉ `id`, `title` và `width` của
njin::ui_popup_desc được dùng; tự đóng popup bằng cách ngừng gọi nó.

Màu phủ làm tối nền là `ui_style::dim`. Panel của popup dùng chung diện mạo `ui_style::panel`.

## Toast

njin::ui_toast() hiện một thông báo nhỏ ở góc màn hình rồi tự biến mất: "Đã lưu game", "Nhặt được 5
vàng", "Mất kết nối".

```cpp
njin::ui_toast(ctx, "Đã lưu game", {.kind = njin::ui_toast_success});
njin::ui_toast(ctx, "Túi đồ đã đầy", {.kind = njin::ui_toast_warning, .seconds = 4.0f});
```

Gọi từ **bất cứ đâu**, ở bất cứ phase nào (kể cả trong `phase_update` hay trong một event handler).
Không cần ui_begin: engine tự xếp hàng, trượt vào, mờ dần và vẽ đè lên mọi thứ, kể cả popup, trừ hiệu
ứng chuyển scene. Chữ dài tự xuống dòng theo `ui_style::toast_width`.

| Tính chất | Chi tiết |
|---|---|
| **Không nuốt phím** | Khác panel của UI: toast chỉ để đọc, game phía dưới vẫn nhận mũi tên, Enter, Esc và chuột |
| Giờ thật | Đếm theo njin::delta_real(): vẫn chạy và tự hết hạn khi game đang pause hay hitstop |
| Loại | njin::ui_toast_info, `_success`, `_warning`, `_error` đổi màu vạch bên trái (`ui_style::toast_accent`) |
| Số lượng | Tối đa `ui_style::toast_max` (mặc định 5); cái cũ nhất bị bỏ khi đầy |
| Vị trí | `ui_style::toast_anchor`: `{1, 1}` là góc dưới phải (mặc định), `{0.5, 0}` là giữa cạnh trên. Toast mới nhất nằm sát góc, cái cũ xếp dần vào trong |
| Diện mạo | `ui_style::toast`: màu, bo góc, ảnh 9-slice và shader như mọi widget khác |

njin::ui_toast_clear() xóa hết toast đang hiện, ví dụ khi đổi scene.

@note Nếu dựng toast bằng ui_begin, panel đó sẽ nuốt phím điều hướng của game mỗi khi nó hiện, và
nhân vật đứng khựng. Dùng njin::ui_toast().

## Tùy biến giao diện

Toàn bộ giao diện nằm trong njin::ui_style: phông, cỡ chữ, `scale`, khoảng cách, và diện mạo
(njin::ui_look) của từng loại widget: `panel`, `label`, `button`, `track`, `fill`, `knob`, `toast`. Lấy
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

**Phông.** Phông mặc định của engine (JetBrains Mono, nhúng sẵn) đã có chữ tiếng Việt. Muốn
phông khác, nạp bằng njin::font_load() và đặt vào `ui_style::font` (xem @ref drawing).

UI vẽ trong không gian màn hình, sau post-processing, nên hiệu ứng như blur hay CRT không làm
mờ menu (xem @ref post_processing; njin::post::paused() hợp với menu pause).
