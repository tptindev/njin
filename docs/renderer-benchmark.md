# Benchmark renderer tái tạo

Harness `src/tools/renderer_bench/` dùng `create/mod_register/run`, ECS render
phase, `begin_3d/draw_instanced3d/end_3d` và render texture của Njin. Không có
render loop hoặc profiler GPU thứ hai. Instrumentation đọc `mem_info`, timing
`frame_state`, `renderer_resource_stats` và `bgfx::getStats`. Adapter chỉ thêm
ID submission cuối của `bgfx::frame()` cho việc ghép timestamp trễ.

## Audit và phạm vi

Audit lại AGENTS.md, CLAUDE.md, renderer-reuse, checkpoint trong build và mã hiện
tại ngày 2026-11-23. Các checkpoint cũ không được coi là kết quả benchmark mới.

| Phần | Mã/API đã có | Kiểm chứng trước nhiệm vụ | Benchmark mới |
|---|---|---|---|
| Culling | CPU frustum; GPU frustum/Hi-Z/LOD; pool; shadow view độc lập | Fixture instancing/occlusion/shadow, báo cáo desktop/Android | Sáu workload cube/panel; bổ sung sphere với cùng scene/chất lượng trong từng A/B |
| Atmosphere | LUT, MS/AP, IBL worker và CPU counters | renderer-atmosphere.md | Chưa đo trong suite này |
| Lightmap | UV/bake/load/model/ECS | renderer-lightmaps.md | Chưa đo bake/memory/runtime trong suite này |
| Streaming/residency | Scheduler, uploads, cache/eviction/cancellation, mip/LOD/VT | renderer-streaming.md, renderer-residency.md | Chưa đo cold/warm trong suite này |

Các trạng thái trong cột kiểm chứng trước là bằng chứng lịch sử được tìm thấy,
không phải chạy lại. Kết quả mới và các backend chạy thật ghi trong báo cáo kết quả.
Đã xác nhận ADB thấy Pixel 6a; suite hiệu năng này chưa port/chạy Android.
Không so renderer sdl3-proj với Njin vì chưa dựng cặp workload tương đương.
Không xếp hạng renderer; không suy diễn Linux GPU vật lý từ WSL software driver.

## Dựng và chạy

PowerShell, từ `D:/projects/njin`; helper dùng các dependency pinned đã có trong
`build/_deps`, không thay build Debug. Dùng configure Njin bình thường trước nếu
máy chưa có chúng. GCC MinGW hiện tại cần tắt PCH để tránh lỗi mapping PCH.

```powershell
python src/tools/renderer_bench/configure.py
cmake --build build-renderer-bench --target njin_renderer_bench njin_occlusion_test njin_instancing_test --parallel 8
ctest --test-dir build-renderer-bench -R '^(occlusion|instancing)_(vulkan|bgfx-opengl|d3d12|vulkan_validation)$' --output-on-failure
python src/tools/renderer_bench/run.py --exe build-renderer-bench/bin/njin_renderer_bench.exe --output build-renderer-bench/repro-cube --repeats 3 --warmup 180 --frames 600
python -m pip install -r src/tools/renderer_bench/requirements.txt
python src/tools/renderer_bench/analyze.py build-renderer-bench/repro-cube
python src/tools/renderer_bench/run.py --exe build-renderer-bench/bin/njin_renderer_bench.exe --output build-renderer-bench/repro-sphere --backends vulkan --workloads visible occluded --variants off cpu gpu --mesh sphere --repeats 3 --warmup 180 --frames 600
python src/tools/renderer_bench/analyze.py build-renderer-bench/repro-sphere
```

Chọn output mới mỗi lần; runner từ chối ghi đè thư mục có manifest.
Bộ dữ liệu thực tế của nhiệm vụ nằm ở `build-renderer-bench/results-final` và
`build-renderer-bench/results-sphere`. Bộ cube lưu cả binary và `harness-source`
trước khi thêm lựa chọn sphere; manifest của từng bộ giữ SHA256 đúng binary đã đo.
Các thay đổi sau bộ cube chỉ nằm trong harness (mesh CLI, output LF, phân tích
warmup GPU và logging validation); không sửa đường renderer giữa hai bộ.

Validation Release cần kênh log độc lập: bgfx dùng BX_TRACE có thể bị loại khi
compile. Runner mới đặt `VK_LAYER_SETTINGS_PATH` **chỉ cho process correctness**,
trỏ tới `src/tools/renderer_bench/validation`, yêu cầu thấy thông báo layer active
và kiểm tra Validation Error/VUID. Đã đối chiếu SDK 1.4.363.0
`Config/vk_layer_settings.txt` và [tài liệu Khronos về logging](https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/docs/error_messages.md).
Không sửa cấu hình Vulkan hoặc power plan của máy.

Chạy regression với kênh log này (khôi phục biến môi trường sau đó):

```powershell
$previousLayerSettings = $env:VK_LAYER_SETTINGS_PATH
try {
  $env:VK_LAYER_SETTINGS_PATH = 'D:/projects/njin/src/tools/renderer_bench/validation'
  ctest --test-dir build-renderer-bench -R '^(occlusion|instancing)_vulkan_validation$' --output-on-failure
} finally {
  $env:VK_LAYER_SETTINGS_PATH = $previousLayerSettings
}
```

CLI chạy một lượt trực tiếp:

```powershell
build-renderer-bench/bin/njin_renderer_bench.exe --renderer=vulkan --workload=occluded --variant=gpu --width=640 --height=480 --seed=17 --warmup=180 --frames=600 --output=build-renderer-bench/single
```

`--workload=low|visible|occluded|groups|moving|shadows`;
`--variant=off|cpu|gpu`. `off` tắt culling của instance, `cpu` dùng CPU frustum,
`gpu` bật frustum/Hi-Z GPU đang có; LOD tắt ở cả ba. `run.py --variants off cpu gpu`
chạy cả ba. Không có lựa chọn giả cho pool/dispatch; các đường đó theo capability.
`--mesh=cube|sphere` chọn primitive hiện có, sphere 768 triangle/instance so với
cube 12. `groups` luôn dùng panel model riêng, từ chối sphere để không gắn nhãn sai.
Không so chất lượng cube với sphere; chỉ so các biến thể cùng mesh trong cùng bộ.
Thời lượng đo theo số frame, không theo tốc độ render; camera lấy mẫu theo frame
để A/B đi qua cùng vị trí. Mỗi lượt còn 32 frame cùng workload để thu timestamp
GPU trễ. Không gọi `bgfx::frame()` từ harness.

`low` có 16 cube; các workload khác có 4096 instance. `visible` chứa grid trong
frustum. `occluded` thêm tường che phần lớn grid. `groups` dùng 128 model/material
khác nhau, 32 instance/nhóm. `moving` chạy camera hình sin để lộ grid sau tường.
`shadows` thêm directional shadows 512 và 16 caster ngoài camera. Chưa có workload
nhiều đèn local, gameplay/physics lớn hoặc asset streaming trong lượt này.
Đây là control chi phí shadow submission/culling, chưa đại diện game nhiều đèn
và receiver phức tạp. Regression riêng kiểm tra ảnh bóng/caster ngoài camera.

## Phương pháp và ý nghĩa chỉ số

Mỗi repeat chạy process mới, đảo CPU/GPU thành AB, BA, AB. Mỗi backend có loạt
correctness/capture riêng chạy trước. Vulkan correctness yêu cầu validation;
regression fixture chạy riêng. Performance không validation, screenshot, GPU
counter readback hoặc ghi file từng frame. Không thay power plan, clock hay OS
disk cache. Cache driver/OS có thể ấm; process mới không đồng nghĩa disk cold.

640×480, target cùng kích thước; render scale 1, VSync off, cap 0, MSAA/AA/HDR/LOD
off. Metadata lưu requested/actual drawable, target, cấu hình và adapter IDs.
Log stdout/stderr giữ diagnostics/capability/driver. Manifest giữ GPU/CPU/RAM/OS/
driver/compiler, CMakeCache, SHA256 executable, revision, staged/unstaged diff
hash và hash từng source/fixture; không stage hay commit. Snapshot trước/sau
phát hiện source thay đổi trong lúc chạy. `source_stable=false` cần điều tra
trước khi dùng kết luận. Nhiệt độ không được đọc; background load không được
khóa, chỉ có CIM snapshot ngoài lượt đo. Không kết luận biến động là do nhiệt.
Một snapshot `nvidia-smi.txt` giữa suite ghi 51°C, P5, 9W; đây không phải log nhiệt
liên tục. Vulkaninfo còn ghi warning implicit OBS hook; không đổi layer toàn máy
và không tuyên bố môi trường sạch overlay. Các biến VK đang có không yêu cầu
validation; process performance không được runner thêm setting correctness.

- `cpu_render_ms`: CPU phase render toàn engine từ main loop; gồm instrumentation
  nhẹ của harness. `scene_submit_ms`: phần public scene submission; không phải GPU.
- `cpu_update_ms`: update của frame đó. `present_ms`: backend end_frame/driver/
  present và giải phóng packet; không phải chỉ thời gian chờ VSync.
- `wall_ms`: khoảng thời gian đầu callback render tới đầu callback kế tiếp,
  bao gồm submission, present và phần main loop giữa hai callback.
- `gpu_ms`: timestamp bgfx, ghép `gpuFrameNum` với chính ID trả về từ frame
  adapter. Bỏ mẫu lặp/invalid; giá trị -1 là unavailable. Không thay bằng CPU.
  `gpu_coverage` phải được xem cùng percentiles. Counter bgfx là snapshot trễ;
  không khẳng định draw/triangle của snapshot thuộc CPU tick bên cạnh.
- RAM: working set/private bytes từ Windows, bytes được allocator Njin theo dõi
  và số allocation (bao gồm engine/STL/harness, không phải toàn driver).
- GPU memory: driver-reported bgfx usage khi budget hợp lệ; không có thì unavailable.
  `texture_estimate_bytes`, `rt_estimate_bytes`, pool bytes là estimate/payload,
  không cộng lại thành bộ nhớ vật lý. Pool không gồm overhead driver.
- Draw/dispatch/buffer/texture/triangle là counters bgfx; triangle của indirect
  submission không thay cho số triangle sau GPU culling. Instance upload/submitted
  và cull allocations là cumulative Njin counters trong raw CSV.
- Upload bytes/frame hiện unavailable. Visible counts chỉ đọc ở cuối lượt
  correctness, không stall từng frame performance. CPU allocations có số đo thật;
  chưa có phân rã allocation theo call site.

Toggle `gpu` chọn cả đường GPU frustum/Hi-Z, pool và indirect submission. Vì
đường hiện có gắn với nhau, lợi ích của toggle không chứng minh lợi ích riêng
của từng thành phần. Ví dụ cảnh không occluder vẫn giữ toàn bộ instance nhưng
có thể đổi chi phí driver/indirect; phải đọc cả visible count và end_frame.

`analysis.json` giữ từng lượt median/p95/p99/max, coverage, startup/create,
warmup/first frame và spikes >2× median wall. Tổng hợp dùng median của các median
theo lượt, kèm min/max và p95/p99 từng lượt; không giấu dao động bằng pooling FPS.

## Đối chứng ảnh

Capture frame 0/30/60 sau warmup trong process riêng. So sánh CPU/GPU (hoặc off)
trên từng backend, cùng camera/seed/material. Tolerance mặc định 4/255 ở từng
kênh RGBA để dung nạp sai số raster số nhỏ, không dung nạp silhouette mất vật.
Không pixel nào được vượt tolerance để được pass. Lưu số pixel khác, cực đại,
bounding box, tile 32×32 có lỗi và heatmap khuếch đại 16×, cùng ảnh gốc.
Pillow là công cụ phân tích ảnh của host; không nhập mã vào engine.

Fixture occlusion hiện có kiểm tra thêm alpha mask, authored/animated LOD,
camera mask, nhiều view, disocclusion và caster độc lập. Ảnh benchmark không
thay thế các control đó. Khác chất lượng hoặc capture thiếu thì không gọi là
tăng tốc cùng workload. Capture không chứng minh toàn bộ camera path không lỗi.

Không dùng ngưỡng FPS cố định vào CI; ngắn hạn pass không phải production-ready.
