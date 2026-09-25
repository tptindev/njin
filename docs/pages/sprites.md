# Sprite và animation {#sprites}

Sprite là **component**: gắn njin::transform và njin::sprite vào một entity, engine tự
vẽ nó mỗi frame. Không cần viết system vẽ.

@include sprites.cpp

## Component sprite

| Trường | Ý nghĩa |
|---|---|
| `texture` | Ảnh cần vẽ |
| `source` | Vùng trong ảnh (pixel). Kích thước 0 là cả ảnh |
| `origin` | Điểm neo theo tỉ lệ: `{0.5, 0.5}` là tâm (mặc định), `{0.5, 1}` là giữa đáy |
| `tint` | Màu nhân vào ảnh. `a` < 1 là trong suốt |
| `layer` | Lớp vẽ: lớp nhỏ vẽ trước, lớp lớn đè lên |
| `flip_x`, `flip_y` | Lật ảnh |
| `visible` | Ẩn tạm mà không cần gỡ component |

Sprite dùng cả ba trường của njin::transform: `pos` là nơi điểm neo rơi vào, `rot` là góc
xoay quanh điểm neo, `scale` là tỉ lệ.

## Thứ tự vẽ

Module sprite của engine vẽ trong `phase_render`:

1. Mọi sprite, tilemap và particle, theo `layer` tăng dần. Cùng lớp thì tilemap vẽ trước,
   rồi sprite, rồi particle.
2. Sau đó mới đến các system vẽ của game trong `phase_render`, nên chúng đè lên sprite.

Cùng một lớp, thứ tự giữa các sprite là thứ tự tạo entity.

## Animation

Thêm njin::sprite_anim để chạy animation từ một **sprite sheet**: một ảnh chứa các frame
cùng kích thước, đánh số từ 0 theo hàng từ trái sang phải rồi từ trên xuống.

| Trường | Ý nghĩa |
|---|---|
| `frame_size` | Kích thước một frame |
| `first`, `count` | Animation dùng frame từ `first` đến `first + count - 1` |
| `fps` | Số frame mỗi giây |
| `loop` | Lặp lại khi hết |
| `playing` | Đặt `false` để dừng ở frame hiện tại |
| `finished` | Đã chạy hết (chỉ khi không lặp) |

Engine chuyển frame trong `phase_post_update` và ghi vào `sprite.source`.

Cần thời lượng riêng cho từng frame, nạp từ Aseprite, hay máy trạng thái idle/run/jump thì
dùng njin::animator, xem @ref animation. Nháy trắng khi trúng đòn: njin::sprite_flash(), xem
@ref particles.

Đổi animation bằng njin::anim_play(). Hàm này **không làm gì nếu animation đó đang chạy**,
nên gọi mỗi frame được mà animation không bị bắt đầu lại liên tục.

Animation dùng thời gian đã nhân tốc độ, nên chậm lại theo njin::time_set_scale() và đứng
yên khi tạm dừng.
