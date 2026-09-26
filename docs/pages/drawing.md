# Vẽ hình và chữ {#drawing}

Mọi hàm vẽ phải gọi trong ba phase vẽ. Không gian phụ thuộc phase (xem @ref game_loop):

| Phase | Không gian | Dùng cho |
|---|---|---|
| `phase_render` | Thế giới, qua camera | Nhân vật, bản đồ, hiệu ứng |
| `phase_post_render` | Màn hình, pixel | UI, điểm số, menu |

@include drawing.cpp

## Hình khối

| Hàm | Vẽ |
|---|---|
| njin::draw_rect() | Hình chữ nhật đặc |
| njin::draw_rect_lines() | Viền hình chữ nhật, viền nằm bên trong |
| njin::draw_rect_rotated() | Hình chữ nhật xoay quanh tâm |
| njin::draw_circle() / njin::draw_circle_lines() | Hình tròn đặc / viền |
| njin::draw_line() | Đoạn thẳng có độ dày |
| njin::draw_triangle() | Tam giác. Thứ tự ba đỉnh không quan trọng |

Màu là njin::rgba, mỗi kênh 0..1. Có sẵn vài màu trong `njin::colors` (`white`, `black`,
`red`, `green`, `blue`, `yellow`, `gray`, `transparent`).

## Chữ

njin::draw_text() vẽ chữ UTF-8 với góc trên trái tại vị trí cho trước, hỗ trợ xuống dòng
bằng `\n`. njin::text_measure() cho kích thước chữ trước khi vẽ, dùng để căn giữa hay
căn phải.

**Font mặc định** (không truyền font) là JetBrains Mono (giấy phép SIL OFL), nhúng sẵn trong
engine nên game không cần file font nào. Nó có bảng chữ Latin và toàn bộ chữ tiếng Việt. Muốn
font khác, nạp một font TrueType/OpenType bằng njin::font_load(); font nạp cũng có sẵn bảng
chữ Latin và tiếng Việt.

Mỗi cỡ chữ được vẽ có một ảnh glyph riêng, dựng đúng cỡ đó ở lần vẽ đầu tiên. Cỡ làm tròn
thành số nguyên pixel (từ 6 đến 256) và vị trí cũng làm tròn về pixel, vì nửa pixel lệch là
nửa pixel nhòe trên một chữ cao mười pixel. njin::text_measure() đo bằng đúng ảnh glyph đó,
nên căn giữa và căn phải khớp với chữ được vẽ.

Nạp thất bại (file thiếu, không đọc được) trả về handle id 0, nên chữ tự rơi về font mặc
định thay vì biến mất.

### Chữ kiểu pixel

njin::font_set_style() với njin::font_pixel (hoặc tham số `style` của njin::font_load()) tắt
khử răng cưa và dùng lọc nearest: mỗi texel của glyph hoặc bật hoặc tắt, và chữ được phóng bằng
cùng bộ lọc nearest với sprite. Handle rỗng `{}` đổi font mặc định.

@snippet drawing.cpp pixel_text

Kiểu này **chỉ nét ở đúng cỡ font được thiết kế**: một font pixel như Press Start 2P thiết kế
ở 8 pixel thì vẽ ở 8, 16, 24. Font vector thường (như JetBrains Mono) ở cỡ nhỏ sẽ răng cưa và
khó đọc. Chữ pixel luôn vẽ trong ảnh ảo, không đi qua lớp chữ nét bên dưới, nên hãy bật
`integer_scale` để mọi pixel chữ to bằng nhau.

### UI mịn trên màn hình có độ phân giải ảo

Ngoài chữ, panel bo góc, nút, thanh trượt và mọi hình vẽ trong ảnh nhỏ cũng bị vỡ hạt khi
phóng bằng lọc nearest. Đặt njin::njin_cfg::smooth_ui thì **world vẫn vẽ trong ảnh ảo** (pixel
art), còn `phase_post_render` (UI, HUD), hội thoại, toast, flash và fade được vẽ **sau khi ảnh đã
phóng**, thẳng vào cửa sổ: engine đặt một phép biến đổi (dời và nhân với mức phóng) nên tọa độ
vẫn theo pixel ảo, nhưng hình khối được rasterize ở độ phân giải cửa sổ, và chữ dựng ở cỡ chữ
nhân với mức phóng. Thứ tự vẽ giữ nguyên hoàn toàn, không có đánh đổi về chồng lớp như lớp chữ
bên dưới. Với chế độ này `crisp_text` không còn tác dụng.

Lưu ý:
- njin::clip_begin() vẫn nhận tọa độ pixel ảo. Shader của UI skin nhận `uiRect` theo pixel cửa sổ.
- Texture vẽ trong UI được lọc theo njin::texture_filter của nó ở độ phân giải cửa sổ, nên
  icon pixel art cần njin::filter_nearest để giữ vẻ pixel.
- Font pixel vẫn được phóng bằng lọc nearest như sprite.
- Không có tác dụng khi mức phóng bằng 1.

### Chữ nét trên màn hình có độ phân giải ảo

Với njin::njin_cfg::virtual_size, cả frame được vẽ vào một ảnh nhỏ (ví dụ 640x360) rồi phóng
lên cửa sổ, nên chữ vẽ trong ảnh đó nhòe theo mức phóng. Khi máy có **GPU thật** và
njin::njin_cfg::crisp_text bật (mặc định), chữ trên màn hình (UI, HUD, hội thoại, thông báo)
không vẽ vào ảnh nhỏ mà xếp hàng đợi, rồi vẽ **sau khi ảnh đã phóng**, thẳng vào cửa sổ, từ
ảnh glyph dựng ở cỡ chữ nhân với mức phóng. Chữ nét ở mọi cỡ cửa sổ, kể cả mức phóng lẻ.
Vị trí vẫn theo pixel ảo, và mỗi dòng được dãn khoảng cách chữ cho đúng bằng độ rộng
njin::text_measure() đã đo.

Đánh đổi:
- Chữ xếp hàng nằm trên ảnh ảo, nên engine ghi lại những gì được vẽ **sau** chữ và phủ lên nó:
  njin::draw_rect() và nền, nút, thanh trượt của UI. Phần chữ nằm dưới một hình mờ (fade, flash,
  lớp làm tối sau popup, panel bán trong suốt) được trộn về màu của hình đó; dưới hình đục thì
  bị bỏ. Nhờ vậy panel vẽ sau che được chữ vẽ trước, đúng thứ tự vẽ. Sprite và texture vẽ sau
  chữ thì **không** che được nó; chữ cần nằm dưới chúng thì vẽ chữ trước bằng font pixel hoặc
  trong world.
- njin::clip_begin() được tôn trọng: chữ xếp hàng bị cắt theo vùng clip lúc nó được vẽ.
- Chữ trong world (giữa `phase_render` với camera) và chữ vẽ vào render texture vẫn vẽ trong
  ảnh ảo, vì chúng thuộc về không gian và ảnh đó.
- Máy chỉ có renderer phần mềm (llvmpipe, SwiftShader, GDI Generic) luôn vẽ chữ trong ảnh
  ảo như trước. Cửa sổ đúng bằng ảnh ảo (mức phóng 1) cũng vậy.

## Trộn màu

njin::blend_begin() đổi cách màu mới trộn với màu đã có, cho đến njin::blend_end():

| Chế độ | Hiệu ứng | Dùng cho |
|---|---|---|
| njin::blend_alpha | Trộn theo độ trong suốt (mặc định) | Mọi thứ bình thường |
| njin::blend_additive | Cộng màu, sáng lên | Lửa, ánh sáng, tia lửa |
| njin::blend_multiply | Nhân màu, tối đi | Bóng đổ, lớp phủ màu |

## Cắt vùng vẽ

njin::clip_begin() chỉ cho vẽ trong một hình chữ nhật, tính bằng **pixel màn hình**,
cho đến njin::clip_end(). Dùng cho khung cuộn trong UI.

## Texture nâng cao

njin::texture_draw_ex() vẽ texture với vùng nguồn (cho sprite sheet), tỉ lệ, điểm neo, góc
xoay và lật, qua njin::texture_draw_desc. Thường không cần gọi trực tiếp: component
njin::sprite làm việc này cho bạn, xem @ref sprites.

njin::texture_set_filter() chọn cách lấy mẫu khi texture bị phóng to: njin::filter_linear
(mặc định, mượt) hoặc njin::filter_nearest (giữ nguyên pixel, cho pixel art).
