# Script Lua {#scripting}

Trang này cho game viết luật chơi và hành vi của entity bằng **Lua 5.4** thay vì C++: sửa file `.lua`, lưu lại,
và game đang chạy đổi theo ngay, không phải biên dịch lại. Phần C++ vẫn dựng thế giới, nạp tài nguyên và giữ những
gì cần nhanh; script gọi vào đó qua module `njin`. Mọi thứ khai báo trong `njin_script.h`; Lua và sol2 nằm trong
engine, game không include header của chúng.

Cần biết trước: @ref ecs (entity, component, system) và @ref getting_started. Phần 2D dùng các khối của
@ref platformer; phần 3D dùng @ref graphics_3d.

Game không dùng script thì không tốn gì: máy Lua chỉ được tạo ở lần gọi script_* đầu tiên.

## Chạy script và gọi hàm

script_run_file() chạy một file `.lua` (đường dẫn như mọi tài nguyên); hàm và biến toàn cục nó tạo ra dùng được
từ C++. script_call() gọi một hàm theo tên (có dấu chấm thì tìm trong bảng), script_set_global() và
script_get_global() đọc ghi biến.

```cpp
njin::script_run_file(ctx, "scripts/rules.lua");
const njin::script_result r = njin::script_call(ctx, "rules.coin_value", {3.0});
if (r.ok)
  NJIN_INFO("một xu đáng %g điểm", std::get<double>(r.value));
njin::script_set_global(ctx, "config.difficulty", 2.0);
```

Giá trị đi qua lại là njin::script_value: nil, bool, số (`f64`), chuỗi, vec2, vec3 hay entity. Bên Lua, entity là
một số nguyên.

## Gọi C++ từ Lua

script_register() đặt một hàm C++ dưới một tên bên Lua. Hàm có thể nhận và trả bool, số, chuỗi, vec2, vec3,
entity, và nhận `context &` ở đầu; tham số Lua thiếu hay sai kiểu thành giá trị mặc định.

```cpp
njin::script_register(ctx, "add_score", [](int points) { score += points; });
njin::script_register(ctx, "game.player", [&] { return player; });
```

```lua
add_score(10)
local p = njin.position(game.player())
```

Lỗi C++ ném ra trong hàm đó thành lỗi Lua, có file và dòng của chỗ gọi.

## Script gắn trên entity

script_attach() gắn một file vào một entity. File trả về một bảng, như một lớp; mỗi entity có một bảng `self`
riêng, nhận làm tham số đầu của mọi hàm:

| Hàm | Khi nào |
|---|---|
| `on_start(self)` | Một lần, ở lần cập nhật đầu tiên sau khi gắn |
| `on_update(self, dt)` | Mỗi frame, trong `phase_update`, trước system của game |
| `on_render(self)` | Mỗi frame, trong `phase_render`, để vẽ bằng `njin.draw_*` |
| `on_destroy(self)` | Khi entity bị hủy hay script bị gỡ (script_detach()) |
| `on_reload(self)` | Sau khi file được nạp lại (hot reload) |

`self.entity` là entity. Dữ liệu game ghi vào `self` (máu, số xu, trạng thái) ở lại với entity; C++ đọc ghi nó bằng
script_field() và script_set_field(). Nhiều entity dùng chung một file thì file chỉ nạp một lần.

Ví dụ một nhân vật platformer: script đọc phím, đưa vào njin::platformer_body, rồi lật sprite và đổi clip theo
trạng thái mà bộ điều khiển trả về.

@include script_player.lua

Phía C++ dựng entity, đăng ký hàm cho script và gắn file:

@include script_host.cpp

## Module njin

Mọi hàm của module chỉ gọi thẳng API C++ cùng tên, không thêm hành vi. Entity là số nguyên; vị trí là `vec2` hay
`vec3` (cộng, trừ, nhân với số, `:length()`, `:normalized()`, `:dot()`, `:cross()`). Màu là bốn số `r, g, b, a`
(0..1, `a` có thể bỏ).

| Nhóm | Hàm |
|---|---|
| Thời gian | `delta`, `elapsed`, `time_scale`, `set_time_scale` |
| Ngẫu nhiên | `random`, `random_range(lo, hi)`, `random_int(lo, hi)` |
| Entity | `entity_create`, `entity_destroy`, `entity_valid` |
| Transform | `position`, `set_position`, `rotation`, `set_rotation`, `scale`, `set_scale`; bản 3D thêm `3d`: `position3d`... |
| Nhập liệu | `key_pressed("space")`, `key_held`, `key_released`, `mouse_pos`, `mouse_delta`, `mouse_wheel`, `mouse_pressed("left")`, `action_pressed("jump")`, `action_held`, `action_released`, `axis("move")` |
| Hẹn giờ | `after(giây, hàm)`, `every(giây, hàm, số_lần)`, `cancel(id)`, `tween_move`, `tween_scale`, `tween_rotate`, `tween_value(từ, đến, giây, hàm)`, `tween_cancel` |
| Âm thanh | `sound_load`, `sound_play`, `sound_play_at(id, vec2)`, `sound_play3d(id, vec3 hay entity)`, `sound_stop` |
| Scene | `scene_set(tên)`, `scene_fade(tên)` |
| Vẽ 2D | `draw_rect`, `draw_circle`, `draw_line`, `draw_text` |
| Thân 2D | `platformer_input(e, move_x, jump, jump_held, drop)`, `platformer(e)`, `platformer_set_velocity`, `topdown_input(e, vec2, dash)`, `topdown(e)`, `topdown_set_velocity` |
| Va chạm 2D | `collision_move(e, vec2)`, `overlap_rect(x, y, w, h)`, `overlap_point(vec2)`, `raycast2d(từ, đến, bỏ_qua)` |
| Sprite | `sprite_flip(e, x, y)`, `sprite_visible`, `sprite_tint`, `anim_play(e, clip)`, `anim_stop`, `anim_resume`, `anim_current`, `anim_set(e, tham_số, giá_trị)`, `anim_trigger` |
| Camera 2D | `camera_spawn(zoom, vec2)`, `camera_follow(camera, mục_tiêu, {offset, deadzone, lookahead, smoothing, bounds = {x, y, w, h}})` |
| Tilemap | `tile_get(map, cột, hàng)`, `tile_set`, `tile_cell(map, vec2)`, `tile_solid(map, vec2)` |
| Hạt 2D | `particles_burst(e, số)`, `particles_spawn(mẫu, vec2, số)` |
| Vật lý 3D | `raycast3d(gốc, hướng, xa_nhất)`, `body_velocity`, `body_set_velocity`, `body_impulse`, `character_move(e, vec3)`, `character_position`, `character_grounded` |
| Log | `log`, `warn`, `error` (và `print`), kèm file và dòng của script |

Hàm trả về một bảng: `platformer(e)` có `velocity`, `grounded`, `on_slope`, `on_wall`, `facing`, `jumped`, `landed`;
`topdown(e)` có `velocity`, `facing`, `moving`, `dashing`; `collision_move` có `moved`, `hit_x`, `hit_y`, `grounded`,
`other_x`, `other_y`; `raycast2d` và `raycast3d` có `point`, `normal`, `distance`, `entity` (và `body` cho 3D), hoặc
nil nếu không trúng. Entity không có component cần thiết thì hàm không làm gì (hay trả nil).

`platformer_input` và `topdown_input` ghi vào `input` của thân như phía C++: lần bấm nhảy hay lướt được giữ đến
nhịp vật lý kế tiếp, nên gọi mỗi frame là đủ.

## Sửa script khi game đang chạy

Khi hot_reload_enable() bật (@ref rendering), engine theo dõi cả file script. File gắn trên entity được chạy lại và
các hàm mới thay hàm cũ cho mọi entity dùng nó, còn dữ liệu trong `self` giữ nguyên; `on_reload(self)` được gọi sau
đó. File nạp bằng script_run_file() được chạy lại từ đầu. File mới có lỗi thì giữ các hàm cũ và ghi lỗi vào log.
Mỗi lần nạp lại gửi một njin::asset_reloaded có `script = true`.

## Lỗi và an toàn

Lỗi Lua (cú pháp hay lúc chạy) không làm game dừng: hàm trả về thất bại và log ghi thông báo kèm file và dòng, ví dụ
`scripts/enemy.lua:12: attempt to index a nil value`. Một `on_update` lỗi thì lỗi mỗi frame: mỗi thông báo chỉ ghi
một lần, cho đến lần hot reload kế tiếp.

Script chạy trong một máy Lua bị giới hạn: không có `os.execute`, `io.popen`, `package.loadlib` hay nạp bytecode
(`load` chỉ nhận chữ); mặc định không có `io`, `dofile`, `loadfile`. `require("ai.patrol")` vẫn dùng được: nó tìm
`ai/patrol.lua` (hay `scripts/ai/patrol.lua`) như mọi tài nguyên. Công cụ tin cậy (trình sửa màn, bản dựng) có thể
mở `io` bằng `script_init(ctx, {.allow_io = true})`, gọi trước mọi hàm script_* khác.

## Hiệu năng và giới hạn

Đo bằng harness với 1000 entity có `on_update` rỗng: bản Debug của engine thêm khoảng 0,8 ms mỗi frame, bản Release
khoảng 0,3 ms. Hàm của module `njin` chỉ là một lệnh gọi C++, nhưng mỗi lần qua lại giữa Lua và C++ vẫn tốn hơn một lệnh
gọi C++ thường: vòng lặp nóng trên hàng nghìn vật (hạt, đạn) nên để ở C++.

- Module chỉ có phần gameplay chung; các phần khác của engine (model 3D, UI, ánh sáng) chưa có hàm Lua. Game cần thì
  tự thêm bằng script_register().
- `on_update` chạy trong `phase_update`; script không có `fixed_update` riêng: điều khiển thân vật lý qua
  `platformer_input`, `topdown_input`, `character_move` như phía C++.
- Dữ liệu `self` giữ qua hot reload nhưng không được lưu vào file save; save game vẫn do C++ (@ref window_files).
