# Asset streaming SDL3/bgfx

Triển khai opt-in tại `njin_stream.h`. API đồng bộ `texture_load`, `model_load`,
LOD build/load và legacy raylib giữ hành vi cũ. `texture_request`/`model_request`
tải nguyên asset. OBJ async, progressive mip/LOD residency và software virtual
texturing được bổ sung bằng API opt-in riêng; xem [renderer-residency.md](renderer-residency.md).

## Audit và lựa chọn tái sử dụng

- Checkpoint khi bắt đầu: `2863070`. Working tree có nhiều thay đổi staged,
  unstaged và untracked; không reset/stash/stage/commit chúng.
- Texture/model stores hiện có là nơi giữ gameplay handles. ID slot tăng dần;
  RendererResources giữ CPU image/mesh, adapter giữ GPU objects. Trước thay đổi,
  texture/glTF đọc/decode/material/animation trên main, upload GPU lười ở draw đầu.
- Không có asset manager tổng quát hoặc shared job pool để mở rộng. Jolt đang dùng
  `JobSystemSingleThreaded`; atmosphere future, networking threads và helper của
  `model_combine` không có submit/completion contract dùng chung.
- Tái dùng `renderer_load_image`, cgltf loader, meshoptimizer decompression,
  normal/material/UV/sampler conversion, PortableAnimation và bgfx/bimg đã pin.
  Parser đồng bộ và streaming dùng cùng implementation, khác nơi commit ownership.
- Đã đọc source Streamer, JobSystem, RendererStream và fixture của snapshot nguồn.
  Có implementation CPU jobs, generation checks, upload, mip/LOD, không chỉ declaration;
  chưa chạy snapshot này. Không có Git/root license truy cập được nên không nhập
  hoặc dịch mã, asset, định dạng riêng, driver Vulkan hoặc framework scene.
  Hash snapshot và nguồn Bevy AssetServer `v0.18.0` đã review nằm trong
  `THIRD_PARTY_NOTICES.md`. Không thêm dependency.
- bgfx `b2513038f4b753ebeced40821c193978daa0492f` có `updateTexture2D` theo mip,
  nhưng adapter Njin chưa có base-mip/sparse-residency contract. Chọn full image
  thay vì thêm sampler/residency policy chưa kiểm chứng. glTF mip chain hiện có
  được chuẩn bị trên worker và upload nguyên chain.

## Ownership và trạng thái

`context.streaming` là scheduler theo context, không giữ một bộ asset GPU khác.
Nó chỉ tạo một worker khi game opt-in. Jobs giữ đường dẫn/options/token và CPU
payload riêng; không capture context, device, ECS hoặc slot đang sống. Một job
đang chạy và tối đa một completion chưa drain tạo backpressure cho I/O.

Worker đọc file, decode/decompress image/buffers, dựng mesh/material/bounds,
normal, skeleton/morph và mip chains. Model staging dùng scratch model_store
không có context/GPU slot, sau đó chuyển ownership vào payload. Mutex bảo vệ
work/completion queues; token loading/cancel là atomic. Game log sinks không
được gọi trên worker; lỗi decode được trả trong `stream_info.error`.

Vòng `run` SDL3/bgfx gọi `streaming_update` trước frame. Main thread nhận CPU
payload, kiểm tra admission, tạo Renderer resources rồi gọi adapter
`ensure_texture[_prepared]`/`ensure_mesh` thật. `uploading` bao gồm cả thời gian
chờ budget; `ready` chỉ sau khi mọi unit đã gửi tới bgfx và publish nguyên asset.
Đây là submission readiness, không phải CPU chờ GPU fence hoàn tất.

Texture placeholder là pixel hồng; model placeholder là tam giác trắng. Slot
gameplay giữ cùng ID khi publish/evict/retry; mesh bounds/material/submesh mapping
và animation đổi đồng thời. Không publish model còn thiếu dependency texture.
Trong thời gian upload model, placeholder vẫn vẽ được. Không có native bgfx handle
trong request, texture_handle hoặc model_handle.

Request cùng loại/đường dẫn/options dùng chung record và tăng reference; cancel
áp dụng record chung. Release reference cuối hoặc unload qua API cũ hủy request,
cleanup tại boundary. Retry tăng generation, completion cũ không được publish.
Request còn mang context identity nên request của context trước bị từ chối.

Context destructor hủy token, đánh thức delay và join worker trước khi các store
và device chết. Không detached jobs. Decoder hiện không preempt giữa một lần đọc/
decode: shutdown chờ job đó kết thúc. Khi unwind giữa render frame, GPU resources
được để renderer/window teardown giải phóng, tránh destroy object đang queued.
Game đổi scene dùng `on_exit` để release requests thuộc scene; resource dùng chung
cần giữ reference riêng. Fixture đi qua cả scene exit và context shutdown.

## Budget, demand và lifetime

- `cpu_bytes`: admission/reservation cho payload được chấp nhận, giữ reservation
  sau ready vì renderer và physics/query vẫn giữ CPU data. Đây là accounting
  theo kích thước vector đã biết, không phải đo chính xác heap: chưa gồm allocator
  capacity, parser/decode scratch, animation internals hoặc một completion/in-flight
  decode chưa admit. Muốn hard process cap dùng memory manager hiện có của Njin.
- `gpu_bytes`: ước tính full mip pixels + vertex/index bytes của streamed assets.
  Không gồm driver alignment, staging, frames in flight, placeholder, animation
  deformation snapshots hay resource sync do game tạo. bgfx deferred destruction
  khiến physical memory có thể giảm sau accounting một số frame.
- `upload_bytes_per_frame` và `upload_units_per_frame`: giới hạn cứng lượng byte/
  số unit submit bởi scheduler. Unit là một texture/chain hoặc một submesh. Nếu
  unit lớn hơn byte cap, request failed với thông báo rõ, không tự vượt cap.
- `upload_ms_per_frame`: giới hạn mềm, kiểm tra sau mỗi unit. Không thể ngắt một
  driver call, allocation, memcpy, publication hoặc shader/pipeline compilation.
  Không bảo đảm worst frame nằm dưới con số này.
- `streaming_touch` hoặc getter ghi demand frame/priority. Game quyết định demand
  từ scene/camera/distance; scheduler không tự suy ra visibility ECS. Priority
  áp dụng job chưa chạy và payload đang chờ upload; không preempt decoder.
- `streaming_pin` giữ resource được ECS, physics, animation hoặc query lưu lâu dài.
  Chỉ resource ready, không pin, đủ `idle_frames` mới được evict. Pressure GPU
  chọn LRU đủ điều kiện; CPU admission có thể evict cold resources. Budget bị pin
  chặn được quan sát qua stats hoặc lý do `uploading.error`.
- Eviction chỉ diễn ra ngoài frame: pending draws/shadow/instance packets đã submit.
  Njin giữ guard queued-resource hiện có; bgfx quản lý deferred GPU destruction.
  Chỉ bỏ CPU data khi toàn asset đủ điều kiện eviction/unload, không bỏ ngay khi upload.
- Sau eviction, getter/touch tái enqueue request, giữ placeholder cùng gameplay ID.
  Hysteresis idle tránh eviction/reload quanh camera mỗi frame. Nếu lưu handle vào
  ECS mà không gọi getter, game phải pin hoặc touch resource đó khi còn cần.

## Sử dụng

```cpp
// startup/update, trên thread sở hữu context
njin::stream_config limits;
limits.upload_bytes_per_frame = 8 * 1024 * 1024;
njin::streaming_configure(ctx, limits);
auto picture = njin::texture_request(ctx, "assets/surface.png", 20);
auto object = njin::model_request(ctx, "assets/object.glb", 10, true);
njin::streaming_pin(ctx, object); // ECS/physics còn giữ model

// draw bình thường; getter ghi demand, có placeholder trước ready
auto material = njin::model_material_get(ctx, model, material_index);
material.albedo = njin::streaming_texture(ctx, picture);
njin::model_material_set(ctx, model, material_index, material);
njin::draw_model(ctx, njin::streaming_model(ctx, object), transform);

// scene on_exit; engine tự drain/cleanup tại boundary
njin::streaming_release(ctx, picture);
njin::streaming_release(ctx, object);
```

Query trả value: giữ `stream_info` hoặc copy chuỗi nếu cần dùng lâu; không giữ
`error.c_str()` lấy từ một temporary đã hết lifetime.
Model streaming hiện nhận path và pbr; node filters/merge/custom LOD descriptors
vẫn dùng API sync. Đặt material override sau ready nếu cần giữ override khi publish.

## Fixture và kiểm chứng

```powershell
cmake --build build --target njin_streaming_test --parallel 4
ctest --test-dir build -R '^streaming_' --output-on-failure -j1
```

Fixture dùng BMP 32×32 procedural, các glTF public/procedural nhỏ trong test assets,
8 texture và static/PBR/skinned/morph models. Chạy camera nhanh rồi đứng yên để so
RGBA screenshot streamed/sync bằng nhau; lần lượt kiểm tra queued/loading/uploading/
ready/failed/cancelled/evicted, dedup texture/model, thiếu/hỏng image, retry, unload
loading, cancel/retry partial upload, budget thấp, pin/LRU/reload, unit quá lớn,
scene exit và delayed job còn sống khi context chết. Mỗi process tạo hai context
và kiểm tra request từ context cũ bị từ chối.

Artifacts ở `build/bin/stream-fixtures/`: BMP tái tạo được, ảnh `*-stream.png`/
`*-sync.png` và `*-metrics.txt`. Counters gồm frame mean/p99/worst, upload peak/ms,
queue depth, CPU reservation, GPU estimate, Njin tracked heap peak, dedup/evictions
và thời gian CPU load sync tham chiếu. Reference load sync chưa gồm GPU lazy upload;
không so nó trực tiếp với full-frame time để kết luận tăng tốc.

`run=0/1` là lần đầu/lần lặp trong cùng process, **không phải** benchmark disk cold
cache có kiểm soát. Không flush OS cache. Shader/driver caches và hoạt động build
khác có thể ảnh hưởng số đo. Kết quả backend và số đo của lượt cuối được ghi bổ sung
bên dưới. Stress tests không phải bằng chứng không có data race; sanitizer được
ghi riêng nếu thực sự chạy.

## Giới hạn

- Texture standalone theo định dạng CPU loader Njin đã build, chuyển RGBA8/base
  image như `renderer_load_image`; fixture đã kiểm BMP và model embedded PNG.
  TGA/JPEG tùy compile options; không tự bật decoder mới. Compressed image chỉ
  được decompress nếu bimg path hiện có hỗ trợ; không native compressed residency.
- Model async dùng glTF/GLB/OBJ với feature/extension mà portable loader hiện hỗ trợ;
  OBJ sync giữ nguyên. Dependency image lỗi làm cả request
  failed, không publish model thiếu texture. Skeleton/morph/clip metadata và CPU
  query buffers được giữ; collision cooking là công việc riêng, cần pin input.
- Mip/LOD residency và virtual texturing dùng API riêng trong tài liệu residency;
  request nguyên asset không đổi ngầm thành progressive. Model LOD API hiện có
  không bị thay đổi. GPU deformation/animation uploads sau draw nằm ngoài budget
  initial asset streaming này.
- Public request API dùng thread sở hữu context; chưa có arbitrary background
  gameplay requests, custom package I/O hay retry tự động. Đường sync không được
  chuyển ngầm sang async.

## Kết quả thực chạy (2026-11-20)

| Môi trường | Compile | Runtime streaming |
|---|---|---|
| Windows Vulkan | Pass | Pass, hai context; validation riêng pass |
| Windows bgfx OpenGL | Pass | Pass, hai context |
| Windows D3D12 | Pass | Pass, hai context |
| Linux WSL Vulkan Lavapipe | Pass | Pass, hai context; validation riêng pass |
| Linux WSL OpenGL llvmpipe | Pass | Pass, hai context |
| Android arm64 Vulkan, Pixel 6a | Pass APK Release | Pass, hai context |
| Android arm64 GLES, Pixel 6a | Pass cùng APK | Pass, hai context |

Linux là software drivers, chưa chứng minh GPU Linux vật lý. Không suy ra hỗ trợ
platform khác từ capability bgfx. Android app được cài/chạy qua runner hiện có,
không dùng một file-loader demo độc lập.

Windows streaming + kiểm lại legacy model/material đạt **6/6** ở
`build/streaming-windows-final.log`. Suite model/material/animation/instancing/mipmap
trước đó đạt 21/22: legacy `materials_opengl` phát hiện lời gọi `njinSurface` thiếu
UV lightmap sau thay đổi shared shader đã có trong working tree. Đã bổ sung một
tham số UV vào adapter legacy, và test này pass trong lượt cuối. Không xóa raylib.
21 test còn lại đã pass; không gọi kết quả ban đầu là 22/22 một lượt.

Linux đạt **18/18** streaming và regression tại `build/streaming-linux-tests.log`;
lượt cuối có scene/foreign-context/metrics và kiểm lại legacy material đạt **4/4**
tại `build/streaming-linux-final-tests.log`. Android lượt cuối: cả hai thư mục
`build/streaming-android-{vulkan,gles}-final/` có `result.txt = PASS 0`,
`controller_passed=True`, ảnh và metrics. Native fixture kiểm RGBA equality trên
mọi backend; đã xem ảnh Windows và Android. Chưa đo throughput với asset lớn.

Số đo `run=1` cuối, đơn vị millisecond; đây là toàn bộ fixture có capture/readback,
shader/pipeline initialization và cảnh lỗi/budget, không phải benchmark steady-state:

| Backend | Frame mean | p99 | Worst | Upload pump max | CPU load sync tham chiếu |
|---|---:|---:|---:|---:|---:|
| Windows Vulkan | 8.613 | 149.927 | 154.588 | 0.155 | 10.155 |
| Windows bgfx OpenGL | 6.933 | 85.880 | 205.409 | 0.151 | 4.798 |
| Windows D3D12 | 41.408 | 169.840 | 5171.710 | 0.106 | 12.236 |
| WSL Vulkan | 29.585 | 1023.561 | 2139.113 | 0.145 | 1.216 |
| WSL bgfx OpenGL | 8.033 | 78.409 | 323.476 | 2.454 | 1.089 |
| Android Vulkan | 109.898 | 2102.170 | 4079.040 | 0.176 | 0.994 |
| Android GLES | 106.246 | 406.067 | 4916.300 | 8.560 | 3.277 |

Mọi backend: upload peak **4096 bytes/frame** dưới cap **8192**, CPU reservation
peak **66096 bytes**, GPU estimate peak **56188 bytes**, queue peak **14**, dedup
hits **2**, eviction **3**, ảnh stream/sync bằng nhau. Tracked Njin heap peak:
Windows Vulkan 347316 bytes; Windows GL/D3D12 344237; WSL Vulkan 755691, GL 1160648;
Android 341740. Đây không phải RSS hoặc tổng device allocation.

`run=0` Windows OpenGL có worst frame 17369 ms; D3D12/Android còn stall nhiều giây
ngay cả `run=1`. Budget streaming không chữa shader compilation, initialization,
screenshot/readback hoặc driver stalls. GLES có một unit vượt upload time cap mềm.
Không có bằng chứng để tuyên bố mọi frame không block hoặc streaming nhanh hơn
sync trong benchmark tương đương. Raw counters trong log/metrics giữ cả hai lượt;
disk cold cache chưa được đo có kiểm soát, warm load chỉ là lần lặp OS cache.

ThreadSanitizer: đã compile scheduler, context/resource/texture/log code, glTF/
animation loader và fixture bằng GCC TSan, link archive/executable riêng rồi chạy
engine Vulkan WSL hai context. Dependency bgfx/SDL/raylib/Mesa không được instrument
toàn bộ. Lượt không suppression báo race ở Mesa Lavapipe mutex destruction; lượt
chỉ suppress Lavapipe còn báo trong LLVM StringMap. Giữ các log gốc tại
`build/streaming-tsan-runtime.log` và `build/streaming-tsan-filtered.log`.
Suppress đúng hai DSO `libvulkan_lvp.so`/`libLLVM.so.21.1` thì fixture pass hai lần,
không có report TSan trong phần Njin được instrument
(`build/streaming-tsan-njin.log`). Không kết luận driver hoặc toàn engine race-free.
Probe và scripts nằm trong build scratch, không thêm sanitizer dependency vào engine.

`graphify update .` đã cập nhật AST graph; extractor vẫn báo các lỗi cú pháp ở file
khác như `njin_log.h`. Doxygen thật đã chạy: không có warning ở `njin_stream.h`,
nhưng docs hiện có còn warning ở các public headers khác; không gọi toàn docs là
warning-free. `git diff --check` pass và các file sửa dùng LF. Không stage/commit.

## File của thay đổi

- API mới: `src/engine/api/njin_stream.h`; include từ `njin.h`.
- Scheduler mới: `src/engine/runtime/njin_stream.cpp`, `njin_stream_internal.h`.
- Context/frame/lifecycle: `njin_ctx_impl.{h,cpp}`, `njin.cpp`, `njin_ctx.cpp`,
  `feature_3d.cpp`; CPU loader staging trong `njin_model_portable.cpp`.
- Adapter: `njin_graphics_device.h`, `njin_bgfx.cpp`; sửa lời gọi material legacy
  trong `njin_graphics_device_gl.cpp`. Worker logging: `njin_log.cpp`, `njin_log_impl.h`.
- Fixture: `src/engine/tests/streaming.cpp`, target/CTest trong `CMakeLists.txt`.
  Android: wrapper `examples/android_renderer/streaming.cpp`, CMakeLists/main và runner.
- Tài liệu: file này, `renderer-reuse.md`, `THIRD_PARTY_NOTICES.md`, `CHANGELOG.md`.
  Build logs, metrics, sanitizer scripts và fixture BMP được tái tạo trong build scratch.
