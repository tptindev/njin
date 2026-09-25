# Va chạm {#collision}

@ref math có các hàm hình học để tự kiểm tra hai hình. Trang này là tầng trên: gắn
njin::collider vào entity, engine tự tìm các cặp chạm nhau và báo cho game bằng event. Kèm
theo là di chuyển có vật cản, truy vấn vùng và bắn tia.

Đây **không** phải mô phỏng vật lý: không có lực, khối lượng, nảy hay xếp chồng. Nó dành cho
những thứ game hành động cần: đạn trúng quái, nhặt đồ, vùng sát thương, tường chắn, quái nhìn
thấy người chơi.

@include collision.cpp

## Collider

| Trường | Ý nghĩa |
|---|---|
| `shape` | `collider_box` (hình hộp thẳng trục), `collider_circle`, hoặc `collider_tiles` |
| `size`, `radius` | Kích thước hộp, bán kính tròn |
| `offset` | Độ lệch của tâm so với `transform.pos` |
| `layer`, `mask` | Thuộc lớp nào, va chạm với lớp nào |
| `trigger` | Chỉ báo chồng nhau, không chặn đường |
| `enabled` | Tắt tạm mà không gỡ component |

`offset`, `size` và `radius` nhân với `transform.scale`; `offset` xoay theo `transform.rot`.
Bản thân hình hộp luôn thẳng trục, không xoay.

Collider có thể nằm trên entity con (njin::child_of), ví dụ hitbox ở đầu kiếm: module collision
chạy sau module hierarchy nên thấy đúng vị trí của frame này.

## Lớp và mặt nạ

Mỗi collider thuộc một hoặc nhiều lớp (`layer`, tối đa 32, đặt tên bằng njin::layer_bit()) và
chọn va chạm với những lớp nào (`mask`). Hai collider A và B chỉ được xét khi **cả hai chiều**
đều cho phép:

```
(A.mask & B.layer) != 0  và  (B.mask & A.layer) != 0
```

Ví dụ đạn của người chơi có `mask = layer_enemy | layer_wall`: nó không bao giờ trúng người
chơi, dù người chơi có `mask = layer_all`. Đây cũng là quy tắc của Box2D.

## Event

Mỗi frame, trong `phase_post_update`, engine tìm mọi cặp collider hộp hoặc tròn đang chồng
nhau và xếp hàng event qua njin::events():

| Event | Khi nào |
|---|---|
| njin::collision_enter | Frame đầu tiên hai collider chồng nhau |
| njin::collision_stay | Mỗi frame sau đó, khi vẫn còn chồng nhau |
| njin::collision_exit | Frame thôi chồng nhau: tách ra, bị tắt, bị gỡ collider, hoặc bị hủy |

- Mỗi cặp gửi **hai** event, một cho mỗi bên (`self` là bên nhận). Handler chỉ cần hỏi "`self`
  là gì, `other` là gì".
- Event được phát sau `phase_post_update`, nên hủy entity ngay trong handler là an toàn. Nhưng
  mọi event của frame đã được xếp hàng trước đó: nếu hai viên đạn cùng trúng một con quái, handler
  thứ nhất hủy quái thì event thứ hai vẫn tới. Luôn kiểm tra `registry.valid()` cho cả `self` và
  `other` ở đầu handler.
- `exit` chỉ gửi cho bên còn sống. `other` có thể đã bị hủy: kiểm tra `registry.valid()`.
- `trigger` trong event là `true` nếu một trong hai là trigger.

Tìm cặp dùng một lưới đều: chỉ những collider chung ô mới được so. Cỡ ô mặc định 64, chỉnh bằng
njin::collision_set_cell_size() cho gần cỡ collider phổ biến của game. Trên máy thử, bản Release,
2000 hộp di chuyển mất khoảng 1 ms mỗi frame.

## Di chuyển có vật cản

njin::collision_move() dời một entity, dừng trước collider không phải trigger và trước các ô
của `collider_tiles`, rồi ghi vị trí mới vào transform:

```cpp
const njin::collision_move_result r = njin::collision_move(ctx, hero, velocity * dt);
if (r.hit_y && velocity.y > 0)
  on_ground = true;
if (r.hit_x)
  velocity.x = 0;
```

- Đi trục ngang trước rồi trục dọc, nên trượt dọc theo tường.
- Mỗi trục được **quét liên tục**: bước dài đến đâu cũng không xuyên qua tường mỏng.
- Vật cản đang chồng sẵn lên entity bị bỏ qua, để entity bị kẹt vẫn đi ra được.
- Bỏ qua collider của các entity con trực tiếp, để vũ khí không chặn chủ.
- Hình tròn được di chuyển như hình hộp bao quanh nó.

## Tilemap làm vật cản

Gắn một collider `collider_tiles` lên entity có njin::tilemap: mọi ô không trống thành vật cản
cho njin::collision_move(), truy vấn và raycast, với `layer`/`mask` như collider thường.

```cpp
reg.emplace<njin::collider>(level, njin::collider{.shape = njin::collider_tiles, .layer = layer_wall});
```

Ô tilemap không sinh event va chạm. njin::tilemap_move() vẫn dùng được khi chỉ cần va chạm với
một tilemap.

## Truy vấn và raycast

| Hàm | Dùng cho |
|---|---|
| njin::collision_overlap_rect() | Vùng đánh của một đòn chém |
| njin::collision_overlap_circle() | Vùng nổ, tầm phát hiện |
| njin::collision_overlap_point() | Chuột đang chỉ vào gì (dùng njin::scr2w()) |
| njin::collision_raycast() | Tầm nhìn, đạn tức thời, laser, kiểm tra đứng trên đất |

Truy vấn chạy ngay khi gọi, trên vị trí hiện tại, và duyệt qua mọi collider: nhanh với vài trăm
collider, nhưng đừng gọi hàng nghìn lần mỗi frame. Raycast trả về vật trúng **gần nhất**, mặc
định bỏ qua trigger, và có tham số `ignore` để bỏ qua người bắn.

## Dò lỗi

njin::collision_set_debug() vẽ khung mọi collider hộp và tròn: xanh lá là vật cản, vàng là
trigger, xám là đang tắt.
