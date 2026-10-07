# Đồ họa 3D {#graphics_3d}

njin vẽ được thế giới 3D: camera phối cảnh, hình khối, hình SDF mịn, model glTF có animation xương,
ánh sáng có bóng đổ, vật liệu, hiệu ứng, hạt, instancing, vật lý và va chạm, entity 3D, chọn vật bằng
chuột và gizmo để debug. Mọi thứ đều qua `njin.h` (khai báo trong `njin_3d.h`, `njin_physics3d.h` và
`njin_gizmo.h`), không cần raylib hay thư viện vật lý.

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
| draw_sdf_blend() với njin::sdf_part | Nhiều hình nón bo tròn hòa làm một khối liền (smooth min) | Nhân vật đất sét ghép từ đầu, thân, tay chân, cử động vẫn liền mạch |
| draw_instanced3d() | Hàng nghìn hình lưới bằng một lệnh vẽ | Rừng, đám đông, gạch lát |
| draw_model(), draw_model_anim() | Model glTF/OBJ nạp bằng model_load() | Đồ vật, nhân vật làm trong Blender |
| model_create() với njin::mesh3d_data | Model từ lưới tam giác game tự dựng (vị trí, màu từng đỉnh, chỉ số) | Địa hình sinh theo seed, hình ghép lúc chạy |

Vẽ hàng nghìn vật nhỏ bằng draw_instanced3d() thì dùng `mesh3d_sphere_low` và `mesh3d_cylinder_low`:
cùng hình với `mesh3d_sphere` và `mesh3d_cylinder` nhưng ít mặt hơn nhiều, vì mỗi bản chỉ vài
điểm ảnh trên màn hình và mỗi tam giác còn được vẽ thêm một lần cho bóng đổ.

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

Vật liệu đánh dấu `doubleSided` trong glTF (lá cây, giấy, vải một lớp) được vẽ cả hai mặt:
`model_material::double_sided`, đổi được bằng model_material_set().

### Cắt bỏ ngoài tầm nhìn và mức chi tiết {#model_lod}

draw_model(), draw_model_anim() và njin::model3d bỏ qua model có hộp bao nằm ngoài tầm nhìn
camera (frustum culling); model đó vẫn đổ bóng vào cảnh. njin::render_info_get() đếm
`models3d` (đã vẽ) và `models3d_culled` (bị bỏ). Một lưới lớn như địa hình hay đường sá nên
chia thành nhiều model theo vùng (mỗi vùng một model_create()), để vùng khuất được bỏ.

model_lod_build() tạo các mức chi tiết cho model: bản giản lược ít tam giác hơn, được vẽ thay
khi model nhỏ trên màn hình. Mức 1 dùng khi model cao chưa tới `screen` (mặc định một phần tư)
chiều cao màn hình, mỗi mức sau ở một nửa mức trước. Model có xương giữ xương và animation ở
mọi mức.

@code
const njin::model_handle person = njin::model_load(ctx, "assets/person.glb");
njin::model_lod_build(ctx, person); // 3 mức, mỗi mức khoảng nửa số tam giác mức trước
@endcode

Việc giản lược dùng thư viện [meshoptimizer](https://github.com/zeux/meshoptimizer) (MIT). Nó
giữ đường ranh giữa các màu và các mảnh UV, nên lưới phẳng có nhiều màu xen kẽ (như bàn cờ) gần
như không giản lược được; model_lod_build() khi đó trả về 0. draw_instanced3d() và ray3d_model()
luôn dùng model gốc.

## Animation của model

Một glTF có skin (xương) mang theo các animation của nó: model_load() nạp chúng cùng model.
model_anim_find() tìm một animation theo tên action trong Blender, model_anim_count(),
model_anim_name() và model_anim_duration() liệt kê chúng. draw_model_anim() vẽ model ở một
njin::model_pose: animation `anim` ở giây `time`, và nếu cần trộn với `blend_anim` theo tỉ lệ `blend`
để chuyển mượt giữa hai động tác.

@code
// Đứng yên trộn sang chạy theo tốc độ, chuyển mượt trong 0.2 giây.
blend = njin::move_toward(blend, moving ? 1.0f : 0.0f, dt / 0.2f);
njin::draw_model_anim(ctx, robot, {.position = pos, .rotation = {0, yaw, 0}},
                      {.anim = idle, .time = t, .blend_anim = run, .blend_time = t, .blend = blend});
@endcode

| Điều | Chi tiết |
|---|---|
| Tính xương | Trên GPU, tối đa 128 xương, 4 xương mỗi đỉnh |
| Bóng đổ | Theo đúng tư thế |
| Nhiều bản | Mỗi lần vẽ một tư thế riêng: cùng một model, mỗi con một động tác |
| Đồng phục | model_material_find() tìm vật liệu theo tên; draw_model_anim() với njin::model_recolor đổi màu nó trong một lần vẽ |
| Động tác giữ khung cuối | Khung cuối của mỗi clip lấy từ khung liền trước (raylib trả về tư thế đầu clip ở đúng thời điểm cuối) |
| Dùng tư thế gốc | Phần vẽ bằng shader của game, draw_instanced3d(), ray3d_model() |
| Không có armature | Xương gốc không có node cha trong glTF: engine cảnh báo, model không có animation |

Với entity, njin::model3d giữ tư thế và engine tự tăng thời gian mỗi frame (@ref entities_3d).

### Nhân vật ghép mảnh {#model_skinned}

Khi mỗi nhân vật là một tổ hợp mảnh (đầu, thân, tay, tóc, mũ) theo một bộ gen, xuất sẵn một
file cho mỗi tổ hợp là không được. model_create_skinned() tạo model có xương từ một lưới game tự
ghép (njin::skinned_mesh3d_data: vị trí, bốn xương và bốn trọng số mỗi đỉnh, chỉ số), chia thành
các phần, mỗi phần một vật liệu. Model mới chép bộ xương của một model có xương đã nạp, và
**dùng chung** các animation của nó: một thư viện clip nạp một lần cho mọi nhân vật.

@code
// Thư viện clip nạp một lần; mỗi tổ hợp mảnh ghép một model.
const njin::model_handle clips = njin::model_load(ctx, "assets/shared_animations.glb");
const njin::model_handle body = njin::model_create_skinned(
    ctx, {.positions = pos.data(), .vertex_count = n, .joints = joints.data(), .weights = weights.data(),
          .indices = idx.data(), .index_count = (njin::u32)idx.size(), .skeleton = clips});
// Mỗi người một tư thế, một bảng màu trên cùng lưới.
const njin::model_recolor palette[] = {{.material = 0, .color = skin}, {.material = 1, .color = shirt}};
njin::draw_model_anim(ctx, body, at, {.anim = njin::model_anim_find(ctx, body, "Walk_Loop"), .time = t},
                      njin::colors::white, palette, 2);
@endcode

| Việc | Hàm |
|---|---|
| Chỉ số xương theo tên, để ghép đúng xương vào mảnh | model_bone_find(), model_bone_name(), model_bone_count() |
| Một xương ở một tư thế (gắn đồ vào tay, vùng trúng đòn theo xương) | model_bone_pose(): vị trí và ba trục trong không gian model, như draw_model_anim() đặt nó |
| Nhiều vật liệu đổi màu trong một lần vẽ | draw_model_anim() với một mảng njin::model_recolor |
| Giải phóng thư viện clip trước | Model ghép chỉ còn tư thế gốc |

## Ánh sáng

| Phần | Đặt bằng | Ghi chú |
|---|---|---|
| Mặt trời, ánh sáng nền | light3d_set() với njin::light3d | Có hiệu lực từ begin_3d() tiếp theo |
| Bóng đổ của mặt trời | `light3d::shadows`, `shadow_range`, `shadow_size`, `shadow_softness` | Hộp bóng quanh chỗ camera nhìn; mép bóng mềm 3 x 3 |
| Sương mù | `light3d::fog_color`, `fog_density` | Theo khoảng cách tới camera |
| Đèn điểm, đèn nón | light3d_add() với njin::light3d_source mỗi lần vẽ, hoặc entity (@ref entities_3d) | Tối đa njin::light3d_max (16) |
| Bóng đổ của đèn điểm, đèn nón | `light3d_source::shadows`, `light3d::source_shadow_size` | Tối đa njin::light3d_shadow_max (4) mỗi lần vẽ; đèn sau đó chiếu sáng mà không đổ bóng |
| Bề mặt | material3d_set() với njin::material3d | Độ bóng, phát sáng, viền sáng (`rim`), `unlit`, `texture`, `cast_shadows` |

Ánh sáng là Lambert cộng điểm sáng bóng Blinn-Phong, cộng ánh sáng nền. `emission` cộng màu sau khi
chiếu sáng: bật bloom (post_fx_set()) để vật phát sáng lan sáng ra xung quanh.

Bóng của đèn điểm và đèn nón tốn: mọi hình đổ bóng được vẽ thêm 6 lần cho một đèn điểm (6 mặt của một
khối lập phương quanh đèn) và 1 lần cho một đèn nón, mỗi mặt một ảnh bóng `source_shadow_size` pixel.
Chỉ bật cho vài đèn quan trọng (đèn pin, đèn treo giữa phòng); `radius` của đèn cũng là tầm của bóng.

@code
// Đèn pin của người chơi: đèn nón đổ bóng.
njin::light3d_add(ctx, {.kind = njin::light3d_spot, .position = eye, .direction = forward,
                        .intensity = 2.0f, .radius = 25.0f, .cone = 40.0f, .shadows = true});
@endcode

## Hiệu ứng

Các hiệu ứng chung của @ref particles đều dùng được cho 3D:

| Hiệu ứng | Với 3D |
|---|---|
| camera_shake() | Rung camera của begin_3d(): độ lệch thành góc nhìn, góc nghiêng thành nghiêng camera |
| hitstop(), screen_flash(), post_fx_set(), chuyển scene | Như 2D, không cần gì thêm |
| Nháy màu, tan biến | fx3d_set() với njin::fx3d: tương đương njin::flash_fx và njin::dissolve_fx của sprite |
| Hạt | particles3d_spawn() dùng lại emitter 2D (njin::fx::explosion(), sparks(), dust()...) |

Hình vẽ bằng lệnh (draw_sphere3d(), draw_shape3d(), draw_model()...) không phải entity: game tự giữ
thời gian của hiệu ứng và đặt mức mỗi frame. Một entity có njin::shape3d_render hay njin::model3d thì
đặt trường `fx` của component đó thay cho fx3d_set() (@ref entities_3d). Với lệnh vẽ:

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

Hàng chục nghìn instance mà camera chỉ thấy một phần (rừng, thành phố) thì xếp chúng theo ô và chỉ vẽ những ô camera
thấy: @ref spatial_batch.

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
| Body từ model | `body3d_desc::model` và `scale` | Sàn, dốc, hang làm trong Blender (tĩnh, kinematic); vật lồi (động) |
| Sensor | `body3d_desc::sensor` | Vùng nhặt đồ, checkpoint, bẫy, đích: không va, chỉ báo chạm |
| Sự kiện chạm | physics3d_contact_count(), physics3d_contact() | Biết cái gì bắt đầu hay thôi chạm cái gì |
| Khớp nối | joint3d_create() | Cửa bản lề, bập bênh, dây xích, piston |
| Ragdoll | ragdoll3d_create() | Nhân vật ngã, trúng đòn: bộ xương của model đi theo vật lý |
| Vật mềm | softbody3d_create() | Bóng cao su, nệm, khối thạch: biến dạng khi va chạm, giữ phồng bằng áp suất |
| Vải | cloth3d_create() | Lá cờ, rèm, áo choàng: ghim vào một chỗ, bay theo gió |
| Xe có bánh | vehicle3d_create() | Ô tô: động cơ, hộp số tự động, lái, phanh, giảm xóc |
| Tia | physics3d_raycast() | Đạn, tầm nhìn, camera không xuyên tường; trả về body bị trúng, đi xuyên sensor |

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

Các nhân vật chặn nhau: không đi xuyên qua nhau mà trượt vòng qua, như với tường. Engine chỉ so
mỗi nhân vật với những người ở gần (một lưới ô), nên cả trăm nhân vật vẫn rẻ; phần tốn là mỗi
nhân vật đang bật cần một lần tính va chạm mỗi bước (vài chục micro giây). Một đám đông lớn nên
chỉ bật những người camera thấy bằng character3d_set_active(), còn người ở xa game tự dời theo
đường đi:

@code
bool seen = false;
njin::camera3d_to_screen(ctx, cam, feet, &seen); // hay cách game tự biết
if (seen != njin::character3d_active(ctx, walker)) {
  if (seen)
    njin::character3d_set_position(ctx, walker, feet); // bật lại ở chỗ game đã dời tới
  njin::character3d_set_active(ctx, walker, seen);
}
@endcode

Vẽ một body động theo đúng chỗ và góc vật lý tính ra bằng body3d_transform():

@code
const njin::transform3d t = njin::body3d_transform(ctx, crate);
njin::draw_shape3d(ctx, {.kind = njin::shape3d_box, .position = t.position, .rotation = t.rotation,
                         .size = {1, 1, 1}}, {0.7f, 0.5f, 0.3f, 1.0f});
@endcode

Một entity có component njin::body3d thì không cần bước này: engine ghi transform cho nó
(@ref entities_3d).

`body3d_desc::user` gắn một số của game vào body (chỉ số trong mảng, id entity): đọc lại bằng body3d_user()
từ body mà physics3d_raycast(), character3d_ground_body() hay physics3d_contact() trả về.

### Body từ model

Có `body3d_desc::model` thì hình của body lấy từ lưới tam giác của model, đặt như draw_model() vẽ nó
với cùng `position`, `rotation` và `scale`. Nên một model vừa là hình vẽ vừa là mặt đất để đi:

@code
const njin::model_handle hill = njin::model_load(ctx, "assets/hill.glb");
njin::body3d_create(ctx, {.position = hill_pos, .model = hill}); // tĩnh: đúng từng tam giác
@endcode

| Loại body | Hình từ model |
|---|---|
| Tĩnh, kinematic | Đúng từng tam giác: dốc, bậc, hang đều đi được |
| Động | Bao lồi của các đỉnh (hình lồi nhỏ nhất bọc model): chỗ lõm bị lấp |

### Sensor và sự kiện chạm

Body có `sensor = true` không va chạm: vật và nhân vật đi xuyên qua, physics3d_raycast() cũng xuyên
qua, còn engine báo sự kiện chạm. Mỗi bước mô phỏng, physics3d_contact_count() và physics3d_contact()
cho các njin::contact3d: hai body (`a`, `b`), hoặc một body `a` và một nhân vật `character` (khi đó
`b` không hợp lệ); `began` là bắt đầu hay thôi chạm, `sensor` nếu một bên là sensor, cùng điểm và pháp
tuyến chạm.

Một cặp chỉ báo một lần khi bắt đầu chạm và một lần khi thôi chạm, dù chạm ở nhiều điểm. Body bị hủy
không có sự kiện thôi chạm. Đọc sự kiện trong `phase_fixed_update`: mỗi bước game thấy đúng sự kiện
của bước trước, không sót, không lặp.

@code
// Trong một system của phase_fixed_update: người chơi chạm đồng xu (sensor) thì nhặt.
entt::registry &reg = njin::world(ctx);
for (njin::i32 i = 0; i < njin::physics3d_contact_count(ctx); i++) {
  const njin::contact3d c = njin::physics3d_contact(ctx, i);
  if (!c.began || !c.sensor || c.character.id != player.id)
    continue;
  const auto e = (entt::entity)njin::body3d_user(ctx, c.a); // user = id entity của đồng xu
  if (reg.valid(e) && reg.all_of<coin>(e))
    reg.destroy(e); // component body3d hủy luôn body của nó
}
@endcode

### Khớp nối

joint3d_create() nối hai body, hoặc một body với một điểm cố định của thế giới (`b` không hợp lệ).
Điểm nối `anchor` và trục `axis` tính trong tọa độ thế giới, lúc tạo khớp. Hủy một body thì hủy luôn
các khớp của nó.

| Loại | Làm gì | Ví dụ |
|---|---|---|
| `joint3d_fixed` | Hàn cứng: giữ nguyên vị trí và góc tương đối | Gắn hai mảnh thành một vật |
| `joint3d_point` | Khớp cầu: xoay tự do quanh `anchor` | Dây xích, con lắc |
| `joint3d_hinge` | Bản lề: xoay quanh `axis` qua `anchor` | Cửa, bập bênh, bánh xe |
| `joint3d_slider` | Trượt dọc `axis`, không xoay | Piston, ngăn kéo, cửa kéo |
| `joint3d_distance` | Giữ khoảng cách giữa `anchor` và `anchor_b` trong `[min, max]` | Dây, thanh nối |

`min` và `max` là giới hạn: góc, độ (bản lề); quãng trượt tính từ vị trí lúc tạo (khớp trượt);
khoảng cách (njin::joint3d_distance). Với bản lề và khớp trượt, `min >= max` là không giới hạn.
`motor_force` lớn hơn 0 cho bản lề và khớp trượt một mô-tơ: joint3d_set_motor() đặt tốc độ,
joint3d_position() đọc góc hay quãng trượt hiện tại.

@code
// Bập bênh: tấm ván động trên một bản lề, nghiêng tối đa 18 độ mỗi bên.
const njin::body3d_handle plank = njin::body3d_create(
    ctx, {.position = pivot, .size = {4, 0.2f, 1}, .motion = njin::body3d_dynamic, .mass = 25});
njin::joint3d_create(ctx, {.kind = njin::joint3d_hinge, .a = plank, .anchor = pivot, .axis = {0, 0, 1},
                           .min = -18, .max = 18});
@endcode

### Ragdoll {#ragdoll3d}

ragdoll3d_create() biến một model có xương thành ragdoll: mỗi xương được chọn là một viên nang động, nối
với phần cha bằng khớp có giới hạn góc (Ragdoll của Jolt), rơi và va chạm như mọi body. Các phần của
cùng một ragdoll không va vào nhau. Các xương không được chọn (ngón tay, gốc) đi theo phần gần nhất phía trên chúng.
Giới hạn góc tính từ tư thế gốc trong file (tư thế T hay A), nên ragdoll bắt đầu được từ bất kỳ khung
animation nào: thường là tư thế vừa vẽ, lúc nhân vật trúng đòn hay ngã.

Mỗi khung, ragdoll3d_bones() đọc tư thế của ragdoll vào một mảng, và `model_pose::bones` vẽ model theo
mảng đó thay cho animation. model_bone_pose() với tư thế ấy cũng trả về xương của ragdoll (để camera đi
theo đầu, chẳng hạn). ragdoll3d_body() trả về body của một phần, để đẩy bằng body3d_add_impulse() hay
nhận ra phần bị trúng trong physics3d_raycast().

| Trường của njin::ragdoll3d_bone | Ý nghĩa |
|---|---|
| `name`, `radius`, `length` | Xương và viên nang dọc theo trục y của nó; `radius`, `length` 0 là đo từ da của model |
| `swing`, `twist` | Khớp cầu: góc lệch và góc vặn tối đa, độ |
| `bend_min`, `bend_max` | Bản lề quanh trục x của xương thay cho khớp cầu (gối, khuỷu tay) |

@code
// Người chơi ngã: từ tư thế đang vẽ, giữ vận tốc lúc chạy.
rag = njin::ragdoll3d_create(ctx, {.model = man, .transform = at, .pose = pose, .bones = parts,
                                   .bone_count = std::size(parts), .velocity = velocity});
// Mỗi khung, trong phase_render:
static njin::bone_pose3d bones[128];
njin::ragdoll3d_bones(ctx, rag, at, bones, 128);
njin::draw_model_anim(ctx, man, at, {.bones = bones});
@endcode

Ví dụ danh sách `parts` cho mannequin của Quaternius nằm ở ragdoll3d_create().

### Vật mềm và vải {#softbody3d}

Vật mềm là một đám điểm (đỉnh) nối bằng lò xo, mỗi bước vật lý biến dạng theo trọng lực, va chạm và áp
suất bên trong (soft body của Jolt). softbody3d_create() dựng nó từ một hình có sẵn hoặc từ lưới của game;
cloth3d_create() dựng một tấm vải chữ nhật. Cả hai trả về njin::softbody3d_handle và dùng chung các hàm
`softbody3d_*`.

| Hình (njin::softbody3d_kind) | Dựng thế nào | Ví dụ |
|---|---|---|
| `softbody3d_box` | Lưới điểm đặc bên trong hộp `size`, giữ thể tích | Nệm, khối thạch, khối cao su |
| `softbody3d_sphere` | Mặt cầu rỗng bán kính `radius`; đặt `pressure` để phồng | Quả bóng, bong bóng |
| `softbody3d_mesh` | Mặt của `mesh` hoặc mọi lưới của `model` (đỉnh trùng vị trí được gộp) | Gối, đồ chơi bơm hơi |

`stiffness` (0..1) là độ cứng của các cạnh: 1 là không dãn. `bend` là độ cứng khi gập. `pressure` là áp suất
bên trong khi vật đúng hình lúc tạo, Pa: bóp nhỏ thì áp suất tăng như bóng bay. Quả bóng 1 kg bán kính 0.5
cần khoảng 50 (mềm, lún khi chạm đất) đến 300 (căng); lớn hơn nữa thì vật phồng to hơn lúc tạo.

Vật mềm va chạm với mọi body nhưng không va với nhau. Nhân vật **đẩy** vật mềm sang bên khi đi qua chứ không
đứng lên nó, nên một tấm rèm không chặn đường. physics3d_raycast() và các hàm `physics3d_*_push`, `_cast` đi
xuyên qua vật mềm.

Vẽ vật mềm bằng softbody3d_model(): một model luôn có hình hiện tại của vật, trong thế giới, engine cập nhật
sau mỗi bước vật lý. Vẽ nó bằng draw_model() với transform mặc định, đổi màu và ảnh bằng
model_material_set(); model thuộc về vật mềm, softbody3d_destroy() hủy nó. Muốn tự vẽ thì đọc
softbody3d_vertices(), softbody3d_normals() và softbody3d_indices().

Đỉnh của tấm vải ở hàng `r`, cột `c` có chỉ số `r * (columns + 1) + c`; hàng 0 là cạnh trên. Vải nằm trong mặt
phẳng x–y của nó (đứng thẳng như lá cờ), `rotation.x = 90` cho vải nằm ngang. Ghim các cạnh bằng `pin_edges`,
các đỉnh khác bằng `pinned` hay softbody3d_pin(); softbody3d_nearest() tìm đỉnh gần một điểm.

| Hàm | Làm gì |
|---|---|
| softbody3d_pin() | Ghim hay bỏ ghim một đỉnh: đỉnh bị ghim đứng yên |
| softbody3d_move_pinned() | Dời một đỉnh bị ghim tới chỗ mới sau bước tới, kéo phần còn lại theo (áo choàng ghim vào vai) |
| softbody3d_set_wind() | Gió thổi qua: mặt nào chắn gió thì bị đẩy theo `drag` và diện tích |
| softbody3d_add_impulse() | Đẩy cả vật một cú, chia đều cho các đỉnh: đá quả bóng |
| softbody3d_position() | Tâm của vật, để camera đi theo |

@code
// Áo choàng: tấm vải ghim cạnh trên, mỗi bước cố định kéo hai góc trên theo hai vai.
cape = njin::cloth3d_create(ctx, {.position = back, .size = {0.6f, 1.0f}, .columns = 8, .rows = 12,
                                  .pin_edges = njin::cloth3d_top});
// Mỗi bước cố định:
njin::softbody3d_move_pinned(ctx, cape, 0, shoulder_left);
njin::softbody3d_move_pinned(ctx, cape, 8, shoulder_right);
@endcode

### Xe có bánh {#vehicle3d}

vehicle3d_create() tạo một ô tô trên bộ điều khiển xe của Jolt: thân xe là một body động hình hộp `size` (hay
bao lồi của `model`), mỗi bánh là một giảm xóc dò mặt đất, có động cơ, hộp số tự động, vi sai, phanh và phanh
tay. Đầu xe hướng +z: xe có `rotation` 0 chạy theo trục z. Mặc định có bốn bánh ở bốn góc dưới của thân: hai
bánh trước lái, cả bốn bánh kéo, hai bánh sau có phanh tay; `wheels` cho bánh của game (xe ba bánh, xe tải
sáu bánh).

Lái bằng vehicle3d_set_input() mỗi bước cố định: ga (-1 lùi .. 1 tiến), lái (-1 trái .. 1 phải), phanh và phanh
tay. Đang chạy tới mà ga âm thì xe phanh trước, dừng hẳn rồi mới lùi, như người lái thật. vehicle3d_body() là
thân xe (body3d_transform() để vẽ, body3d_velocity() cho đồng hồ tốc độ), vehicle3d_wheel_transform() là chỗ
và góc của từng bánh lúc này: trục y của nó là trục bánh, nên vẽ bánh bằng một hình trụ.
vehicle3d_rpm() và vehicle3d_gear() cho tiếng máy và đồng hồ.

`engine_torque` mạnh quá sức bám của lốp thì bánh quay trượt, và hộp số không lên số khi bánh đang trượt.

Cảnh dưới đây có một lá cờ bay trong gió, một quả bóng và một chiếc xe lái bằng phím mũi tên:

@include physics3d_soft.cpp

## Entity 3D {#entities_3d}

Thay vì gọi lệnh vẽ và đọc body mỗi frame, một entity (@ref ecs) có thể mang component 3D. Engine vẽ và
cập nhật chúng theo njin::transform3d của entity, và inspector hiện đủ các component này.

| Component | Engine làm gì |
|---|---|
| njin::transform3d | Vị trí, góc xoay, tỉ lệ của entity; các component dưới đây đọc hoặc ghi nó |
| njin::model3d | Vẽ `model` tại transform; tăng `pose.time` và `pose.blend_time` mỗi frame (theo delta(), nhân `speed`) |
| njin::shape3d_render | Vẽ hình SDF `shape` tại transform (bỏ qua `shape.position` và `shape.rotation`) |
| njin::light3d_source | Đèn đặt tại `transform3d::position` (bỏ qua `position`), chiếu mọi lần vẽ 3D |
| njin::body3d | Động: sau mỗi bước ghi vị trí và góc của body vào transform. Kinematic: trước mỗi bước đưa body tới transform. Tĩnh: không làm gì |
| njin::character3d | Sau mỗi bước ghi vị trí chân của nhân vật vào `transform3d::position` (góc xoay do game đặt) |

Gỡ njin::body3d hay njin::character3d, hoặc hủy entity, thì body hay nhân vật bị hủy theo. njin::model3d
và njin::shape3d_render có `fx` (nháy màu, tan biến như fx3d_set()) và `visible` để ẩn mà không gỡ
component. Entity được vẽ ở end_3d() của mọi lần vẽ có `camera3d::entities` (mặc định bật), cùng các lệnh
vẽ của game; tắt nó cho một cảnh phụ, như model xoay trong menu.

@code
// Một thùng: vật lý đặt transform, shape3d_render vẽ đúng chỗ đó.
entt::registry &reg = njin::world(ctx);
const auto crate = reg.create();
reg.emplace<njin::transform3d>(crate);
reg.emplace<njin::shape3d_render>(crate, njin::shape3d_render{
    .shape = {.kind = njin::shape3d_box, .size = {0.9f, 0.9f, 0.9f}, .rounding = 0.05f},
    .color = {0.7f, 0.5f, 0.3f, 1.0f}});
reg.emplace<njin::body3d>(crate, njin::body3d_create(ctx, {.position = {0, 3, 0}, .size = {0.9f, 0.9f, 0.9f},
                                                           .motion = njin::body3d_dynamic, .mass = 8}));

// Người chơi: nhân vật vật lý và một robot có animation.
const auto hero = reg.create();
reg.emplace<njin::transform3d>(hero);
reg.emplace<njin::character3d>(hero, njin::character3d_create(ctx, {.position = start}));
reg.emplace<njin::model3d>(hero, njin::model3d{.model = robot, .pose = {.anim = idle, .blend_anim = run}});
// Mỗi frame game chỉ đổi tỉ lệ trộn: reg.get<njin::model3d>(hero).pose.blend = speed / max_speed;
@endcode

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

## Game mẫu

| Game | Xem gì |
|---|---|
| `njin_fps` | Camera góc nhìn thứ nhất, model glTF (khẩu súng), bóng đổ, sương mù, hai đèn màu và đèn pin (đèn nón, phím F) đều đổ bóng, đèn nòng súng, vệt đạn phát sáng với bloom, tia lửa và nổ bằng hạt 3D, mục tiêu nháy rồi tan, rung camera, hitstop, `ray3d_box`, gizmo (phím G) |
| `njin_sokoban` | Camera 2.5D, sàn và tường bằng draw_instanced3d(), thùng có texture, nhân vật là viên nang SDF có viền sáng, đèn điểm trên ô đích, bụi và lấp lánh bằng hạt, hiện dần khi vào màn, gizmo (phím G) |
| `njin_platformer3d` | Camera góc nhìn thứ ba không xuyên tường (physics3d_raycast), người chơi là entity (njin::character3d và njin::model3d: robot glTF với animation đứng, chạy, nhảy; đứng và chạy trộn theo tốc độ) với coyote time và nhảy đôi, bục tĩnh, bục di chuyển chở người (body kinematic), thùng đẩy được và tấm ván bập bênh là entity (njin::body3d, njin::shape3d_render), bập bênh là bản lề giới hạn ±18°, đồi cỏ là body lưới tam giác từ `assets/hill.glb`, sáu đồng xu là sensor (chạm là một sự kiện chạm, entity đồng xu bị hủy), đích là sensor, checkpoint, rơi thì tan rồi hiện lại, vòng đích phát sáng, gizmo (phím G) |
