# Lan can và tay vịn low poly retro

**22 kiểu × 3 style Indochine / Modern / Brick = 66 module.**
Nguồn: `railings_retro_kit.blend`; scene `Railings_Retro_Catalog`.
Asset Browser → Current File để kéo collection `RAIL_<style>_<design>`.
Mesh gốc dùng mét, +Z lên. Điểm gốc là đầu đường ghép, không phải tâm model.
Không nhúng texture, không chữ, không bo mềm kiểu đất sét.

| Nhóm | Module |
|---|---|
| Thẳng | StraightVertical, StraightHorizontal |
| Vuông / chéo | SquareGrid, SquareFrame, DiagonalCross, DiagonalSingle, Diamond |
| Đường nét cong | Wave, Arch |
| Tường thấp / gỗ | SolidParapet, WoodFence |
| Cầu / bờ nước | BridgeHeavy, RiversideBars, LakeLow |
| Đường đi đổi hướng | Corner90, ArcR2A45, ArcR4A30 |
| Cầu thang / gắn tường | StairSlope, StairHandrail, WallHandrail |
| Trụ kết thúc / chuyển tiếp | EndPost, TransitionPost |

Mẫu thẳng dài 2 m, cao 1,1 m; LakeLow và tay vịn gắn tường cao 0,9 m.
Mẫu StairSlope khớp cầu thang đã sửa: 15 bậc, cao 3 m, chạy 3,75 m.
Corner90 có tay vịn liên tục qua góc vuông; cung cong R2/45° và R4/30° dùng
ống low poly liên tục. Wave/Arch/Diamond là họa tiết, khác với cung cong của
đường ghép. Metal dùng bảng màu của kit nhà; WoodFence dùng gỗ,
SolidParapet dùng bê tông. Toàn bộ 66 module có tổng 18.624 tam giác.

## Ghép trong game

- `asset_manifest.json`: ID, collection, kích thước, số tam giác, vật liệu,
  socket `start/end`, `handrail_start/end`, hướng tiếp tuyến và node trụ.
- `railing_rules.json`: lựa chọn theo cầu thang, ban công, cầu, hồ, sông,
  sân thượng và công viên. Chọn một mẫu/bảng màu cho cả chuỗi cạnh bằng seed.
- Ghép socket cuối với socket đầu và khớp hướng tiếp tuyến. Chuyển tọa độ
  Blender sang glTF bằng `[x,z,-y]`; manifest xuất có thêm `sockets_gltf`.
- Hai trụ biên tách thành `*_StartPost` và `*_EndPost`; khi ghép nhiều mảnh,
  ẩn EndPost của mảnh trước và giữ StartPost của mảnh sau. Giữ trụ cuối chuỗi.
- Curve không scale lệch trục. Đoạn cuối ngắn cần trim/tạo lại và bịt đầu ống.
  Trụ kết thúc thấp được scale theo chiều cao tay vịn đã chọn.
- Tách chuỗi tại cửa, cổng và chiếu nghỉ. Collider là hàng rào mỏng theo cạnh,
  không dùng toàn bộ AABB thành một khối chắn lối đi.
- Có contract instancing, culling và LOD trong rule. Chưa thêm consumer C++,
  chưa tự đặt các lan can mới vào bản đồ hoặc thay lan can ban công hiện có.

`rule_reference.py` là ví dụ chọn mẫu ổn định theo seed và quyền sở hữu trụ nối.
Rule building đã liên kết pack này qua `runtime_rule_packs.railings`.

## Tạo và kiểm tra nguồn

```powershell
& 'C:/Program Files/Blender Foundation/Blender 4.4/blender.exe' --factory-startup -b --python-exit-code 1 --python assets/models/railings_retro/build_railings.py
python assets/models/railings_retro/make_rules.py
& 'C:/Program Files/Blender Foundation/Blender 4.4/blender.exe' --factory-startup -b assets/models/railings_retro/railings_retro_kit.blend --python-exit-code 1 --python assets/models/railings_retro/validate_railings.py
```

`validation.json` kiểm tra geometry kín, normal, mặt suy biến, bounds/pivot,
đầu tay vịn, socket, hướng cầu thang và rule bao phủ đủ 22 kiểu.

## Export thủ công khi cần

```powershell
& 'C:/Program Files/Blender Foundation/Blender 4.4/blender.exe' --factory-startup -b assets/models/railings_retro/railings_retro_kit.blend --python-exit-code 1 --python assets/models/railings_retro/export_railings.py
& 'C:/Program Files/Blender Foundation/Blender 4.4/blender.exe' --factory-startup -b --python-exit-code 1 --python assets/models/railings_retro/validate_exports.py
```

GLB nằm trong `modules/<style>/<design>.glb`. Hai script tạo/sửa nguồn không
gọi exporter. Đã xuất và kiểm tra đủ **66 GLB**. Xuất toàn bộ kit bằng
`tools/export_all_retro_glb.ps1`; xem `../procedural_building/EXPORTS.vi.md`.
