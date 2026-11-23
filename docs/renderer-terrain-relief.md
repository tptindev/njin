# Relief địa hình trên SDL3/bgfx

`terrain3d_layer::height`, `relief`, `terrain3d_desc::relief_distance` và
`relief_detail` đi qua API terrain hiện có. Không đổi ECS hay dữ liệu vật lý.
Đường raylib/OpenGL vẫn dùng shader legacy trong thời gian đối chiếu.

## Cách triển khai

- Mảnh gần camera chia thành các lưới con, tăng mật độ 1–8 lần và giới hạn
  kích thước để giữ chỉ số 16 bit. Tạo lười khi vào tầm; giải phóng lưới mịn
  ngoài tầm khi không còn draw tham chiếu. LOD xa và skirt vẫn được giữ.
- Ba texture do renderer sở hữu chứa pháp tuyến/độ cao mặt gốc (RGBA32F),
  trọng số splat (RGBA8), và kênh đỏ của bốn height map đóng vào RGBA8 có mip.
  Nội suy texture float thực hiện trong shader, không yêu cầu lọc float tuyến tính.
- Vertex shader đọc height map theo tile và triplanar, trộn trọng số splat lũy
  thừa 1,6, dời theo pháp tuyến và fade từ 70% đến hết tầm relief. Mip phụ thuộc
  bước lưới nhằm hạn chế aliasing. Height map khác kích thước được nội suy khi đóng gói.
- Sai phân trung tâm tính pháp tuyến relief, gồm cả thay đổi do fade. Normal map
  làm nghiêng pháp tuyến này; albedo đọc ở vị trí trước displacement, gồm phép
  chiếu ba phía trên vách dốc. Skirt không làm lệch vị trí đọc height map.
- Màu, normal/depth prepass và shadow map dùng cùng vertex shader. Shader velocity
  tính vị trí hiện tại và vị trí với camera fade của frame trước.
- AABB được nới theo biên displacement trước culling. Caster ngoài camera vẫn
  được giữ cho shadow map. Paint/edit làm mất hiệu lực dữ liệu cache; thay thế
  resource đang nằm trong queue bị từ chối trước khi giải phóng.

Va chạm, `terrain3d_height()`, bố trí cỏ và scatter tiếp tục theo mặt địa hình trơn.
`RenderTerrainRelief` chỉ chứa handle và giá trị CPU; không có object bgfx trong API.
Do packet renderer được thêm trường ở cuối, ứng dụng phải build lại cùng engine.

## Tham khảo và phạm vi

Đã đọc `wiTerrain.cpp` và `objectHF_tessellation.hlsli` của Wicked Engine ở
`754848e403986314cf49a038793ae9b62aebb74a`: tăng mật độ theo camera, displacement
theo normal và culling có biên displacement. Njin dùng lưới con cộng vertex
texture fetch vì bgfx không cung cấp tessellation. Công thức splat, triplanar
và fade được dùng lại từ mã Njin legacy; không nhập mã Wicked Engine.
Xem [quyết định tái sử dụng](renderer-reuse.md).

Giới hạn còn lại:

- Height map đầu vào phải là texture RGBA8 chưa đóng atlas. Độ cao lấy kênh R.
- Relief không đi cùng ray tracing: BVH hiện lấy mesh CPU trước displacement.
  Khi có lượt trace thực sự, engine báo lỗi rõ ràng thay vì trace mặt trơn rồi
  coi đó là bóng/phản xạ của relief. Shadow map raster được hỗ trợ.
- Không nên dùng relief với detile: offset của ảnh màu không dời hình học,
  giống giới hạn API legacy. Nội suy splat và nhiều lượt cộng màu của renderer
  hiện tại vẫn khác cách blend một lượt trong legacy.
- Shader velocity đã tích hợp; kiểm chứng riêng TAA khi edit/đổi LOD và dịch
  camera qua vùng fade vẫn cần bổ sung. Chưa tuyên bố kiểm chứng Apple/Web.
- D3D12 chạy bình thường đã qua fixture. Lượt bật GPU validation bị timeout ở
  lượt vẽ đầu, trước khi tạo ảnh relief; không tính lượt này là đạt.

## Kiểm thử

`njin_terrain_relief_test` tạo height map trong bộ nhớ, dùng entry point Njin,
vẽ terrain và shadow vào render target rồi đọc ảnh 320×240. Các control gồm mặt
trơn, displacement hằng, fade ngoài tầm, mật độ lưới, splat phủ toàn phần/một
phần, height map không phẳng, pháp tuyến, edit và height query không bị displacement.
Fixture còn từ chối destroy resource đã submit và kiểm tra thu hồi lưới mịn
khi terrain không đổ bóng rời cả frustum lẫn tầm relief.
Không cần asset bên ngoài.

```powershell
cmake --build build --target njin_terrain_relief_test --parallel 3
ctest --test-dir build -R '^terrain_relief_' -j1 --output-on-failure
```

Harness Android có suite `terrain_relief` và `ocean`. Chỉ tính đạt khi
`status.txt` có `PASS 0` và `controller_passed=True`. Các kết quả cuối cùng
được ghi trong `build/renderer-checkpoint.md` và backlog renderer.

Kết quả runtime ngày 2026-11-11:

| Nền tảng | Lượt chạy | Kết quả |
|---|---|---|
| Windows | Relief legacy OpenGL, Vulkan, bgfx OpenGL, D3D12, Vulkan validation | 5/5 đạt |
| Linux WSL | Relief và ocean: legacy OpenGL, Vulkan lavapipe, OpenGL llvmpipe; thêm relief Vulkan validation | 7/7 đạt |
| Android Pixel 6a, Android 16 | Relief và ocean, mỗi suite Vulkan/GLES | 4/4 có `PASS 0` và `controller_passed=True` |

Regression cleanup bổ sung cũng đạt: Windows 5/5, Linux 4/4 (thêm Vulkan
validation), Android relief Vulkan/GLES 2/2. Build toàn bộ target Windows đạt.

Ảnh Windows đã đối chiếu với legacy: silhouette IoU thấp nhất 0,998063 ở
các control, 0,998683 với height map gợn sau edit. Sai khác RGB trung bình
1,51–5,85/255; không coi là pixel-perfect. Pháp tuyến sai phân trung tâm khác
legacy sai phân tiến, và blending/lighting vẫn theo pipeline bgfx hiện có.
Số liệu từng ảnh: `build/renderer-relief-windows-parity.json`.
Lượt D3D12 GPU validation timeout không nằm trong 5/5 đạt nói trên.
