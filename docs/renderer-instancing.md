# Portable GPU instancing

The SDL3/bgfx renderer implements the existing `instance_buffer_*`,
`instancing_available` and `draw_instanced3d` APIs for models, shared animated poses and the
cube, sphere and plane primitives. Legacy raylib instancing remains independent.

The existing 4/8/12/16-float public record format is preserved: translation and
uniform scale; optional color; optional Euler rotation; optional per-axis scale.
Zero scale fields mean 1, as before. `first` and `count` select the submitted
range. Non-finite data, singular transforms and invalid colors fail explicitly.

Njin converts records into an immutable matrix/color snapshot. Each model
material submits one actual bgfx instance draw per orientation group, rather
than one draw per object. Positive and negative determinants use separate
groups to preserve front-face culling. Inverse-transpose normals and tangent
handedness account for nonuniform/mirrored scales. Instance color is decoded
separately for linear PBR multiplication.

The device uploads each snapshot once per frame and reuses that buffer across
shadow atlas views and the forward pass. The cache retains the snapshot's
ownership to prevent recycled CPU addresses from aliasing another upload.
Buffers are released after submission using bgfx's deferred destruction.
Gameplay and ECS never see native handles. The public CPU instance buffer can
be released after a portable draw has captured its snapshot.

The pinned bgfx revision uses `TEXCOORD31..27` for `i_data0..4`, matching its
05-instancing example and D3D input-layout configuration. Compatibility must be
checked against that revision when upgrading bgfx. Device availability is
checked against its actual instance-data and vertex-attribute limits.

`renderer_resource_stats` includes cumulative `instanced_draws`,
`instances_submitted` and `instance_uploads`. `njin_instancing_test` checks 1,001
instances produce one submission per orientation group, range selection,
color/rotation/scaling, snapshot lifetime, pixels, cleanup and two engine
contexts. The shadow regression alternates static and instanced casters and
checks the same sun/point/spot/alpha-mask/HDR controls.

```powershell
cmake --build build --target njin_instancing_test njin_shadow_test
ctest --test-dir build -R "instancing_|shadows_" --output-on-failure
```

Windows runtime verified: bgfx OpenGL, Vulkan, D3D12 and Vulkan validation.
Other platforms require their own runtime evidence.

The descriptor groups instances by projected size and submits their selected
LOD through the same mesh and pose path as individual model draws. Camera
frustum culling uses conservative spheres. Instances outside the camera use
shadow-only groups at base detail so they can still cast shadows into view.
Animated models require a positive descriptor radius to enable frustum
culling. The radius should bound the entire pose. Shared poses reuse the
deformed geometry across orientation groups.

The LOD fixture checks a large and small visible instance plus an off-camera
caster, then repeats the check with a shared animated pose. It inspects the
mesh or deformed indices submitted at each level and verifies the number of
instances that reach the camera pass.
It passed on Windows Vulkan, bgfx OpenGL, D3D12 and Vulkan validation, on Linux
Vulkan and OpenGL, and on Android Vulkan and OpenGL ES.

Transparent groups require OIT. The upload limit is one million records.
Custom instanced shaders,
cylinder and lower-detail primitive variants remain pending.


## Hi-Z trên GPU được chưng cất từ fl

Bật `render3d_options::occlusion` và dùng overload `draw_instanced3d` với
`instanced3d_desc::cull=true` cho instance; model thường đục dùng shader tích hợp
cũng được lọc. Model đục không instanced làm occluder. Bounds lấy từ mesh/pose
hiện tại, bao kín hình học: depth gần nhất của bound nằm trước raster depth
của chính model nên không tự loại nó. Instance dùng radius theo hợp đồng descriptor.
Depth hiện tại được đóng RGBA8, giảm max vào mip R32F; compute thử hộp bao bảo
thủ, compact đủ matrix/color và ghi số lượng cho indirect draw của bgfx.
Không có readback CPU trong đường vẽ thông thường, không có lịch sử nên vật
vừa lộ ra không đợi frame sau. Hỗ trợ render target và đổi kích thước.

`renderer_gpu_occlusion_available` kiểm tra compute, indirect và R32F image
read/write; khi bật với ứng viên mà thiếu khả năng này, engine báo lỗi rõ.
`renderer_occlusion_readback` là chẩn đoán đồng bộ đầu vào/còn thấy, chỉ dùng
khi kiểm chứng vì phải chờ GPU. Stats `occlusion_builds`,
`occlusion_dispatches`, `occlusion_indirect_draws` là số tích lũy;
dispatches đếm lệnh compute thực, gồm pyramid và caster culling.
`occlusion_buffer_allocations` đếm buffer tạo mới, `occlusion_pool_slots` và
`occlusion_pool_bytes` là slot/byte yêu cầu hiện được giữ (không phải VRAM vật lý).
`renderer_shadow_culling_readback` đọc riêng tổng input/visible của shadow views.
Các số readback cộng theo job/view/LOD bucket, không phải số object duy nhất.
`instances_submitted` vẫn đếm đầu vào lệnh vẽ, không phải số sống sau culling.
`render_info::instances3d_culled` chỉ đếm phần CPU loại; với nhóm chọn trên GPU,
dùng readback chẩn đoán để kiểm chứng số còn thấy, không suy từ counter CPU.

Pool dùng dynamic source buffer cập nhật CPU, compact/count/indirect giữ qua
frame. Mỗi job trong cùng frame có slot riêng; capacity tăng theo lũy thừa hai,
slot không dùng 120 frame được hủy bằng deferred destruction của bgfx. Không
ghi đè slot của camera trước trong cùng frame. Depth target cache theo kích
thước, thu hồi entry không dùng quá 120 frame ở lượt chuẩn bị Hi-Z kế tiếp;
đổi camera/cut không dùng history.

Nhóm cùng view chia sẻ bốn view reset/cull/finalize/render. Nhóm tối đa 64 phần
tử gộp reset/compaction/indirect vào một workgroup và một dispatch; nhóm lớn
dùng ba dispatch, tránh giả định có barrier giữa workgroup. Pyramid vẫn một
dispatch/view mỗi mip; giới hạn tổng view vẫn báo lỗi rõ.

Caster dùng bounds pose hiện tại và frustum của từng đèn/cascade/cube face
trước atlas tiling, không dùng camera Hi-Z để loại caster. Camera frustum chỉ
phân loại LOD: vật trong camera dùng LOD camera, vật ngoài camera dùng base,
giống bước chuẩn bị CPU cũ; mọi bucket giữ snapshot nguồn đầy đủ. World-special
geometry giữ đường shadow cũ vì vertex displacement cần bound riêng.

Khi bật occlusion, nhóm descriptor đục không OIT/SSR chuyển frustum và LOD sang
GPU. Mỗi orientation/LOD bucket giữ snapshot gốc; GPU dùng đúng distance,
ortho half-height/tan(fovy/2), radius nhân max scale, quality.lod_scale, dấu
so sánh và bước threshold giảm nửa của `model_lod_level`. CPU vẫn chuẩn bị
orientation và shared pose. OIT/SSR giữ lựa chọn CPU hiện có để bảo toàn snapshot
auxiliary; transparent/custom/world-special không dùng camera Hi-Z. Model thường
vẫn chọn LOD bằng CPU. Normal/velocity không dùng camera occlusion result.
Camera sphere/frustum dùng cùng sáu plane chuẩn hóa và bán kính nhân max scale
như CPU. Fixture có authored LOD quad → triangle và receiver có ánh sáng để
phát hiện cả lựa chọn mesh camera lẫn bóng LOD sai silhouette.

Đây là culling cùng frame, không triển khai visibility history hai pha. Vì thế
không cần invalidation history khi cut/disocclusion/resize/nhiều camera.

Thiết kế đối chiếu fl và bgfx example 37; không thay SDL3/bgfx bằng driver fl.
Revision, SHA-256 và phạm vi dùng lại nằm trong `THIRD_PARTY_NOTICES.md`.

```powershell
cmake --build build --target njin_occlusion_test
ctest --test-dir build -R '^occlusion_' --output-on-failure
```

Fixture kiểm tra số instance thực trên GPU, các cặp ảnh bật/tắt, disocclusion,
đổi camera, resize render target, perspective, scale âm và caster ngoài camera.
Camera mask được loại khỏi occluder vì depth shadow không khoét theo camera;
fixture xác nhận vật nhìn qua lỗ vẫn được giữ.
Kết quả bản chưng cất ban đầu: Windows Vulkan/bgfx OpenGL/D3D12 đã runtime đạt: 12 đầu vào còn 1 khi bị tường
che, trở về 12 ngay khi bỏ tường; cả năm cặp ảnh có 0 pixel lệch theo ngưỡng test.
Kết quả các nền tảng khác được bổ sung sau khi chạy, không suy ra từ bgfx.


Kiểm chứng bổ sung của đợt chưng cất:

- Windows: full build GCC/MinGW đạt; Hi-Z Vulkan/bgfx OpenGL/D3D12 và Vulkan
  validation đạt. Hồi quy terrain relief/shadows/instancing/LOD: 18/18 đạt,
  bao gồm legacy OpenGL và các fixture validation đã đăng ký.
- Linux WSL: Vulkan Lavapipe/OpenGL llvmpipe và Vulkan validation đạt 3/3.
  Đây là software drivers, chưa phải GPU Linux vật lý.
- Android Pixel 6a: NDK build APK đạt (SPIR-V và ESSL compute đã compile).
  Vulkan và GLES đều trả capability không đủ; native fixture FAIL 1 trước khi
  thực hiện culling, controller_passed=False. Không ghi nhận runtime thành công.
  Không tự fallback sang đường CPU. Đường vẽ mặc định không bật occlusion nên
  không đòi các capability mới này.

Suite Android: `--suite occlusion`; các capture có prefix `occlusion-<backend>-`.
Shader compute mới đi qua shader asset pipeline hiện có, không dùng đường dẫn
máy phát triển khi chạy game. Log chi tiết đợt này nằm tại
`build/renderer-distill-*` (không đưa artifact build vào source control).

## Kiểm chứng bản mở rộng pool/model/caster/GPU LOD — 2026-11-11

Build Windows GCC/MinGW Debug đạt. Các hồi quy instancing/shadows/animation/
LOD/SSR/temporal: 25/25 đạt (gồm legacy OpenGL). Fixture mới `occlusion_*` và
`shadow_gpu_*`: 8/8 đạt trên Vulkan, bgfx OpenGL, D3D12 và Vulkan validation.
Log cuối có authored LOD: `build/culling-lod-parity-windows-tests.log`;
guard capability cuối build và validation lại ở `build/culling-final-capability-*`;
hồi quy:
`build/culling-windows-tests.log` (lượt đầu có lỗi sampler, đã sửa và chạy lại).

Fixture chạy hai context; 11 cặp scene cộng camera phụ, đối chiếu 12 cặp ảnh
mỗi context. Tất cả có 0 pixel lệch theo ngưỡng test. Có camera cut/disocclusion,
target khác kích thước trong cùng frame, alpha mask/dissolve/camera mask,
scale âm/nonuniform, shared animated pose và chuyển LOD. Có cả job nhỏ một
workgroup và job 129 instance dùng ba dispatch. GPU giữ 2/15 bản ghi camera
khi bị tường che, shadow giữ 15/15; multi-workgroup giữ 43/130 bản ghi (gồm
tường). Hai camera với ba LOD giữ đúng 6/18 bản ghi bucket. Allocation pool
ổn định bằng 0/frame sau warmup; slot và byte về 0 sau idle 125 frame.

Fixture shadow bật `--gpu-culling` chạy lại sun, point/spot, sáu cube face,
cascade, mask, mirrored caster, shadow-only, HDR và ảnh ổn định dưới dịch camera
nhỏ bằng các pixel assertion hiện có. Camera Hi-Z không loại caster.
Đối chứng cuối đặt caster tại (-8,0,8), ngoài cả camera đã resize, để bóng chiếu
vào receiver tại gốc. GPU camera giữ 1/2 bản ghi, light giữ 1/1 caster (receiver
không cast shadow); pixel assertion xác nhận vùng bóng còn tối và vùng ngoài
bóng còn sáng. Sau bổ sung này, shadow GPU Windows đạt 4/4 tại
`build/culling-offcamera-final-windows-tests.log`, Linux đạt 3/3 tại
`build/culling-linux-offcamera-final-tests.log`.

Linux WSL: Lavapipe Vulkan, llvmpipe OpenGL và Vulkan validation đạt cả occlusion
3/3 và shadow GPU 3/3. Các hồi quy LOD/instancing 5/5 đạt. Đây là software
drivers. Log cuối có authored LOD: `build/culling-linux-lod-parity-tests.log`
(6/6). Guard capability cuối build và Vulkan validation lại đạt 1/1 tại
`build/culling-linux-capability-*`.
Lượt đầu Vulkan phát hiện sampler pyramid chưa bind khi shadow chạy trước Hi-Z.
Shader frustum-only hiện có nhánh tường minh và bind depth trung tính hợp lệ;
không dùng camera depth để loại caster. Validation sau sửa sạch VUID.

Benchmark cùng scene 4.096 cube, một tường và shadow; không bật validation,
không screenshot/readback trong vùng đo. Sáu block tắt/bật luân phiên, mỗi block
240 frame, bỏ 80 frame warmup và frame cuối dùng readback, còn 159 sample.
Bảng là median của ba block cho mỗi chế độ, đơn vị ms; CPU frame từ
`bgfx::Stats::cpuTimeFrame`, GPU từ timestamp bgfx. Đây là build Debug Windows,
Vulkan chọn NVIDIA vendor/device `10de:25a2`, không phải kết luận hiệu năng Release.

| Backend | CPU frame tắt → bật | GPU frame tắt → bật | CPU chuẩn bị/submit tắt → bật |
|---|---:|---:|---:|
| Vulkan | 10.763 → 10.753 | 2.666 → 2.470 | 8.866 → 8.212 |
| bgfx OpenGL | 8.750 → 8.967 | 1.195 → 1.502 | 6.599 → 6.239 |
| D3D12 | 14.611 → 12.801 | 1.177 → 1.411 | 8.992 → 8.141 |

Mỗi frame bật có 18 compute dispatch, pool cấp mới 0 buffer trong vùng đo;
camera GPU giữ 1/4.097 bản ghi (chỉ tường). Số allocation này chỉ tính buffer
culling, không phải toàn bộ allocator renderer/driver. Culling tắt có 0 dispatch
culling. GPU OpenGL/D3D12 chậm hơn trong lượt đo này, CPU Vulkan gần như không
đổi; không khẳng định tăng tốc chung. Log `build/culling-bench-final-*.log`,
số liệu block thô và median
`build/culling-benchmark.json`. Lượt đo lại thực hiện sau khi các build/test
desktop khác kết thúc; vẫn có nhiễu clock/power và chưa có workload game lớn.

Android NDK/ESSL/SPIR-V và APK cuối build đạt (`build/culling-last-android-build.log`,
và full rebuild `build/culling-lod-parity-android-build.log`).
Pixel 6a đã kết nối lại và chạy APK cuối trên cả Vulkan/GLES: cả hai từ chối
capability ngay startup với `GPU occlusion is unavailable on the requested backend`,
native `FAIL 1`, `controller_passed=False`. Chưa chạy được đường Hi-Z trên Pixel;
không phải kết quả runtime pass. Artifacts:
`build/android-device-captures/culling-final-pixel-{vulkan,gles}` và
`build/culling-final-pixel-{vulkan,gles}.log`.
Samsung SM-A505F Vulkan chạy APK cuối từ chối capability ở startup: `FAIL 1`,
controller không đạt (`culling-final-samsung-vulkan`). Lượt GLES trước bước
authored LOD khởi tạo được backend nhưng controller timeout 180 s, log Scudo ghi
`Exhausted 256M for size class 4624` trước ảnh đầu tiên; tiến trình test đã được
force-stop sau thu log. Không ghi nhận runtime Hi-Z Android thành công và không
suy nguyên nhân OOM là pool culling (frame đầu còn tắt occlusion). Artifacts tại
`build/android-device-captures/culling-final-samsung-vulkan` và
`build/android-device-captures/culling-samsung-gles`.

```powershell
cmake --build build --target njin_occlusion_test njin_shadow_test --parallel 5
ctest --test-dir build -R '^(occlusion|shadow_gpu)_' --output-on-failure
build/bin/njin_occlusion_test.exe --renderer=vulkan --benchmark
```

Giới hạn còn lại: chưa có GPU Linux vật lý; GPU LOD không áp dụng model thường
hoặc nhóm OIT/SSR; world-special/displacement cần bound riêng trước khi lọc
caster. Mỗi LOD bucket vẫn upload snapshot nguồn riêng; chưa có arena toàn scene
hoặc multi-draw xuyên material. Pool dùng high-water capacity của slot đang hoạt
động và TTL cho slot idle; không có VRAM budget toàn renderer. View limit vẫn
được kiểm tra và báo lỗi. Bounds model thường hiện quét CPU vertex snapshot,
chưa có cache bounds tĩnh. Không có CPU readback trong đường vẽ bình thường.

Graphify AST đã update; `git diff --check` và kiểm tra LF cho các file sửa đạt.
Doxygen Việt đã chạy: 303 cảnh báo tài liệu có sẵn ở renderer/debug API, không
còn cảnh báo cho `RenderGpuCull`, `gpu_cull` hay các field pool mới. Doxygen
tiếng Anh đạt với warning log rỗng. Check i18n
vẫn báo thiếu/stale/code drift ở bản tiếng Anh như quy tắc tạm thời của
CLAUDE.md; không sửa English hoặc chạy stamp.

Các file triển khai sửa trong đợt này: `njin_bgfx_occlusion.h`, `njin_bgfx.cpp`,
`cs_njin_occlusion.sc`, `njin_occlusion_portable.{h,cpp}`,
`njin_model_portable.cpp`, `njin_model.h`, `njin_shadow_portable.cpp`,
`njin_renderer.cpp`, `njin_graphics_device.h`; API `njin_renderer.h`, `njin_3d.h`;
fixture `occlusion3d.cpp`, `shadows.cpp`; `CMakeLists.txt`. Tài liệu cập nhật:
file này, `renderer-reuse.md`, `renderer-port-backlog.md`, `THIRD_PARTY_NOTICES.md`.
Không stage/reset/stash/commit, không nhập asset mới hoặc mã bên thứ ba.

## Android occlusion compatibility — 2026-11-14

Đã chẩn đoán trực tiếp Pixel 6a `24281JEGR05411`, Mali-G78, Android 16
`BP3A.250905.014`. Vulkan API 1.3.283, driver `0x0cc00000`:
`multiDrawIndirect=0`, `maxDrawIndirectCount=1`,
`drawIndirectFirstInstance=1`. R32F optimal features `0xdc83` có sampled/storage;
compute workgroup tối đa 512 invocation, shared memory 32768 byte. Vì bgfx ghim
capability indirect bằng cả multi-draw và first-instance, nó từ chối indirect
drawCount=1 dù lệnh này hợp lệ theo Vulkan. Đây không phải thiếu compute/R32F.

GLES thực tế là 3.2, GLSL ES 3.20, driver
`v1.r51p0-00eac0.20897de31e8026c4063960e040e2e9ba`. Có 512 compute invocation,
32768 byte shared memory, 35 compute storage blocks và 8 compute image uniforms.
bgfx mặc định build baseline ES 3.0 và kiểm tra compute bằng macro baseline,
bỏ qua hỗ trợ runtime. Tên renderer bgfx vẫn ghi `OpenGL ES 3.0`: đó là baseline
được build, không phải chuỗi phiên bản thực tế của driver.

`cmake/njin_bgfx_occlusion_fix.cmake` tái tạo bản vá tối thiểu ở revision
`b2513038f4b753ebeced40821c193978daa0492f`, từ chối revision/anchor khác:

- Vulkan chia ordinary indirect submission thành drawCount=1 khi thiếu
  multi-draw; indirect-count vẫn yêu cầu multi-draw. Njin tiếp tục dùng bgfx
  submit, không gửi native command hoặc bỏ capability check.
- GLES tái dùng `m_essl310Support` và core entry points đã import để kiểm tra
  compute/indirect; đường dispatch/barrier cũng dùng capability runtime.
  Baseline ES 3.0 được giữ. Máy thực sự không hỗ trợ vẫn bị từ chối rõ ràng.
- Giữ nguyên Hi-Z R32F max-depth và shader compaction. Njin yêu cầu R32F
  sampled + image read/write, ít nhất 5 compute bindings/samplers, 5 instance
  slots và 11 vertex attributes. Không yêu cầu bit `BGFX_CAPS_IMAGE_RW` chung:
  GLES báo bit chung bằng 0 nhưng riêng R32F read/write đều hợp lệ.

`renderer_gpu_occlusion_diagnostics` truy vấn capability/giới hạn/đường chọn;
startup log còn ghi GPU/driver và thông tin native Vulkan/GLES. Pixel dùng
`compute-r32f-indirect`, Vulkan báo `submission=split-single-indirect`.
Không có readback mới trong vòng lặp vẽ thường. Đồng bộ chỉ xảy ra ở API debug
đọc counters và screenshot của fixture như trước.

Android NDK 29 arm64/API 26, shader ESSL/SPIR-V và APK đã build đạt. Lượt cuối
suite occlusion trên Vulkan và GLES đều có cả `last_result=PASS 0` và
`controller_passed=True` trong `build/occlusion-android-final-{vulkan,gles}/status.txt`.
Mỗi backend chạy hai context, 22 stage và view phụ: occluded/disocclusion,
camera/target size, mirrored scale, caster ngoài camera, mask/dissolve,
animated/authored LOD, nhiều camera, 129 instance qua nhiều workgroup và pool TTL.
Tất cả 24 so sánh A/B mỗi backend (12 mỗi context) có 0 pixel khác biệt.
Đã xem ảnh đối chứng; camera mask không che nhầm vật thấy qua lỗ.

GPU counters thực từ `occlusion-{vulkan,gles}-checks.txt`, giống nhau ở hai context
và hai backend:

| Control | Input | Visible |
|---|---:|---:|
| Bị tường che, stage 1 | 15 | 2 |
| Disocclusion, stage 3 | 14 | 13 |
| Camera mask, stage 9 | 15 | 14 |
| Hai camera/LOD, stage 19 | 18 | 6 |
| Nhiều workgroup, stage 21 | 130 | 43 |

Caster stage 1 giữ 15/15, độc lập camera culling. Stage 21 có 18 dispatch,
0 buffer allocation ở frame đo. Đây là kiểm chứng chức năng, không phải đo
tăng tốc. Artifact ảnh tổng hợp: `build/occlusion-android-comparison.png`.

Windows: 12/12 instancing/occlusion/shadow_gpu trên Vulkan, bgfx OpenGL,
D3D12 và Vulkan validation (`build/occlusion-compat-windows-tests.log`). Sau khi
thêm tệp ghi counters cho fixture, occlusion đạt lại 4/4
(`build/occlusion-evidence-windows-tests.log`).
Legacy OpenGL build đạt; shadows/materials/sky đạt 3 ca, model thất bại ở
`image conversion to RGBA8 failed` khi raylib không đổi được compressed image.
Đường giải mã ảnh này không được sửa trong đợt occlusion; không ghi nhận legacy
4/4 pass (`build/occlusion-compat-legacy-tests.log`). Linux/Apple/Web không chạy
lại trong đợt này. Chưa kiểm thử trên thiết bị Android khác hoặc GPU GLES 3.0-only.

Android regressions cuối: instancing, LOD và shadows đều đạt native/controller
trên Vulkan và GLES (6/6); shadows có `lifecycle=True`, qua background/resume và
xoay Activity. Artifacts: `build/occlusion-instancing-final-{vulkan,gles}` và
`build/occlusion-regression-{lod,shadows}-{vulkan,gles}`. Fixture instancing ban
đầu lấy tọa độ 640x480 trên screenshot fullscreen 2400x1080; nay vẽ/chụp target
640x480 cố định, giữ nguyên kiểm tra 1001 instance, range/color/rotation,
mirrored culling, lifetime và cleanup. Bản fixture sửa đạt lại desktop 4/4
(`build/occlusion-instancing-windows-tests.log`). Không chuyển unsupported thành pass.

File sửa trong đợt này: `cmake/njin_bgfx{,_occlusion_fix}.cmake`;
`src/engine/api/njin_renderer.h`; runtime `njin_bgfx.cpp`,
`njin_bgfx_occlusion.h`, `njin_graphics_device.h`, `njin_renderer.cpp`,
`njin_occlusion_portable.cpp`, `njin_raytrace_vk.{h,cpp}`;
fixtures `occlusion3d.cpp`, `instancing3d.cpp`; Android wrapper `instancing.cpp`,
`main.cpp`, `CMakeLists.txt`, `run_device.py`, `README.md`; tài liệu này,
`renderer-reuse.md` và `THIRD_PARTY_NOTICES.md`.
Patch idempotence (cả byte/mtime), revision refusal, Python compile và LF/diff
checks đạt. Doxygen Việt có 303 cảnh báo cũ, không cảnh báo API diagnostics mới;
không sửa English hoặc chạy stamp. Giữ toàn bộ staged/unstaged work của người
dùng; không reset/stash/stage/commit.

Tổng hợp status, counters và checksum APK cuối:
`build/occlusion-android-verification.json`. Graphify AST đã update (còn 8 file
syntax error/partial extraction đã có, gồm `njin_log.h`); `i18n.py check` báo
86 vấn đề missing/stale/links của English, giữ nguyên theo quy tắc tạm thời.
