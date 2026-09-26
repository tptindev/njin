# Màn hình ảo, hẹn giờ và tween {#screen_timers}

## Màn hình ảo cho pixel art {#virtual_screen}

Game pixel art nên vẽ trên một màn hình nhỏ cố định (320 x 180) rồi phóng lên, để mọi pixel to bằng nhau
ở mọi cỡ cửa sổ.

@include virtual_screen.cpp

Khi bật, **mọi thứ** đi qua màn hình ảo: thế giới, UI, toast, chuyển scene. Code của game không cần biết
cửa sổ to bao nhiêu:

- screen_size() trả về cỡ ảo, không đổi khi kéo cửa sổ.
- mouse_pos() và mouse_delta() tính theo pixel ảo, nên bấm chuột trúng nút dù cửa sổ bị phóng.
- Ảnh được phóng **không làm mượt**. `integer_scale` chỉ phóng 1, 2, 3... lần (viền dày hơn nhưng pixel đều);
  tắt thì phóng vừa khít cửa sổ.
- window_size() và window_viewport() cho cỡ thật của cửa sổ và vùng ảnh chiếm, khi cần.

Với camera, `zoom = 2` trên màn hình ảo 640 x 360 cho một thế giới 320 x 180. Kết hợp với
`camera_follow::pixel_snap` (xem @ref platformer).

## Hẹn giờ theo entity {#entity_timers}

njin::timer trong `_tween.h` là bộ đếm bạn tự giữ và tick. Với việc "sau 0.5 giây thì làm X", có
các hàm không cần biến đếm:

@include timers_tweens.cpp

| Hàm | Việc làm |
|---|---|
| timer_after() | Gọi hàm một lần sau một khoảng |
| timer_every() | Gọi mỗi khoảng, mãi mãi hoặc đủ số lần |
| tween_move(), tween_scale(), tween_rotate(), tween_tint() | Chuyển vị trí, tỉ lệ, góc, màu của entity |
| tween_value() | Chuyển một số bất kỳ và đưa cho hàm của bạn mỗi frame |
| timer_cancel(), tween_cancel(), tween_cancel_all() | Hủy |

Quy tắc chung:

- **`owner`**: gắn hẹn giờ với một entity. Entity bị hủy thì hẹn giờ tự hủy, nên hàm không bao giờ
  chạy với entity đã chết. Tween trên entity tự có quy tắc này.
- Hẹn giờ và tween thuộc về **scene đang chạy lúc tạo** và bị hủy khi rời scene, trừ khi
  `keep_across_scenes`.
- Theo giờ của game: dừng khi pause và hitstop. `real_time = true` để chạy theo giờ thật (menu, UI).
- Tween thay tween **cùng loại** trên cùng entity: gọi tween_move hai lần thì lần sau thắng.
- `repeat` và `yoyo` cho chuyển động qua lại (nút nảy, đồng xu nhấp nhô).
- Hàm của hẹn giờ được thêm, hủy entity, tạo hẹn giờ mới thoải mái: chúng chạy ở `phase_update`
  trước system của game.
