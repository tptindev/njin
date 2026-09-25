# Camera {#camera}

Camera trong njin là một **entity** có ba component. Không có đối tượng camera riêng.

| Component | Vai trò |
|---|---|
| njin::transform | `pos` là điểm trong thế giới mà camera nhìn vào, `rot` là góc xoay. `scale` bị bỏ qua |
| njin::camera_2d | `offset` là vị trí trên màn hình nơi `pos` được vẽ, `zoom` là độ phóng đại |
| njin::camera_on | Tag đánh dấu đây là camera đang dùng |

## Tạo camera

@include camera_follow.cpp

Trong ví dụ:

- `offset` là **nửa kích thước màn hình**, nên điểm camera nhìn vào luôn nằm giữa màn hình.
- `zoom = 2` làm mọi thứ to gấp đôi. Giá trị `<= 0` được coi là 1.
- `follow_player` chạy ở `phase_post_update`, sau khi người chơi đã di chuyển, để camera không bị chậm một frame.

## Đổi camera

Camera được đọc **trực tiếp từ registry** mỗi lần cần, nên sửa
`transform` hay `camera_2d` là có hiệu lực ngay, không cần gọi hàm nào.

Muốn chuyển qua camera khác: bỏ njin::camera_on khỏi camera cũ và gắn vào camera mới.

@warning Nếu **nhiều** entity cùng có njin::camera_on, một trong số chúng được
dùng nhưng bạn không kiểm soát được là cái nào. Chỉ giữ một camera có tag này.

Không có camera nào thì thế giới **trùng với màn hình**: tọa độ (0, 0) là góc trên trái.

## Đổi tọa độ

| Hàm | Đổi từ | Sang |
|---|---|---|
| njin::w2scr() | Thế giới | Pixel màn hình |
| njin::scr2w() | Pixel màn hình | Thế giới |

Cả hai đi qua njin::camera_active(), là góc nhìn đang dùng (njin::camera_view).

Trường hợp thường gặp nhất là đổi vị trí chuột thành vị trí trong thế giới.
njin::mouse_pos() trả về pixel màn hình, không qua camera:

```cpp
const njin::vec2 target = njin::scr2w(ctx, njin::mouse_pos(ctx));
```

Xem @ref input.

njin::camera_bounds() trả về vùng thế giới đang hiện trên màn hình. Dùng để bỏ qua những
thứ nằm ngoài màn hình; tilemap dùng nó để chỉ vẽ các chunk nhìn thấy.

## Camera và các phase vẽ

Module camera của engine bật camera ở đầu `phase_pre_render` và tắt ở đầu `phase_post_render`:

- Mọi thứ vẽ ở `phase_pre_render` và `phase_render` chịu ảnh hưởng của camera.
- Mọi thứ vẽ ở `phase_post_render` **không**. Đây là chỗ đặt UI.

Xem @ref game_loop.
