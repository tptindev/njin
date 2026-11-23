# Renderer reuse decisions

## Benchmark renderer — 2026-11-23

Tái sử dụng timing main loop, `mem_info`, resource/culling counters và capture
API hiện có. Đã đọc source pinned bgfx
`b2513038f4b753ebeced40821c193978daa0492f`: `bgfx.h` Stats,
`bgfx.cpp` Context::frame/swap, `bgfx_p.h` getPerfStats và các renderer
Vulkan/OpenGL/D3D12 xuất `gpuFrameNum`. Không nhập profiler hay thuật toán mới;
adapter chỉ ghi ID thật trả về từ frame để ghép query GPU trễ. Mã harness mới
là Njin tự viết; không dịch/copy source upstream, không thêm runtime dependency.
Phương pháp, phạm vi chưa đo và lệnh chạy: [renderer-benchmark.md](renderer-benchmark.md).

## Mip/LOD residency và virtual texturing (2026-11-20 → 2026-11-23)

Mở rộng scheduler/store đã có; không thêm pool, asset manager, RHI hoặc driver.
OBJ async dùng chính parser fast_obj/MTL hiện có. LOD worker dùng lại policy
welding/UV/skin/morph của `njin_model_lod_portable.cpp` và meshoptimizer v1.3;
static LOD compact vertex cả trước simplify lẫn mỗi output, animated LOD giữ
vertex indices để bone/morph khớp.
Mip dùng ImageMipChain đã có, publish tail texture nhỏ rồi thay tail lớn hơn
tại boundary, tránh sample mip chưa khởi tạo và không cần sparse API.

Đã mở Godot texture storage `4.5-stable` để đối chiếu ownership/update contracts,
không nhập RenderingDevice hoặc thuật toán VT từ đó. bgfx pinned API phân biệt
texture immutable (có initial memory) và writable (không initial memory);
runtime đầu đã bắt được update immutable bị từ chối, nên page atlas dùng writable.
Software VT của Njin dùng NVT1 range reads, page table và bordered cache trong
cùng sampler, permanent tail và ancestor fallback. CPU scene/camera gửi UV demand,
không có GPU feedback hoặc native sparse residency. Dùng chung albedo model/terrain.
Shader VT nằm trong package/variant opt-in riêng; đường shader thường không trả
chi phí lookup hay liên kết bytecode VT khi không gọi tính năng.
Mã quadtree/table/shader là Njin tự viết; provenance trong notices, bằng chứng và
giới hạn trong [renderer-residency.md](renderer-residency.md).

## Asset streaming (2026-11-20)

Audit Njin: texture/model stores dùng ID tăng dần, RendererResources giữ CPU
image/mesh và adapter bgfx upload lười khi draw. Parser cgltf, meshoptimizer,
material UV/sampler, skeleton/morph và CPU query mesh đã có. Không có shared job
pool: Jolt dùng JobSystemSingleThreaded, atmosphere có future riêng, model_combine
chỉ spawn helper tạm. Vì vậy dùng một worker theo context, không nhập pool thứ hai.

Đã đọc implementation Streamer/JobSystem/RendererStream và StreamingTest của
snapshot renderer được ghim bằng hash trong THIRD_PARTY_NOTICES.md. Source có
load CPU, generation check, upload budget và mip/LOD residency thực, nhưng chưa
chạy snapshot trong phiên này. Không có license/root Git truy cập được nên không
sao chép hoặc dịch source. Đã mở Bevy AssetServer tại tag v0.18.0
(`crates/bevy_asset/src/server/mod.rs`) để đối chiếu dedup/dependency states;
không nhập Rust/runtime Bevy. Chi phí tích hợp cả hai hệ cao hơn việc tách loader
Njin hiện có: ownership/format và driver khác, còn Njin đã có CPU loaders và
bgfx lifecycle phù hợp.

bgfx dependency hiện tại hỗ trợ updateTexture2D theo mip nhưng adapter Njin dùng
immutable full-chain texture; không có native sparse residency hay base-mip
contract trong gameplay API. Chọn asset streaming nguyên texture/glTF, model
upload theo texture/submesh, publish nguyên model ở frame boundary. Giữ sync
loader, SDL3/bgfx, opaque handles và legacy raylib. Không nhập định dạng riêng
hoặc framework scene. Phạm vi, budget đo/ước tính và bằng chứng backend ghi tại
[renderer-streaming.md](renderer-streaming.md).

This assessment covers the whole renderer, following the request to learn from
O3DE, Bevy, Godot and threepp. It does not change the selected SDL3 + bgfx stack, Njin's
public gameplay API, EnTT registry or independent raylib renderer. Status below
distinguishes reviewed source, adopted code/algorithms and future candidates.

## Upstream candidates for future work

Start with the Njin implementation and select the closest upstream candidate for
each feature. The table suggests areas to investigate and carries no adoption
status. Compare quality, integration effort and maintenance before importing code.

| Project | Where to look first |
|---|---|
| [godot-cpp](https://github.com/godotengine/godot-cpp) | C++ extension contracts, versioning and generated bindings. For Godot renderer internals, use the [Godot engine](https://github.com/godotengine/godot) source. |
| [Bevy](https://github.com/bevyengine/bevy) | Render preparation, visibility, batching, temporal effects and regression scenes. |
| [O3DE](https://github.com/o3de/o3de) | Atom feature processors, per-view state, pass dependencies and shadow atlas organization. |
| [threepp](https://github.com/markaren/threepp) | C++ material and renderer interfaces, environment controls and scene examples. |
| [three.js](https://github.com/mrdoob/three.js) | Use it as the upstream reference for threepp's design. Look at the `src/renderers/shaders/ShaderChunk` GLSL chunks (physical material, shadow filtering, tone mapping), the PMREM environment prefiltering and the `examples/jsm/postprocessing` passes. It is JavaScript/WebGL, so treat it as a source of algorithms and shaders, not code to link. The [root license is MIT](https://github.com/mrdoob/three.js/blob/dev/LICENSE). |
| [Fyrox](https://github.com/FyroxEngine/Fyrox) | Integration of 2D and 3D renderer features with scenes, assets and editor workflows. |
| [OpenXRay](https://github.com/OpenXRay/xray-16) | Study world-rendering behavior and workloads. Verify individual files and provenance before adaptation. The [license notice](https://github.com/OpenXRay/xray-16/blob/dev/License.txt) describes MIT terms for team modifications and new code. |
| [Acid](https://github.com/EQMG/Acid) | Deferred PBR, particle passes and shader reflection as Vulkan design references. |
| [Inexor Vulkan Renderer](https://github.com/inexorgame/vulkan-renderer) | Pass dependencies, resource lifetimes and render-graph diagnostics as Vulkan design references. |
| [Filament](https://github.com/google/filament) | PBR and IBL reference math, material models, clustered lighting, glTF assets and mobile quality trade-offs. |
| [Wicked Engine](https://github.com/turanszkij/WickedEngine) | Ray-traced shadows, reflections and AO on DXR/Vulkan, glTF material extensions, GPU instancing, procedural terrain and FFT ocean. Use its [feature index](https://github.com/turanszkij/WickedEngine/blob/master/features.txt) to locate the relevant implementation for review. |

## Decisions by subsystem

### Atmosphere LUT — 2026-11-14

Đã audit procedural sky, SH9/GGX environment, HDR target và shader bindings
hiện có của Njin; đọc source atmosphere thực trong archive `sdl3-proj` cùng
notices (hash snapshot ở `THIRD_PARTY_NOTICES.md`). Archive không có root
LICENSE/.git nên không nhập hoặc dịch mã của archive. Đã mở Bevy v0.18.0
[`functions.wgsl`](https://github.com/bevyengine/bevy/blob/v0.18.0/crates/bevy_pbr/src/atmosphere/functions.wgsl)
và `transmittance_lut.wgsl` để đối chiếu dependency medium/view; không nhập WGSL
hay mapping Bruneton. Bevy compute/render schedule không khớp adapter bgfx.

Triển khai độc lập spherical Rayleigh/Mie/ozone, MS isotropic closure và ground
bounce: transmittance 256×64, MS 32×32, sky-view 192×108, cặp RGB aerial atlas
512×32 RGBA16F, background với sun/moon/stars trong HDR hiện có. Tái dùng shaderc,
reflected shader bindings, engine
vertex stage, target ownership/suspend/resume và tone mapping của Njin; không
thêm RHI/dependency hay compute requirement. Sun helper CPU opt-in cập nhật nắng
trực tiếp sun/moon; game giữ ambient/fog. IBL SH9 tích phân trên worker, GPU GGX
snapshot được publish nhất quán theo cadence; override opt-in riêng từng view,
không thay environment gốc. Không GPU readback hoặc bake CPU nặng mỗi frame.
Night có phase Lambert, star field và airglow; không phải ephemeris/weather model.
API, cache, memory, giới hạn và kết quả kiểm chứng:
[Atmosphere](renderer-atmosphere.md).

### Android Hi-Z: capability và submission — 2026-11-14

Đọc lại source bgfx `b2513038f4b753ebeced40821c193978daa0492f`:
`renderer_vk.cpp`, `renderer_gl.cpp`, `glimports.h`, `glcontext_egl.cpp`,
`config.h`, public buffer/capability API và example 37. Đọc thêm Bevy
v0.18.0 `mesh_preprocess.wgsl` để đối chiếu compute/storage theo view; không
nhập WGSL, lịch sử visibility hoặc renderer của Bevy.

Probe trực tiếp Pixel 6a/Mali-G78 cho thấy Vulkan hỗ trợ R32F storage và compute,
`drawIndirectFirstInstance=1` nhưng `multiDrawIndirect=0`; GLES thực tế là 3.2,
có compute, trong khi bgfx được build với baseline ES 3.0. Chọn vá tối thiểu
bgfx: chia indirect submission thành drawCount=1 khi thiếu multi-draw, giữ
indirect-count bị chặn trong trường hợp đó; GLES dùng kiểm tra ESSL 3.1 runtime
và core entry points sẵn có, kể cả đường barrier compute. Giữ baseline ES 3.0
và kiểm tra R32F read/write, sampled, binding/instancing của Njin. Không đổi
định dạng Hi-Z hoặc thêm readback vào đường vẽ. CMake tái tạo bản vá, kiểm tra
revision/anchor; license và modifications ở `THIRD_PARTY_NOTICES.md`.

API `renderer_gpu_occlusion_diagnostics` và log startup báo capability, giới hạn
và đường đang chọn. Fixture vẫn từ chối nếu unsupported. Kết quả compile/runtime
và artifacts được ghi riêng trong [GPU instancing](renderer-instancing.md).

### Relief địa hình và lỗi stack GLES — 2026-11-11

Đã mở mã thật của Wicked Engine tại
`754848e403986314cf49a038793ae9b62aebb74a`:
[`wiTerrain.cpp`](https://github.com/turanszkij/WickedEngine/blob/754848e403986314cf49a038793ae9b62aebb74a/WickedEngine/wiTerrain.cpp)
và [`objectHF_tessellation.hlsli`](https://github.com/turanszkij/WickedEngine/blob/754848e403986314cf49a038793ae9b62aebb74a/WickedEngine/shaders/objectHF_tessellation.hlsli).
Tham khảo tăng mật độ theo camera, displacement theo normal và nới biên culling.
Đưa tessellation vào bgfx đòi hỏi mở rộng nhiều backend và tăng chi phí kiểm chứng;
chọn lưới con tạo lười kết hợp vertex texture fetch, dùng registry texture/mesh
và pipeline Njin đang có. Triplanar, splat, mip và fade dùng lại từ Njin legacy.
Không nhập mã Wicked Engine.

Đồng thời đã đọc `ShaderGL::create` trong `src/renderer_gl.cpp` của bgfx
`b2513038f4b753ebeced40821c193978daa0492f` và `bx::MemoryBlock` trong
`include/bx/readerwriter.h` của bx `da13683750ff8df8f44a5a15f637b6ae9460a04b`.
Native backtrace Pixel 6a xác định stack overflow khi bgfx cấp nhiều buffer
theo chiều dài shader trên stack SDLThread. Tái sử dụng allocator/RAII có sẵn
của bx thay vì tăng stack thread hoặc thêm allocator riêng. Bản vá tái tạo khi
configure, từ chối nếu upstream đổi đoạn cần vá; provenance, BSD-2-Clause và
modifications được ghi trong `THIRD_PARTY_NOTICES.md`.

Chi tiết triển khai và giới hạn: [terrain relief](renderer-terrain-relief.md).

| Subsystem | Reviewed source | Njin decision / status |
|---|---|---|
| GPU/platform/shader compilation | Existing pinned SDL3, bgfx and shaderc integration | **Already reused directly.** Continue using these dependencies for devices, native windows, shader formats and submission. Keep the adapter thin. |
| Render preparation, batching and visibility | Bevy [mesh preparation](https://github.com/bevyengine/bevy/blob/v0.18.0/crates/bevy_pbr/src/render/mesh.rs), [prepass setup](https://github.com/bevyengine/bevy/blob/v0.18.0/crates/bevy_pbr/src/prepass/mod.rs) | **GPU instancing implemented.** Immutable prepared packets feed real bgfx instance buffers, shared by shadow/forward passes. Preserve the existing public record format and mirrored culling. Camera culling/LOD optimizations remain pending; reuse existing meshoptimizer/bounds work when porting them. See [instancing](renderer-instancing.md). |
| PBR/material variants | Bevy [PBR functions](https://github.com/bevyengine/bevy/blob/v0.18.0/crates/bevy_pbr/src/render/pbr_functions.wgsl), Godot [forward lighting](https://github.com/godotengine/godot/blob/4.5-stable/servers/rendering/renderer_rd/shaders/scene_forward_lights_inc.glsl) | **Core texture maps implemented.** Normal/MR/AO/emission, UV1/tangents, per-map transforms/samplers and linear PBR share shader math across adapters. Advanced lobes and mipmap minification remain pending. See [materials](renderer-materials-ibl.md). |
| IBL | Bevy [environment map shader](https://github.com/bevyengine/bevy/blob/v0.18.0/crates/bevy_pbr/src/light_probe/environment_map.wgsl), pinned bgfx [IBL sample](https://github.com/bkaradzic/bgfx/blob/b2513038f4b753ebeced40821c193978daa0492f/examples/18-ibl/fs_ibl_mesh.sc) | **Implemented by reusing existing Njin SH9 + GGX equirectangular-atlas conventions.** Repository audit showed cubemap infrastructure was unnecessary for the current API. CPU importance prefiltering, float16 HDR, BRDF fit, rotated reflections and sky share the existing GGX shader. Bevy/bgfx remain design references; their IBL code was not copied. See [IBL](renderer-materials-ibl.md). |
| Animation/skinning/morphs | Bevy [skinning shader](https://github.com/bevyengine/bevy/blob/v0.18.0/crates/bevy_pbr/src/render/skinning.wgsl), Godot [mesh/skeleton storage](https://github.com/godotengine/godot/blob/4.5-stable/servers/rendering/renderer_rd/storage_rd/mesh_storage.cpp) | **Portable CPU deformation implemented.** Preserve clip/pose, bone queries and ECS APIs; evaluate native STEP/LINEAR/CUBICSPLINE channels, ancestors, cross-fades, inverse-bind skinning and morphs once into an immutable mesh snapshot. GPU upload is shared across forward/shadow/instance groups. Bevy/Godot informed the pass/lifetime design; their skinning code was not copied. GPU deformation/motion-vector optimization remains pending. See [animation](renderer-animation.md). |
| Shadows | Bevy [cascade construction](https://github.com/bevyengine/bevy/blob/v0.18.0/crates/bevy_light/src/cascade.rs), O3DE [atlas](https://github.com/o3de/o3de/blob/development/Gems/Atom/Feature/Common/Code/Source/CoreLights/ShadowmapAtlas.cpp) and [directional light processor](https://github.com/o3de/o3de/blob/development/Gems/Atom/Feature/Common/Code/Source/CoreLights/DirectionalLightFeatureProcessor.cpp), Godot forward lighting | **Implemented raster path.** Bevy's camera-space cascade sizing and texel anchoring are adapted in C++, with MIT attribution. The mixed-size atlas remains Njin-owned; O3DE's tree/array approach is a reviewed option if current packing becomes a measured bottleneck. Godot's bias/filter choices inform later quality controls, not an assertion that those algorithms were imported. |
| HDR, bloom and post-processing | Godot [tone mapping shader](https://github.com/godotengine/godot/blob/4.5-stable/servers/rendering/renderer_rd/shaders/effects/tonemap.glsl), Bevy [bloom passes](https://github.com/bevyengine/bevy/blob/v0.18.0/crates/bevy_post_process/src/bloom/mod.rs), pinned bgfx [HDR sample](https://github.com/bkaradzic/bgfx/tree/b2513038f4b753ebeced40821c193978daa0492f/examples/09-hdr) | **HDR, SSAO, bloom and FXAA implemented.** Reuse existing Njin tone curves and its 13-tap/Karis downsample, tent upsample and FXAA math through shared shaderc-compatible functions, with resize/lifetime tests. SSR is implemented below; temporal effects remain pending. See [post-processing](renderer-post.md). Godot/Bevy are references for pass order and quality variants. |
| Screen-space reflections | Godot [SSR trace](https://github.com/godotengine/godot/blob/4.5-stable/servers/rendering/renderer_rd/shaders/effects/screen_space_reflection.glsl), Bevy [SSR](https://github.com/bevyengine/bevy/blob/v0.18.0/crates/bevy_pbr/src/ssr/ssr.wgsl) | **Implemented, with controlled desktop/Android/Linux runs.** Reuse Njin legacy growing-step trace, thickness checks, binary refinement and fog fallback. Godot/Bevy source review informed separate normal/roughness data, orthographic incident rays, roughness rejection and miss handling. Their compute/WGSL implementations were not copied. Prepared mesh/animation snapshots are reused for the mask pass. See the post-processing document for quality limits and the migration log for exact verification scope. |
| Outlines and sun optics | Existing Njin `modules/post3d.cpp`; the post-process organization references above | **Implemented.** Depth silhouettes/creases/fade, half-resolution float16 radial sun shafts, 25-tap depth visibility and five lens ghosts/halo reuse Njin legacy formulas. They run in the same HDR pipeline with controlled captures, resize, UI and resource checks. These are screen-space effects, not volumetric light transport. |
| C++ renderer/material contracts | threepp [Renderer interface](https://github.com/markaren/threepp/blob/master/include/threepp/renderers/Renderer.hpp), [GLRenderer](https://github.com/markaren/threepp/blob/master/include/threepp/renderers/GLRenderer.hpp), [MeshPhysicalMaterial](https://github.com/markaren/threepp/blob/master/include/threepp/materials/MeshPhysicalMaterial.hpp) | **Reviewed, not imported.** Backend-neutral view/output settings, material lobe controls and the explicit HDR-environment sun policy are useful references. Njin keeps its existing APIs and bgfx device/resource ownership. Clearcoat/transmission and environment sun separation are candidates requiring their own implementation and controls. |
| Per-view resources/settings | O3DE [post-process feature processor](https://github.com/o3de/o3de/blob/development/Gems/Atom/Feature/Common/Code/Source/PostProcess/PostProcessFeatureProcessor.cpp), Godot mesh storage | **Reviewed.** Extend Njin's per-context caches into explicit per-view resources when multiple views require them. Retain opaque handles and queued lifetime protection. Introduce a pass graph only when actual dependencies justify it. |

The source inspections above identified substantial coupling: Bevy's renderer
uses Rust/WGSL imports, bind groups and render schedules; Godot's implementation
uses its RenderingDevice/storage and shader preprocessing; Atom uses RPI passes,
feature processors and AzCore. Those classes are not interchangeable with Njin's
bgfx adapter. The reusable units are bounded algorithms, shader routines, data
contracts and tests. Existing compatible dependencies and Njin legacy code get
priority over a translated implementation requiring a second rendering stack.

## Reuse already applied

The glTF transmission and volume imports follow Khronos channel semantics and
use the existing cgltf loader, renderer-owned images and OIT path. Filament's
[material feature and UV mapping](https://github.com/google/filament/blob/main/libs/gltfio/include/gltfio/MaterialProvider.h)
informed the choice to preserve each texture's UV set. glTF clearcoat maps
follow the same path (cgltf views, renderer-owned images, per-map UV
transform/sampler) and the Khronos `KHR_materials_clearcoat` channel and
coat-normal semantics; they share the transmission samplers instead of adding
bindings, and the base normal-map tangent frame is reused for the coat normal.
No Filament or Wicked Engine source was copied. OBJ/MTL reuses fast_obj from
the pinned meshoptimizer checkout and reproduces the legacy raylib `LoadOBJ`
conventions (reviewed in the pinned raylib source). The bgfx sky and weather
are the legacy `world3d_draw.cpp` shaders carried over (`sky_glsl`,
`precip_vs/fs`, `cover_glsl`, `rain_rings`, the terrain wetness and water
Fresnel sky), with the drop hash moved to the CPU bit for bit and the shadow
pass reused for the cover map; no external renderer was needed because the
target is parity with that legacy code. The bgfx ray traced shadows build
their BVH with tinybvh (pinned, MIT) and translate its GLSL traversal kernel
into a bgfx compute shader; Wicked Engine's software `TraceRay_Any` path was
reviewed as the design reference; the lamps, reflections and the ray traced
irradiance grid translate Njin's own legacy `modules/rt/*.comp` (shade_hit,
gi.comp's DDGI depths and history) to bgfx compute. The Vulkan ray query path
follows tinybvh's pinned ray query reference (`external/vulkanrt/shaders/tinyrq.comp`
and the acceleration structure setup in `program_vk.cpp`), reviewed, not copied.
Njin's own legacy
renderer supplies the frustum-sphere and terrain detiling formulas. The
instancing path reuses Njin's model LOD and deformation pipeline.

`njin_shadow_portable.cpp::cascade` now derives a fixed projection extent from
camera-space frustum diagonals, rounds the extent, and anchors the light-space
center to texels. This follows the Bevy reference while retaining Njin's OpenGL
clip convention, depth padding and atlas layout. It addresses scale changes from
view-rotation-dependent tight bounds. The regression includes identical image
output under a tiny camera translation, every cascade and actual device runs.
Verification results belong in [renderer-shadows.md](renderer-shadows.md).

The adaptation is recorded in [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md),
with the upstream MIT license preserved in `third_party/licenses/bevy-MIT.txt`.
The Android packager includes the notice/license assets. There is no Bevy runtime
dependency, Rust toolchain requirement or ECS replacement from this adaptation.

## Rules for the remaining implementation

### Lightmap baking: UV charts, model/ECS và diffuse GI

Đã audit baker snapshot và source Godot lightmapper `4.5-stable`; không sao chép
snapshot thiếu license chung hoặc nhập Vulkan driver/scene framework. Tái dùng
tinybvh đã pin cho CPU visibility/closest hit và xatlas
`f700c7790aaa030e794b52ba7791a05c085faf0c` (MIT) cho unwrap/packing/vertex remaps.
UV lightmap là kênh riêng, giữ UV0/secondary UV material. Chart pages cô lập có
dilation và mip float tuyến tính. Njin tự tích phân diffuse paths nhiều bounce,
texture reflectance và emission; có đối chứng color bleeding/GI off/2 bounce.
Snapshot lấy từ model/ECS/transform/light Njin, runtime dùng engine asset handle
và Renderer API; package shader cũ giữ interface qua vertex variant tương thích.
Mixed/shadowmask không được tuyên bố hỗ trợ. Provenance trong notices; lệnh,
chi phí và kết quả compile/runtime:
[renderer-lightmaps.md](renderer-lightmaps.md).

1. Inspect the relevant existing Njin legacy feature and compatible bgfx code.
   Use the engine references above to fill gaps in algorithms and design.
2. Before importing/translating source, pin a tag/commit and record the files,
   modifications, required notices and component-specific licenses. O3DE links
   currently use `development` for review only; pin a commit before copying.
3. Keep GPU types inside adapters. Convert WGSL/GLSL binding conventions to the
   existing shaderc asset pipeline, and preserve the same material/pose semantics
   in forward, depth and shadow passes.
4. Prove each reused unit with a Njin scene and image/resource controls on the
   available backends. Source availability or permissive licensing does not
   establish compilation, platform support or runtime correctness.

The repositories' root terms are [Bevy MIT/Apache](https://github.com/bevyengine/bevy#license),
[Godot MIT](https://github.com/godotengine/godot/blob/4.5-stable/LICENSE.txt) and
[O3DE Apache/MIT choice](https://github.com/o3de/o3de/blob/development/LICENSE.txt).
Inspect file-specific/third-party terms for each actual import. No Godot/O3DE
renderer source is currently copied into Njin by this assessment.

After shadows, material maps/IBL, shared animated geometry, GPU instancing and
bloom/FXAA, prioritize GPU deformation/pose caching, culling/LOD and the remaining
depth/temporal post effects. This order lets each later feature
reuse the rendering and validation work already present rather than restarting
the pipeline.


## threepp assessment (2026-11-04)

The reviewed threepp interfaces expose a backend-neutral renderer, specialized
GL accessors, view/target sizing, output color space/exposure, shader diagnostics
and physical material settings. Its environment-sun policy explicitly addresses
double lighting when an HDRI sun and a directional light coexist. These are
useful contracts to compare with Njin when extending materials and IBL.

This review does not import threepp's scene graph, Canvas, renderer or material
classes. Njin already has SDL3/bgfx and gameplay/ECS APIs, so wholesale renderer
inheritance would introduce another resource/platform ownership model. Current
changes reuse compatible Njin post shaders and existing bgfx submission instead.
There is no new threepp runtime/build dependency or copied threepp source.
Its [root license is MIT](https://github.com/markaren/threepp/blob/master/LICENSE).
The `master` references above are review pointers; pin the exact revision and
retain component-specific notices before importing any actual implementation.


## Chưng cất renderer fl: Hi-Z instance culling

Đã đọc `cull.comp`, `hiz_first.comp`, `hiz_down.comp` và `renderGpuScene` của
snapshot fl được ghim trong `THIRD_PARTY_NOTICES.md`, cùng bgfx example 37 tại
revision dependency hiện tại. Chọn tái sử dụng thuật toán max-depth và phép thử
bốn texel; không nhập driver Vulkan, hệ resource hay scene riêng của fl.
Cách này giữ chi phí bảo trì ở adapter bgfx và shaderc đã có, tái dùng packet,
bounds, pose, LOD và render-target ownership của Njin. Không có LICENSE chung
trong snapshot fl nên không sao chép file nguyên khối hoặc giả định quyền nhập.

Đã nối compute compaction và indirect draw vào `end_3d`/descriptor instancing,
giữ bóng độc lập với camera. Dùng depth cùng frame thay cho visibility history
hai pha: dễ kiểm chứng disocclusion và không cần CPU readback lúc vẽ, nhưng
hiện chỉ model không instanced làm occluder. Đây là một subsystem đã triển khai,
không phải toàn bộ GPU scene renderer của fl. Windows Vulkan/OpenGL/D3D12 đã
đạt đối chứng hình ảnh và GPU counters. Chi tiết giới hạn, lệnh kiểm thử và API:
[GPU instancing](renderer-instancing.md).


Linux WSL Vulkan/OpenGL và Vulkan validation cũng đã đạt. Android Pixel 6a đã
compile APK nhưng capability Hi-Z không đủ trên cả Vulkan/GLES: fixture từ
chối, không ghi nhận runtime pass. Chưa có số đo chứng minh tăng tốc.

### Mở rộng culling và resource reuse

Đọc lại source `cull.comp` của snapshot fl trên (HEAD/hash khớp notices), API
dynamic buffer và example 37 của bgfx `b2513038f4b753ebeced40821c193978daa0492f`.
Đã mở thêm [Bevy mesh_preprocess.wgsl v0.18.0](https://github.com/bevyengine/bevy/blob/v0.18.0/crates/bevy_pbr/src/render/mesh_preprocess.wgsl)
để đối chiếu work item theo view, frustum và early/late phase. Chỉ review,
không nhập/biên dịch lại WGSL hoặc driver; Bevy yêu cầu render schedule và
binding model khác. Chọn pool dynamic buffer có sẵn của bgfx và cùng-frame
depth của Njin thay vì thêm history cùng cơ chế invalidation.

Lợi ích/dependency/kiểm chứng theo bước: (1) giữ buffer để giảm allocation và
gom view/phép dispatch nhỏ; cần update buffer theo thứ tự submission, kiểm tra
allocation đứng yên và slot idle được thu hồi. (2) indirect draw cho model thường
dùng bound mesh/pose bao kín chính nó; kiểm tra ảnh A/B, mask/dissolve và cut.
(3) caster compaction theo projection đèn chưa tile; kiểm tra GPU counters và
fixture shadow sun/spot/point/cascade. (4) nhóm descriptor đục không OIT/SSR
chọn frustum/LOD trên GPU bằng quy ước Njin; kiểm tra shared pose, các LOD và
nhiều target khác kích thước. Không có thêm dependency hoặc mã bên thứ ba.
Kết quả runtime/benchmark của bản mở rộng ghi trong renderer-instancing.md.
