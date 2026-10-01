# Procedural building kit — đất sét

Phiên bản nguồn `unified-kit-v3-clay-v8-window-glazing`: 105 module, 3 bảng màu Indochine / Modern /
Brick, 9 module cửa có skin và clip. ID và socket giữ cùng quy ước với kit gốc.
Mở `modular_building_kit.blend`, chọn scene `PBK_Modular_Buildings`. Asset Browser
→ Current File để kéo từng mảnh; chạy text `PBK_Generator.py` để bật panel và
sinh thêm biến thể. Các shell mới cũng nhận UV, vật liệu và xử lý đất sét.

**Đã export và kiểm tra lại 105 GLB theo phiên bản nguồn v8.**
`export_manifest.json` ghi đường dẫn, hash, pivot và animation của từng mảnh.
`source_status.json` ghi trạng thái export; texture được nhúng trong GLB.

Đã giữ model, script, texture, rule và báo cáo validation. Ảnh/GIF kiểm tra
và ZIP đã được dọn, có thể tạo lại bằng script. `cleanup_report.json` liệt kê các file sinh tự động đã xóa.

## Visual

- Cửa sổ Indochine có hai cánh chớp gỗ đủ kích thước che ô cửa khi đóng,
  nan nghiêng với mép ngoài dốc xuống (góc X -25° khi đóng), bản lề và hai bone `Shutter_Left` / `Shutter_Right`. Khung
  cánh liền khối; cánh xoay ra ngoài 115°. Cửa phẳng, cửa cong R2/R4 và
  CornerWindow90 đều dùng cánh thật. `shutter_validation.json` kiểm tra
  kích thước, trọng số, chuyển động và va chạm qua 11 tư thế mỗi mẫu.
- Clip cánh chớp `Shutter_CloseOpen`: frame 1 mở, 24 đóng, 48 giữ đóng,
  72 mở, 96 giữ mở. Chọn rig rồi kéo timeline để xem; cửa kính bên trong
  đã bỏ tấm kính phía sau cửa có cánh chớp gỗ. Script export thủ công nhận diện rig mới,
  không gọi bộ bake cửa ra vào cho cánh chớp.
- Cánh đóng nằm trong lòng khung, mỗi cánh rộng 0.532 m, cao 1.452 m;
  khe quanh viền 4 mm, khe giữa hai cánh 8 mm. Bản lề đặt trước mặt khung
  để cánh xoay ra ngoài. `shutter_validation.json` kiểm tra từng đỉnh gỗ
  nằm trong lòng khung bo tròn trên cả bốn mẫu cửa, cùng va chạm khi chuyển động.
- Chọn rig `PBK_Indochine_*_Shutter_Rig`, mở Dope Sheet → Action Editor.
  Hai action `*_Shutter_Open` và `*_Shutter_Close` có keyframe thật từ frame
  1 đến 24; hai bone `Shutter_Left` / `Shutter_Right` điều khiển từng cánh.
  Action mặc định `*_Shutter_CloseOpen` dùng để xem chu kỳ đầy đủ, cũng là
  clip mà script export thủ công xuất theo chế độ SCENE hiện tại.
- Tường đỏ/Brick dùng bề mặt đất sét trơn, không có họa tiết mạch gạch ngang.
  Generator cũng bỏ các thanh `mortar`; xem `wall_pattern_validation.json`.
- 24 mẫu khung cửa dùng một mesh liền khối với góc bo liên tục, gồm cả khung
  uốn cong. Khung cửa ra vào mở ở chân; đã bỏ 36 thanh chia tạo dấu cộng trên
  kính. Cánh cửa và rig vẫn tách riêng để chuyển động. Kiểm tra 99 tư thế cửa
  không va chạm khung; xem `frame_validation.json`.
- Tường kem hoặc terracotta vẫn dùng chất liệu đất sét nhám. Kính cửa sổ,
  shopfront, cửa ban công và cánh cửa chính dùng kính trong transmission 1.0,
  roughness 0.035, IOR 1.45, alpha 1.0; ánh sáng truyền qua tấm kính thật.
  Cửa sổ Indochine có cánh chớp gỗ không có kính; cửa sổ Modern/Brick giữ
  tấm kính kín nhìn xuyên qua được. Xem `glazing_validation.json`.
  Tay nắm cửa giữ chất liệu kim loại.
- Khung cửa dùng gỗ nâu theo từng style, có texture màu, vân normal và ORM
  được pack vào Blender. Lan can dùng kim loại metallic 0.9, roughness 0.30.
  Generator dùng cùng chất liệu khi sinh thêm nhà. Xem `material_validation.json`.
- Cạnh các chi tiết thẳng bo tối đa 65 mm, 5 segment; bán kính giới hạn theo
  độ dày để giữ hình dạng các thanh nhỏ. Chi tiết tiếp xúc cùng màu được union
  rồi bo fillet tại các góc lồi/lõm phù hợp.
- Vỏ tường nhiều aperture giữ topology liên tục và khoảng thông cửa. Chi tiết
  đã uốn cong giữ topology uốn gốc, dùng smooth normals để tránh sliver Boolean.
- Mặt cắt nối module giữ phẳng; các điểm nối không bị center hoặc đổi scale.
- Các bề mặt đất sét có roughness 0.82–0.98, metallic 0. Normal texture hạt nhỏ và ORM có UV lặp
  mỗi 0.25 m; seed texture 41023. Texture được nhúng vào GLB và pack vào Blender.
  Không phụ thuộc Noise/Bump node chỉ chạy trong Blender.
- Mesh được triangulate và xuất tangents, cần thiết cho normal map trong game.

Ảnh render kiểm tra đã được dọn,
có thể tạo lại bằng các script render ở thư mục cha. `art_direction.json` ghi palette
linear RGBA, tham số và lịch sử xử lý.

## Nối vào game / giao cho Claude

Dùng **manifest của thư mục clay**, không dùng manifest ở thư mục cha:

```text
assets/models/procedural_building/clay/export_manifest.json
```

Tra `module_id` như `Indochine/Window` và nạp `path` tương đối với thư mục
manifest. Asset cache nên dùng `(kit_revision, module_id)` để không lấy nhầm
mesh/vật liệu đã cache của phiên bản gốc. Nạp toàn bộ node hierarchy, UV,
vertex normals, tangent, normalTexture và metallicRoughnessTexture.

Normal/ORM dùng màu tuyến tính, không đọc như sRGB; màu nền dùng quy ước PBR
glTF. Đơn vị mét, +Y lên, mặt trước +Z; render scale của sandtable là 0.1875.
Đừng đổi trục lần nữa sau khi loader glTF đã chuyển sang trục của renderer.
Kính trong cần renderer hỗ trợ `KHR_materials_transmission`.

Skin một bone `Door_Hinge`, clip `Door_OpenClose`: t=0 đóng, t=23/24 mở 90°,
t=47/24 giữ mở, t=71/24 đóng, t=95/24 giữ đóng. Không auto-loop cửa. Bộ rule
trong `rules/` có palette mới và revision mới; schema/ID/cách bố trí giữ tương
thích. Nội thất và collision vẫn được tạo theo rule riêng.

## Hiệu ứng giống metaball / smin

Trong kit, độ mềm được tạo bằng mesh union, fillet và normal của bề mặt cong.
**GLB riêng lẻ không tự thực hiện smin với GLB kế bên.** Khi ráp nhà, ưu tiên
một vỏ tường liên tục và dressing như generator Blender. Nếu cần hòa khối mềm
xuyên nhiều module trong game, thêm bước union/remesh sau khi ráp hoặc renderer
SDF với smin. Giữ cánh cửa chuyển động thành đối tượng riêng và trừ aperture
sau khi hòa phần kiến trúc, để vùng thông cửa không bị lấp.

Đây là giới hạn của bộ mesh được xuất, không phải hiệu ứng shader đã tích hợp
vào game. Các nối cùng mặt phẳng cần dùng socket chính xác, cùng pigment và
normal liên tục; việc đặt chồng hai full module không tự xóa mặt trùng.

## Kiểm tra

- `export_validation.json` (tạo lại khi export/validate thủ công): nạp lại 105 GLB, kiểm tra hash, pivot, bounds,
  UV/tangents, texture nhúng, skin và 45 tư thế cửa.
- `door_validation.json`: 11 rig mẫu, 99 tư thế, skin/bản lề và giao cắt tường.
- `generator_validation.json`: mở lại file và sinh nhà thẳng + nhà góc bo;
  kiểm tra 105 asset và trọng số đầy đủ của đỉnh cửa.
- `rules/rules_validation.json`: schema và 3 layout mẫu theo revision clay.

Chưa kiểm tra trong renderer game; collision/navmesh/LOD không nằm trong GLB.

## Xuất lại

Từ thư mục project:

```powershell
& 'C:/Program Files/Blender Foundation/Blender 4.4/blender.exe' --factory-startup -b assets/models/procedural_building/clay/modular_building_kit.blend --python-exit-code 1 --python assets/models/procedural_building/export_modules.py -- --root assets/models/procedural_building/clay
& 'C:/Program Files/Blender Foundation/Blender 4.4/blender.exe' --factory-startup -b --python-exit-code 1 --python assets/models/procedural_building/validate_exports.py -- --root assets/models/procedural_building/clay
```

Kit gốc và script chuyển đổi ban đầu đã chuyển ra khỏi assets vào
`.legacy-kit-archive/procedural_building/` ở thư mục project sau khi game chuyển sang clay.
Chỉnh nguồn `modular_building_kit.blend`, dùng generator và script cập nhật clay
ở thư mục cha; công cụ export/validation mặc định dùng thư mục clay.
`render_clay.py` render lại các góc nhìn. Bản gốc được giữ riêng để đối chiếu.
ZIP `clay_module_kit_glb.zip` chứa bộ GLB, texture, rule, manifest và các báo
cáo; file Blender source được cung cấp riêng để chỉnh sửa.
