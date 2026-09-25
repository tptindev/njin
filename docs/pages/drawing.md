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

**Font mặc định** (không truyền font) có sẵn, không cần nạp, nhưng **chỉ có ký tự ASCII**.
Muốn viết tiếng Việt có dấu, nạp một font TrueType/OpenType bằng njin::font_load(). Font
được nạp kèm sẵn bảng chữ Latin và toàn bộ chữ tiếng Việt.

@note Font được dựng thành ảnh ở cỡ chữ khi nạp. Vẽ đúng cỡ đó thì chữ sắc nhất; cỡ khác
vẫn được nhưng bị co giãn. Cần chữ nhỏ và chữ to đều sắc thì nạp hai font.

Nạp thất bại (file thiếu, không đọc được) trả về handle id 0, nên chữ tự rơi về font mặc
định thay vì biến mất.

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
