# Làm game platformer {#platformer}

Trang này ghép các phần có sẵn của engine thành một nhân vật platformer: chạy, nhảy có
cảm giác tốt, dốc, bục một chiều, bục di chuyển, và camera bám theo. Game mẫu
`njin_platformer` (@ref samples) dùng đúng những thứ này. Chưa làm gì bao giờ? Bắt đầu với @ref first_jump : một nhân
vật nhảy được trong 50 dòng.

@include platformer_body.cpp

## Nhân vật: njin::platformer_body

Gắn njin::platformer_body cạnh một njin::transform và một njin::collider hộp. Engine di chuyển
nó trong `phase_fixed_update` bằng collision_move(), rồi ghi lại trạng thái (`grounded`, `on_wall`,
`velocity`...). Game chỉ cần cho nó input và đọc trạng thái để chọn animation.

Các cảm giác quen thuộc đều có sẵn, chỉnh bằng số ngay trên component:

| Cảm giác | Trường |
|---|---|
| **Coyote time**: rời mép rồi vẫn nhảy được | `coyote_time` |
| **Jump buffer**: bấm nhảy sớm trước khi chạm đất | `jump_buffer` |
| Thả nút nhảy sớm thì nhảy thấp | `jump_cut` |
| Rơi nhanh hơn lúc lên | `gravity`, `fall_gravity`, `max_fall` |
| Nhảy đôi | `air_jumps` |
| Trượt tường, nhảy khỏi tường | `wall_slide_speed`, `wall_jump` |
| Gia tốc trên đất và trên không | `ground_accel`, `air_accel`... |

Có hai cách đưa input vào:

- **Tự ghi** vào `body.input` mỗi frame (`move_x`, `jump`, `jump_held`, `drop`). `jump` là một
  *yêu cầu*: engine tự xóa khi đã xử lý, nên bấm ở frame không có nhịp vật lý nào vẫn không bị lỡ.
- Gắn njin::platformer_input_map với một axis và một action: engine đọc chúng giúp bạn.

Muốn đẩy lùi khi trúng đòn thì ghi thẳng vào `velocity`.

Nhân vật gửi hai event để game rung màn hình, phát tiếng, tạo bụi: njin::body_jumped và
njin::body_landed (kèm tốc độ rơi).

## Dốc, bục một chiều, ô không va chạm

Hình va chạm của một ô nằm trong njin::tilemap::shapes, đặt bằng tilemap_set_shape():

| njin::tile_shape | Việc làm |
|---|---|
| `tile_solid` | Chặn mọi phía (mặc định) |
| `tile_none` | Chỉ để nhìn, dù nằm trong layer vật cản |
| `tile_one_way` | Bục chỉ đỡ từ trên xuống; nhảy xuyên từ dưới lên |
| `tile_slope_r`, `tile_slope_l` | Dốc 45 độ, cao bên phải hoặc bên trái |
| `tile_slope_*_low`, `*_high` | Dốc 22.5 độ, hai ô liền nhau |

Khi nạp từ **Tiled**, đặt thuộc tính chuỗi `collision` trên ô trong tileset
(`one_way`, `slope_r`, `slope_l_high`, `none`...). Khi nạp từ **LDtk**, đặt tên giá trị IntGrid, hoặc
gắn enum tag / custom data trên tileset bằng cùng những tên đó. Ô dùng cho animation
(nước, đuốc) cũng lấy từ tileset, xem @ref tilemap.

Nhân vật đi lên dốc mượt, và **dính vào mặt dốc** khi đi xuống thay vì nảy ra khỏi nó. Nhảy
xuống khỏi bục một chiều: giữ phím xuống rồi bấm nhảy (`input.drop`, hoặc `platformer_input_map::down`).

collision_move() nhận thêm njin::collision_move_opts nếu bạn tự viết bộ điều khiển: `drop_through`,
`snap_down`, `test_only` (dò tường hay mặt đất mà không di chuyển). Kết quả có `grounded`, `ground`,
`on_slope`.

## Bục di chuyển: njin::path_mover

Một entity có transform, collider hộp (thường `one_way = true`) và njin::path_mover đi theo một đường
gấp khúc lặp lại. Engine dời nó bằng collision_move_platform(), **chở theo** mọi nhân vật đứng trên
nó và đẩy những thứ nó đâm vào. Trong Tiled, vẽ bục bằng một object có polyline; các điểm của
polyline là các điểm dừng.

## Camera bám theo: njin::camera_follow

@code
const entt::entity cam = njin::camera_spawn(ctx, 2.0f);
reg.emplace<njin::camera_follow>(cam, njin::camera_follow{
    .target = player, .deadzone = {24, 40}, .lookahead = {36, 0},
    .bounds = njin::level_bounds(ctx, level), .pixel_snap = true});
@endcode

Camera trễ nhẹ (`smoothing`), đứng yên khi nhân vật đi trong vùng chết (`deadzone`), nhìn trước
theo hướng chạy (`lookahead`), và **không bao giờ lộ ra ngoài level** (`bounds`; level nhỏ hơn
màn hình thì nằm giữa). Với pixel art, bật `pixel_snap` để các ô không rung một pixel. Sau khi
dịch chuyển nhân vật xa, đặt `started = false` để camera nhảy thẳng tới thay vì trượt qua cả level.

Rung màn hình của njin::camera_shake() cộng thêm lúc vẽ, không ảnh hưởng camera_follow.

## Kiểm tra bằng inspector

njin_inspector hiện `platformer_body` (vận tốc, đang đứng đất, tường, các bộ đếm coyote và
buffer) và `camera_follow` của entity đang chọn. Xem @ref debug.
