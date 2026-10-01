# Export bộ clay

Nguồn: `clay/modular_building_kit.blend`. Giữ đơn vị mét, socket, hierarchy, skin và PBR.

```powershell
& 'C:/Program Files/Blender Foundation/Blender 4.4/blender.exe' --factory-startup -b assets/models/procedural_building/clay/modular_building_kit.blend --python-exit-code 1 --python assets/models/procedural_building/export_modules.py
& 'C:/Program Files/Blender Foundation/Blender 4.4/blender.exe' --factory-startup -b --python-exit-code 1 --python assets/models/procedural_building/validate_exports.py
```

GLB và manifest được tạo trong `clay/`. Xem `clay/README.vi.md` để tích hợp.
Đạo cụ có exporter riêng tại `../street_clay/export_street_kit.py`.
Đóng gói cả 130 mảnh cùng rule bằng `tools/package_all_clay_exports.py` sau khi validation đạt.
