# Progressive residency và software virtual texturing

Phần mở rộng của [asset streaming](renderer-streaming.md), cùng SDL3/bgfx,
texture/model stores và scheduler một worker đã có. Không thêm renderer, RHI,
asset manager hoặc thread pool khác. Các API load đồng bộ không đổi hành vi.

## Phạm vi đã triển khai

| API | CPU worker | GPU/main trước frame | Resource khi chưa đủ chi tiết |
|---|---|---|---|
| `model_request` với OBJ | fast_obj/MTL, ảnh, normals/bounds | texture/submesh upload, publish nguyên model | Placeholder cũ |
| `texture_request_mips` | Decode ảnh, tạo mip chain đúng color/normal/data | Tail nhỏ trước, thay bằng tail lớn dần | Mip đã resident |
| `model_request_lods` | Loader hiện có, welding và meshoptimizer | Coarse trước, thêm hoặc bỏ từng LOD | LOD coarse đã resident |
| `virtual_texture_load/request` | Range read page từ NVT1 | Writable atlas + page-table update | Ancestor hoặc permanent tail |

Không gọi bốn API này là native sparse residency. Software VT có địa chỉ ảo,
page table shader và cache vật lý giới hạn; không cần sparse texture extension.
Demand hiện do game/camera gửi, không có GPU feedback/readback tự động.

## Mip residency

`texture_request_mips` dùng `renderer_load_image` và ImageMipChain hiện có. Kind
color lọc sRGB, normal tái chuẩn hóa, data lọc tuyến tính. Decode/mip generation
chạy trên worker; hiện vẫn đọc/decode nguyên PNG/BMP/DDS... theo CPU decoder đã
build, không tuyên bố range streaming từ ảnh nén nguyên khối.

GPU chỉ cấp tail từ mip resident tới 1×1. Tail đầu là 1×1, mỗi bước refine thêm
một mức bằng texture allocation mới, upload **toàn tail mới đã khởi tạo**, rồi
swap trước frame và destroy allocation cũ qua bgfx. Đây là progressive mip
residency bằng replacement, không phải update một native sparse image hoặc chỉ
upload đúng mip mới. Chi phí lặp các mip coarse được tính đủ vào upload budget.

Sampler xem mip resident là physical level 0, UV vẫn là UV của ảnh gốc. Kích thước
gameplay vẫn là kích thước ảnh gốc. Không có mip GPU nào được sample trước khi
upload; các sprite/material nhận version bump khi image thay đổi. Format GPU là
RGBA8 như loader hiện có; không giữ compressed BC/ASTC residency.

`streaming_detail(ctx, request, level)` yêu cầu mức đích (0 là fine). Coarsen cấp
tail nhỏ và giải phóng tail lớn; refine giữ tail cũ tới khi upload xong. Nếu unit
mới vượt byte cap hoặc GPU budget, giữ tail cũ, state uploading và error quan sát
được. Cancel/retry làm mới CPU source; `texture_reload` trên resource progressive
trả false kèm cảnh báo để tránh cache CPU cũ ghi đè bản hot reload.

## Mesh LOD residency

Worker tái dùng welding/UV/skin/morph policy của Njin và meshoptimizer v1.3 đã pin.
Static mesh compact vertex/index sau welding trước khi simplify, rồi compact lại
cho mỗi LOD; triangle soup OBJ không còn giữ các đỉnh trùng không được tham chiếu
làm mất khả năng giảm mesh. Skinned/morph mesh giữ vertex order
gốc để bone/weight/target vẫn thẳng hàng; giảm index count, không tuyên bố cùng
mức tiết kiệm vertex memory như model static.

CPU model, bounds, material/submesh mapping, clips và query buffers luôn từ model
full detail. Upload material dependencies như trước, rồi upload coarsest geometry.
Chỉ publish level khi đủ mọi submesh. Những level chưa resident có handle nội bộ
0; draw selector tìm resident coarse, không gửi handle 0 tới bgfx. Camera LOD của
renderer, shadow/instance/deformation packets dùng cùng geometry lookup.

Refine thêm các level; coarsen destroy các GPU buffers finer, giữ fallback. Model
handle, bounds và CPU query data không đổi trong các chuyển level. Không blend
chéo hình học giữa LOD: đây là discrete LOD, có thể thấy pop. Không được gọi
`model_lod_build/load` để ghi đè một streamed LOD model; dùng `streaming_detail`.
Bake lightmap phải refine tới level 0 và pin trước khi snapshot.

Model material images nhúng trong glTF/OBJ vẫn upload nguyên image/chain theo
loader. Progressive mip riêng có thể gắn vào material qua texture handle bình
thường; không gọi material image dependencies nhúng là progressive mip I/O.
GPU deformation snapshots ở các draw sau vẫn nằm ngoài budget initial asset upload.

## Virtual albedo cho terrain và model

Cook ảnh vuông power-of-two <=16384 thành NVT1, page power-of-two 8..128. File
little-endian có size/page/mip metadata và fixed-size RGBA8 tiles, mỗi tile có
gutter 1 texel lấy từ đúng mip. Runtime chỉ range-read các page cần; không giữ ảnh
ảo full-resolution trong cache CPU/GPU. Cooker tạo mip sRGB offline.

Một writable RGBA8 atlas chứa page table ở các hàng đầu và cache cells phía dưới.
RGB của table entry chứa cell X/Y và mip ancestor thực. Shader chung của model và
terrain tự tra table, sample bordered tile rồi trilinear giữa hai lookup. Chỉ dùng
sampler albedo sẵn có, không thêm hai sampler riêng vào pipeline Android vốn đã
nhiều map. Per-draw flags giữ Repeat/Clamp/Mirror; global edge clamp không đọc
gutter repeat sai. VT atlas không có hardware mip chain, vì downsample page table
sẽ làm hỏng địa chỉ.

Cell 0 luôn giữ mip 1×1 cuối, không eviction. Trước tail thật là placeholder hồng.
Request UV tự enqueue ancestor coarse trước fine; table của page thiếu trỏ tới
ancestor đã khởi tạo. Cache LRU chỉ tái dùng cell đủ idle; không có cell thì demand
deferred, giữ fallback. Cell generation và weak ownership ngăn completion cũ ghi
đè cell đã tái sử dụng hoặc texture đã unload. Update tile và table cùng boundary,
không sửa atlas khi đang render. bgfx quản lý thứ tự GPU commands/deferred lifetime.

Atlas dùng `createTexture2D` **không initial memory** rồi upload toàn atlas khởi tạo,
vì bgfx từ chối `updateTexture2D` trên texture immutable có initial memory. Những
asset texture bình thường giữ path immutable cũ. Tail upload được tính vào global
stream GPU reservation theo toàn atlas; page updates chỉ tăng upload bytes, không
giả vờ cấp thêm VRAM mỗi page. Cache allocation cố định theo descriptor; page jobs
dùng cùng queue/worker/budgets với texture/model streaming.

Phiên bản này phục vụ **opaque albedo**. File có alpha bị từ chối rõ; VT normal,
roughness, height/displacement và lightmap chưa hỗ trợ. Renderer từ chối gắn atlas
vào các material-map slot đó; terrain CPU height/roughness và grass ground-color
baking cũng từ chối thay vì đọc nhầm page table. CPU lightmap baker cần ảnh source.
Custom fragment shaders/post dùng atlas làm source bị từ chối; cần shader biết
indirection. Terrain/model opaque shadows vẫn dùng alpha 1 và geometry resident.
Terrain detile/relief và model world-UV detile với virtual albedo hiện bị từ chối;
heightfield terrain thông thường và model dùng UV material được hỗ trợ.

Shader VT là variant opt-in đóng gói trong object riêng, được đăng ký khi gọi
`virtual_texture_load`. Program và uniforms chỉ tạo khi thực sự vẽ VT. Fragment
shader thông thường không chứa vòng tra page table; executable chỉ dùng asset
streaming không kéo package shader VT vào liên kết tĩnh.

CPU cache gồm atlas copies/metadata và page payloads, không chỉ tile bytes. CPU
reservation là accounting có giới hạn admission, không hard process/RSS cap;
decoder scratch/allocator/driver overhead không được tính chính xác. GPU estimate
là dimensions/vertex/index payload; destruction thực có độ trễ frames in flight.
Upload byte/unit cap là cứng cho scheduler, time cap mềm sau mỗi unit. Cache demand
gửi từ `phase_update` trên thread context; file cooked phải bất biến khi đang mở.

## API sử dụng

```cpp
auto color = njin::texture_request_mips(ctx, "assets/ground.png", 20);
auto normal = njin::texture_request_mips(ctx, "assets/normal.png", 10,
                                       njin::stream_texture_kind::normal);
auto model = njin::model_request_lods(ctx, "assets/scene.glb",
                                    {.levels=3, .ratio=.5f}, 20, true);
njin::streaming_detail(ctx, model, 2); // muốn coarse; getter vẫn trả model handle thường
auto info = njin::streaming_info(ctx, model);
// info.renderable: CPU model/full material + ít nhất một GPU LOD đã publish
// resident_level/target_level: phân biệt fallback đang có và mục tiêu upload

// Cooker offline, không chạy mỗi frame:
njin::virtual_texture_cook("assets/large.png", "assets/large.nvt", 64);
auto virtualColor = njin::virtual_texture_load(ctx, "assets/large.nvt",
                                             {.cache_side=4, .idle_frames=8});
// phase_update, theo vùng UV visible/dự đoán từ camera:
njin::virtual_texture_request(ctx, virtualColor, {0,0}, {.25f,.25f}, 0, 30);
// Dùng virtualColor như model_material::albedo hoặc terrain3d_layer::albedo.
// Scene exit: streaming_release cho requests; texture_unload cho virtualColor.
```

Pin resource còn được physics/ECS giữ; getter/touch ghi nhận demand. Khi budget
coarsen resource idle, demand tiếp theo có thể refine lại mức game yêu cầu.
Cancel/release/unload/context shutdown vẫn theo generation/token/join của scheduler
cũ. Worker không gọi context/device. Unload bị queued-draw guard từ chối không
được hủy request hoặc đóng cache trước khi guard kiểm tra xong.

## Fixture

```powershell
cmake --build build --target njin_residency_test njin_streaming_test --parallel 4
ctest --test-dir build -R '^(residency|streaming)_' --output-on-failure -j1
```

`residency.cpp` dùng BMP 512×512 tái tạo, NVT1 và OBJ triangle soup trong build
scratch, skinned/morph grid và glTF procedural `virtual-wrap.gltf`.
Đi qua coarse→fine→coarse→fine cho cả static và animated mesh,
cap không đủ nhưng giữ fallback, cancel/retry, camera nhanh và trở lại vùng vừa
rời, shadow draws trong lúc cache eviction/LOD transitions, tail/page jobs còn
pending khi unload và hai context mỗi process. Ảnh model/terrain/mip/LOD sau khi
ổn định được so với sync, tolerance tối đa 2 mức RGBA8; fixture wrap có UV âm với
Clamp U/Mirror V nên không thể pass chỉ nhờ sampler Repeat.

`streaming.cpp` thêm OBJ/MTL vào đối chứng sync/async và các case lifecycle/budget
đã có. Artifacts tại `build/bin/residency-fixtures`, gồm source BMP/NVT tái tạo,
ảnh `*-resident.png`, `*-reference.png` và `*-metrics.txt`. File format và shader
là Njin-owned; reuse/provenance trong notices và renderer-reuse.

## Kiểm chứng ngày 2026-11-23

Windows build và runtime đạt Vulkan, bgfx OpenGL, D3D12 và Vulkan validation:
8 fixture streaming/residency cùng 9 hồi quy model/material/animation/mipmap/
instancing/LOD/world đạt **17/17**. Log:
`build/residency-final-20261123-build.log`,
`build/residency-final-20261123-tests.log`.

Linux WSL Ubuntu build và runtime đạt Vulkan Lavapipe, OpenGL llvmpipe và Vulkan
validation. Lượt hồi quy đầu đạt 24/27, bắt đúng ba lỗi residency do static OBJ
chưa compact trước simplify. Sau sửa, build lại và chạy **6/6** streaming/
residency đạt trên cả ba cấu hình; 21 hồi quy còn lại đã đạt trước sửa helper đó.
Không ghi lượt 27 test cũ thành 27/27. Log cuối:
`build/residency-linux-final-20261123-{build,tests}.log`; log lỗi gốc được giữ ở
`build/residency-linux-tests.log`. Đây là software drivers trong WSL, chưa kiểm
chứng GPU Linux vật lý. OpenGL residency mất 136,80 giây cho cả fixture, gồm
driver/pipeline compilation; không suy ra tốc độ frame thực tế từ thời gian này.

Android arm64 APK build đạt; Pixel 6a chạy native `PASS 0` và controller xác nhận
đúng renderer cho residency và streaming trên cả Vulkan/GLES. Artifacts:
`build/residency-android-{vulkan,gles}-20261123` và
`build/streaming-android-{vulkan,gles}-20261123`. Residency tạo/hủy hai context,
ảnh sau ổn định lệch tối đa **1** mức RGBA8 so với sync trên cả hai backend.

| Số đo residency fixture | Fine | Coarse/giới hạn |
|---|---:|---:|
| GPU mip tail 512×512 RGBA8 | 1.398.100 byte | 5.460 byte ở mip 4 |
| GPU animated LOD geometry, Windows | 119.520 byte | 38.238 byte |
| Atlas VT, 4×4 cells, page 32 | 75.616 byte | cố định khi thay page |
| Số page VT resident | 16 | tối đa 16, bao gồm tail |
| Eviction VT trong mỗi lần chạy | 37 | ancestor vẫn render được |
| Upload unit lớn nhất | 1.398.100 byte | nằm trong cap fixture |

Android animated geometry fine là 119.526 byte do kết quả simplifier; không dùng
chênh lệch vài byte này để suy ra sai khác hình ảnh. Windows ảnh lệch tối đa 2.
Byte GPU là payload estimate, không phải phép đo VRAM driver; coarsen giải phóng
resource qua deferred destruction của bgfx.

ThreadSanitizer dùng binary riêng, instrument scheduler, context, renderer
resource store, texture/log, model loader, animation, LOD preparation, VT và
fixture residency. Lượt thô dừng với exit 66 tại race trong `libvulkan_lvp.so`;
giữ log `build/residency-tsan-runtime.log`. Chạy lại với đúng hai suppression
`libvulkan_lvp.so`/`libLLVM.so.21.1` đạt hai `RESIDENCY PASS`, exit 0, không có
diagnostic TSan chưa suppression (`build/residency-tsan-filtered.log`). Đây là
kiểm tra có giới hạn trên phần Njin đã instrument, không phải toàn bộ dependency
stack đã được chứng minh không có race.

Frame timing đầy đủ vẫn ghi bởi fixture streaming, không chứng minh hết stall:
Windows Vulkan lần đầu mean/p99/worst lần lượt 52,441/145,838/9.365,703 ms; lần
lặp 6,966/105,799/151,907 ms (`build/streaming-vulkan.log`). Những số này gồm
shader/pipeline/driver startup và controlled-delay stress, không phải benchmark
disk cold cache hay bằng chứng tăng FPS. CPU sync load đối chứng và upload-pump
timings nằm trong `build/bin/stream-fixtures/*-metrics.txt`; phân biệt cách đo ở
[asset streaming](renderer-streaming.md).

Đã chạy Doxygen thực và `graphify update .`, kiểm tra LF và `git diff --check`.
Doxygen còn 303 cảnh báo có sẵn ở các header cũ; không có cảnh báo cho
`njin_stream.h` hoặc `njin_virtual_texture.h`. Graphify còn báo syntax errors ở
một số file cũ, gồm `njin_log.h`; không sửa ngoài phạm vi để làm sạch số liệu.
