# Procedural building kit — low poly retro

Nguồn `unified-kit-v3-retro-v17-window-cross`: **105 module**, ba bảng màu
Indochine / Modern / Brick. GLB đã xuất lại và kiểm tra theo nguồn mới, gồm cầu thang 15 bậc và dấu cộng
chỉ cho kính phẳng.
Game và script dùng đường dẫn `retro` cho bộ model hiện tại.

- GLB rời: `modules/<style>/<module>.glb`.
- Danh mục, đường dẫn, hash, pivot và animation: `export_manifest.json`.
- Nguồn: `modular_building_kit.blend`; generator: `generate.py` và text
  `PBK_Generator.py` trong Blender. Asset Browser → Current File để lấy từng mảnh.
- Quy tắc kiến trúc, kính, cánh chớp và lighting: `rules/`.
- 25 đạo cụ nằm trong `../../street_retro/`.

## Visual hiện tại

Cạnh cứng, flat shading, khung vuông và màu vật liệu trực tiếp. GLB không nhúng
texture. Đã bỏ chữ và các chi tiết bo mềm kiểu đất sét. Cửa kính phẳng có thanh chia
dấu cộng cân đối, rộng 4 cm, cùng vật liệu khung; cửa cong không có dấu cộng.
Tường ngoài là mặt mesh không độ dày, có lỗ cửa thật; nhà sinh từ generator có
một mesh tường ngoài liên tục. Collider vẫn được dựng riêng từ layout, dày 0,2 m.

Khung cửa chạm tường, kính và đế shopfront khít với khung. Shopfront có 80% kính,
20% đế dùng cùng vật liệu khung. Các cửa sổ có cánh chớp gỗ không có kính;
cửa sổ khác giữ kính đục. Mối nối dùng socket chính xác, không đặt chồng cả
substrate lên vỏ tường đã có. Xem `../RETRO_STYLE.md`.

## Tích hợp game

Tra ID như `Indochine/Window` trong `export_manifest.json`, nạp `path` tương đối
với thư mục này. Cache bằng `(kit_revision, module_id)`.
GLB dùng mét, +Y lên, mặt trước +Z; chuyển từ Blender bằng `[x,z,-y]`.
Sandtable dùng render scale 0.1875. Giữ hierarchy và skin khi nạp animation.
Màu glTF là tuyến tính; chuyển đúng sang hệ màu của renderer.

13 module có animation: 9 cửa ra vào và 4 mẫu cửa sổ cánh chớp.
`Door_OpenClose`: frame 1 đóng, 24 mở 90°, 48 giữ mở, 72 đóng, 96 giữ đóng.
`Shutter_CloseOpen`: frame 1 mở, 24 đóng, 48 giữ đóng, 72 mở, 96 giữ mở;
hai bone `Shutter_Left` / `Shutter_Right` xoay 115°. FPS = 24.
Không tự loop cửa; cánh chớp đổi trạng thái hiếm theo `rules/shutter_behavior.json`.
Collision, nội thất, navmesh và LOD được code game dựng theo rule riêng.

## Xuất lại

Chạy thủ công từ thư mục Sandtable sau khi sửa nguồn:

```powershell
& 'C:/Program Files/Blender Foundation/Blender 4.4/blender.exe' --factory-startup -b assets/models/procedural_building/retro/modular_building_kit.blend --python-exit-code 1 --python assets/models/procedural_building/export_modules.py
& 'C:/Program Files/Blender Foundation/Blender 4.4/blender.exe' --factory-startup -b --python-exit-code 1 --python assets/models/procedural_building/validate_exports.py
```

`export_validation.json` ghi kiểm tra 105 GLB, hash, pivot, bounds, skin và 65
mẫu tư thế animation. `source_status.json` ghi trạng thái export hiện tại.
Script tạo/chỉnh model không tự export. ZIP có thể tạo bằng
`tools/package_all_retro_exports.py` khi cần.
Bản kiến trúc gốc để phục hồi nằm ngoài assets tại
`.legacy-kit-archive/procedural_building/`.


Xuất toàn bộ 196 GLB bằng `tools/export_all_retro_glb.ps1`; xem `../EXPORTS.vi.md`.
