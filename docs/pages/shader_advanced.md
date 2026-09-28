# Shader nâng cao: ảnh phụ, mảng uniform, ánh sáng {#shader_advanced}

Trang này dành cho khi shader một ảnh với vài số (`amount`, `time`) không còn đủ: shader cần đọc **thêm một ảnh**
(bảng màu, nhiễu, mặt nạ), nhận **một danh sách** (các nguồn sáng), hoặc chạy trên **cả khung hình**.

Cần biết trước: phần Shader của @ref rendering, và ba bài shader trong @ref learn (bài 10 đến 12). Chạy `njin_render_demo`
rồi bấm phím 7, 8, 9 để xem mọi thứ dưới đây hoạt động.

## Bốn thứ engine cho shader

| Cần | Dùng | Ghi chú |
|---|---|---|
| Đọc thêm một ảnh ngoài `texture0` | njin::shader_set_texture() | Tối đa 4 ảnh phụ mỗi shader. Nhận texture hoặc render texture |
| Nhận một danh sách số | njin::shader_set_vec4_array() | Mảng `vec4`. `vec2` và `vec3` đóng vào `vec4` |
| Chạy trên cả khung hình | njin::camera_set_post_shader() | Một lượt vẽ toàn màn hình, không tính UI |
| Chạy nhiều lượt nối nhau | njin::render_texture_begin() rồi njin::shader_set_texture() | Lượt trước vẽ vào render texture, lượt sau đọc nó |

## Ví dụ: đêm, bảng màu và sương mù trong một shader

Một fragment shader chạy trên cả khung hình, với ba hiệu ứng độc lập, mỗi cái có một uniform từ 0 đến 1 để bật dần:

- **Đêm** (`night`): khung hình tối xuống, rồi mỗi nguồn sáng thêm một vệt sáng. Các nguồn sáng là một **mảng `vec4`**.
- **Hoàng hôn** (`dusk`): độ sáng của từng pixel được **tra trong một ảnh bảng màu** `256 x 1` (`ramp`), nên tối thành
  xanh tím, sáng thành kem ấm.
- **Sương mù** (`haze`): hai lần lấy mẫu một **ảnh nhiễu** (`noise`) đẩy nhẹ chỗ shader đọc khung hình, nên cả cảnh
  gợn như hơi nóng.

@include shader_scene.fs

Phía C++ nạp shader, gắn hai ảnh phụ **một lần**, rồi mỗi frame đặt uniform và danh sách đèn:

@include shader_scene.cpp

@image html render_shader_night.gif "Đêm (phím 7): đèn của nhân vật, một đèn ở con trỏ chuột và bốn đèn cố định, đều nằm trong một mảng vec4. Ngọn đèn chập chờn vì độ sáng đổi theo thời gian"

@image html render_shader_dusk.png "Hoàng hôn (phím 8): độ sáng mỗi pixel được tra trong ảnh bảng màu 256 x 1, nên cả cảnh có một dải màu duy nhất"

@image html render_shader_haze.gif "Sương mù (phím 9): ảnh nhiễu đẩy chỗ đọc khung hình, cây và mặt nước gợn nhẹ"

@image html render_shader_haze.png "Sương mù, ảnh tĩnh"

Ba phím chạy cùng một shader, đặt `night`, `dusk`, `haze` về 0 hoặc 1. Bật cả ba thì chúng chồng lên nhau. Khi cả ba
đều 0, `njin_render_demo` tắt shader bằng `camera_set_post_shader(ctx, {})`, để không phải trả tiền cho một lượt vẽ thừa.
Các hiệu ứng dựng sẵn (blur, bloom, CRT ở phím 4 đến 6) chạy **trước**, nên shader của bạn thấy kết quả của chúng.

## Ảnh phụ: njin::shader_set_texture()

```cpp
njin::shader_set_texture(ctx, scene, "ramp", njin::texture_load(ctx, "assets/ramp.png"));
```

Trong shader, khai báo `uniform sampler2D ramp;` và lấy mẫu như `texture0`. Gọi hàm **một lần** là đủ: engine gắn lại
ảnh mỗi khi shader chạy (`shader_begin`, `camera_set_post_shader`, `draw_instanced`). Ảnh nạp lại khi hot reload thì shader thấy ảnh mới.

Những điều cần biết:

- **Tối đa 4 ảnh phụ** mỗi shader; cái thứ năm bị bỏ qua và ghi một dòng cảnh báo. Đặt lại cùng tên thì thay ảnh.
- **Dùng ảnh riêng** nạp bằng njin::texture_load(). Ảnh xếp trong atlas bị từ chối kèm cảnh báo, vì shader sẽ thấy cả trang
  atlas chứ không phải riêng ảnh đó.
- **Bộ lọc là của ảnh**: bảng màu thì để `filter_linear` (mặc định) cho chuyển màu mượt; ảnh pixel art cần lấy mẫu
  chính xác thì njin::texture_set_filter() với `filter_nearest`.
- **Ảnh nhiễu nên lặp được** (tiling): mặc định ảnh lặp lại khi toạ độ vượt quá 1, nên `uv * 3.0` đọc ba lần ảnh
  liền mạch nếu ảnh không có đường nối. `assets/noise.png` được `tools/make_assets.py` sinh ra theo cách đó.
- **Render texture lưu ngược trục dọc.** Khi gắn một render texture, đọc bằng `vec2(uv.x, 1.0 - uv.y)`. Chính
  khung hình mà `camera_set_post_shader` đưa vào `texture0` cũng ngược như vậy: trong ví dụ trên, `pixel` được tính bằng
  `vec2(uv.x, 1.0 - uv.y) * resolution` cho đúng chiều với toạ độ màn hình.
- Đừng gắn render texture **đang được vẽ vào** (giữa njin::render_texture_begin() và njin::render_texture_end()).

### Chỗ nào ảnh phụ đáng tin cậy

| Cách bật shader | Ảnh phụ |
|---|---|
| njin::camera_set_post_shader() | Tốt: một lượt vẽ toàn màn hình |
| njin::draw_instanced() | Tốt: engine tự gắn lại cho từng lệnh vẽ |
| njin::shader_begin() rồi vẽ sprite | Chỉ sống đến lần raylib đẩy batch kế tiếp |

Với `shader_begin()`, raylib gom các lệnh vẽ thành một batch và **quên** ảnh phụ mỗi lần đẩy batch: khi batch đầy (8192
hình), sau 256 lần đổi texture, hoặc khi có njin::draw_instanced() hay đổi render texture xen vào. Dùng cho vài lệnh vẽ lớn
(một lớp nền, vài sprite), không cho hàng nghìn sprite. Cần hàng nghìn hình có ảnh phụ thì dùng njin::draw_instanced().

## Mảng uniform: njin::shader_set_vec4_array()

```cpp
const njin::vec4 lights[2] = {{320.0f, 180.0f, 190.0f, 1.0f}, {900.0f, 500.0f, 130.0f, 0.9f}};
njin::shader_set_vec4_array(ctx, scene, "lights", lights, 2);
```

Trong shader: `uniform vec4 lights[8];`. Tên truyền vào là `lights`, không kèm `[0]`. Vài điều:

- `count` **không được lớn hơn** kích thước khai báo trong shader. Số phần tử tối đa còn do card đồ họa quyết định;
  vài chục là an toàn.
- Mảng **chưa dùng hết** là bình thường: đặt thêm một `int` (như `light_count`) cho biết dùng bao nhiêu, và shader lặp
  đến đó.
- **`vec2` và `vec3` đóng vào `vec4`.** Một `vec3` màu thì dùng `.rgb` của phần tử `vec4`; hai `vec2` thì `xy` và `zw`.
  Trong ví dụ, vị trí, bán kính và độ sáng của một đèn nằm chung một `vec4`, màu ở một mảng thứ hai cùng chỉ số.
- Như mọi uniform, đặt **trước** khi shader chạy. Với shader hậu kỳ của camera, đặt ở `phase_pre_render` là đủ sớm.
- Toạ độ đèn trong ví dụ là **pixel màn hình** (tính bằng njin::w2scr()), nên vệt sáng đúng chỗ dù camera zoom hay cuộn.

## Nhiều lượt vẽ nối nhau

Muốn lượt sau đọc kết quả của lượt trước (mặt nạ ánh sáng, ảnh mờ của nền), vẽ lượt trước vào một render texture rồi
gắn nó cho shader của lượt sau:

```cpp
mask = njin::render_texture_load(ctx, 256, 256);
njin::shader_set_texture(ctx, sheet_shader, "mask", mask); // một lần, ở phase_startup

// mỗi frame, ở phase_post_update hoặc phase_post_render:
njin::render_texture_begin(ctx, mask, {0.0f, 0.0f, 0.0f, 1.0f});
// ... vẽ mặt nạ ...
njin::render_texture_end(ctx);
```

njin::render_texture_begin() đặt lại phép biến đổi của camera, nên chỉ gọi ngoài phase thế giới. Quy tắc phase đầy đủ
ở @ref post_processing. Nhớ đọc bằng `1.0 - uv.y` trong shader (xem trên).

## Sửa shader khi game đang chạy

Bật hot reload trong game của bạn (njin::hot_reload_enable(), xem @ref rendering) thì lưu file `.fs` là shader được nạp
lại ngay; lỗi biên dịch thì shader cũ giữ nguyên và lỗi hiện trong log. Cách nhanh nhất để chỉnh độ rộng vệt sáng hay hệ
số sương mù. `njin_render_demo` không bật hot reload: thêm một dòng vào `main.cpp` nếu muốn thử.

## Chưa có

Những thứ sau chưa có trong API vì chưa game nào cần: uniform `mat4` và `vec3` riêng (dùng `vec4`), ảnh phụ bền qua
mọi lần đẩy batch khi vẽ hàng nghìn sprite bằng `shader_begin` (dùng njin::draw_instanced() hoặc lượt vẽ toàn màn hình), và
uniform dựng sẵn như thời gian, độ phân giải (tự đặt bằng njin::shader_set_f32() và njin::shader_set_vec2(), như trong
ví dụ).
