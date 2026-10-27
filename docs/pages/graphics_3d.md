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

Mọi lệnh vẽ 3D nằm giữa begin_3d() và end_3d(), trong `phase_render`. njin::camera3d mặc định là camera
phối cảnh: `position`, `target`, `up` và góc nhìn dọc `fovy`. Trục y hướng lên, hệ tay phải như glTF: nhìn
theo `-z` thì `+x` ở bên phải. Tỉ lệ khung hình lấy theo screen_size(), nên màn hình ảo vẫn đúng.

`projection = camera3d_orthographic` là nhìn song song: vật xa không nhỏ đi, hợp với game chiến thuật,
xây thành phố, puzzle nhìn nghiêng và góc isometric dựng bằng 3D. Khi đó `ortho_height` là chiều cao vùng
nhìn theo đơn vị thế giới (nhỏ đi là phóng to) và `fovy` bị bỏ qua. Bóng đổ, hình SDF, hiệu ứng theo độ
sâu, chọn vật và camera_shake() (trượt thay vì xoay) đều theo phép chiếu này.

@code
// Góc nhìn isometric: chếch xuống, xoay 45 độ, mắt lùi đủ xa để thấy cả cảnh.
const njin::camera3d iso{.position = focus + njin::vec3{20, 16, 20}, .target = focus, .far_plane = 100,
                         .projection = njin::camera3d_orthographic, .ortho_height = 12};
njin::begin_3d(ctx, iso);
@endcode

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
như không giản lược được; model_lod_build() khi đó trả về 0. ray3d_model() luôn dùng model gốc;
draw_instanced3d() cũng vậy, trừ bản nhận njin::instanced3d_desc (xem @ref instancing3d_cull).

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

### Morph target {#model_morph}

Morph target (blend shape, shape key trong Blender) là các hình khác của cùng một lưới: mặt cười, mắt
nhắm, cơ phồng. model_load() nạp chúng cùng model, theo tên trong `extras.targetNames` của glTF (Blender
ghi tên shape key vào đó). Target cùng tên ở nhiều mesh, như cái nháy mắt ở mặt và ở mi, là **một** morph.

Trọng số của mỗi lần vẽ được tính theo ba lớp:

1. Trọng số mặc định trong file (`mesh.weights`).
2. Animation của njin::model_pose (`anim`, trộn với `blend_anim`) đặt lại những morph mà clip có đường
   cong (kênh `weights` của glTF). Model không có xương mà có những clip như vậy thì đó là các animation
   của nó (model_anim_count()).
3. Cộng thêm `model_pose::morph_weights`, mảng theo chỉ số model_morph_find(): game nháy mắt hay nói
   trong khi clip cử động phần còn lại.

@code
const njin::i32 blink = njin::model_morph_find(ctx, hero, "Blink");
njin::f32 morphs[16] = {};
morphs[blink] = blinking ? 1.0f : 0.0f;
njin::draw_model_anim(ctx, hero, at, {.anim = talk, .time = t, .morph_weights = morphs, .morph_count = 16});
@endcode

| Điều | Chi tiết |
|---|---|
| Trộn ở đâu | Trên CPU, vào bộ đệm đỉnh của mesh, ngay trước mỗi lần vẽ có trọng số khác lần trước, rồi xương uốn nó trên GPU. Nên chạy với mọi shader, kể cả shader của game, và bóng đổ theo đúng hình |
| Vì sao không trên GPU | Chỉ còn ba chỗ cho thuộc tính đỉnh (một mặt có hàng chục morph), và sẽ phải sửa mọi shader dựng sẵn lẫn shader của game |
| Chi phí | Mỗi mesh có morph tốn một lần chép đỉnh lên GPU cho mỗi lượt vẽ (cả lượt bóng) khi trọng số đổi; vẽ lại cùng trọng số thì không tốn gì |
| Đọc trọng số | model_morph_weights() trả về đúng trọng số lần vẽ sẽ dùng |
| Vẽ nhiều bản | draw_instanced3d() có bản nhận njin::model_pose: **mọi** instance của một lệnh có cùng một hình. Bản không có nó vẽ hình mặc định của file |
| Nhiều hình trong một đám đông | Chia instance thành vài nhóm, mỗi nhóm một lệnh với `pose` riêng (trọng số theo instance cần thêm một bộ đệm instance và một texture độ lệch, chưa có) |
| Bắn tia | ray3d_model() có bản nhận njin::model_pose, thử đúng hình đã morph (bản cũ thử hình trong file) |
| Mức chi tiết | model_lod_build() giữ morph ở mọi mức: độ lệch của mỗi target đi qua cùng phép giản lược, và việc giản lược tính cả độ lệch nên mặt phẳng mà morph làm cong vẫn giữ đủ cạnh |
| Gộp mesh | `model_load_desc::merge` giữ morph của các mesh được gộp |
| Tangent | Target có `TANGENT` thì tangent cũng được trộn, nên normal map theo đúng hình |

@code{.cpp}
// Một đám đông cùng cười: một lệnh vẽ cho mọi instance.
njin::f32 smile[16] = {};
smile[njin::model_morph_find(ctx, face, "Smile")] = 1.0f;
const njin::model_pose happy{.morph_weights = smile, .morph_count = 16};
njin::draw_instanced3d(ctx, face, crowd, 0, 500, happy);
// Bắn trúng má đang phồng, không phải má trong file.
const njin::ray3d_hit hit = njin::ray3d_model(ctx, shot, face, at, happy);
@endcode

### Xương lò xo {#spring3d}

Tóc, đuôi, râu, vạt áo choàng: những xương không có trong animation nhưng phải trễ theo quán tính khi
nhân vật chạy, đung đưa, rồi trở về. spring3d_create() nhận các chuỗi (njin::spring3d_chain: một xương
và mọi xương dưới nó) và các collider (njin::spring3d_collider: hình cầu hay viên nang gắn vào một
xương, như đầu và thân) mà xương lò xo không xuyên qua. Tham số giống spring bone của VRM.

Mỗi frame, spring3d_update() lấy tư thế của animation (hay xương game đặt), chạy lò xo trong thế giới theo
transform của lần vẽ, và ghi tư thế cuối vào một mảng để vẽ bằng `model_pose::bones`:

@code
njin::bone_pose3d bones[128];
const njin::model_pose run{.anim = run_clip, .time = t};
njin::spring3d_update(ctx, hair, run, at, njin::delta(ctx), bones, 128);
njin::draw_model_anim(ctx, hero, at, {.anim = run_clip, .time = t, .bones = bones});
@endcode

| Trường của njin::spring3d_chain | Ý nghĩa |
|---|---|
| `stiffness` | Lực kéo về tư thế của animation; lớn là cứng |
| `drag` | 0..1, cản: 0 đung đưa mãi, 1 chỉ trễ rồi về |
| `gravity`, `gravity_dir` | Trọng lực kéo đầu xương, trong thế giới |
| `radius` | Bán kính va chạm của đầu mỗi xương |

Mỗi nhân vật cần một bộ lò xo riêng, vì nó nhớ đầu các xương đang ở đâu. Lò xo chạy theo bước 1/60
giây; `dt` 0 (game tạm dừng) giữ nguyên. Khi dịch chuyển tức thời nhân vật, gọi spring3d_reset() để
tóc không bay theo cả quãng đường.

### Chép animation sang bộ xương khác {#retarget3d}

Một bộ clip (chẳng hạn từ Mixamo) cho nhiều nhân vật có bộ xương khác tên, khác tỉ lệ:
retarget3d_create() ghép xương của model đích với xương của model nguồn, và retarget3d_pose() cho tư thế
của model đích để vẽ bằng `model_pose::bones`.

- **Ghép xương:** các cặp tên game đưa (`retarget3d_desc::pairs`) trước, rồi theo tên chuẩn của
  bone_humanoid_name(), hiểu cách đặt tên của Mixamo (`mixamorig:LeftForeArm`), Unreal
  (`lowerarm_l`, `spine_02`), Unity và VRM (`LeftLowerArm`) và Blender (`forearm.L`), rồi theo đúng tên.
  retarget3d_source_bone() cho biết một xương đã được ghép với xương nào.
- **Góc xoay:** xương đích xoay đúng góc mà xương nguồn xoay khỏi tư thế gốc của nó. Hai model cần cùng
  trục và tư thế gốc giống nhau (cùng chữ T hay cùng chữ A). Xương không được ghép (đuôi ngựa, vũ khí)
  đi theo xương cha như ở tư thế gốc.
- **Hông:** dời theo hông của nguồn, nhân với tỉ lệ độ cao hông của hai model, nên người chân ngắn bước
  ngắn hơn và chân không trượt.

@code
const njin::retarget3d_handle to_dwarf = njin::retarget3d_create(ctx, {.source = hero, .target = dwarf});
njin::bone_pose3d bones[128];
njin::retarget3d_pose(ctx, to_dwarf, {.anim = run_clip, .time = t}, bones, 128);
njin::draw_model_anim(ctx, dwarf, at, {.bones = bones});
@endcode

Ví dụ đầy đủ: tóc lò xo, nháy mắt bằng morph, và người lùn chạy bằng clip của người hùng.

@include anim3d.cpp

### Đặt chân lên mặt đất {#foot3d}

Animation được làm trên sàn phẳng: đứng trên bậc thang hay dốc thì một bàn chân lơ lửng, bàn kia lún vào đất.
foot3d_create() và foot3d_update() đặt từng bàn chân lên mặt đất ngay dưới nó:

1. Dò thẳng xuống dưới mỗi cổ chân (physics3d_raycast(), hay một hàm njin::foot3d_ground của game cho địa hình hay
   lưới ô) để biết mặt đất cao bao nhiêu so với gốc của lần vẽ.
2. Hạ cả người xuống cho bàn chân thấp hơn với tới (đứng ở mép bậc, một chân thõng xuống).
3. IK hai xương (đùi, cẳng chân) cho mỗi chân: cổ chân tới đúng chỗ, đầu gối gập về phía animation gập nó, hay về
   `knee_forward` khi chân thẳng.
4. Lòng bàn chân nghiêng theo mặt đất (không quá `max_tilt`, nên mép bậc không làm chân lật).

Bàn chân đang nhấc trong animation (khi bước) vẫn cao hơn mặt đất dưới nó bấy nhiêu, nên đi lên bậc thang vẫn là
bước. Độ cao được làm mượt theo `smoothing` (đi lên nhanh gấp đôi đi xuống, để chân không lún vào bậc vừa bước
lên); foot3d_reset() sau khi dịch chuyển tức thời. `weight` giảm về 0 khi nhân vật rời mặt đất (nhảy, rơi).

Không đưa `legs` thì foot3d_create() tự tìm hai chân người theo bone_humanoid_name(). Gốc của lần vẽ là chỗ nhân vật
đứng: chân của character3d (character3d_position()). Tia dò không trúng character3d, chỉ trúng body và địa hình.

Thứ tự khi dùng chung: retarget3d_pose(), rồi foot3d_update() với `pose.bones` là kết quả đó, rồi spring3d_update()
với `pose.bones` là kết quả của foot3d (tóc đung đưa theo người đã hạ xuống), rồi vẽ.

@include foot3d.cpp

Đo bằng harness với một người 65 xương, animation đứng yên: trên bậc 0,2 m, mép bậc và dốc 20 độ (dọc, ngang, chéo
45 độ), cổ chân cách mặt đất đúng như trên sàn phẳng, sai tối đa 6 mm, mũi chân không lún; trên sàn phẳng tư thế
giống hệt animation.

## Ánh sáng

| Phần | Đặt bằng | Ghi chú |
|---|---|---|
| Mặt trời, ánh sáng nền | light3d_set() với njin::light3d | Có hiệu lực từ begin_3d() tiếp theo |
| Bóng đổ của mặt trời | `light3d::shadows`, `shadow_range`, `shadow_size`, `shadow_softness` | Hộp bóng quanh chỗ camera nhìn; mép bóng mềm 3 x 3 |
| Bóng nhiều tầng | `light3d::shadow_cascades`, `shadow_cascade_split` | @ref shadow3d_cascades |
| Bóng bằng tia (ray tracing) | `light3d::ray_traced_shadows`, `sun_angle`, `shadow_rays` | @ref shadow3d_raytraced |
| Phản chiếu bằng tia | `light3d::ray_traced_reflections`, `reflection_roughness` | @ref reflect3d_raytraced |
| Ánh sáng gián tiếp bằng tia | `irradiance3d_desc::ray_traced` | @ref probe3d |
| Sương mù | `light3d::fog_color`, `fog_density` | Theo khoảng cách tới camera |
| Sương khối, luồng sáng trong sương | `light3d::volume_density` và các trường `volume_*` | @ref fog3d_volume |
| Đèn điểm, đèn nón | light3d_add() với njin::light3d_source mỗi lần vẽ, hoặc entity (@ref entities_3d) | Mặc định tối đa njin::light3d_max (16); `light3d::lights_max` tới njin::light3d_limit (@ref lights3d_many) |
| Bóng đổ của đèn điểm, đèn nón | `light3d_source::shadows`, `light3d::source_shadow_size` | Mặc định tối đa njin::light3d_shadow_max (4) mỗi lần vẽ, `light3d::source_shadows` tới njin::light3d_shadow_limit; đèn sau đó chiếu sáng mà không đổ bóng |
| Bề mặt | material3d_set() với njin::material3d | Độ bóng, phát sáng, viền sáng (`rim`), `unlit`, `texture`, `cast_shadows`; PBR: `pbr`, `metallic`, `roughness` (@ref pbr3d) |
| Tone mapping, ảnh môi trường | `light3d::tonemap`, `exposure`, `environment` | @ref pbr3d |

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

### Bóng nhiều tầng {#shadow3d_cascades}

Một ảnh bóng phủ cả `shadow_range` thì bóng gần camera mờ và răng cưa khi tầm rộng, còn thu hẹp tầm thì vật
ở xa mất bóng. `light3d::shadow_cascades` từ 2 tới 4 chia đoạn từ camera tới `shadow_range` mét thành từng
tầng (cascaded shadow maps, như Bevy và Unreal), mỗi tầng một ảnh `shadow_size`: tầng gần ngắn nên bóng sắc,
tầng xa dài nên vẫn có bóng. `shadow_cascade_split` chọn cách chia, 0 đều, 1 theo cấp số nhân; 0.75 hợp với
đa số cảnh. Mép mỗi tầng trộn sang tầng sau nên không thấy đường đổi độ sắc, và mỗi tầng giữ cỡ cố định, dịch
theo từng pixel ảnh bóng, nên mép bóng không nhấp nháy khi camera chạy.

@code
// Thế giới mở: bóng tới 150 m, bốn tầng 2048 pixel.
njin::light3d_set(ctx, {.direction = sun, .shadows = true, .shadow_range = 150.0f, .shadow_size = 2048,
                        .shadow_cascades = 4});
@endcode

Mỗi tầng vẽ lại mọi hình đổ bóng một lần, nên bốn tầng tốn gấp bốn một ảnh bóng. Camera song song
(`camera3d_orthographic`) luôn dùng một ảnh bóng quanh chỗ nó nhìn.

### Bóng bằng tia (ray tracing) {#shadow3d_raytraced}

`light3d::ray_traced_shadows` tính bóng của mặt trời và của tối đa ba đèn có `light3d_source::shadows` bằng tia
thay cho ảnh bóng. Bóng sắc ở chỗ vật chạm đất và mềm dần ra xa theo cỡ của nguồn sáng (`sun_angle` cho mặt
trời, `light3d_source::source_size` cho đèn), không răng cưa, không lệch bóng, và không bị giới hạn bởi
`shadow_range`: vật ở xa camera vẫn có bóng.

@code
// Bóng bằng tia khi máy có, ảnh bóng như cũ khi không: bật luôn cũng an toàn.
njin::light3d_set(ctx, {.direction = sun, .shadows = true, .ray_traced_shadows = true, .sun_angle = 1.5f});
njin::begin_3d(ctx, cam);
njin::light3d_add(ctx, {.position = lamp, .radius = 10.0f, .shadows = true, .source_size = 0.1f});
@endcode

Cách làm: njin vẽ độ sâu của các hình đục trước, rồi một thiết bị Vulkan (qua
[NVRHI](https://github.com/NVIDIA-RTX/NVRHI) của NVIDIA) dựng cảnh thành cấu trúc tăng tốc, bắn
`shadow_rays` tia mỗi điểm ảnh từ bề mặt tới nguồn sáng và làm mịn kết quả (`shadow_denoise` pixel). OpenGL vẫn
vẽ cả cảnh và đọc kết quả đó khi tính ánh sáng; hai bên dùng chung ảnh trên cùng một GPU.

| Trường | Ý nghĩa |
|---|---|
| `ray_traced_shadows` | Bật. Mặt trời đổ bóng bằng tia khi `shadows` cũng bật; đèn khi có `light3d_source::shadows` |
| `sun_angle` | Đường kính góc của mặt trời, độ; 0.53 là mặt trời thật, 2..5 là trời âm u, 0 là bóng sắc |
| `shadow_rays` | Số tia mỗi điểm ảnh mỗi nguồn sáng, 1..16 |
| `shadow_denoise` | Bán kính bộ lọc khử nhiễu, pixel, 0..8 |
| `light3d_source::source_size` | Bán kính bóng đèn: lớn thì bóng đèn mềm hơn |

Cần ba thứ:

- njin được build với `-DNJIN_RAYTRACING=ON` (hiện chỉ trên Windows, cần Vulkan SDK, xem @ref setup_engine_build).
- GPU có ray tracing: NVIDIA RTX, AMD Radeon RX 6000 trở lên, Intel Arc.
- Game chạy trên chính GPU đó. Laptop có hai GPU thì game phải link `njin::gpu` (các game mẫu đều link), không thì
  OpenGL chạy trên GPU tích hợp và log ghi rõ lý do ray tracing tắt.

raytracing_available() cho biết các điều kiện trên có đủ không, chẳng hạn để hiện tuỳ chọn trong menu đồ hoạ.
Khi thiếu, njin dùng ảnh bóng như cũ (`shadows`, `source_shadows`), và `graphics_quality` tắt bóng thì bóng
bằng tia cũng tắt.

Những gì đổ bóng bằng tia: hình khối, model (model đang có tư thế được tính lại trên CPU mỗi frame), vẽ
instanced (draw_instanced3d()) và địa hình. Hình SDF (draw_shape3d()) và cỏ nhận bóng nhưng chưa đổ bóng;
phần trong suốt của model không đổ bóng. Bóng bằng tia chỉ cho các bề mặt đục của lần vẽ vào thế giới: kính,
nước và sương khối vẫn dùng ảnh bóng của mặt trời, nên njin vẫn vẽ ảnh đó khi lần vẽ có chúng; đèn đổ bóng bằng
tia không đổ bóng lên chúng. Lần vẽ vào render texture (begin_3d() có `target`) dùng ảnh bóng.

Model có tư thế dựng lại cấu trúc tăng tốc mỗi frame, nên hàng trăm nhân vật có xương sẽ tốn hơn.

Thời gian một frame đo trên RTX 3050 laptop, 1280 x 720, một cảnh khoảng 50 vật (40 vẽ instanced, một model có
xương đang chạy, một đèn đổ bóng), sàn PBR nhẵn:

| Bật | ms mỗi frame |
|---|---|
| Không ray tracing (ảnh bóng như cũ) | 1.0 |
| `ray_traced_shadows` (2 tia) | 2.3 |
| `ray_traced_reflections` (ảnh bóng cho bóng đổ) | 2.1 |
| `irradiance3d_desc::ray_traced`, lưới 8 x 4 x 8, 128 tia | 2.0 |
| Cả ba | 2.7 |

Phần lớn chênh lệch là phần chung, trả một lần dù bật mấy phần: vẽ độ sâu trước, gửi cảnh sang Vulkan, đồng bộ
hai bên. Nên bật thêm một phần khi đã có phần khác thì rẻ hơn con số riêng của nó.

### Phản chiếu bằng tia {#reflect3d_raytraced}

`light3d::ray_traced_reflections` cho mỗi điểm ảnh một tia theo hướng phản chiếu của bề mặt. Vật liệu PBR nhẵn
(`material3d::pbr`, `roughness` dưới `reflection_roughness`) phản chiếu đúng những vật quanh nó, cả vật ngoài
màn hình hay bị che, thay cho ảnh môi trường hay probe. Nhám dần lên tới `reflection_roughness` thì phản chiếu mờ
dần về môi trường/probe; chỗ tia không gặp gì vẫn lấy môi trường (hay `ambient`).

@code
// Sàn đá bóng phản chiếu đồ đạc và nhân vật.
njin::light3d_set(ctx, {.direction = sun, .shadows = true, .tonemap = njin::tonemap3d_agx,
                        .ray_traced_shadows = true, .ray_traced_reflections = true});
njin::material3d_set(ctx, {.pbr = true, .metallic = 0.2f, .roughness = 0.08f});
njin::draw_plane3d(ctx, {0, 0, 0}, {20, 20}, {0.4f, 0.4f, 0.42f, 1});
@endcode

Vật được phản chiếu tô đơn giản: màu nền của nó (màu nhân màu trung bình của ảnh, không chi tiết ảnh), nắng
qua bóng của nó, tối đa 8 đèn (không bóng), ánh sáng nền hay lưới ánh sáng bằng tia (@ref probe3d), và màu phát
sáng. Hướng phản chiếu lấy theo mặt hình học, không theo normal map. Cần những điều kiện như bóng bằng tia
(@ref shadow3d_raytraced); những gì được phản chiếu cũng là những gì đổ bóng bằng tia.

### Nhiều đèn {#lights3d_many}

Mặc định mỗi lần vẽ có tối đa 16 đèn và mỗi điểm ảnh tính cả 16. `light3d::lights_max` lớn hơn 16 (tới
njin::light3d_limit, 1024) bật clustered lighting như Bevy: khung nhìn được chia thành 16 x 9 ô và 24 lớp độ
sâu, njin ghi các đèn chạm mỗi cụm, và mỗi điểm ảnh chỉ tính đèn của cụm nó. Hàng trăm đèn nhỏ (đuốc dọc
hành lang, đèn đường, tia đạn phát sáng) vẫn nhanh, miễn mỗi đèn có `radius` vừa đủ: đèn tầm xa chạm nhiều
cụm hơn.

`light3d::source_shadows` lớn hơn 4 (tới njin::light3d_shadow_limit, 16) cho nhiều đèn đổ bóng hơn, cũng bằng
cách này. Mỗi đèn đổ bóng thêm một hàng vào ảnh bóng và vẽ lại mọi hình đổ bóng 6 lần (đèn điểm) hay 1 lần
(đèn nón), nên giới hạn này là về tốc độ hơn là về số.

@code
// Một con phố đêm: 200 đèn đường, 8 đèn gần nhất đổ bóng.
njin::light3d_set(ctx, {.color = {0.05f, 0.05f, 0.1f, 1}, .lights_max = 256, .source_shadows = 8});
njin::begin_3d(ctx, cam);
for (const lamp &l : street_lamps)
  njin::light3d_add(ctx, {.position = l.pos, .color = {1, 0.8f, 0.5f, 1}, .intensity = 2.0f, .radius = 8.0f,
                          .shadows = l.near_player});
@endcode

Nước có `water3d_desc::lamps` cũng nhận các đèn này. Shader riêng của game vẫn chỉ nhận mặt trời.

Đèn mở rộng (`lights_max` trên 16, `source_shadows` trên 4), probe, lưới ánh sáng và vật xuyên sáng
(`material3d::transmission`) dùng một bản shader riêng, chỉ được dựng khi một lần vẽ cần tới. Bản đó cần hơn 16
texture unit; trình điều khiển chỉ có 16 (OpenGL trên macOS) không dựng được nó, nên ở đó các phần này tắt: log ghi
một cảnh báo, đèn quay về 16 đèn và 4 bóng, probe và lưới không dùng, vật xuyên sáng chỉ trong suốt. Cảnh không
dùng chúng vẽ như thường trên mọi máy.

### Sương khối {#fog3d_volume}

`light3d::volume_density` lớn hơn 0 bật sương có thật trong không gian: nắng và đèn tán xạ trong nó thành
những luồng sáng, bóng của vật cắt thành vệt tối (nắng lọt qua cửa sổ, đèn pin trong sương, đèn đường đêm
mưa). Mỗi điểm ảnh dò một tia từ camera tới bề mặt (hay tới `volume_distance`) qua `volume_steps` bước, ở nửa
độ phân giải, mỗi bước lấy ánh nắng qua bóng của nó (cả các tầng bóng), ánh các đèn qua bóng của chúng và
ánh sáng nền.

| Trường | Ý nghĩa |
|---|---|
| `volume_density` | Mật độ mỗi mét; 0.02 là sương nhẹ, 0.1 là dày |
| `volume_color` | Màu ánh sáng sương tán xạ lại |
| `volume_base`, `volume_falloff` | Sương dày dưới độ cao `volume_base`, mỏng dần lên trên |
| `volume_noise`, `volume_noise_size`, `volume_wind` | Sương loang thành đám, trôi theo gió |
| `volume_anisotropy` | Dương: sáng nhất khi nhìn về phía nguồn sáng (quầng quanh mặt trời, đèn pha) |
| `volume_sun`, `volume_ambient`, `volume_lamps` | Phần nắng, ánh sáng nền và đèn mà sương tán xạ |

@code
// Nhà kho bỏ hoang: nắng chiều lọt qua khe tường.
njin::light3d_set(ctx, {.direction = {0.3f, -0.4f, 0.85f}, .color = {3, 2.6f, 2, 1}, .shadows = true,
                        .tonemap = njin::tonemap3d_agx, .hdr = true,
                        .volume_density = 0.03f, .volume_noise = 0.4f, .volume_anisotropy = 0.6f});
@endcode

Sương khối chỉ áp cho lần vẽ vào thế giới và đẹp nhất với `light3d::hdr`: ánh sáng trong sương cộng thẳng vào
ảnh. Nó khác `fog_density` (sương phẳng theo khoảng cách, vẫn dùng được cùng lúc). Mỗi bước dò tốn như một lần
tính ánh sáng: 32 bước ở nửa độ phân giải là một hiệu ứng nặng vừa, nên giảm `volume_steps` trên máy yếu.

## Tone mapping và vật liệu PBR {#pbr3d}

Mặc định njin tô theo Blinn-Phong và ghi thẳng màu tính ra: chỗ sáng quá 1 thì cháy trắng. Ba phần
dưới đây bật riêng từng phần, game không bật thì vẫn vẽ y như cũ.

**Tone mapping** (`light3d::tonemap`, `exposure`): ánh sáng được tính trong không gian tuyến tính (màu,
đèn và ảnh coi như sRGB rồi đổi ra), nén chỗ sáng cho vừa màn hình rồi đổi lại sRGB, ngay trong shader
3D. Đèn mạnh hơn 1 không còn cháy trắng, và vật liệu PBR ra đúng màu.

| `tonemap` | Hợp với |
|---|---|
| `tonemap3d_none` | Như trước giờ (mặc định) |
| `tonemap3d_linear` | Chỉ nhân `exposure` rồi cắt ở 1, trong không gian tuyến tính |
| `tonemap3d_aces` | Như ACESFilmic của three.js: tương phản cao, kiểu điện ảnh |
| `tonemap3d_agx` | Như mặc định của Blender: mềm, chỗ rất sáng nhạt dần về trắng thay vì đổi màu |
| `tonemap3d_neutral` | Khronos PBR Neutral: giữ màu vật liệu gần như nguyên, cho sản phẩm, đồ vật |

Vật `unlit`, hạt 3D, nền và màu sương giữ nguyên màu nhập vào (sương trộn sau khi nén, nên vẫn khớp màu
nền). Bloom của post_fx_set() đọc ảnh đã nén; bật `light3d::hdr` để có bloom và tự chỉnh độ phơi sáng trên
ánh sáng thật (@ref hdr3d).

**Vật liệu PBR** (`material3d::pbr`, `metallic`, `roughness`): BRDF Cook-Torrance GGX, cách tô của glTF,
Blender và three.js, thay cho `specular` và `shininess`. Ánh sáng giữ cùng thang với Blinn-Phong: đèn
trắng chiếu thẳng vào mặt trắng nhám vẫn ra màu của đèn. Ảnh `metal_rough` (kênh xanh lá là độ nhám,
xanh dương là kim loại, như glTF) và `occlusion` (kênh đỏ) cho hình khối và instanced. Model glTF nạp
bằng `model_load_desc::pbr = true` thì mọi vật liệu của file tô PBR, với metallic, roughness, ảnh
metallic/roughness và occlusion trong file (ảnh chỉ được đọc lần đầu vẽ PBR, kể cả khi nằm ở bộ UV thứ
hai) và `KHR_materials_emissive_strength`.

**Môi trường** (`light3d::environment`): một ảnh toàn cảnh equirectangular 2:1 (`.hdr` hay png/jpg) chiếu
sáng và phản chiếu lên vật liệu PBR, thay cho `ambient` với chúng; kim loại phản chiếu nó, rõ hay mờ theo
độ nhám. Lần đầu gặp một ảnh, njin dựng sẵn ánh sáng khuếch tán (spherical harmonics) và sáu mức phản
chiếu theo độ nhám, tốn vài chục mili giây; đổi ảnh thì dựng lại. `environment_visible` vẽ ảnh làm nền
trời, `environment_intensity` và `environment_rotation` chỉnh độ sáng và hướng.

@code
const njin::texture_handle sky = njin::texture_load(ctx, "assets/sky/venice_sunset_1k.hdr");
const njin::model_handle helmet = njin::model_load(ctx, {.path = "assets/helmet.glb", .pbr = true});

njin::light3d_set(ctx, {.direction = {-0.5f, -0.7f, -0.6f}, .shadows = true,
                        .tonemap = njin::tonemap3d_agx, .environment = sky, .environment_visible = true});
njin::begin_3d(ctx, cam);
njin::draw_model(ctx, helmet, {});
njin::material3d_set(ctx, {.pbr = true, .metallic = 1.0f, .roughness = 0.3f});
njin::draw_sphere3d(ctx, {2, 1, 0}, 0.8f, {0.95f, 0.64f, 0.54f, 1}); // đồng
njin::material3d_set(ctx, {});
njin::end_3d(ctx);
@endcode

Phần phản chiếu môi trường bù lại năng lượng mà mặt nhám mất đi (multiple scattering, như three.js),
nên một quả cầu kim loại trắng thật nhám trong ánh sáng trắng đều vẫn trắng. Không có `environment` thì
vật liệu PBR nhận `ambient` như một bầu trời đều màu: kim loại khi đó chỉ phản chiếu màu ấy, nên đặt một
ảnh môi trường khi cảnh có kim loại. Bầu trời tự sinh của world3d (draw_sky3d()) chưa tự thành môi trường.

### HDR và tự chỉnh độ phơi sáng {#hdr3d}

Tone mapping ở trên chạy trong từng shader, nên ảnh của lần vẽ vẫn là 8 bit: chỗ sáng hơn 1 đã bị nén trước
khi bloom, SSR hay TAA đọc nó. `light3d::hdr` (cùng một `tonemap` khác tonemap3d_none) vẽ cảnh vào một ảnh
số thực 16 bit rồi tone map một lần ở cuối lần vẽ, như Bevy và Unreal. Thêm được hai thứ:

- **Bloom theo năng lượng** (`post3d::bloom`, @ref post3d_bloom): đèn và vật phát sáng mạnh tỏa quầng theo
  độ sáng thật của chúng, không cần ngưỡng.
- **Tự chỉnh độ phơi sáng** (`light3d::auto_exposure`): njin đo độ sáng trung bình của ảnh (ưu tiên giữa màn
  hình) và đưa nó dần về `auto_exposure_key`, với tốc độ `auto_exposure_speed`, trong khoảng
  `auto_exposure_min` tới `auto_exposure_max`: đi vào hang thì cảnh sáng dần lên, ra nắng thì tối lại.

@code
njin::light3d_set(ctx, {.direction = sun, .shadows = true, .tonemap = njin::tonemap3d_agx, .hdr = true,
                        .auto_exposure = true});
njin::post3d_set(ctx, {.bloom = 0.08f});
@endcode

Với HDR, mọi thứ của lần vẽ đi qua đường cong tone mapping, kể cả nền (màu xóa, 2D vẽ trước begin_3d()), như
Bevy: vật được chiếu sáng, nước và bầu trời nhân thêm độ phơi sáng; vật `unlit`, hạt 3D, sương và nền thì
không. Với tonemap3d_linear và `exposure` 1 các màu đó giữ nguyên; với ACES hay AgX chúng theo đường cong.
HDR chỉ áp cho lần vẽ vào thế giới (lần vẽ vào render texture vẫn tone map trong từng shader) và cần ảnh số
thực, nên tốn thêm bộ nhớ cỡ hai ảnh màn hình 8 bit.

### Vật liệu PBR nâng cao {#pbr3d_more}

Các trường sau chỉ có tác dụng khi `material3d::pbr`, như các phần mở rộng vật liệu của glTF và
StandardMaterial của Bevy. Model nạp bằng `model_load_desc::pbr` lấy chúng từ file
(`KHR_materials_clearcoat`, `_anisotropy`, `_ior`, `_transmission`, `_volume`; chỉ các hệ số, không đọc ảnh).

| Trường | Cho |
|---|---|
| `clearcoat`, `clearcoat_roughness` | Lớp sơn bóng trong suốt trên lớp dưới: sơn xe, gỗ đánh vecni |
| `anisotropy`, `anisotropy_rotation` | Điểm sáng bóng kéo dài theo hướng u của ảnh: kim loại chải xước, đáy nồi, tóc |
| `transmission`, `ior`, `thickness` | Ánh sáng xuyên qua: kính, nước trong chai, nhựa trong; cảnh phía sau bẻ cong theo chiết suất, mờ theo `roughness` (kính mờ) |
| `attenuation_color`, `attenuation_distance` | Màu của khối trong suốt: rượu vang, kính màu, nước biển |

@code
// Chai thủy tinh xanh, dày 1 cm, và vỏ xe sơn đỏ bóng.
njin::material3d_set(ctx, {.pbr = true, .roughness = 0.05f, .transmission = 1.0f, .ior = 1.5f,
                           .thickness = 0.01f, .attenuation_color = {0.6f, 0.9f, 0.7f, 1},
                           .attenuation_distance = 0.02f});
njin::draw_model(ctx, bottle, {.position = {0, 1, 0}});
njin::material3d_set(ctx, {.pbr = true, .roughness = 0.5f, .clearcoat = 1.0f, .clearcoat_roughness = 0.05f});
njin::draw_model(ctx, car, {});
@endcode

Vật xuyên sáng được vẽ sau mọi hình đục, như kính, và nhìn qua bản sao của cảnh đục mà njin tự chép ở lần vẽ
vào thế giới (từ frame sau frame đầu tiên có vật xuyên sáng). Ở lần vẽ vào render texture không có bản sao,
nên vật chỉ trong suốt theo `transmission`; trên trình điều khiển chỉ có 16 texture unit (macOS) cũng vậy
(@ref lights3d_many).

Hai trường nữa dùng cho mọi vật liệu, có `pbr` hay không:

- **Parallax** (`height`, `height_scale`): một ảnh độ cao (kênh đỏ, trắng là cao) làm mặt phẳng trông lồi lõm
  thật khi nhìn xiên, ảnh dịch theo góc nhìn và chỗ thấp bị chỗ cao che (parallax occlusion mapping). Cần
  lưới có UV, không dùng cùng `world_uv`.
- **Lightmap** (`lightmap`, `lightmap_intensity`): ánh sáng gián tiếp nướng sẵn (Blender, các công cụ bake),
  theo bộ UV thứ hai của lưới (bộ thứ nhất nếu lưới không có), thay `ambient` và môi trường trên bề mặt đó.
  Mặt trời và đèn vẫn chiếu thêm như thường.

### Probe phản chiếu và lưới ánh sáng {#probe3d}

Một ảnh môi trường chung cho cả cảnh không hợp với trong nhà: sàn kim loại trong phòng lại phản chiếu bầu
trời. probe3d_add() đặt một probe: njin chụp cảnh quanh `probe3d_desc::position` theo sáu hướng, ở lần vẽ 3D
vào thế giới kế tiếp, từ chính các lệnh vẽ của lần vẽ đó, rồi dựng thành ảnh môi trường riêng. Vật liệu PBR
trong hộp `size` lấy ánh sáng nền và phản chiếu từ probe đó thay cho `environment`, mờ dần sang probe bên
cạnh trong dải `blend` ở mép hộp. `parallax` sửa phản chiếu theo hình hộp nên tường và đồ đạc phản chiếu
đúng chỗ, không như ở xa vô tận. Tối đa njin::probe3d_max probe.

irradiance3d_set() đặt một lưới ánh sáng gián tiếp: njin chụp cảnh ở từng điểm của lưới (vài điểm mỗi frame
cho tới hết) và nhớ ánh sáng tới từ sáu hướng. Mọi hình trong hộp, kể cả vật liệu không `pbr`, lấy ánh sáng
nền từ lưới thay cho `ambient`: góc phòng tối đi, sàn cạnh tường đỏ ửng đỏ, mặt dưới của vật được sàn sáng hắt
lên.

@code
// Một căn phòng 8 x 3 x 8 m: một probe giữa phòng và một lưới 4 x 2 x 4.
const njin::probe3d_handle room = njin::probe3d_add(ctx, {.position = {0, 1.5f, 0}, .size = {8, 3, 8}});
njin::irradiance3d_set(ctx, {.center = {0, 1.5f, 0}, .size = {7.6f, 2.8f, 7.6f}});
// ... khi đèn trong phòng tắt hay đồ dời chỗ:
njin::probe3d_refresh(ctx, room);
njin::irradiance3d_refresh(ctx);
@endcode

Probe và lưới chỉ được chụp lại khi gọi probe3d_refresh(), irradiance3d_refresh() (hay đặt lại), trừ probe có
`live`, chụp lại mỗi frame (vẽ lại cảnh 6 lần). Cảnh chụp gồm hình đục, model, địa hình và bầu trời; không có
hạt, nước và kính. Trên trình điều khiển chỉ có 16 texture unit (macOS), probe và lưới không dùng được
(@ref lights3d_many).

`irradiance3d_desc::ray_traced` tính lưới bằng tia mỗi frame thay vì chụp (cần raytracing_available(), xem
@ref shadow3d_raytraced; không có thì lưới được chụp như trên). Mỗi điểm bắn `rays` tia ra mọi hướng; chỗ tia
gặp được chiếu bởi nắng (qua bóng), đèn và chính lưới, nên ánh sáng dội nhiều lần, như cách DDGI của NVIDIA làm:
phòng sáng dần qua cửa sổ, tường đỏ hắt sáng đỏ lên sàn, và lưới theo kịp đèn bật tắt, nắng đổi giờ, vật di chuyển
mà không cần irradiance3d_refresh(). `history` là phần giữ lại từ frame trước: lớn thì mịn, nhỏ thì đổi nhanh.

@code
// Lưới phủ cả phòng 8 x 3 x 8 m, lớn hơn phòng một chút: điểm nằm trong tường bị bỏ qua.
njin::irradiance3d_set(ctx, {.center = {0, 1.5f, 0}, .size = {8.1f, 3.1f, 8.1f}, .count_x = 7, .count_y = 4,
                             .count_z = 7, .blend = 0.05f, .ray_traced = true});
@endcode

Ánh sáng không lọt qua tường: như DDGI, mỗi điểm nhớ khoảng cách tới vật quanh nó theo mọi hướng (một ô bát diện
16 x 16), và khi một bề mặt lấy ánh sáng từ lưới, điểm nào bị vật che khỏi bề mặt đó (phép thử Chebyshev) hay
nằm sau lưng bề mặt thì gần như không tính. Một lưới phủ nhiều phòng vẫn để phòng tối tối dù phòng bên cạnh đầy
nắng. Điểm có phần lớn tia gặp mặt trong của vật (nằm trong tường, dưới sàn) bị bỏ hẳn, nên hộp có thể phủ tới
giữa bề dày tường để sàn và tường cũng nhận ánh sáng gián tiếp. Mặt trong được nhận theo chiều tam giác như
OpenGL vẽ; phần model vẽ hai mặt (`double_sided` trong glTF: lá, vải, tấm mỏng) không có mặt trong nên không
tính. Ở mép và góc phòng, nơi gần như mọi điểm bị che, ánh sáng tối đi thay vì loé lên. Chi phí tỉ lệ với số
điểm nhân `rays` (xem bảng ở @ref shadow3d_raytraced).

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

`particles3d_desc` còn có:

| Trường | Tác dụng |
|---|---|
| `style` | Kiểu vẽ có sẵn bằng shader của engine: `particle3d_smoke` (khói, bụi cuộn theo noise, sáng phía mặt trời), `particle3d_fire` (lõi lửa lập loè), `particle3d_spark` (vệt tia lửa nguội dần), `particle3d_flame` (luồng lửa phụt của động cơ phản lực, tên lửa) |
| `stretch` | Kéo hạt thành vệt dọc hướng bay, dài thêm quãng bay trong `stretch` giây: tia lửa, mưa, mảnh vụn |
| `shader` | Shader của game vẽ hạt thay cho kiểu có sẵn; đỉnh mang toạ độ thế giới |

Kiểu có sẵn đọc màu của hạt: độ trong suốt giảm theo tuổi, nên khói già thì mỏng, tia lửa đỏ dần.
Luồng lửa phụt dựng từ nhiều hạt nhanh cùng tốc độ: tuổi của hạt là quãng cách miệng phun.

@code
// Một bước cố định của động cơ phản lực: luồng lửa dài 1.5 m từ miệng phun theo hướng `dir`.
njin::particle_emitter flame{};
flame.life = {0.11f, 0.12f};          // 1.5 m ở 13 m/s
flame.speed = {650, 680};             // 13 m/s, đơn vị pixel (scale 0.02)
flame.spread = 4;
flame.size_start = 11;
flame.size_end = 34;
flame.color_start = {thrust, thrust, thrust, 1}; // kênh đỏ là lực đẩy, alpha là tuổi
flame.color_end = {thrust, thrust, thrust, 0};
flame.blend = njin::blend_additive;
njin::particles3d_spawn(ctx, flame, nozzle, 6,
                        {.direction = dir, .stretch = 0.025f, .style = njin::particle3d_flame});
@endcode

### Phát liên tục

Hạt 3D không phải entity, nên muốn phát đều (lửa trại, khói, sương) thì game giữ một bản
njin::particle_emitter như giữ trạng thái và gọi particles3d_emit() mỗi frame. Engine phát
`rate` hạt mỗi giây và giữ phần lẻ trong `emit_accum`, nên tốc độ thấp vẫn đều và `rate` đổi được
mỗi frame. Vùng sinh là njin::particle3d_area:

| `kind` | Hạt sinh ở |
|---|---|
| `particle3d_area_point` | Đúng `center` |
| `particle3d_area_ring` | Rải đều trên đường tròn bán kính `radius` quanh `center`, vuông góc với `normal` |
| `particle3d_area_disc` | Rải đều trên cả mặt tròn đó |

`outward` nghiêng hướng phát của vòng và đĩa ra ngoài theo bán kính: nước toả ra từ đài phun, lửa
toả ra từ vòng phép. Khi game tự chọn chỗ cho từng hạt (một điểm trên bề mặt model tìm bằng tia,
một trong vài điểm cho trước), particles3d_due() cho biết frame này cần bao nhiêu hạt, rồi game gọi
particles3d_spawn() cho mỗi hạt.
particles3d_area_point() lấy một chỗ ngẫu nhiên trong vùng, cho các chùm hạt sinh cùng một chỗ.

@code
// Giữ trong state của game, giữa các frame.
njin::particle_emitter flames = njin::fx::fire(), sparks = njin::fx::sparks();
flames.rate = 200.0f; // hạt mỗi giây
sparks.rate = 2.0f;   // chùm mỗi giây

// Mỗi frame: lửa trại trên cả mặt đống củi.
const njin::particle3d_area logs{.kind = njin::particle3d_area_disc, .center = campfire, .radius = 0.4f};
njin::particles3d_emit(ctx, flames, logs, {.style = njin::particle3d_fire});
// Tia lửa bắn từng nhúm 1–4 hạt từ một chỗ trong đống củi, khi củi nổ lách tách.
for (int n = njin::particles3d_due(ctx, sparks); n > 0; n--)
  njin::particles3d_spawn(ctx, sparks, njin::particles3d_area_point(ctx, logs), 1 + rand() % 4,
                          {.stretch = 0.03f, .style = njin::particle3d_spark});
@endcode

Khói, hơi nước và sương nhẹ thì dùng `shape = particle_soft`: đốm tròn mờ dần ra mép, không cần
tự làm ảnh.

Hiệu ứng tính trên ảnh 3D theo độ sâu (góc tường tối đi, sàn bóng phản chiếu, vết đạn dán lên bề mặt, mờ
chuyển động, tia nắng, lóa ống kính) ở trang riêng: @ref post_3d.

## Instancing {#instancing3d}

draw_instanced3d() vẽ `count` bản của một hình có sẵn hay một model bằng một lệnh vẽ, dữ liệu mỗi bản
nằm trong một bộ đệm instance như draw_instanced() của 2D. Không truyền shader thì shader có sẵn đọc:

| Thuộc tính | Nội dung |
|---|---|
| `instance0` | Vị trí `xyz`, tỉ lệ đều `w` (0 là 1) |
| `instance1` (từ 8 số) | Màu `rgba` |
| `instance2` (từ 12 số) | Góc xoay `xyz`, độ |
| `instance3` (16 số) | Tỉ lệ theo x, y, z (0 là 1) |

Hàng chục nghìn instance mà camera chỉ thấy một phần (rừng, thành phố) thì xếp chúng theo ô và chỉ vẽ những ô camera
thấy: @ref spatial_batch. Hoặc để engine tự bỏ từng instance, như dưới đây.

### Bỏ từng instance và mức chi tiết {#instancing3d_cull}

Bản draw_instanced3d() nhận njin::instanced3d_desc đọc vị trí và tỉ lệ của từng instance (`instance0`, `instance3`),
bỏ những instance nằm ngoài tầm nhìn, và chọn mức chi tiết (model_lod_build()) cho từng instance theo cỡ của nó trên
màn hình. Bóng đổ bỏ instance theo tầm nhìn của đèn, nên instance sau lưng camera vẫn đổ bóng vào cảnh.

@code
njin::model_lod_build(ctx, tree, {.levels = 2});
// Cả rừng trong một bộ đệm; engine chỉ vẽ phần camera thấy, cây xa bằng bản ít tam giác.
njin::draw_instanced3d(ctx, tree, forest, njin::instanced3d_desc{});
@endcode

| Trường | Mặc định | Ý nghĩa |
|---|---|---|
| `first`, `count` | cả bộ đệm | Đoạn instance |
| `cull` | bật | Bỏ instance ngoài tầm nhìn, và instance bị che khi bật render3d_options::occlusion |
| `lod` | bật | Mỗi instance một mức chi tiết; model không có mức chi tiết thì không đổi gì |
| `radius` | 0 (tự tính) | Bán kính quả cầu bao một instance ở tỉ lệ 1, quanh vị trí của nó |
| `pose`, `shader` | không | Như các bản draw_instanced3d() khác |

Engine duyệt các instance trên CPU, cho camera và cho mỗi bóng đổ, rồi chép những instance còn lại vào bộ đệm riêng
của nó, nên cần bản sao dữ liệu trên CPU: từ lệnh vẽ đầu tiên, mỗi instance_buffer_upload() giữ lại một bản, và lần
đầu engine đọc bộ đệm từ GPU về. Với vài trăm instance luôn nằm trong tầm nhìn thì bản thường rẻ hơn.
njin::render_info_get() đếm `instances3d` (đã vẽ) và `instances3d_culled` (bị bỏ).

## Bớt việc vẽ {#render3d_options}

render3d_options_set() bật ba cách bớt việc cho GPU và CPU. Mặc định tắt cả, nên cảnh không bật gì vẽ y như trước.
Chúng chỉ áp dụng cho vật đục (hình khối, model, hình SDF) vẽ bằng shader có sẵn, trong lần vẽ của camera; vật trong
suốt, vật vẽ bằng shader của game và thế giới ngoài trời (@ref world_3d) giữ thứ tự gọi.

@code
njin::render3d_options_set(ctx, {.order = njin::draw_order3d_state, .batch = true, .occlusion = true});
@endcode

| Trường | Làm gì | Khi nào nên bật |
|---|---|---|
| `order` | `draw_order3d_front_to_back`: vật gần camera vẽ trước, GPU bỏ điểm ảnh bị che phía sau. `draw_order3d_state`: gom theo shader, model hay hình và ảnh, rồi gần trước xa sau | Cảnh nhiều vật chồng lên nhau, vật liệu nặng |
| `batch` | Các draw_model() cùng model, cùng mức chi tiết, cùng màu và cùng njin::fx3d (và hình khối cùng hình, cùng vật liệu, cùng màu) vẽ bằng một lệnh instanced mỗi phần, cả trong bóng đổ. Giữ mọi ảnh của vật liệu, khác với tự gọi draw_instanced3d() | Hàng trăm bản của vài model vẽ từng cái một (đồ đạc, đạn, cây đặt tay) |
| `occlusion` | Bỏ vật bị vật khác che hẳn (Hi-Z), kể cả từng instance của @ref instancing3d_cull. Vật bị che vẫn đổ bóng | Trong nhà, thành phố, hang: nhiều thứ nằm sau tường |

Không gom: model có tư thế (draw_model_anim(), njin::model3d đang chạy animation), có morph, có model_recolor, vật
trong suốt, vật vẽ bằng shader của game, và phần lưới hơn 4096 đỉnh (mỗi đỉnh đọc lại ma trận của bản, trên GPU tích
hợp tốn hơn số lệnh vẽ tiết kiệm được). Đổi thứ tự hay gom lệnh có thể đổi bên thắng giữa hai mặt trùng đúng lên
nhau (z-fighting), vì ma trận tính trên GPU thay vì CPU.

Occlusion culling: sau các vật đục, engine chép độ sâu của lần vẽ, thu nhỏ thành ảnh 320 điểm ngang (mỗi điểm giữ độ sâu
xa nhất bên dưới nó), đọc về CPU mà không chờ GPU, rồi ở các frame sau thử hộp bao của mỗi vật với tháp mip của ảnh đó.
Độ sâu vì thế trễ một đến vài frame:

- vật vừa ló ra sau góc tường có thể hiện trễ một frame;
- camera đi xa hơn `occlusion_max_move` (mặc định 1 m) hay quay hơn khoảng 18 độ kể từ lúc lấy độ sâu thì frame đó
  không bỏ gì, chờ độ sâu mới. Camera chạy nhanh (xe, rơi) thì tăng `occlusion_max_move` nếu chấp nhận được vật
  hiện trễ;
- mỗi camera (chia đôi màn hình) giữ độ sâu riêng; begin_3d() vào render texture không dùng occlusion.

njin::render_info_get() đếm `occluded3d` (vật bị bỏ vì bị che), `batches3d` (lệnh do gom) và `batched3d` (lệnh vẽ đã
gom vào đó).

## Shader của game

shader_begin() trước một lệnh vẽ 3D thì hình đó vẽ bằng shader của game. Engine đặt sẵn các uniform
`vec3` `lightDir`, `lightColor`, `ambient`, `viewPos` nếu shader khai báo chúng, cùng `mvp`, `matModel`,
`matNormal`, `colDiffuse` theo tên chuẩn của raylib. Hình SDF luôn dùng shader của engine.

Khi bật `light3d::tonemap`, shader của game nhận thêm `int toneMap`, `float exposure`, `vec3
lightColorLinear`, `ambientLinear` (đèn trong không gian tuyến tính) nếu khai báo chúng: tính ánh sáng
bằng các màu tuyến tính rồi đưa qua cùng đường cong để vật vẽ bằng shader riêng khớp màu với cảnh.

@code
// Đoạn GLSL của njin, chép vào shader của game (uniform do engine đặt sẵn).
uniform int toneMap;     // 0 tắt, 1 linear, 2 ACES, 3 AgX, 4 Neutral
uniform float exposure;
vec3 linear_to_srgb(vec3 c) {
  c = max(c, vec3(0.0));
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}
vec3 tone_aces(vec3 c) {
  const mat3 into = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
  const mat3 outof = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
  c = into * (c / 0.6);
  vec3 a = c * (c + 0.0245786) - 0.000090537;
  vec3 b = c * (0.983729 * c + 0.4329510) + 0.238081;
  return clamp(outof * (a / b), 0.0, 1.0);
}
// tone_agx, tone_neutral: như trong src/engine/runtime/modules/render3d.cpp.
vec3 tone_out(vec3 linear) {
  vec3 c = linear * exposure;
  if (toneMap == 2)
    c = tone_aces(c);
  return linear_to_srgb(clamp(c, 0.0, 1.0));
}
@endcode

## Vật lý và va chạm {#physics3d}

`njin_physics3d.h` dựng trên [Jolt Physics](https://github.com/jrouwe/JoltPhysics) (giấy phép MIT, engine
kéo về khi build, game không thấy nó). Hình của body dùng cùng quy ước với njin::shape3d, nên một vật và
hình vẽ nó dùng chung số.

| Phần | Tạo bằng | Dùng cho |
|---|---|---|
| Body tĩnh | body3d_create() với `body3d_static` | Sàn, tường, bục cố định |
| Body kinematic | `body3d_kinematic`, rồi body3d_move_kinematic() mỗi bước | Bục di chuyển, thang máy, cửa: chở và đẩy vật khác |
| Body động | `body3d_dynamic` | Thùng, bóng, mảnh vỡ: rơi, va, lăn, bị đẩy; body3d_add_impulse() cho cú nổ, body3d_add_force() và body3d_add_torque() cho lực đẩy liên tục |
| Nhân vật | character3d_create() | Người chơi, quái: viên nang đi trên sàn, leo bậc, trượt dọc tường, đẩy body động |
| Body từ model | `body3d_desc::model` và `scale` | Sàn, dốc, hang làm trong Blender (tĩnh, kinematic); vật lồi (động) |
| Sensor | `body3d_desc::sensor` | Vùng nhặt đồ, checkpoint, bẫy, đích: không va, chỉ báo chạm |
| Sự kiện chạm | physics3d_contact_count(), physics3d_contact() | Biết cái gì bắt đầu hay thôi chạm cái gì |
| Khớp nối | joint3d_create() | Cửa bản lề, bập bênh, dây xích, piston, lò xo, bánh răng, ròng rọc |
| Ragdoll | ragdoll3d_create() | Nhân vật ngã, trúng đòn: bộ xương của model đi theo vật lý |
| Vật mềm | softbody3d_create() | Bóng cao su, nệm, khối thạch: biến dạng khi va chạm, giữ phồng bằng áp suất; va chạm với nhau, nhân vật đứng lên được |
| Vải | cloth3d_create() | Lá cờ, rèm, áo choàng: ghim vào một chỗ, bay theo gió |
| Xe có bánh | vehicle3d_create() | Ô tô: động cơ, hộp số tự động, lái, phanh, giảm xóc |
| Xe máy | motorcycle3d_create() | Xe hai bánh: tự giữ thăng bằng, nghiêng vào cua |
| Xe bánh xích | tracked3d_create() | Xe tăng, máy xúc: rẽ bằng hai dải xích, quay tại chỗ |
| Tia | physics3d_raycast() | Đạn, tầm nhìn, camera không xuyên tường; trả về body bị trúng, đi xuyên sensor; bản có njin::soft3d_hit trúng cả vật mềm |

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

### Lò xo, truyền động và lực

Các khớp và hàm dưới đây đủ để lắp một cỗ máy từ linh kiện: động cơ, pít-tông, lò xo, dây chun,
bánh răng, xích, thanh răng, ròng rọc, cánh quạt.

| Linh kiện | Làm bằng |
|---|---|
| Lò xo, giảm xóc | `spring` (tần số, Hz) và `damping` trên bản lề, khớp trượt, njin::joint3d_distance: giới hạn `[min, max]` thành lò xo; không có giới hạn thì lò xo giữ khớp ở vị trí lúc tạo |
| Dây chun | njin::joint3d_distance với `min = 0`, `max` là chiều dài lúc chùng và `spring`: chỉ kéo khi bị căng |
| Động cơ dây chun, chân ga | joint3d_set_motor_force() đổi lực mô-tơ lúc chạy (0 là tắt, khớp quay tự do); joint3d_speed() đếm số vòng đã vặn |
| Bánh răng, xích, dây curoa | njin::joint3d_gear nối hai bản lề `joint_a`, `joint_b` với tỉ số `ratio`: dương là ngược chiều (bánh răng ăn khớp), âm là cùng chiều (xích, curoa) |
| Thanh răng | njin::joint3d_rack nối bản lề `joint_a` (bánh) và khớp trượt `joint_b` (thanh): thanh trượt `ratio` đơn vị mỗi vòng bánh |
| Ròng rọc, palăng | njin::joint3d_pulley: dây từ `a` vắt qua `pulley_a`, `pulley_b` xuống `b`; `ratio` 2 là palăng |
| Cánh quạt, ống phụt | body3d_add_force() tại một điểm, mỗi bước cố định; joint3d_speed() cho tốc độ quay của cánh |
| Máy hỏng | joint3d_force(), joint3d_torque() cho lực khớp đang chịu: quá ngưỡng thì joint3d_destroy() |
| Cầm vật, cẩu, nam châm | body3d_pull() mỗi bước cố định: lò xo giảm chấn kéo body về chỗ và hướng cần tới; vật vẫn giữ khối lượng khi va chạm, đẩy vật khác như vật thật thay vì hất văng như khi hàn vào body kinematic |
| Con quay giữ thăng bằng | body3d_keep_upright() mỗi bước cố định: giữ một trục của body thẳng đứng, quay quanh trục đứng thì để yên |
| Linh kiện lồng nhau | body3d_set_group(): các body cùng nhóm không va chạm nhau (trục trong ổ, lá bản lề trong khe) |
| Mỗi linh kiện nặng theo hình của nó | `mass` của njin::body3d_desc là model_volume() (thể tích chất liệu của model, m³) nhân khối lượng riêng: thanh nhôm 2700 kg/m³, nên thanh rỗng nhẹ hơn thanh đặc cùng cỡ |
| Vật nhỏ quay tít như cao su | `angular_damping` (và `linear_damping`) của njin::body3d_desc: tăng lên 1..5 để vật lật rồi nằm yên |

Bánh răng tính theo góc quay của mỗi bánh so với khung của nó (body `b` của bản lề), nên vẫn đúng khi
cả cỗ máy chạy và xoay. Mô-men truyền theo tỉ số: bánh nhỏ (`ratio` 3) quay bánh lớn chậm hơn ba lần
với mô-men gấp ba. Hủy một bản lề thì bánh răng hay thanh răng trên nó bị hủy theo. Răng và mắt xích
chỉ là hình vẽ: khớp giữ tỉ số, không cần va chạm giữa các răng.

@code
// Bánh nhỏ 10 răng có mô-tơ quay bánh lớn 30 răng, cả hai trên khung `frame`, trục z.
const njin::joint3d_handle small = njin::joint3d_create(ctx, {.kind = njin::joint3d_hinge, .a = pinion,
    .b = frame, .anchor = {0, 1, 0}, .axis = {0, 0, 1}, .motor_force = 50});
const njin::joint3d_handle big = njin::joint3d_create(ctx, {.kind = njin::joint3d_hinge, .a = wheel,
    .b = frame, .anchor = {2, 1, 0}, .axis = {0, 0, 1}});
njin::joint3d_create(ctx, {.kind = njin::joint3d_gear, .joint_a = small, .joint_b = big, .ratio = 3});
njin::joint3d_set_motor(ctx, small, 360); // bánh lớn quay 120 độ mỗi giây, ngược chiều

// Pít-tông có đệm lò xo cuối hành trình: trượt tự do dọc y trong 0..0.5, vượt ra thì lò xo 4 Hz kéo lại.
njin::joint3d_create(ctx, {.kind = njin::joint3d_slider, .a = rod, .b = cylinder, .axis = {0, 1, 0},
                           .min = 0, .max = 0.5f, .spring = 4, .damping = 0.3f});

// Mỗi bước cố định: vật đang cầm ở 2 m trước mắt, giữ hướng lúc nhặt; khung nặng 20 kg gắn vào nó
// (quán tính 5 kg·m² quanh nó) được kéo theo.
njin::body3d_pull(ctx, held, eye + forward * 2.0f, held_turn, {.extra_mass = 20, .extra_inertia = 5});
// Con quay trên thân xe một bánh: giữ trục y của nó thẳng đứng.
njin::body3d_keep_upright(ctx, gyro, {0, 1, 0}, 2.0f, frame_inertia);
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

Vật mềm va chạm với mọi body, và với các vật mềm khác cũng bật `collide_soft` (mặc định bật): hai tấm vải
chồng lên nhau, tấm vải rơi lên võng, quả bóng rơi lên nệm. Mỗi đỉnh giữ cách mặt của vật kia một khoảng bằng
độ dày của hai vật (`thickness` của vải). Jolt không tính va chạm giữa hai vật mềm, nên engine tự tính trước mỗi
bước vật lý: đỉnh nào sắp lấn vào mặt của vật kia thì bị hãm lại bằng vận tốc, phần đà của nó truyền sang mặt bị
chạm (võng trũng xuống dưới tấm vải). Hai vật ở xa nhau chỉ tốn một phép so khung bao; tắt `collide_soft` cho vật
mềm không bao giờ gặp vật mềm khác.

Nhân vật **đứng được** trên mặt trên của vật mềm bật `walkable` (mặc định bật): nệm lún xuống theo sức nặng của
nhân vật (njin::character3d_desc::mass), bạt nhún võng xuống. Mặt dựng đứng (tấm rèm) thì nhân vật vẫn đi
xuyên, đẩy các đỉnh sang bên, nên rèm không chặn đường. character3d_ground_soft() cho biết nhân vật đang đứng
trên vật mềm nào (character3d_ground_body() khi đó không hợp lệ), còn character3d_ground_velocity() là vận tốc
của mặt vật mềm dưới chân: cộng vào cú nhảy để bật cao hơn khi bạt đang nảy lên.

physics3d_raycast() và các hàm `physics3d_*_push`, `_cast` đi xuyên qua vật mềm. Các bản physics3d_raycast(),
physics3d_box_cast() và physics3d_hull_cast() có thêm tham số njin::soft3d_hit thì trúng cả vật mềm: bắn vào
tấm rèm thì trúng rèm, và biết vật mềm, tam giác và đỉnh gần chỗ trúng nhất.

@code
// Bắn vào rèm: chỗ bị trúng bị đẩy theo hướng đạn.
njin::soft3d_hit soft;
const njin::ray3d_hit hit = njin::physics3d_raycast(ctx, shot, 100.0f, nullptr, &soft);
if (hit.hit && soft.soft.id != 0)
  njin::softbody3d_add_impulse(ctx, soft.soft, shot.direction * 0.5f);
@endcode

Vẽ vật mềm bằng softbody3d_model(): một model luôn có hình hiện tại của vật, trong thế giới, engine cập nhật
sau mỗi bước vật lý. Vẽ nó bằng draw_model() với transform mặc định, đổi màu và ảnh bằng
model_material_set(); model thuộc về vật mềm, softbody3d_destroy() hủy nó. Vật nhiều đỉnh (quá 65535, vải tính
gấp đôi vì có mặt sau) được chia thành nhiều lưới trong cùng một model. Muốn tự vẽ thì đọc
softbody3d_vertices(), softbody3d_normals() và softbody3d_indices(); softbody3d_draw_debug() vẽ các cạnh và đỉnh
bị ghim bằng gizmo.

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

### Xe máy và xe bánh xích {#vehicle3d_kinds}

motorcycle3d_create() tạo một xe máy trên bộ điều khiển xe máy của Jolt: một bánh trước lái trên phuộc nghiêng,
một bánh sau kéo, và một lò xo giữ xe đứng thẳng, nghiêng xe vào cua theo tốc độ và góc lái. Xe đứng yên không
đổ, chạy nhanh thì góc lái được engine giảm bớt để xe không văng (cua rộng hơn); `max_lean` là góc nghiêng lớn
nhất khi vào cua. Số mặc định là một xe 240 kg kể cả người lái, chạy ổn không cần chỉnh.

tracked3d_create() tạo một xe bánh xích (xe tăng, máy xúc): mỗi bên một dải xích chạy trên `wheels_per_side`
bánh, rẽ bằng cách cho hai dải chạy khác tốc độ. vehicle3d_set_input() lái nó như ô tô; khi xe gần đứng yên mà
chỉ lái không ga, hai dải chạy ngược chiều và xe quay tại chỗ. vehicle3d_set_tracks() điều khiển thẳng từng dải
(-1 lùi .. 1 tiến), vehicle3d_track_speed() cho tốc độ của dải xích để cuộn ảnh của nó.

Cả hai trả về njin::vehicle3d_handle, nên mọi hàm `vehicle3d_*` dùng được: vehicle3d_body() cho thân xe,
vehicle3d_wheel_transform() cho từng bánh, vehicle3d_rpm(), vehicle3d_gear(). vehicle3d_draw_debug() vẽ thân,
bánh, giảm xóc và điểm bánh chạm đất bằng gizmo, cho mọi loại xe.

@include physics3d_vehicles.cpp

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
| camera3d_ray() | Tia từ camera qua một điểm trên màn hình, ví dụ mouse_pos(); với camera song song, mọi tia cùng hướng nhìn, bắt đầu từ mặt phẳng của mắt |
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
