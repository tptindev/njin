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
| `on_fixed_update(self, dt)` | Theo nhịp cố định, trong `phase_fixed_update` (dt là bước cố định), ngay trước bước vật lý 3D |
| `on_update(self, dt)` | Mỗi frame, trong `phase_update`, trước system của game |
| `on_render(self)` | Mỗi frame, trong `phase_render`, để vẽ bằng `njin.draw_*` (cả 3D, giữa `njin.begin_3d` và `njin.end_3d`) |
| `on_ui(self)` | Mỗi frame, trong `phase_post_render` (không gian màn hình), để dựng giao diện bằng `njin.ui_*` |
| `on_destroy(self)` | Khi entity bị hủy hay script bị gỡ (script_detach()) |
| `on_reload(self)` | Sau khi file được nạp lại (hot reload) |
| `on_load(self)` | Sau khi script_load_state() đổ dữ liệu đã lưu vào `self` (@ref script_save) |

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
| Tìm đường 3D | `nav3d_path(navmesh, from, to)`, `nav3d_set_target(agent, đích)`, `nav3d_stop`, `nav3d_position`, `nav3d_velocity`, `nav3d_arrived` (xem @ref nav_3d) |
| Vẽ 3D | `begin_3d({position, target, up, fovy, near, far})`, `end_3d`, `draw_cube3d(tâm, cỡ, màu)`, `draw_sphere3d(tâm, bán_kính, màu)`, `draw_cylinder3d(từ, đến, bán_kính, màu)`, `draw_capsule3d`, `draw_plane3d(tâm, vec2, màu)`, `draw_shape3d({kind, position, rotation, size, radius, height, thickness, rounding}, màu)`, `material3d_set({specular, shininess, emission, rim, unlit, cast_shadows, reflect, world_uv})` |
| Model 3D | `model_load(đường_dẫn)`, `model_valid`, `draw_model(m, transform, {anim, time, loop, blend_anim, blend_time, blend, morphs = {tên = trọng_số}, tint})`, `model_anim_count`, `model_anim_find`, `model_anim_name`, `model_anim_duration(m, tên hay chỉ_số)`, `model_morph_count`, `model_bone_count`, `model_bone_find`, `model_bone_position(m, xương, transform, tư_thế)`, `model3d_set(e, {model, anim, time, loop, speed, tint, visible})`, `model3d(e)` |
| Ánh sáng 3D, trời | `light3d_set({direction, color, ambient, shadows, shadow_range, shadow_softness, fog_color, fog_density})`, `light3d_get`, `light3d_add({kind = "point"/"spot", position, direction, color, intensity, radius, cone, softness, shadows})`, `draw_sky3d({hour, latitude, season, north, weather, ...})`, `sky3d_sun_direction`, `weather3d_preset("clear"/"overcast"/"rain"/"snow"/"fog")` |
| Ánh sáng 2D | `lighting_set({enabled, ambient, exposure})`, `light2d_set(e, {kind, color, temperature, intensity, radius, size, angle, cone, softness, height, elevation, cast_shadows, enabled})` |
| Giao diện | `ui_begin({id, title, anchor, pivot, offset, width, background, navigable})`, `ui_end`, `ui_row(số_cột)`, `ui_label`, `ui_space`, `ui_button(nhãn, bật)`, `ui_toggle(nhãn, giá_trị)`, `ui_slider(nhãn, giá_trị, min, max, bước, phần_trăm)`, `ui_choice(nhãn, chỉ_số, {lựa_chọn...})`, `ui_progress`, `ui_back`, `ui_active`, `ui_mouse_over`, `ui_last_rect`, `ui_toast(chữ, giây)` |
| Chữ | `font_load(đường_dẫn, cỡ)`, `draw_text_font(chữ, vec2, cỡ, font, màu)`, `text_measure(chữ, cỡ, font)`, `tr(khóa)`, `trf(khóa, ...)` |
| Spline | `spline_create({điểm...}, {kind = "catmull_rom"/"bezier", closed, alpha, steps, owner})`, `spline_set_points`, `spline_destroy`, `spline_valid`, `spline_length`, `spline_point(id, t)`, `spline_point_at(id, khoảng_cách)`, `spline_tangent_at`, `spline_nearest(id, điểm)`, `spline_follow(id, {distance, speed, end}, dt)`, `spline_draw_debug` |
| Hiệu ứng 3D | `post3d_set({ssao, ssao_radius, ssao_half, ssr, motion_blur, shafts, flare, taa, taa_sharpen})`, `post3d_off`, `decal3d_add({position, normal hay rotation, size, color, lifetime, fade, paint})`, `decal3d_remove`, `decal3d_clear` |
| Log | `log`, `warn`, `error` (và `print`), kèm file và dòng của script |

Hàm trả về một bảng: `platformer(e)` có `velocity`, `grounded`, `on_slope`, `on_wall`, `facing`, `jumped`, `landed`;
`topdown(e)` có `velocity`, `facing`, `moving`, `dashing`; `collision_move` có `moved`, `hit_x`, `hit_y`, `grounded`,
`other_x`, `other_y`; `raycast2d` và `raycast3d` có `point`, `normal`, `distance`, `entity` (và `body` cho 3D), hoặc
nil nếu không trúng. Entity không có component cần thiết thì hàm không làm gì (hay trả nil).

`platformer_input` và `topdown_input` ghi vào `input` của thân như phía C++: lần bấm nhảy hay lướt được giữ đến
nhịp vật lý kế tiếp, nên gọi mỗi frame là đủ.

## Cảnh 3D, ánh sáng và giao diện từ Lua {#script_3d_ui}

Một script dựng được cả một cảnh 3D: nạp model, vẽ nó có ánh sáng và bóng đổ dưới bầu trời theo giờ, thêm đèn, cho
một quả bóng chạy theo spline, và dựng menu. Vẽ 3D nằm trong `on_render`, giữa `njin.begin_3d` và `njin.end_3d` như
phía C++; giao diện nằm trong `on_ui`, vì các hàm `ui_*` của engine chỉ chạy trong `phase_post_render`.

@include script_scene3d.lua

Vài quy ước của nhóm hàm này:

- Vị trí và màu nhận cả kiểu sẵn có lẫn bảng: `njin.vec3(1, 2, 3)` hay `{1, 2, 3}` hay `{x = 1, y = 2, z = 3}`; màu là
  bảng `{r, g, b, a}` hay `{1, 0, 0, 1}`. Transform là bảng `{position, rotation, scale}` (scale là một số hay một
  `vec3`), hoặc chỉ một `vec3` vị trí.
- Animation gọi theo tên clip hay chỉ số (từ 0, như phía C++). Trọng số morph đặt theo tên trong `morphs`.
- `model_load` nhớ model theo đường dẫn: gọi lại với cùng file trả về cùng số, không nạp lần hai.
- Hàm trả về hai giá trị theo kiểu Lua: `changed, value = njin.ui_slider("Âm lượng", value, 0, 1)`. `ui_choice` đếm
  từ 1 như bảng Lua.
- Spline là một số do `spline_create` trả về, sống đến `spline_destroy`, hay đến khi entity `owner` bị hủy. Một
  spline của bảng hai số là 2D (trả về `vec2`), ba số là 3D. Trạng thái đi dọc đường (`distance`, `speed`, `end`,
  `finished`) nằm trong một bảng của script, `spline_follow` sửa nó tại chỗ.
- Số không hợp lệ (model chưa nạp, spline đã hủy, entity không còn, kiểu thời tiết lạ) là lỗi Lua có file và dòng của
  script, ví dụ `scripts/scene.lua:21: njin.draw_model: 424242 is not a loaded model`; game vẫn chạy tiếp.

## Lưu và nạp game {#script_save}

Dữ liệu trong `self` (máu, túi đồ, cửa đã mở) đi vào save game bằng script_save_state(): nó trả về một
njin::json_value để ghi cùng phần còn lại của save bằng json_save() (@ref window_files). Khi nạp, game dựng lại
màn, gắn script, rồi gọi script_load_state(): mỗi script nhận lại các trường đã lưu, sau đó `on_load(self)` chạy.

Entity tạo lại có số khác, nên mỗi entity cần lưu phải có một **tên lưu** giống nhau ở mọi lần dựng màn:
script_set_save_id() phía C++, hay `self.save_id = "cua_kho"` trong script. Entity không có tên lưu thì không được
lưu (đạn, hạt, những gì dựng lại từ đầu là đủ).

@include script_save.cpp

| Trong `self` | Lưu thành |
|---|---|
| Số, bool, chuỗi | Như cũ (số nguyên vẫn là số nguyên khi nạp) |
| `njin.vec2`, `njin.vec3` | `{"$vec2": [x, y]}`, `{"$vec3": [x, y, z]}`, nạp lại đúng từng số |
| Bảng có khóa 1..n liền nhau | Mảng JSON |
| Bảng khác | Object; khóa số nguyên ghi là `"#n"` |
| `self.entity` | Không lưu: script_attach() đặt lại |
| Hàm, userdata khác, số không hữu hạn, vòng tham chiếu, khóa không phải chuỗi hay số nguyên | Bỏ, kèm một cảnh báo có đường dẫn, ví dụ `chest_1.self.inv[3]` |

Hai điều cần nhớ:

- `on_start` của script vừa gắn vẫn chạy ở lần cập nhật đầu tiên, **sau** `on_load`. Đặt giá trị mặc định trong
  bảng của file (`M.hp = 10`: `self.hp` đọc ra 10 cho đến khi entity tự đặt) hay viết
  `self.hp = self.hp or 10`, để `on_start` không đè lên dữ liệu vừa nạp.
- Số entity cất trong `self` (mục tiêu đang đuổi) được lưu như số thường và không còn đúng sau khi nạp. Lưu tên lưu
  của entity đó thay vì số của nó.

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

- Module có phần gameplay chung, vẽ 3D, ánh sáng, giao diện, spline và hiệu ứng 3D; các phần khác (địa hình, nước,
  vật lý mềm, video, âm thanh 3D theo voice) chưa có hàm Lua. Game cần thì tự thêm bằng script_register().
- Chuyển động và vật lý cần chạy như nhau ở mọi FPS thì đặt trong `on_fixed_update`: nó chạy đúng số bước cố định
  của engine (mặc định 60 lần mỗi giây, config::fixed_hz), trước bước vật lý 3D, nên `njin.body_set_velocity` đặt ở
  đó có hiệu lực ngay trong bước. Không script nào có hàm này thì engine không đi qua các entity ở nhịp cố định.
- Dữ liệu `self` giữ qua hot reload, và vào file save qua script_save_state() khi entity có tên lưu
  (@ref script_save). Script không tự ghi file được (không có `io`): việc lưu do C++ gọi.
