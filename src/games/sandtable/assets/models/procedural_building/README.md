# Bộ model retro cho Sandtable

Game dùng `retro/` làm kit nhà mặc định. Kit kiến trúc gốc và pipeline chuyển đổi ban đầu đã chuyển vào `.legacy-kit-archive/procedural_building/` ở thư mục project.

- Nguồn nhà: `retro/modular_building_kit.blend`; generator: `retro/generate.py`.
- GLB nhà: `retro/modules/`; danh mục: `retro/export_manifest.json`.
- Rule nhà/cánh chớp: `retro/rules/`.
- Nguồn và GLB đạo cụ: `../street_retro/`; rule: `../street_retro/distribution_rules.json`.
- Nguồn 66 lan can/tay vịn: `../railings_retro/railings_retro_kit.blend`; hướng dẫn và rule trong `../railings_retro/`. Đã xuất và kiểm tra đủ 66 GLB.
- Hướng dẫn: `retro/README.vi.md` và `../street_retro/RULES.vi.md`.
- ZIP giao nhận đã dọn; tạo lại bằng `tools/package_all_retro_exports.py`.

Các helper, script cập nhật/render retro và exporter/validator dùng chung vẫn ở thư mục này.
Chạy export/validation từ project; mặc định dùng retro, hoặc truyền `--root`.
Không tự chạy lại script cập nhật revision cũ lên nguồn mới; chỉ dùng khi chủ động chỉnh model.

Một lệnh export/validate cả 196 GLB: `tools/export_all_retro_glb.ps1`; xem `EXPORTS.vi.md`.
