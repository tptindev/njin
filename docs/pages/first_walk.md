# Nhân vật đi 8 hướng trong 50 dòng {#first_walk}

Bản top-down của @ref first_jump : một nhân vật nhìn từ trên xuống, đi 8 hướng, lướt, va vào tường đá, với
**dưới 50 dòng code** (46 dòng, không tính dòng trống và dòng chú thích). Nó dùng njin::tilemap làm bản
đồ, njin::topdown_body làm nhân vật, và njin::topdown_input_map nối phím vào nhân vật.

@include first_walk.cpp

Bấm mũi tên hoặc WASD để đi, Space để lướt. Nhân vật trượt dọc tường đá thay vì dính vào, và camera
không bao giờ lộ ra ngoài bản đồ.

@image html first_walk.gif "Đi chéo, lướt (giữ Space khi đang đi), rồi đi xuống. Hình chữ nhật vàng là nhân vật, camera bám theo"

## Chuẩn bị

Cần một ảnh tileset gồm các ô 16 x 16 tại `assets/tiles.png`. Dùng luôn ảnh của game mẫu:
`src/games/topdown/assets/tiles.png` (ô 0 là cỏ, ô 7 là tường đá). Thư mục `assets` phải nằm cạnh
file exe: gọi `njin_add_assets(my_game assets)` trong CMake (xem @ref getting_started).

## Từng bước

### 1. Phím: hai axis và một action

@code
const njin::axis_handle move_x = njin::axis_define(ctx, "move_x", {{njin::key_left, njin::key_right}, {njin::key_a, njin::key_d}});
const njin::axis_handle move_y = njin::axis_define(ctx, "move_y", {{njin::key_up, njin::key_down}, {njin::key_w, njin::key_s}});
const njin::action_handle dash = njin::action_define(ctx, "dash", {njin::key_space, njin::pad_face_down});
@endcode

Top-down cần **hai** axis, ngang và dọc, vì engine không có axis hai chiều. Mỗi axis nhận nhiều cặp phím
(mũi tên và WASD cùng chạy) và một trục tay cầm. Xem @ref input.

### 2. Bản đồ: cỏ để đi, đá để chặn

Vòng `for` lồng nhau đặt cả bản đồ 20 x 12 ô là cỏ (ô 0), trừ đường viền là đá (ô 7). Ba việc để đá chặn
đường còn cỏ thì không:

- Gắn njin::collider với `shape = collider_tiles` lên entity của bản đồ: mọi ô không trống thành vật cản.
- njin::tilemap_set_shape() cho ô số 0 hình `tile_none`: cỏ **chỉ để nhìn**, đi xuyên qua được.
- Ô đá giữ hình mặc định (`tile_solid`), nên chặn mọi phía.

Cùng một bản đồ phục vụ cả hai việc (vẽ và va chạm) nên không cần một layer riêng cho tường. Đọc thêm ở
@ref tilemap.

### 3. Nhân vật: njin::topdown_body

| Component | Vai trò |
|---|---|
| njin::transform | Vị trí. `pos` là **chân** vì `offset` của collider là `{0, -4}` |
| njin::collider | Hộp va chạm 10 x 8, nhỏ và thấp để nhân vật đi sát tường mà không kẹt |
| njin::topdown_body | Đi, tăng tốc, giảm tốc, lướt |

Engine di chuyển nó trong `phase_fixed_update` bằng njin::collision_move(). Hai điều đáng biết:

- **Đi chéo không nhanh hơn**: độ dài của hướng đi lớn hơn 1 được đưa về 1, kể cả khi bạn cộng hai axis.
- **Lướt** mặc định tắt. `dash_speed = 230.0f` bật nó; `dash_time` và `dash_cooldown` chỉnh độ dài và
  thời gian hồi.

njin::topdown_input_map đọc hai axis và action giúp bạn: không cần system nào để xử lý phím.

### 4. Camera: giới hạn trong bản đồ

njin::camera_follow bám theo `target`. `bounds` là vùng thế giới mà khung nhìn không được vượt ra ngoài; ở
đây là cả bản đồ, 320 x 192 pixel. Khi nạp bản đồ từ Tiled hoặc LDtk thì lấy thẳng từ njin::level_bounds()
(xem @ref level) thay vì tự tính.

## Chỉnh cảm giác

@code
njin::topdown_body body{};
body.speed = 78.0f;        // đi chậm hơn
body.accel = 600.0f;       // trượt lâu hơn khi bắt đầu đi
body.decel = 600.0f;       // trượt lâu hơn khi thả phím
body.dash_speed = 230.0f;
body.dash_time = 0.2f;     // lướt dài hơn
@endcode

`accel` và `decel` lớn (mặc định 900 và 1300) là nhân vật dừng và chạy tức thì; nhỏ là cảm giác trơn
như đi trên băng.

## Bước tiếp theo

- @ref topdown : sắp xếp theo Y (cây che nhân vật), quái đuổi theo bằng A\*, kiếm
- @ref tilemap : viết bản đồ bằng chữ thay cho vòng `for` (njin::tilemap_from_text()), không cần editor
- @ref level : hoặc thay đoạn đặt ô bằng một màn vẽ trong Tiled hoặc LDtk
- @ref sprites và @ref animation : thay hình chữ nhật bằng nhân vật có animation
- @ref cheatsheet : "muốn làm X thì dùng hàm nào"
