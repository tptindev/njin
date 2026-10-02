# Lan can cầu và bờ sông

`city/railings.cpp` sinh đường biên chung cho render và collider. Hai bờ
sông đặt về phía đất liền; mép cầu theo polyline đường và chiều rộng gồm
vỉa hè. Cắt các cửa lên cầu khỏi bờ sông, loại cạnh nằm trong đường khác
ở chỗ giao nhau. Các điểm giao dùng cùng tọa độ, không dùng hộp bao cầu.

Runtime đọc `assets/models/railings_retro/railing_rules.json` và
`export_manifest.json`. Chọn mẫu theo trọng số context river/bridge bằng
seed, giữ bảng màu Modern cho vùng bờ sông. Không chọn lại mỗi mảnh.

- Ghép mảnh thẳng 2 m bằng socket đầu/cuối, scale đều từ mét sang đơn vị game.
- Bỏ node StartPost/EndPost của panel; instance một EndPost tại mỗi vị trí
  nối duy nhất, dùng TransitionPost tại chỗ bờ sông nối sang mẫu cầu.
  Đoạn cuối ngắn được tạo lại với thanh bịt đầu và nan đơn giản,
  lấy màu từ asset đã chọn; không kéo dài model để phủ cả cầu.
- Collider riêng cao 1,1 m, dày 0,12 m, bám từng cạnh; không chắn ngang
  lối lên cầu hay chiếm cả hộp bao model.
- Gộp mesh theo vật liệu, instance theo asset và chunk. Frustum culling áp
  dụng ở mọi góc nhìn. LOD gần dùng panel đầy đủ; LOD xa bỏ infill và giữ
  trụ/tay vịn. Vật liệu opaque, backface culling; lan can không tạo bóng
  nhỏ trong shadow map. Không thêm texture hoặc đèn.

Kiểm tra: `njin_sandtable.exe --railcheck 30` kiểm tra giới hạn bản đồ và
điểm nối trên fixture sông gấp khúc/cầu xiên cùng 30 seed có sông.
`--pbk-city-test` chụp thêm `sandtable_bridge_railings.png` và
`sandtable_river_railings.png` để kiểm tra góc gần.

Thay đổi này không sửa steering/navigation của nhân vật và không thay
lan can ban công trong kit nhà. Không chạy export Blender.
