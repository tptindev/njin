# Lighting và tối ưu retro trong runtime

Mode quan sát dùng kính opaque. Mode third person/live
dùng kính trong bán kính 8 mét và đèn trong phòng; ở xa vẫn phản chiếu,
xem `interior-lighting.vi.md`.
`city/pbk_render.cpp::window_lighting`
đổi màu kính và emission theo `view_options.night`: bắt đầu sáng từ 0.25,
sáng hết ở 0.55, tắt khi trời sáng. Dùng chính mesh kính nên cửa cong, cửa
shop và cửa ra vào giữ đúng hình; khung gỗ và cánh chớp không phát sáng.
Material dùng chung giữa các instance; thay đổi được lượng tử hóa thành
64 bước, không upload lại hình học hay tạo đèn cho từng cửa. Model tải
sau khi trời tối cũng nhận trạng thái sáng hiện tại.

`city/render_pbk.cpp` chọn tối đa 3 đèn cửa sổ gần vùng camera nhìn, trong
160 world units. Ánh sáng giảm từ khoảng cách 80 đến 160, bán kính 3 mét,
không có bóng đổ. Cộng với tối đa 12 đèn đường, còn 1 trong 16 slot đèn
cho gameplay. Các cửa có cánh chớp gỗ không được chọn làm đèn chiếu ra phố.

LOD dùng chung `city/render_lod.cpp`:

- Ở gần: facade, cửa, cánh chớp và nhà procedural đầy đủ.
- Ở xa: khối nhà instanced đơn giản và mái. Nhà procedural lấy phân rã
  slab mái để giữ footprint góc cong; nhà fallback lấy bounding box.
- Cảnh cutaway vẫn dùng floor cache đầy đủ, không thay bằng khối LOD.
- `detail_r` là 750 khi camera distance dưới 35, 420 khi dưới 60, và 0
  khi từ 60 trở lên; khi focus dùng `focus_radius`. Đơn vị là world units.

Các module, lá cửa đứng yên và proxy xa gom instance theo asset và chunk.
Chunk ngoài vùng nhìn không gửi đi; các range liên tiếp gộp chung draw.
Animation chuyển động chỉ gửi hình ở gần và trong vùng nhìn. Kiểm tra
pose thay đổi một lần mỗi nhà, không lặp lại cho từng batch lá cửa.
Physics và timeline vẫn cập nhật để tương tác đúng khi camera quay lại.

Đường vẽ model/instance 3D của engine hiện dùng backface culling mặc định.
Không bật thêm transparency, bóng cho từng cửa hoặc occlusion query.
LOD hiện chuyển theo chunk, không crossfade; proxy xa giảm chi tiết màu
và facade. Mức tăng FPS cần đo trên máy và cảnh thực tế.

Kiểm tra: `--pbk-test` có các assertion về emission ngày/đêm, kính opaque,
gỗ không phát sáng và cửa cong tải sau khi trời tối. `--pbk-city-test`
chụp thêm `sandtable_pbk_city_windows_night.png` và
`sandtable_pbk_city_windows_lod.png`, log instance và số chunk chi tiết.
Không cần export lại GLB cho các thay đổi runtime này.
