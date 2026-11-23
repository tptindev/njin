# Lightmap baking: UV charts, model/ECS và diffuse GI

## Audit và nguồn tái sử dụng

Audit bắt đầu tại checkpoint `2863070`, với working tree renderer đã có thay đổi
staged/unstaged. Không reset, stash, stage hoặc commit; giữ SDL3/bgfx và raylib.
Njin đã có sampling material lightmap, UV0/UV1, model/cgltf, HDR, IBL và asset
resources; chưa có baker/asset bake. Sampling material lightmap cũ được giữ.

Đã đọc `Lightmap.h/.cpp`, `LightmapGpu.cpp`, sample và dữ liệu bake trong snapshot
được yêu cầu: CPU/GPU paths, BVH, xatlas, direct/GI, mixed shadowmask, denoise,
seams, dilation và serialization. Không có revision/root license đọc được nên
không sao chép source hay nhập Vulkan driver/scene framework của snapshot.
SHA256 `Lightmap.cpp` được ghim trong `THIRD_PARTY_NOTICES.md`. Sự tồn tại của
asset/sample không được dùng làm bằng chứng runtime Njin.

Đã review source Godot `4.5-stable/modules/lightmapper_rd/lightmapper_rd.cpp`
(MIT) về atlas padding, progress, cancellation và denoiser/storage boundaries;
không sao chép Godot. Các lựa chọn thực tế:

| Thành phần | Nguồn/decision và chi phí |
|---|---|
| Unwrap/packing | xatlas `f700c7790aaa030e794b52ba7791a05c085faf0c`, MIT, gọi không sửa source. Dùng MeshDecl cho unwrap và UvMeshDecl cho UV hợp lệ có sẵn. Giữ UV0/secondary UV; remap qua xref. |
| Visibility/closest hit | tinybvh `c9be3103cc9693b456b0a0353927c11cac7d710f`, MIT, đã có trong Njin; không viết BVH thứ hai hoặc thêm GPU RT dependency. |
| Integration | Njin tự viết directional direct và cosine-weighted CPU paths nhiều bounce, diffuse reflectance, material textures và emission. |
| Dilation/mips | Nearest dilation không blur; mỗi chart có page riêng. Tái dùng footprint integration của Njin cho mip RGBA32F tuyến tính. Nhiều texture/draw hơn atlas chung, nhưng mip không thể lấy ánh sáng chart khác. |
| Asset/runtime | Format `.nlm` v3, độc lập backend, geometry/remap/UV chart, source object/mesh identity, world transform, settings, fingerprint và checksum. Upload riêng từng context, dùng engine resource handles. |

License từng file xatlas `.h/.cpp` đã kiểm tra, gồm notices Thekla/NVIDIA và
Fast-BVH. Header notices đầy đủ ở `third_party/licenses/xatlas-MIT.txt`, được
packager Android hiện có đưa vào APK. Dependency được FetchContent pin và target
`EXCLUDE_FROM_ALL`, chỉ link vào feature 3d; không có editor/framework scene mới.

## Quy ước ánh sáng và hình học

RGB của page là **(direct + indirect) irradiance / pi**, tuyến tính RGBA32F,
alpha 1. Direct là một directional; indirect dùng 0..8 bounce, 1..1024 paths
cho mỗi mẫu texel. Miss ray là đen: không tự cộng ambient/sky. Emission của bề
mặt khác được truyền qua paths; emission của chính receiver vẫn cộng tại shader.
Runtime nhân albedo một lần rồi đi qua HDR/exposure/tone mapping hiện có.

`illuminance` là RGB irradiance tuyến tính theo đơn vị tương đối Njin; geometry
và ray bias dùng mét. Cosine sampling ước lượng E/pi bằng trung bình incoming
radiance; mỗi hit thêm emission + diffuse reflectance * direct E/pi rồi tiếp tục
path. Có cảnh đối chứng hai bounce và color bleeding, không thay GI bằng AO.
RGBA8 albedo/emission được giải mã sRGB; ảnh float là tuyến tính. CPU tracing
giữ UV set, UV transform, nearest/bilinear và wrap của material snapshot.

Opaque diffuse triangle lists được hỗ trợ. Normals dùng inverse transpose,
normalization và world geometric bias; negative/nonuniform scale được kiểm tra.
Occluder chặn hai mặt; back face không phát/xạ diffuse giả. Dynamic/skinned,
animation/morph, masked/transmission/custom shader không bị nhận bake âm thầm.

Chế độ bake total thay realtime diffuse, ambient và IBL/RT trên draw static để
không cộng trùng. Mesh không có binding giữ hành vi cũ. Đây không phải mixed
lighting/shadowmask; local/dynamic light không cộng lên static baked draw.
Dynamic object tiếp tục dùng renderer bình thường. Legacy OpenGL từ chối chế độ
mới rõ ràng; sampling material lightmap cũ vẫn hoạt động như trước.

## UV, pages và invalidation

`RenderVertex::lightmap_uv` là kênh riêng, không dùng UV0 hoặc secondary UV1 của
glTF material. `generate_uv=true` unwrap/pack bằng xatlas. Khi false, baker kiểm
tra UV lightmap có sẵn rồi repack chart; thiếu, nonfinite, zero-area, overlap hay
UV ngoài miền hợp lệ bị từ chối. Không sửa nguồn model hoặc UV material.

Mỗi chart được trích ra page riêng, giữ mapping về source object và mesh. Border
được thêm rõ ràng, nearest dilation lấp phần trống của page rồi tạo full mip
chain. Hai instance chung mesh có page/binding/world transform riêng. Có texture
density theo world area và giới hạn page size; density thực có thể giảm khi chart
chạm budget, có thể tính lại từ geometry/UV/dimensions được lưu trong asset.

Fingerprint bao gồm geometry, normals, UV sets, transform, identities, ánh sáng,
albedo/emission, CPU texture bytes/sampler/UV transform và settings/version.
Caller phải snapshot lại scene liên quan khi kiểm tra stale/load sau thay đổi;
runtime draw kiểm tra geometry/transform khớp binding. Fingerprint/checksum là
FNV-1a để invalidation/debug, không là chữ ký bảo mật.

Save ghi file tạm rồi atomic replacement; bake/cancel/write thất bại không ghi đè
asset hợp lệ. Load kiểm tra version/layout/count/float/index/hash/checksum và
fingerprint scene hiện tại. File không lưu path tuyệt đối hoặc GPU handle.
Format cũ cần rebake khi version/fingerprint không khớp.

## API và entry points

`lightmap_add_model` snapshot CPU geometry/material từ model_load/model_create.
`lightmap_scene_snapshot` đọc ECS transform3d/model3d có component lightmap3d
(receiver) hoặc lightmap_occluder3d (occluder-only), cùng directional hiện tại của
light3d_set. Không tạo gameplay scene song song. Context load trả lightmap_handle;
component gameplay/ECS chỉ giữ handle đó và identity ổn định của object.

```cpp
auto &ecs = njin::world(ctx);
const auto entity = ecs.create();
ecs.emplace<njin::model3d>(entity).model = static_model;
ecs.emplace<njin::transform3d>(entity, placement);
ecs.emplace<njin::lightmap3d>(entity, njin::lightmap3d{{}, 44});
// Gọi light3d_set(ctx, sun) trước khi snapshot.
auto source = njin::lightmap_scene_snapshot(ctx, "static-room-v1");
njin::LightmapSettings quality;
quality.generate_uv = true;
quality.indirect_bounces = 2;
quality.indirect_samples = 256;
auto baked = njin::lightmap_bake(source, quality);
njin::lightmap_save("assets/room.nlm", baked);
ecs.get<njin::lightmap3d>(entity).asset = njin::lightmap_load(
    ctx, "assets/room.nlm", njin::lightmap_fingerprint(source, quality));
// begin_3d/end_3d với camera.entities=true sẽ vẽ qua Renderer API hiện có.
```

Manual draws dùng draw_lightmapped_model(ctx, model, transform, asset, object).
Low-level MeshDraw có lightmap_upload_instance/lightmap_bind; caller giải phóng
collection bằng lightmap_unload. Asset handle được giải phóng bằng
lightmap_unload(ctx, handle), sau frame sử dụng. Queued/active draws chặn unload;
stale/foreign context handles bị từ chối. Asset CPU và GPU resources thuộc từng
context. Shader vertex tương thích riêng giữ interface package `.njshader` cũ
khi vertex layout mới thêm UV lightmap.

## Lệnh tái tạo

```powershell
cmake --build build --target njin_lightmap njin_lightmap_test njin_lightmap_model_test --parallel 4
build/bin/njin_lightmap.exe --fixture build/direct.nlm 64 4 1
build/bin/njin_lightmap.exe scene.json build/gi.nlm 64 4 1 --unwrap --bounces=2 --paths=256
ctest --test-dir build -R '^lightmap_' --output-on-failure -j1
```

Tools/tests hiện thuộc NJIN_BUILD_RHI_SLICE. CLI JSON là serialization của
neutral snapshot: identity, direction[3], illuminance[3], instances[]. Mỗi
instance có id, receiver/dynamic, transform column-major[16], albedo/emission[3],
vertices và indices uint16. Vertex là [x,y,z,nx,ny,nz,lightmapU,lightmapV], hoặc
thêm [u0,v0,u1,v1]. albedoImage/emissionImage có path tương đối scene JSON, uvSet,
transform[6], linear, wrapS/wrapT. `--density=` đặt texel/mét. Ctrl+C hủy an toàn.

Android dùng packager/runner hiện có, cùng output directory/debug key qua các
lần cài để không cần gỡ ứng dụng hoặc xóa dữ liệu:

```powershell
python examples/android_renderer/build_apk.py --engine-build build-android-pivot --sdk "$env:LOCALAPPDATA/Android/Sdk" --out build/android-renderer
python examples/android_renderer/run_device.py --serial YOUR_DEVICE_SERIAL --renderer vulkan --suite lightmap_model --apk build/android-renderer/njin-renderer-test.apk --out build/android-lightmap-model-vulkan
python examples/android_renderer/run_device.py --serial YOUR_DEVICE_SERIAL --renderer gles --suite lightmap_model --out build/android-lightmap-model-gles
```

## Kiểm chứng và số đo

Windows đã đạt 43/43 regression/integration trên Vulkan/OpenGL/D3D12, gồm
material/model/animation/instancing/shadows/environment/post/mipmap/temporal,
custom shader và lightmap. Suite lightmap mở rộng có 10 test, gồm CPU, legacy
refusal, model/ECS/GI trên ba backend và Vulkan validation. Linux WSL/Mesa đã
đạt 5/5 test CPU/Vulkan/OpenGL. Lượt cuối sau thay đổi packing UV đạt 10/10 trên
Windows (`build/lightmap-final-tests.log`) và 5/5 trên Linux
(`build/lightmap-linux-tests.log`). Android arm64 Release đã build và chạy suite
model/ECS/GI trên Vulkan lẫn OpenGL ES: runner trả PASS, mỗi backend đủ sáu ảnh,
hai context/lần chạy. Log và ảnh ở `build/android-lightmap-model-vulkan/` và
`build/android-lightmap-model-gles/`. Control direct/HDR được ghi riêng ở
`build/android-lightmap-vulkan/` và `build/android-lightmap-gles/`.

Model test bake qua scene/model API, save/load qua engine asset handle và render
qua begin_3d/end_3d. Kiểm tra zero-direct floor, một/hai bounce, red/green texture
bleeding, emission, seed repeatability, stale material/texture/ECS transform,
queued unload refusal, nhiều context/repeated initialization, glTF import và
hai ECS instance chung cube nhưng chart bindings khác nhau. Direct fixture kiểm
tra blocker A/B, double-count, HDR >1 trước resolve, exposure và minification.

Corner 32x32 max page, 256 paths/sample, 2 bounce: mean red trên floor direct
0.000000; bounce1 0.381449; bounce2 0.454538; green 0.003270. Emission-only mean
red 0.475990. Bake GI Windows Debug 498..834 ms trong lượt Vulkan validation;
asset corner 3 page là 51008 byte. Hai cube có 12 chart/page, asset 69320 byte.
Không suy ra hội tụ hoặc hiệu năng production từ cảnh này.

Runtime thêm một trilinear lightmap lookup mỗi fragment; geometry per chart và
nhiều draw/page là chi phí rõ ràng. CPU resource store giữ base texels, GPU giữ
full mip chain; caller giữ LightmapAsset có thêm bản base CPU. Tổng bytes có thể
tính từ dimensions mỗi page. Corner có 49152 byte base CPU, 65520 byte mip GPU
RGBA32F và 1044 byte chart vertex/index; hai cube tương ứng 62208, 81792 và
4176 byte. Đây là payload lý thuyết từ asset, chưa gồm allocator/driver overhead,
bản copy asset CPU và geometry cache. Chưa đo GPU time/speedup.

Doxygen đã chạy: không có warning từ header lightmap mới; còn warning có sẵn ở
các API khác. `git diff --check`, kiểm tra LF và `graphify update .` đã chạy.

Capture `lightmap-model-<backend>.0..5.png`: direct, GI, emission-only, hai cube
qua ECS, minified cubes, imported glTF. Capture direct `lightmap-<backend>-0..5.png`
giữ các control sun/exposure/off/HDR/minification. Dữ liệu lớn và logs ở build,
không đưa vào repository. Source fixture đều procedural/public regression assets.

## Phạm vi đã định nghĩa

Baker hỗ trợ static opaque diffuse và một directional + emission transport.
Không tuyên bố mixed/stationary shadowmask, local/area/sky/environment transport,
specular GI hoặc normal-mapped/directional lightmaps. Những material/deformation
không tương thích được báo lỗi; không âm thầm gán ánh sáng bake sai. Không có
denoise/blur che lỗi; noise được điều khiển bằng paths/seed. Mỗi chart/page tránh
cross-chart mip bleed nhưng đổi lấy nhiều texture và draw hơn atlas chung.

Receiver validation overlap hiện O(T^2), giới hạn 8192 triangle/source receiver,
mesh unwrap 16-bit, page 8..512 và asset tối đa 256 MiB. Progress/cancellation có
trong unwrap/validation/tracing/dilation; BVH build đồng bộ chưa có callback nội
bộ. Đây là các giới hạn được thực thi của implementation, không là bằng chứng
production readiness. Các API, commands và tests ở trên là phần đã triển khai.
