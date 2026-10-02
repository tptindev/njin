# Export toàn bộ kit retro

Toàn bộ nguồn hiện tại gồm **196 GLB**: 105 module building, 25 đạo cụ và 66 lan can.
Script chỉ export các kit tự tạo này; model đã nhập sẵn trong `city/`, `interior/`
và `person.glb` không cần chạy lại qua Blender.

## Một lệnh để xuất tất cả

1. Nếu vừa chỉnh model trong Blender, lưu file nguồn bằng **Ctrl+S**.
2. Mở PowerShell tại `D:\projects\njin\src\games\sandtable`.
3. Chạy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\export_all_retro_glb.ps1
```

Script dùng Blender 4.4 ở đường dẫn cài đặt hiện tại, lần lượt mở ba nguồn đã lưu,
export rồi nạp lại GLB để kiểm tra. Khi thành công, cuối log ghi `DONE: 196 validated GLBs`.
Nếu lỗi, script dừng và đưa đường dẫn log chi tiết; pack lỗi giữ trạng thái cần export.
Chỉnh model hoặc mở game không tự kích hoạt exporter.

GLB được ghi đè tại:

- `assets/models/procedural_building/retro/modules/<style>/<module>.glb`
- `assets/models/street_retro/modules/<asset>.glb`
- `assets/models/railings_retro/modules/<style>/<design>.glb`

Mỗi bộ có `export_manifest.json`, `export_validation.json` và `source_status.json`.
`assets/models/retro_export_status.json` tổng hợp lần export vừa chạy.
Script giữ đơn vị mét, pivot/socket, hierarchy và skin/animation; không sinh ZIP.

## Chỉ xuất một bộ

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\export_all_retro_glb.ps1 -Pack Building
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\export_all_retro_glb.ps1 -Pack Street
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\export_all_retro_glb.ps1 -Pack Railings
```

Nếu cài Blender ở vị trí khác, thêm `-Blender 'C:\path\to\blender.exe'`.

## Gọi trực tiếp exporter building

Nguồn: `retro/modular_building_kit.blend`.

```powershell
& 'C:/Program Files/Blender Foundation/Blender 4.4/blender.exe' --factory-startup -b assets/models/procedural_building/retro/modular_building_kit.blend --python-exit-code 1 --python assets/models/procedural_building/export_modules.py
& 'C:/Program Files/Blender Foundation/Blender 4.4/blender.exe' --factory-startup -b --python-exit-code 1 --python assets/models/procedural_building/validate_exports.py
```

GLB và manifest được tạo trong `retro/`. Xem `retro/README.vi.md` để tích hợp.
Đạo cụ có exporter riêng tại `../street_retro/export_street_kit.py`.
Lan can có exporter riêng tại `../railings_retro/export_railings.py`.
Nếu cần ZIP, chạy `tools/package_all_retro_exports.py` sau khi cả 196 mảnh validation đạt.
