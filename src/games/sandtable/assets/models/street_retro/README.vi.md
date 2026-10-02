# Bộ đạo cụ đường phố Việt Nam — low poly retro

25 asset gốc, ít polygon, cạnh cứng và dùng bảng màu đơn giản cùng bộ nhà.
Nguồn chỉnh sửa: `street_retro_kit.blend`. Đã export 25 GLB vào `modules/`,
nạp lại và kiểm tra geometry/pivot; xem `export_manifest.json`
và `export_validation.json`.

## Lấy từng món trong Blender

- Chuyển editor sang Asset Browser → Current File rồi kéo collection `ST_*`.
- `Street_Retro_Catalog` trưng bày đủ 25 mẫu. Các instance trưng bày được thu/phóng
  để dễ so sánh; collection nguồn và manifest vẫn dùng kích thước thật theo mét.
- `Street_Retro_Sidewalk` là cảnh mẫu theo kích thước thật, có lối đi 1,6 m.
- `File → Append → street_retro_kit.blend → Collection → ST_<tên>` để lấy asset
  vào file khác. Chọn mẫu rời để thay đổi từng món; mẫu `*Space` đã ráp bàn/ghế/dù.
- Mỗi asset dùng gốc tọa độ ở mặt đất; +Z lên, mặt phục vụ/nhìn chính hướng -Y.
  Mesh dùng màu vật liệu trực tiếp, không nhúng texture vào GLB; đã bỏ chữ trên model.

## Danh mục

| Nhóm | Asset |
|---|---|
| Ghế đá | `StoneBench`, `StoneBenchBack` |
| Ghế công cộng gỗ/kim loại | `PublicBench`, `PublicBenchBack` |
| Ghế nhựa có tựa cao/thấp | `PlasticChairHigh`, `PlasticChairLow` |
| Đôn nhựa cao/thấp | `PlasticStoolHigh`, `PlasticStoolLow` |
| Bàn và dù | `PlasticTableLow`, `StreetUmbrella` |
| Hạ tầng | `FireHydrant`, `StreetLampSingle`, `StreetLampDouble`, `UtilityPole` |
| Thùng rác | `TrashBinRound`, `TrashBinWheelie` |
| Quầy rời | `HuTieuCart`, `BanhMiCart`, `MiQuangCounter` |
| Cụm bán hàng | `HuTieuSpace`, `BanhMiSpace`, `MiQuangSpace` |
| Trà đá | `TeaShoulderPole`, `TeaServiceSet`, `SidewalkTeaSpace` |

Quầy hủ tiếu có nồi nước lèo, bếp, tô, gia vị và rau. Quầy bánh mì có tủ kính,
khay bánh và thớt. Quầy mì Quảng có tô mì vàng, trứng, rau và bánh tráng.
Gánh trà đá có hai giỏ, quang gánh, thùng đá, ấm trà và ly; cụm trà đá có ghế thấp.
Các chi tiết quầy/cánh tủ hiện là mesh tĩnh.

## Dùng cho procedural layout

`distribution_rules.json` có rule chọn/phân bố đủ 25 asset theo zoning, spacing,
mật độ và composition. Xem `RULES.vi.md` để tích hợp, gồm cả lịch cánh chớp hiếm
theo thời gian game. `runtime_rules_validation.json` ghi kết quả kiểm tra.

`asset_manifest.json` ghi ID, collection, kích thước thật, bounds, điểm ngồi,
điểm phục vụ/làm việc, socket nối dây điện và điểm phát sáng. `sidewalk_layout.json`
ghi các placement và vùng lối đi cần giữ trống. Cụm hàng cách nhau 6 m trong cảnh mẫu.
Các khoảng cách là lựa chọn bố trí cho game, không phải thông số thiết kế hạ tầng thực tế.

Box collision trong manifest chỉ phục vụ chọn vị trí sơ bộ. Khi tạo physics/navmesh,
tách collider theo chân, mặt ghế, thân quầy và cột; giữ khoảng trống dưới bàn, giữa
các ghế và lối phục vụ. Không biến cả cụm `*Space` thành một hộp đặc.
Trụ đèn có vị trí nguồn sáng trong metadata; hiệu ứng ánh sáng và dây nối giữa
các trụ điện do code game tạo. Chưa kiểm tra importer/physics trong game.

`validation.json`: kiểm tra lại 25 asset từ file lưu, bounds, mesh kín, mặt suy biến,
UV, transform đã apply, gốc đặt sát đất và lối đi. Ảnh render và log đã được dọn; script render vẫn được giữ để tạo lại.

## Export thủ công

Chạy từ thư mục project khi bạn muốn xuất:

```powershell
& 'C:/Program Files/Blender Foundation/Blender 4.4/blender.exe' --factory-startup -b assets/models/street_retro/street_retro_kit.blend --python-exit-code 1 --python assets/models/street_retro/export_street_kit.py
```

Script xuất 25 GLB vào `modules/` và tạo `export_manifest.json`. GLB dùng mét,
+Y lên, mặt trước +Z; chuyển từ Blender bằng `[x,z,-y]`. Giữ ID `street/<tên>` khi
cache trong game. Script không chạy tự động khi dựng hoặc render kit.
