# Atmosphere LUT đầy đủ trên SDL3/bgfx

`draw_atmosphere3d` có Rayleigh/Mie single scattering, ozone absorption,
multiple scattering dạng closure isotropic, ground bounce, aerial perspective
RGB, sky-generated SH9/GGX và night sky có moon/stars/airglow. Không thay SDL3,
bgfx, scene/ECS hoặc procedural sky `draw_sky3d` của Njin.

## Bật dùng

```cpp
auto atmosphere = njin::atmosphere3d_at_hour(njin::atmosphere3d_physical(), hour);
njin::light3d light;
light.hdr = true;
light.tonemap = njin::tonemap3d_aces;
light.environment_visible = false;
light.ambient = {0, 0, 0, 1}; // IBL sky cung cấp indirect; không cộng ambient giả lần nữa
light.fog_density = 0;       // AP thay phần fog atmosphere, không cộng fog cũ
light.volume_density = 0;
// Giữ chính sách exposure của game; night sky vật lý cần exposure cao hơn day.
light = njin::atmosphere3d_key_light(atmosphere,
    camera.position.y - atmosphere.sea_level_m, light);
njin::light3d_set(ctx, light); // trước begin_3d, vì light được snapshot theo view
njin::begin_3d(ctx, camera);
njin::draw_atmosphere3d(ctx, atmosphere);
// draw_model/draw_cube3d/... qua API Njin bình thường
njin::end_3d(ctx);
```

`atmosphere3d_desc{}` giữ các default của phiên single-scattering trước:
MS/AP/IBL/night đều tắt. `atmosphere3d_physical()` bật chúng tường minh:
MS=1, ground albedo=0.2, AP 32 km, IBL intensity=1 và update interval=1 s,
full-moon illuminance=0.25 lux, airglow=0.002 cd/m², stars=0.2 cd/m².
Hai API không đổi environment/ambient/fog/exposure lưu trong game. IBL override
chỉ áp dụng những packet lit của view đã gọi atmosphere với `environment_lighting`.
View kế tiếp không bật tùy chọn này nhận lại environment của game.
Ambient của game vẫn là phần cộng thêm có chủ ý: đặt về zero như ví dụ nếu chỉ
dùng indirect từ atmosphere, đặc biệt để geometry tối đúng khi chuyển sang đêm.

`atmosphere3d_at_hour` tái dùng `sky3d_sun_direction` cho giờ/vĩ độ/mùa; đặt
moon đối sun và xoay sao theo giờ. Vị trí moon và phase có thể điều khiển riêng.
Đây là quy ước time-of-day cho renderer, không phải lịch thiên văn/ephemeris.

## Transport và pass

| Pass/cache | Kích thước, format | Dependency và thuật toán |
|---|---|---|
| Transmittance | 256×64 RGBA16F | 64 bước Beer–Lambert. Bán kính, density/extinction; không phụ thuộc sun/camera/g. |
| Multiple scattering | 32×32 RGBA16F | 64 hướng Fibonacci × 20 bước. Tích phân L2 và transfer F, closure `L2/(1-F)` với denominator floor 0.01. Medium/ground albedo; không phụ thuộc sun/camera/g. |
| Sky view | 192×108 RGBA16F | 48 bước, Rayleigh + Henyey–Greenstein Mie, MS source, sun/moon planet shadow, ground Lambert bounce và night floor. |
| Aerial radiance + transmittance | Hai atlas 512×32 RGBA16F | 32 azimuth × 32 nonlinear latitude × 16 distance slices bình phương; 24 bước mỗi ray, RGB radiance và RGB T riêng biệt. |
| Background | Far-plane quad | Sky LUT + một sun disk + phased moon disk + procedural stars; exposure rồi tone mapping Njin. |
| Environment snapshot sky | 192×108 RGBA16F | Sky của đúng snapshot SH worker, giữ độc lập sky camera hiện tại. |
| Environment base | 256×128 RGBA16F | Equirectangular radiance, không có sun/moon disks hoặc stars. |
| GGX environment atlas | 128×384 RGBA16F | Sáu roughness levels 128×64, importance sampling 64 hướng, wrap azimuth rõ ràng. |
| SH9 | 9×RGB float CPU | 512 hướng Fibonacci × 32 bước; worker dùng T 128×32, MS 16×16/32 hướng × 20 bước cache theo medium. |

MS là xấp xỉ isotropic all-orders của Hillaire, không phải path tracing spectral
hay một ambient multiplier đặt tên MS. MS strength nhân vào unit LUT nên đổi
strength không rebuild transmittance/MS. Ground direct bounce cũng có khi MS=0.
Sun/moon chiếu cùng lúc lên sky/AP; direct-light helper chọn một directional
light mạnh hơn vì Njin có một main directional light. Shadow policy của base
được giữ nguyên; game vẫn điều khiển light3d_set.

Transmittance dùng squared-height và signed-square-root cosine mapping riêng;
sky/AP dùng latitude phi tuyến quanh chân trời có độ võng theo độ cao. Có texel
center mapping, explicit LOD, clamp căn/phase/extinction, azimuth guard tại pole,
interpolation giữa slices và fade về zero radiance/unit T ở distance=0.
Night floor, stars, disk/radius/rotation và exposure không làm rebuild MS.

## Aerial perspective và HDR

AP lấy sampled depth của HDR scene, unproject bằng inverse view-projection và
tính khoảng cách từ camera. Sau SSR/TAA/effects/OIT, trước motion blur/bloom/
tone mapping: `color = scene * RGB_transmittance + radiance * exposure`.
Sky có depth clear được giữ nguyên để không tích phân atmosphere hai lần.
AP target theo kích thước view; target còn in-use được retire tới lúc an toàn.
Hai camera/target khác kích thước được fixture kiểm tra trong cùng frame.

Với alpha/OIT, depth opaque làm đại diện cho ray khi composite: đây là giới hạn
của screen-space atmospheric perspective cho transparency, không phải transport
theo từng lớp glass/particle hoặc volumetric fog đầy đủ. Fog density/volume density
cũ khác zero gây lỗi rõ khi bật AP, tránh cộng fog hai lần. Không tự tắt fog của game.

## IBL bất đồng bộ và cadence

`environment_lighting` là lựa chọn override, không thay texture/rotation/intensity
của environment gốc. Diffuse dùng SH9, specular dùng GGX theo representation và
BRDF hiện có của Njin. Disks không được bake nên không cộng nắng trực tiếp hai lần.
Sky appearance, direct sun/moon và lighting trên geometry được test riêng.

Tích phân SH và medium tables chạy trên worker CPU với dữ liệu immutable, không
gọi bgfx trên worker và không GPU readback. Khi job xong, render thread tạo sky
snapshot/base/GGX cùng chính xác sun/moon/phase/probe của SH rồi publish cùng nhau.
Trong lúc chờ, dùng snapshot đã publish; lần đầu dùng environment của game.
`environment_pending` báo job đang xử lý/chờ publish. Medium đổi khiến job cũ
bị discard; `environment_discarded` đếm việc này. Tối đa một job mỗi context.
Release/teardown join worker; fixture kiểm tra bỏ job stale và thoát khi còn job.

IBL có observer `environment_altitude_m` riêng (default 2 m), không theo camera,
để nhiều camera dùng cùng probe một cách tường minh. Camera rotation/XZ/altitude,
resize và exposure không bake lại IBL. Medium, sun/moon/phase, airglow hoặc probe
đổi tạo key mới. `environment_update_seconds` default 1 s giới hạn cadence khi
sun đổi; không bake nếu key không đổi. `atmosphere3d_environment_refresh` bỏ qua
interval cho lần update tiếp theo, vẫn bất đồng bộ. `environment_intensity`
không yêu cầu bake. Static IBL không bị mất khi tắt atmosphere trong view sau.

## Đêm và đơn vị

Scattering/AP nhận illuminance từ sun và moon. Moon phase là phần đĩa sáng;
flux dùng phase function Lambert và disk Lambert được normalize full moon.
Moon dưới chân trời bị planet occlusion. Stars deterministic bằng integer hash,
xoay theo sidereal-style parameter và fade theo sun; nằm sau RGB transmittance,
không chiếu qua hành tinh, không bị đưa vào specular prefilter. Airglow/starlight
nền được đưa vào sky/SH/GGX. `atmosphere3d_moon_light` và `key_light` dùng extinction
như sun helper, giữ ambient/IBL/fog/shadows của base.

Night scene có radiance thấp thật: dùng exposure/auto exposure của Njin theo game,
không tự nâng ambient để đêm sáng giả. Fixture dùng exposure 20000 để nhìn các
control ban đêm, tách khỏi auto-exposure regression hiện có. Không có cloud,
weather, eclipse, catalog sao thật, lunar terrain texture hay chemistry airglow.

Shader dùng km, extinction 1/km, illuminance lux và radiance cd/m².
`radiance_scale` default 0.0001 đổi cd/m² sang scene units. RGB sun/moon tuyến
tính; helper light mã hóa RGB sRGB theo API light3d hiện tại, alpha=lux/100000.
Đây là normalization của Njin, không phải thay toàn engine sang photometric lighting.
Camera world dùng mét, cao độ = Y - sea_level_m. Hỗ trợ perspective, 1..20000 m,
planet radius 1000..10000 km, thickness 30..200 km. Không hỗ trợ camera ngoài
atmosphere. Các phạm vi profile/coefficients/lights đều có trong public header
và được validate; thiếu shader/capability/format hoặc dữ liệu invalid báo exception.

## Bộ nhớ và ownership

Các target tạo lười, theo context; T/MS/sky/AP không đổi kích thước khi resize.
Shader bindings snapshot theo draw, không expose bgfx handle vào ECS.
`atmosphere3d_release` chỉ chạy ngoài view và khi frame không còn dùng resources;
thu hồi mọi LUT/snapshot/scene/program và join worker. Context cũng tự teardown.

Full cache color: **1388544 bytes (1356 KiB)**; D24S8 đi kèm target factory thêm
**694272 bytes (678 KiB)** nominal: tổng **2082816 bytes (2034 KiB)**, chưa gồm
alignment/driver overhead. AP resolve thêm `width × height × 12` bytes nominal;
HDR scene và post targets là tài nguyên Njin riêng. CPU medium cache khoảng 51 KiB,
job có thể giữ cache trước trong lúc tạo medium mới. Single-only giữ 290 KiB color
và không cấp MS/AP/IBL target hay worker.

## Reuse và source

Đã đọc AGENTS/CLAUDE/renderer-reuse/checkpoint; giữ working tree của các phiên
khác, không stage/reset/stash/commit. Review archive `D:/projects/sdl3-proj`:
Atmosphere CPU, transmittance/MS/sky-view/AP/night shaders và notices. Archive
không có root LICENSE/.git nên không sao chép hoặc dịch code; hash file review
được ghim trong THIRD_PARTY_NOTICES. Bevy v0.18.0 functions/transmittance là
reference dependency đã review; không nhập WGSL, Bruneton mapping, scene hay RHI.

Tái dùng Njin shaderc/bgfx fragment draws, reflected bindings, target lifecycle,
SH9 basis, GGX atlas/BRDF, scene depth, HDR/exposure và time-of-day direction.
Integrators/quadrature/night shader mới được viết độc lập; không thêm dependency
hay compute requirement. Renderer validation nay cho phép initialized RGBA16F
environment target, từ chối feedback hoặc target chưa initialized.

File chính: `src/engine/api/njin_atmosphere.h`,
`src/engine/runtime/njin_atmosphere_{portable.h,portable.cpp,cpu.cpp}`,
`shaders/bgfx/fs_njin_atmosphere.sc` dưới runtime. Hook ở `njin_model_portable.cpp`,
`njin_post_portable.cpp`, `njin_renderer.cpp`; CMake và Android suite được cập nhật.

## Kiểm chứng

`njin_atmosphere_test` giữ regression single trước. `njin_atmosphere_full_test`
dùng create/mod_register/run và API gameplay bình thường, black environment sinh
tại chỗ, hai PBR cube diffuse/metallic và cube đỏ xa 6 km. Không asset game riêng.
Các control tách MS energy, SH/GGX, budget/force/revert game environment, AP
radiance/extinction, 20 km altitude, multi-target, phase/stars/rotation, day-night
transition, SSAO/bloom/FXAA/OIT, legacy fog rejection, release/recreate, stale job,
teardown khi job pending, hai context liên tiếp. Android patch chọn camera ray
theo aspect thật, không giả định màn hình 4:3.

Capture `atmosphere-full-<backend>.<stage>.png`: 0/1/2 single/MS/round trip,
3/4 game-black/atmosphere IBL, 6/7/8 night/deferred/forced IBL,
11/12/13 AP off/radiance/extinction, 16/17/18 full/half/new moon,
19/20 no-stars/rotated stars, 22 post stack, 25 stale worker, 26 OIT.

## Kết quả đã kiểm chứng

Chốt báo cáo 2026-11-18. Source engine không đổi sau build desktop/Linux cuối;
chỉ chốt tài liệu và hoàn tất lượt assertion Android còn thiếu sau khi tạm dừng.

| Môi trường | Compile verified | Runtime verified |
|---|---|---|
| Windows Vulkan, bgfx OpenGL, D3D12 | C++20; SPIR-V/GLSL430/HLSL s_5_0 | Single/full hai context mỗi backend; 8/8 gồm Vulkan validation, 255.09 s (2026-11-14). |
| Regression Windows | Các executable liên quan đã build/relink | Batch 37/37, 1134.50 s: 8 atmosphere và 29 materials/sky/environment/post/exposure/OIT/world, có legacy OpenGL và Vulkan validation. |
| Linux WSL | C++; GLSL/SPIR-V | 6/6 single/full Vulkan/OpenGL/validation, 170.35 s (2026-11-14); Xvfb Lavapipe/llvmpipe, không phải GPU Linux vật lý. |
| Pixel 6a Vulkan | APK arm64 Release; SPIR-V | Native `PASS 0`, `controller_passed=True`, 27 capture; assertion pha trăng cuối đạt 2026-11-18. |
| Pixel 6a GLES | APK arm64 Release; ESSL 300 | Native `PASS 0`, `controller_passed=True`, 27 capture; assertion pha trăng cuối đạt 2026-11-18. |

Pixel 6a là baseline mobile bắt buộc theo yêu cầu hiện tại. Thiết bị cũ là
coverage tùy chọn, không chặn kết quả này; không chạy lại hoặc suy diễn PASS cho
chúng. Android suite không chạy lifecycle/rotation/input/audio controls; các flag
đó giữ `False` trong status. Không suy diễn Android Vulkan validation từ desktop.

Control Windows Vulkan (giá trị trung bình patch kênh 8-bit): sky single/MS
**150.36/170.94**; geometry dùng game-black/atmosphere IBL **0.00/171.37**, vật
liệu còn lại **136.01**. Kênh blue của vật xa khi AP off/on **11.00/128.14**;
khi tắt nguồn sáng, kênh red giảm **217.00→211.00** bởi extinction. Geometry đêm
trăng tròn/trăng mới **15.02/1.15**. Các số này là đối chứng fixture, không phải
photometric ground truth hoặc tuyên bố chất lượng hơn renderer khác.

Disk sáng full/half/new moon có **63/29/0 pixel** trên desktop và
**1267/640/0 pixel** trên Pixel 6a ở cả Vulkan/GLES. 27 stage × hai backend
desktop so với Vulkan: mean channel error lớn nhất **0.01442/255**, channel error
cực đại **3/255**. Đã kiểm tra capture AP, đêm và pha trăng trực quan.

Trong 20 mẫu publication đã log của Windows Vulkan Debug: render-thread
publication/prefilter submission **0.237–1.027 ms**, median **0.404 ms**. Worker
**24.5–486.413 ms**, median **56.981 ms**, gồm cold medium-cache construction và
có tải build/test đồng thời. Worker không chặn render thread; IBL trước/fallback
được giữ trong lúc chờ. Đây là wall-clock CPU, **không phải GPU execution time**,
không phải benchmark FPS đại diện game. Chưa có timestamp GPU riêng cho các pass.

Artifacts tại workspace:

- `build/atmosphere-full-verification.json`: matrix, source hashes, raw stage samples,
  pixel controls, so sánh backend, memory và CPU timing summary.
- `build/atmosphere-full-report.py`: tái tạo JSON từ log/capture đã có.
- `build/atmosphere-full-final-tests.log`: 8/8 cuối;
  `build/atmosphere-full-windows-tests.log`: batch 37/37.
- `build/atmosphere-full-{vulkan,bgfx-opengl,d3d12,vulkan-validation}.log`:
  stdout/counters/assertions của fixture cuối.
- `build/atmosphere-full-linux-final-{build,tests}.log`,
  `build/atmosphere-full-linux-captures/`.
- `build/atmosphere-full-android-final-build.log`,
  `build/android-atmosphere-full/njin-renderer-test.apk`.
- `build/atmosphere-full-android-{vulkan,gles}/{status,result,logcat}.txt` và PNG;
  status xác nhận native/controller riêng, logcat chứa assertion pha trăng.
- `build/bin/captures/atmosphere-full-{vulkan,bgfx-opengl,d3d12}.0..26.png`.

`git diff --check` đạt; các file thay đổi giữ LF. Graphify AST đã update (vẫn có
parse warnings cũ). Doxygen tiếng Việt: 0 warning cho API atmosphere, còn 303
warning tài liệu khác và warning translator cũ. `i18n.py check` báo 87 vấn đề
English missing/stale/code/link; tuân thủ quy tắc tạm thời không sửa English hoặc
stamp. Không stage/commit hay ghi đè thay đổi có trước của các phiên khác.
