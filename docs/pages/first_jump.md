# Nhân vật nhảy được trong 50 dòng {#first_jump}

Trang này dựng một nhân vật platformer chạy và nhảy được trên một bản đồ ô vuông, với **dưới
50 dòng code** (47 dòng, không tính dòng trống và dòng chú thích). Nó dùng ba thứ: njin::tilemap
làm mặt đất, njin::platformer_body làm nhân vật, và njin::platformer_input_map nối phím vào
nhân vật.

@include first_jump.cpp

Bấm mũi tên trái/phải để chạy, Space để nhảy. Nhân vật rơi xuống đất, chạy được, nhảy được, và nhảy
xuyên từ dưới lên bục rồi đứng lên trên nó.

## Chuẩn bị

Cần một ảnh tileset gồm các ô 16 x 16 tại `assets/tiles.png`. Dùng luôn ảnh của game mẫu:
`src/games/platformer/assets/tiles.png` (ô 0 là đất có cỏ, ô 1 là đất, ô 3 là bục). Thư mục `assets`
phải nằm cạnh file exe: gọi `njin_add_assets(my_game assets)` trong CMake để nó được copy mỗi lần
build (xem @ref getting_started và @ref window_files).

## Từng bước

### 1. Phím: axis và action

@code
const njin::axis_handle move = njin::axis_register(ctx, "move");
njin::axis_bind_keys(ctx, move, njin::key_left, njin::key_right);
const njin::action_handle jump = njin::action_register(ctx, "jump");
njin::action_bind_key(ctx, jump, njin::key_space);
@endcode

Một **axis** là một trục từ -1 đến 1, ở đây gộp hai phím thành trục ngang. Một **action** là một
tên logic ("jump") gắn với một hoặc nhiều phím. Game không hỏi "phím Space có đang bấm không" mà
hỏi "action `jump` có đang bấm không", nên sau này đổi phím hay thêm tay cầm chỉ là thêm một
dòng `bind`. Xem @ref input.

### 2. Bản đồ: njin::tilemap

njin::tilemap_set(map, x, y, id) đặt ô `(x, y)` thành ô số `id` của tileset. Không cần khai báo kích thước
bản đồ: đặt ô ở đâu, bản đồ mở rộng tới đó. Ô có thể ở tọa độ âm.

Bản đồ cần hai thứ nữa để nhân vật đứng lên được:

- Gắn một njin::collider có `shape = collider_tiles` lên entity của bản đồ. Khi đó mọi ô không trống
  thành vật cản.
- njin::tilemap_set_shape() cho ô số 3 hình `tile_one_way`: một **bục một chiều**, chỉ đỡ từ trên xuống,
  còn nhảy từ dưới lên thì xuyên qua. Hình va chạm đặt theo số thứ tự ô nên áp dụng cho mọi ô loại đó.
  Ngoài ra còn có dốc, xem @ref platformer.

`transform` của entity là góc trên trái của ô (0, 0), nên ở đây ô hàng 10 có mặt trên nằm ở
`y = 160`.

### 3. Nhân vật: njin::platformer_body

Nhân vật là một entity có ba component:

| Component | Vai trò |
|---|---|
| njin::transform | Vị trí. Với nhân vật này, `pos` là **chân** vì `offset` của collider là `{0, -7}` |
| njin::collider | Hộp va chạm 10 x 14 |
| njin::platformer_body | Chạy, nhảy, rơi, ghi lại `grounded`... |

`platformer_body{}` với mọi giá trị mặc định đã cho một cú nhảy tốt: có **coyote time** (rời mép vẫn nhảy
được một chút), **jump buffer** (bấm nhảy sớm một chút vẫn được tính), và nhảy thấp khi thả nút sớm.
Engine di chuyển nó trong `phase_fixed_update` nên kết quả không phụ thuộc FPS.

njin::platformer_input_map nối axis `move` và action `jump` vào body: engine đọc chúng mỗi frame và ghi
`body.input` giúp bạn. Không cần viết một system nào để xử lý phím.

### 4. Camera: njin::camera_follow

njin::camera_spawn(ctx, 3.0f) tạo camera phóng to 3 lần (hợp với pixel art), và njin::camera_follow cho nó
bám theo `target`. Xem @ref camera.

### 5. Vẽ nhân vật

Chưa có sprite, nên nhân vật là một hình chữ nhật vàng vẽ trong `phase_render` (không gian thế giới, đi
qua camera). njin::rect_from_center() đổi tâm và kích thước thành hình chữ nhật. Khi có ảnh nhân vật, đổi thành
njin::sprite và njin::sprite_anim, xem @ref sprites.

## Chỉnh cảm giác nhảy

Mọi thông số nằm ngay trên njin::platformer_body. Tạo body, sửa, rồi `emplace`:

@code
njin::platformer_body body{};
body.jump_speed = 350.0f;        // nhảy cao hơn
body.air_jumps = 1;              // nhảy đôi
body.wall_slide_speed = 55.0f;   // trượt tường
body.wall_jump = {150.0f, 300.0f}; // nhảy khỏi tường
reg.emplace<njin::platformer_body>(player, body);
@endcode

Độ cao cú nhảy xấp xỉ `jump_speed² / (2 * gravity)`: với giá trị mặc định (300 và 1000) là chừng
45 pixel, gần ba ô (đo thực tế được 42 pixel vì vật lý đi theo nhịp rời rạc). Bục ở hàng 8 cao hơn mặt
đất 32 pixel nên nhảy lên được; đặt nó cao hơn 42 pixel thì phải tăng `jump_speed`.

## Bước tiếp theo

- @ref platformer : dốc, bục di chuyển, nhảy tường, camera giới hạn trong màn chơi
- @ref level : thay đoạn đặt ô bằng một màn vẽ trong Tiled hoặc LDtk
- @ref sprites và @ref animation : thay hình chữ nhật bằng nhân vật có animation
- @ref cheatsheet : "muốn làm X thì dùng hàm nào"
