# Bộ model clay cho Sandtable

Game dùng `clay/` làm kit nhà mặc định. Kit kiến trúc gốc và pipeline chuyển đổi ban đầu đã chuyển vào `.legacy-kit-archive/procedural_building/` ở thư mục project.

- Nguồn nhà: `clay/modular_building_kit.blend`; generator: `clay/generate.py`.
- GLB nhà: `clay/modules/`; danh mục: `clay/export_manifest.json`.
- Rule nhà/cánh chớp: `clay/rules/`.
- Nguồn và GLB đạo cụ: `../street_clay/`; rule: `../street_clay/distribution_rules.json`.
- Hướng dẫn: `clay/README.vi.md` và `../street_clay/RULES.vi.md`.
- ZIP giao nhận đã dọn; tạo lại bằng `tools/package_all_clay_exports.py`.

Các helper, script cập nhật/render clay và exporter/validator dùng chung vẫn ở thư mục này.
Chạy export/validation từ project; mặc định dùng clay, hoặc truyền `--root`.
Không tự chạy lại script cập nhật revision cũ lên nguồn mới; chỉ dùng khi chủ động chỉnh model.
