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

### Chữ nét trên màn hình có độ phân giải ảo

Với njin::njin_cfg::virtual_size, cả frame được vẽ vào một ảnh nhỏ (ví dụ 640x360) rồi phóng
lên cửa sổ, nên chữ vẽ trong ảnh đó nhòe theo mức phóng. Khi máy có **GPU thật** và
njin::njin_cfg::crisp_text bật (mặc định), chữ trên màn hình (UI, HUD, hội thoại, thông báo)
không vẽ vào ảnh nhỏ mà xếp hàng đợi, rồi vẽ **sau khi ảnh đã phóng**, thẳng vào cửa sổ, từ
ảnh glyph dựng ở cỡ chữ nhân với mức phóng. Chữ nét ở mọi cỡ cửa sổ, kể cả mức phóng lẻ.
Vị trí vẫn theo pixel ảo, và mỗi dòng được dãn khoảng cách chữ cho đúng bằng độ rộng
njin::text_measure() đã đo.

Đánh đổi:
- Chữ trên màn hình nằm **trên** mọi thứ vẽ trong ảnh ảo, kể cả panel vẽ sau nó. Hình chữ
  nhật phủ **cả màn hình** vẽ sau (fade scene, flash, lớp làm tối sau popup) thì làm tối cả chữ
  đã xếp hàng, như khi chúng phủ lên chữ thật.
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
