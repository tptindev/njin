# Đồ họa 3D {#graphics_3d}

njin vẽ được thế giới 3D: camera phối cảnh, hình khối, hình SDF mịn, model glTF, ánh sáng có bóng đổ,
vật liệu, hiệu ứng, hạt, instancing, vật lý và va chạm, chọn vật bằng chuột và gizmo để debug. Mọi thứ
đều qua `njin.h` (khai báo trong `njin_3d.h`, `njin_physics3d.h` và `njin_gizmo.h`), không cần raylib
hay thư viện vật lý.

Cần biết trước: @ref drawing, @ref game_loop và @ref rendering. Chạy `njin_fps` (bắn súng góc nhìn thứ
nhất), `njin_sokoban` (đẩy thùng 2.5D) và `njin_platformer3d` (nhảy bục góc nhìn thứ ba) để xem mọi
thứ dưới đây trong một game thật.

@image html graphics_3d.png "Hình lưới (hộp, cầu), hình SDF (hộp bo góc, viên nang, xuyến) và 8 hộp instanced dưới nắng có bóng đổ, cùng gizmo 2D và 3D"

@include draw_3d.cpp

## Một lần vẽ 3D

Mọi lệnh vẽ 3D nằm giữa begin_3d() và end_3d(), trong `phase_render`. njin::camera3d là camera phối
cảnh: `position`, `target`, `up` và góc nhìn dọc `fovy`. Trục y hướng lên, hệ tay phải như glTF: nhìn
theo `-z` thì `+x` ở bên phải. Tỉ lệ khung hình lấy theo screen_size(), nên màn hình ảo vẫn đúng.

Lệnh vẽ được **ghi lại** và vẽ thật ở end_3d(), theo đúng thứ tự gọi: trước hết engine tính bóng đổ
từ tất cả, rồi mới vẽ từng hình. Vì vậy thứ tự material3d_set(), fx3d_set() và lệnh vẽ vẫn có ý nghĩa
như bình thường, còn light3d_add() thì chiếu lên mọi hình của lần vẽ dù gọi trước hay sau.

Vẽ 2D trước begin_3d() thì 3D đè lên; vẽ 2D sau end_3d() (cùng `phase_render`) thì đè lên 3D. UI trong
`phase_post_render` luôn ở trên cùng. Một frame có thể có nhiều lần vẽ 3D (ví dụ một cảnh nhỏ trong UI).

## Hình khối

| Hàm | Là gì | Khi nào dùng |
|---|---|---|
| draw_cube3d(), draw_sphere3d(), draw_plane3d(), draw_cylinder3d(), draw_capsule3d() | Lưới tam giác | Nhiều, rẻ: tường, sàn, đạn |
| draw_shape3d() với njin::shape3d | Hình SDF: cầu, hộp bo góc, viên nang, trụ bo cạnh, xuyến | Vật cần mịn khi nhìn gần: nhân vật, vật phẩm |
| draw_instanced3d() | Hàng nghìn hình lưới bằng một lệnh vẽ | Rừng, đám đông, gạch lát |
| draw_model() | Model glTF/OBJ nạp bằng model_load() | Đồ vật, nhân vật làm trong Blender |

Hình SDF được tính trên từng điểm ảnh (sphere tracing trong hộp bao của nó), nên viền luôn tròn ở mọi
cỡ và bo góc được, nhưng tốn hơn hình lưới. Nó vẫn nhận ánh sáng, đổ và nhận bóng, dùng
njin::material3d và njin::fx3d như hình khác.

## Model và vật liệu

model_load() nạp `.glb`, `.gltf` hoặc `.obj`, giữ màu và texture của từng vật liệu trong file.
model_material_get() và model_material_set() đọc và đổi vật liệu của từng phần:

@code
njin::model_material m = njin::model_material_get(ctx, crate, 0);
m.albedo = njin::texture_load(ctx, "assets/crate_wood.png");  // ảnh màu
m.normal = njin::texture_load(ctx, "assets/crate_wood_n.png"); // normal map
m.emission = njin::texture_load(ctx, "assets/crate_glow.png"); // ảnh phát sáng
m.shader = my_shader;                                          // shader riêng cho phần này
m.surface.specular = 0.1f;                                     // bề mặt mờ
njin::model_material_set(ctx, crate, 0, m);                    // -1 cho mọi phần
@endcode

Normal map không cần tangent trong file: shader tự dựng hệ trục từ đạo hàm màn hình.

## Ánh sáng

| Phần | Đặt bằng | Ghi chú |
|---|---|---|
| Mặt trời, ánh sáng nền | light3d_set() với njin::light3d | Có hiệu lực từ begin_3d() tiếp theo |
| Bóng đổ của mặt trời | `light3d::shadows`, `shadow_range`, `shadow_size`, `shadow_softness` | Hộp bóng quanh chỗ camera nhìn; mép bóng mềm 3 x 3 |
| Sương mù | `light3d::fog_color`, `fog_density` | Theo khoảng cách tới camera |
| Đèn điểm, đèn nón | light3d_add() với njin::light3d_source, mỗi lần vẽ | Tối đa njin::light3d_max (16), không đổ bóng |
| Bề mặt | material3d_set() với njin::material3d | Độ bóng, phát sáng, viền sáng (`rim`), `unlit`, `texture`, `cast_shadows` |

Ánh sáng là Lambert cộng điểm sáng bóng Blinn-Phong, cộng ánh sáng nền. `emission` cộng màu sau khi
chiếu sáng: bật bloom (post_fx_set()) để vật phát sáng lan sáng ra xung quanh.

## Hiệu ứng

Các hiệu ứng chung của @ref particles đều dùng được cho 3D:

| Hiệu ứng | Với 3D |
|---|---|
| camera_shake() | Rung camera của begin_3d(): độ lệch thành góc nhìn, góc nghiêng thành nghiêng camera |
| hitstop(), screen_flash(), post_fx_set(), chuyển scene | Như 2D, không cần gì thêm |
| Nháy màu, tan biến | fx3d_set() với njin::fx3d: tương đương njin::flash_fx và njin::dissolve_fx của sprite |
| Hạt | particles3d_spawn() dùng lại emitter 2D (njin::fx::explosion(), sparks(), dust()...) |

Hình 3D không phải entity, nên game tự giữ thời gian của hiệu ứng và đặt mức mỗi frame:

@code
// Mục tiêu trúng đạn: nháy trắng rồi tan trong 0.4 giây.
const float t = age / 0.4f;
njin::fx3d_set(ctx, {.flash = {1, 1, 1, 1 - t * 3}, .dissolve = t});
njin::draw_sphere3d(ctx, pos, 0.6f, njin::colors::red);
njin::fx3d_set(ctx, {});
@endcode

Hạt 3D quay mặt về camera, dừng trong hitstop, và `particles3d_desc::scale` đổi đơn vị pixel của các
mẫu njin::fx sang đơn vị thế giới 3D.

## Instancing

draw_instanced3d() vẽ `count` bản của một hình có sẵn hay một model bằng một lệnh vẽ, dữ liệu mỗi bản
nằm trong một bộ đệm instance như draw_instanced() của 2D. Không truyền shader thì shader có sẵn đọc:

| Thuộc tính | Nội dung |
|---|---|
| `instance0` | Vị trí `xyz`, tỉ lệ đều `w` (0 là 1) |
| `instance1` (từ 8 số) | Màu `rgba` |
| `instance2` (từ 12 số) | Góc xoay `xyz`, độ |
| `instance3` (16 số) | Tỉ lệ theo x, y, z (0 là 1) |

## Shader của game

shader_begin() trước một lệnh vẽ 3D thì hình đó vẽ bằng shader của game. Engine đặt sẵn các uniform
`vec3` `lightDir`, `lightColor`, `ambient`, `viewPos` nếu shader khai báo chúng, cùng `mvp`, `matModel`,
`matNormal`, `colDiffuse` theo tên chuẩn của raylib. Hình SDF luôn dùng shader của engine.

## Vật lý và va chạm {#physics3d}

`njin_physics3d.h` dựng trên [Jolt Physics](https://github.com/jrouwe/JoltPhysics) (giấy phép MIT, engine
kéo về khi build, game không thấy nó). Hình của body dùng cùng quy ước với njin::shape3d, nên một vật và
hình vẽ nó dùng chung số.

| Phần | Tạo bằng | Dùng cho |
|---|---|---|
| Body tĩnh | body3d_create() với `body3d_static` | Sàn, tường, bục cố định |
| Body kinematic | `body3d_kinematic`, rồi body3d_move_kinematic() mỗi bước | Bục di chuyển, thang máy, cửa: chở và đẩy vật khác |
| Body động | `body3d_dynamic` | Thùng, bóng, mảnh vỡ: rơi, va, lăn, bị đẩy; body3d_add_impulse() cho cú nổ |
| Nhân vật | character3d_create() | Người chơi, quái: viên nang đi trên sàn, leo bậc, trượt dọc tường, đẩy body động |
| Tia | physics3d_raycast() | Đạn, tầm nhìn, camera không xuyên tường; trả về body bị trúng |

Engine mô phỏng ở `phase_fixed_update`, **ngay sau** các system của game trong phase đó: game đặt vận tốc
hay vị trí đích, rồi vật lý chạy luôn trong cùng bước. Nhân vật được điều khiển bằng vận tốc, và **game tự
cộng trọng lực và cú nhảy** (để tự chỉnh cảm giác nhảy: coyote time, nhảy đôi...):

@code
// Mỗi bước cố định:
const bool on_ground = njin::character3d_grounded(ctx, player);
vertical = on_ground ? 0.0f : vertical - gravity * dt;
if (jump_pressed && on_ground)
  vertical = jump_speed;
// Đứng trên bục đang chạy thì cộng vận tốc của bục.
const njin::vec3 ground = njin::character3d_ground_velocity(ctx, player);
njin::character3d_set_velocity(ctx, player, walk + ground + njin::vec3{0, vertical, 0});
@endcode

Vẽ một body động theo đúng chỗ và góc vật lý tính ra bằng body3d_transform():

@code
const njin::transform3d t = njin::body3d_transform(ctx, crate);
njin::draw_shape3d(ctx, {.kind = njin::shape3d_box, .position = t.position, .rotation = t.rotation,
                         .size = {1, 1, 1}}, {0.7f, 0.5f, 0.3f, 1.0f});
@endcode

`body3d_desc::user` gắn một số của game vào body (chỉ số trong mảng, id entity): đọc lại bằng body3d_user()
từ body mà physics3d_raycast() hay character3d_ground_body() trả về.

## Chọn vật bằng chuột

| Hàm | Làm gì |
|---|---|
| camera3d_ray() | Tia từ camera qua một điểm trên màn hình, ví dụ mouse_pos() |
| camera3d_to_screen() | Điểm 3D ra màn hình, để đặt nhãn hay thanh máu |
| ray3d_box(), ray3d_sphere(), ray3d_plane() | Tia với hộp, cầu, mặt phẳng |
| ray3d_shape() | Tia với hình SDF, đúng như draw_shape3d() vẽ |
| ray3d_model() | Tia với từng tam giác của model |
| physics3d_raycast() | Tia với mọi body vật lý, trả về body gần nhất bị trúng |

@code
const njin::ray3d ray = njin::camera3d_ray(ctx, cam, njin::mouse_pos(ctx));
const njin::ray3d_hit hit = njin::ray3d_shape(ray, player_shape);
if (hit.hit && njin::mouse_pressed(ctx, njin::mouse_left))
  select_player();
@endcode

Trong 2D, dùng scr2w() cho vị trí chuột rồi collision_overlap_point() hoặc collision_raycast()
(@ref collision).

## Gizmo và inspector

Các hàm `gizmo_*` (njin_gizmo.h) vẽ hình để debug: đường, mũi tên, hộp, cầu, trục tọa độ, điểm, nhãn chữ,
cả 2D lẫn 3D. Gọi được ở mọi phase, vẽ đè lên trên cùng, và giữ lại `duration` giây nếu muốn thấy vết.
gizmos_set_visible() bật tắt tất cả. Xem thêm @ref debug.

Khi njin_inspector đang nối, ô World của nó tự chuyển sang 3D trong lúc game vẽ 3D: mỗi lệnh vẽ (cả
từng instance) là một chấm màu, cùng các đèn, hướng nắng, khung nhìn của camera game và gizmo. Kéo chuột
phải để xoay, chuột giữa để dời, cuộn để phóng.

@image html inspector_world_3d.png "Ô World của njin_inspector khi njin_sokoban chạy: sàn, tường, thùng và nhân vật là các chấm, khung xanh là camera của game, vạch vàng là hướng nắng"

## Chưa có

- Animation xương của model (glTF skin): model vẽ ở tư thế tĩnh.
- Bóng đổ của đèn điểm và đèn nón: chỉ mặt trời đổ bóng.
- Khớp nối (joint), body từ lưới tam giác của model, sự kiện va chạm: chưa có trong `njin_physics3d.h`.
- Component 3D trong ECS: hình 3D và body vật lý dùng trực tiếp, không phải entity.

## Game mẫu

| Game | Xem gì |
|---|---|
| `njin_fps` | Camera góc nhìn thứ nhất, model glTF (khẩu súng), bóng đổ, sương mù, đèn màu, đèn pin (đèn nón), đèn nòng súng, vệt đạn phát sáng với bloom, tia lửa và nổ bằng hạt 3D, mục tiêu nháy rồi tan, rung camera, hitstop, `ray3d_box`, gizmo (phím G) |
| `njin_sokoban` | Camera 2.5D, sàn và tường bằng draw_instanced3d(), thùng có texture, nhân vật là viên nang SDF có viền sáng, đèn điểm trên ô đích, bụi và lấp lánh bằng hạt, hiện dần khi vào màn, gizmo (phím G) |
| `njin_platformer3d` | Camera góc nhìn thứ ba không xuyên tường (physics3d_raycast), nhân vật vật lý (character3d) với coyote time và nhảy đôi, bục tĩnh, bục di chuyển chở người (body kinematic), thùng đẩy được (body động), checkpoint, rơi thì tan rồi hiện lại, vòng đích phát sáng, gizmo (phím G) |
