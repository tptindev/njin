# Kết quả benchmark renderer — 2026-11-23

Đã triển khai và chạy harness qua main loop/API bình thường của Njin.
**126 lượt performance × 600 frame = 75.600 mẫu đo**, thêm 180 warm-up và
32 drain frame mỗi lượt; 42 lượt correctness riêng. Cả hai bộ có source/dependency
snapshot ổn định trong lúc chạy. Timestamp GPU ghép đúng submission, coverage
**100%**; 66 cặp capture A/B có **0 pixel khác biệt**, cả trước tolerance 4/255.
Không có GPU readback/capture/validation trong lượt performance.

Máy: Windows 11 build 26300, i5-12500H, RAM 34.049.417.216 byte,
NVIDIA RTX 3050 Laptop 4 GiB; driver Windows 32.0.16.1062 (SMI 610.62).
GCC 15.2.0, Release `-O3 -DNDEBUG`, SDL3 3.4.18/bgfx pinned tại revision trong
manifest. 640×480 thực, target 640×480, scale 1, VSync/cap/MSAA/AA/HDR/LOD tắt.
Chỉ workload shadows bật directional shadow 512. Không thay power/clock.
Nhiệt/background không khóa; snapshot SMI giữa suite là 51°C/P5/9W, không phải
log nhiệt liên tục. Không coi OS/driver cache là disk cold được kiểm soát.

## Kết quả chính

A = CPU frustum, B = đường GPU frustum/Hi-Z + pool/indirect hiện có.
Mỗi cấu hình có **3 process độc lập**, thứ tự AB/BA luân phiên. Các số dưới là
median của median từng process; timing ms, bộ nhớ MiB. Không so cube với sphere
để suy ra tăng tốc; mỗi hàng so cùng mesh/scene/camera/chất lượng.

| Backend/workload | CPU render A→B | GPU A→B | Frame wall A→B | RSS A→B | GPU driver usage A→B |
|---|---:|---:|---:|---:|---:|
| Vulkan, 16 cube | 0.0605→0.0680 | 0.246→0.347 | 0.567→0.596 | 342.8→342.8 | 184.2→189.3 |
| Vulkan, 4096 cube bị che | 1.966→2.063 | 0.493→0.524 | 2.782→3.162 | 343.8→349.4 | 185.9→190.5 |
| Vulkan, 128 nhóm panel | 3.355→3.540 | 1.153→1.971 | 5.245→6.422 | 346.6→351.8 | 239.8→239.0 |
| bgfx OpenGL, 128 nhóm panel | 2.380→2.241 | 3.783→5.435 | 5.390→7.011 | 158.8→163.0 | N/A |
| D3D12, 128 nhóm panel | 2.054→3.039 | 5.264→3.218 | 48.817→7.814 | 360.1→373.2 | 35.2→57.3 |
| Vulkan, 4096 sphere phần lớn thấy | 2.654→2.506 | 6.420→6.515 | 6.457→6.558 | 340.4→341.8 | 185.9→190.2 |
| Vulkan, 4096 sphere bị che | 2.390→2.128 | 6.321→1.392 | 6.368→3.286 | 341.1→341.8 | 186.0→190.6 |

Sphere dùng primitive sẵn có: 768 triangle/instance, so với cube 12. Reference
bgfx báo 3.145.732 triangle (gồm các quad bổ sung), không phải visible triangles
thực sau indirect. Diagnostic ngoài performance: visible giữ 4096/4096; occluded
giữ 733/4097 phần tử (gồm tường). Không lấy GPU visible count mỗi frame đo.

**Lợi ích đã đo:** sphere bị che trên Vulkan giảm GPU **78,0%**, frame wall
**48,4%**. Median GPU của ba lượt A nằm 6.3158–6.3323 ms, B 1.3908–1.3920 ms;
wall A 6.3545–6.3705, B 2.9375–3.3250 ms. GPU p95/p99 A 6.693/6.706 ms,
B 1.432/1.810 ms; wall p95/p99 A 7.070/7.936 ms, B 4.518/5.030 ms.
Lượt tắt toàn bộ instance culling cũng đã chạy: occluded sphere GPU 6.328 ms,
wall 6.351 ms, không có lợi so với CPU frustum vì cảnh vẫn trong frustum.

**Overhead đã đo:** Vulkan 16 cube tăng GPU **40,7%** (khoảng 0,100 ms).
Sphere phần lớn thấy tăng GPU khoảng **1,5%** và wall **1,6%**, nên không nên
bật đường này chỉ dựa vào số instance. Cube bị che vẫn không cho lợi ích ở đây;
chi phí hình học nhỏ và CPU/driver chiếm phần đáng kể. Các CPU median cube
Vulkan dao động lớn giữa process, nên không coi chênh lệch CPU nhỏ là chắc chắn.

**D3D12 nhiều nhóm là trường hợp riêng đáng điều tra:** wall giảm **84,0%**,
A 47.8485–49.6290 ms, B 5.1310–7.8955 ms. Wall p95/p99 A 230.502/379.258 ms,
B 14.080/21.662 ms; vẫn giữ cả worst frame **678,078 ms** của reference.
`present_ms`/end_frame median A 46,486 ms, B 4,443 ms, trong khi backend CPU
submission A khoảng 0,655 ms và GPU chỉ khoảng 5,264 ms. Đây là bằng chứng stall
ở phần end_frame/driver/present/fence chưa phân rã, không phải 48 ms GPU.
4096/4096 instance còn thấy, nên không quy lợi ích này cho việc bỏ vật bị che.
Đường GPU cũng tăng RSS khoảng 13,1 MiB và driver-reported GPU usage 22,1 MiB.

Pool culling không cấp buffer mới trong **bất kỳ khoảng đo nào** của cả hai bộ.
Pool payload scene 128 nhóm là 792.576 byte, nhỏ hơn nhiều chênh lệch driver
memory D3D12; không dùng payload estimate để thay số đo driver. Allocation C++
128 nhóm vẫn 1669→1671/frame, so với 27→29 ở scene occluded một nhóm: đường GPU
không loại bỏ CPU packet/allocation work. Chưa có call-site profile để kết luận
chính xác chỗ cấp phát nào cần sửa.

## First-use và giới hạn

Context create trong bộ chính khoảng 0,462–1,112 s, nhưng first render có thể
đắt hơn rất nhiều. `vulkan-occluded-gpu-1.csv`, tick 0: wall **21.907,428 ms**,
CPU render 1,466 ms, end_frame 21.905,889 ms, bgfx backend CPU **21.888,365 ms**,
GPU **3,400 ms**. Đây là first-use CPU/backend cost, không phải 21,9 s GPU.
Nó nằm trong warm-up và vẫn được giữ trong raw/analysis. Cần trace sâu hơn trước
khi quy cho pipeline/shader creation hoặc đưa ra bản sửa cache.

Không tối ưu thuật toán renderer trong nhiệm vụ này; đã xác minh hiệu quả và
chi phí của các đường hiện có. Không tuyên bố production-ready từ benchmark ngắn.
Chưa benchmark atmosphere LUT/update/steady state, lightmap bake/load/runtime,
streaming cold/warm/residency, nhiều local lights, Android hoặc Linux GPU vật lý.
ADB xác nhận Pixel 6a kết nối; không suy ra kết quả hiệu năng Android từ desktop.
Không chạy/so renderer sdl3-proj, macOS hoặc Web.

Ưu tiên tiếp theo theo bằng chứng:

1. Phân rã first-use Vulkan CPU backend ~20 s và stall D3D12 nhiều nhóm bằng
   lượt diagnostic riêng; giữ cùng workload rồi đo lại nếu có bản sửa.
2. Profile CPU packet/instance preparation và allocations của nhiều nhóm,
   dùng profiler hiện có trước khi sửa cache/pooling.
3. Mở rộng heavy-geometry A/B sang bgfx OpenGL/D3D12 và scene nhiều đèn/receiver;
   sau đó mới xét policy bật GPU culling theo nội dung.
4. Thêm atmosphere/lightmap/streaming vào cùng schema, giữ bake/cold/first-use
   riêng steady state; không tạo hàng số liệu giả để đủ bảng.

## Artifact và kiểm chứng

- `build-renderer-bench/results-final/`: 108 performance, 36 correctness;
  `manifest.json`, `analysis.json`, `summary.csv`, `report.md`, CSV từng frame,
  stdout/stderr, 108 PNG và 54 heatmap. Có binary/harness-source đúng SHA đã đo.
- `build-renderer-bench/results-sphere/`: 18 performance, 6 correctness,
  18 PNG/12 heatmap và cùng loại metadata/raw. Variant off/CPU/GPU, chỉ Vulkan.
- `build-renderer-bench/regression.log`: 8/8 instancing/occlusion trên ba backend
  và Vulkan validation. `validation-regression-observable.log`: 2/2 chạy thêm với
  kênh stdout trực tiếp của layer; log xác nhận CURRENT-VALIDATION-ENABLED.
- `validation-observable.log`: control GPU occlusion với logging độc lập, không
  có Validation Error/VUID. Source/runtime renderer giữ nguyên khi thêm mesh CLI.
- `results/` và `pilot-*`: giữ các lần phát triển đã dừng/sửa readback; không gộp
  chúng vào kết quả cuối, không xóa hoặc biến failure thành pass.

Toàn bộ median/p95/p99 CPU/GPU của từng cấu hình và từng lượt, khoảng dao động,
startup, spikes, GPU coverage và counters nằm trong analysis/report của mỗi bộ.
Lệnh build/run/analyze và định nghĩa chỉ số: [renderer-benchmark.md](renderer-benchmark.md).
