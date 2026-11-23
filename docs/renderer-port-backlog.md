# Legacy feature parity on SDL3/bgfx

The active request is to port all twenty groups below. SDL3/bgfx Auto remains
the default. This ledger distinguishes partial implementation from completed,
runtime-verified parity; enabling the backend does not complete the list.

| # | Feature family | Current work |
|---|---|---|
| 1 | Custom shaders and legacy shader API | Fragment packages implemented: existing shader_* setters, engine-owned program registry, metadata/reflection validation, 2D per-draw snapshots, material/opaque 3D final-value semantics, UI skins and transactional package/binary hot reload. Windows four-backend/validation run passes. Custom vertex/instanced/OIT/compute and camera post variants remain pending; see [shader migration](renderer-shaders.md). |
| 2 | Legacy render textures, 3D targets and HDR composition | In progress: public 2D/3D target adapters, nesting/resume, synchronous GPU save/readback, texture replacement and ownership controls pass four Windows backend/validation tests. HDR resolve, FXAA/SMAA and nested parent restoration now pass eight render-target/temporal Windows controls. Per-explicit-target TAA/motion histories and preserving prior 2D contents through HDR remain pending. |
| 3 | Virtual resolution and render scaling | Implemented: portable frame target, logical projection, letterboxing, integer/fractional fit, supersampling, smooth UI transform, resize and virtual size changes. Windows runtime verified (4/4, repeated in an 8/8 combined animation/virtual run). Linux and Android Vulkan/GLES also verified in the later platform run. Crisp deferred text now implemented with logical clipping, panel occlusion, immutable font/alpha/offset snapshots and atlas lifetime checks; Windows 4/4 and Linux focused regression 20/20 pass. Android crisp text plus virtual/gizmo/sprite-FX regression 8/8 native suites pass on Vulkan/GLES; see [text rendering](renderer-text.md). |
| 4 | Packed atlases | Implemented and Windows runtime verified (4/4): shared shelf packing/padding, renderer-owned pages, packed UVs in manual/ECS/tile/particle draws, transactional reload, filters/mipmaps and ownership controls. Linux and Android Vulkan/GLES also verified in the later platform run. |
| 5 | Sprite flash/dissolve/masks | 2D flash/dissolve implemented through built-in draw packets, shared ECS timers, combined flash/dissolve, reverse/destroy behavior, atlas/UV sampling and manual scopes. Windows shader/FX matrix 8/8 and Android Vulkan/GLES native controls pass. Zero/one endpoints corrected in both legacy and bgfx for edge_width=0. 3D sphere masks/sweeps are now implemented under group 8; see [sprite FX](renderer-sprite-fx.md). |
| 6 | 2D lighting | Ambient/point/spot/directional HDR lights, sprite normal/MRA/emissive G-buffers, GPU-marched pixel shadows and CPU-traced silhouette shadows are implemented. The bgfx path uses 32 angular sectors for point/spot lights and 32 strips for directional lights. See [integration scope](renderer-completion-batch.md). |
| 7 | Legacy post_fx, depth of field and backdrop blur | Portable post_fx grading/CRT/grain/blur/bloom/DOF plus GPU backdrop blur/masking implemented, including virtual/UI composition, offsets, clipping and resize. Custom camera post remains pending. See [integration scope](renderer-completion-batch.md). |
| 8 | Advanced 3D materials and entity FX | Manual/ECS FX, rim, clearcoat, tangent anisotropy, POM, lightmap, world UV, transmission, mesh snow, glitter and SDF clay are implemented. The glTF PBR loader imports transmission and thickness maps, IOR, attenuation, anisotropy strength/rotation (as the legacy loader, kept only when every primitive has TANGENT), and clearcoat with its intensity, roughness and normal maps. Clearcoat maps share samplers 8/9 with transmission, so the two cannot be combined on one material. Other extension maps (sheen, specular, iridescence, anisotropy) remain pending. See [integration scope](renderer-completion-batch.md). |
| 9 | Fog, toon, volumetric and clustered lighting | Fog, toon and volumetric scattering implemented. Clustered forward/OIT/volume lighting now shares the legacy CPU 16x9x24 builder and supports up to 1024 lights; shadowed sources still capped at four. Volume half resolution remains pending. See [integration scope](renderer-completion-batch.md). |
| 10 | Ray-traced lighting | Implemented in compute shaders against a CPU BVH (tinybvh): `ray_traced_shadows` for the sun and the first three shadowed lamps (sun_angle/source_size, shadow_rays, denoise, shadow_lod with shadow_lod_gap), `ray_traced_reflections`, the ray traced irradiance grid (multi-bounce, DDGI visibility), and BVH reuse for unchanged scenes; `raytracing_available()` reports compute support. Verified on Windows (Vulkan, bgfx OpenGL, D3D12, validation), Linux (Vulkan, bgfx OpenGL, validation) and Android (vivo Vulkan, compute traversal; GLES 3.0 falls back to shadow maps). On bgfx Vulkan with `VK_KHR_ray_query` the traces run through ray queries (patched bgfx, `raytracing_hardware()`; verified on an RTX 3050, images identical to the compute path). Pending: DXR on Direct3D 12, BVH refit for moving scenes, the captured irradiance grid on bgfx. See [ray tracing](renderer-completion-batch.md#ray-traced-shadows-bgfx-compute). |
| 11 | Terrain, water, grass and legacy world environment | Partial: terrain with edit/paint/LOD, snow, imprints, detiling, scale variation and glitter, GPU-instanced grass/wind/far-cover, Gerstner lake/ocean patch, pour/splash rings, ice/snow-on-ice, water optics and scatter use the shared renderer. draw_sky3d's sky (gradient, glow, sun disc, clouds, stars, moon, fog) and its drive_light/wind state now run on bgfx and match the legacy renderer (sky_parity test), as do rain and snow (flake shapes, sparkle), the weather cover (cover_auto top-down map and weather3d_cover_set boxes), wet terrain and water rain rings; water now reflects the procedural sky (or one made from the light) with legacy's Fresnel share. The open sea reaches the horizon (legacy rings to 8 km, per-wave anti-aliasing fade, filtered ripples; `ocean3d` on all renderers). Relief hình học đã triển khai qua vertex texture fetch, lưới mịn, mip/triplanar và fade (Windows 5/5; Linux relief/ocean 7/7; Android relief/ocean Vulkan/GLES 4/4 đạt); xem [relief và giới hạn](renderer-terrain-relief.md). See [water/grass continuation](renderer-water-grass-parity.md) and [world port scope](renderer-world-port.md). |
| 12 | Advanced signed-distance shapes | Rounded box and cylinder, capsule, torus and blended endpoint spheres use sampled SDF meshes in bgfx. Clay uses a continuous group-local field. Per-pixel SDF ray marching remains in the legacy renderer. See [SDF rendering](renderer-completion-batch.md#signed-distance-shapes). |
| 13 | Additional model formats and compressed assets | Partial: OBJ/MTL loads on bgfx with the legacy loader's layout (mtllib materials from index 0, Kd/Ke as 8-bit colours, map_Kd albedo and map_bump normal with nearest filtering, V flip, +Y default normals, fan triangulation, unknown usemtl to material 0), parsed by fast_obj from the pinned meshoptimizer checkout. Verified on Windows (Vulkan, bgfx OpenGL, D3D12, validation), Linux WSL and Android Vulkan/GLES. EXT/KHR_meshopt_compression buffers are decoded with the pinned meshoptimizer (attributes/triangles/indices, octahedral/quaternion/exponential filters). Block-compressed images that the legacy path uploads natively (DDS BC1-3, KTX/PVR ETC1/ETC2/PVRTC, ASTC) are decoded to RGBA8 on the CPU with the pinned bimg. IQM/VOX/M3D (raylib-only, undocumented for games), Draco and the glTF KHR_texture_basisu/MSFT_texture_dds image sources remain pending and are refused with explicit errors. See [OBJ models](renderer-completion-batch.md#obj-models). |
| 14 | Dynamic/split meshes and procedural skinned models | Implemented and Windows runtime verified: shared split remapping (>65535 vertices), transactional CPU/RHI updates, bounds, draw protection, procedural skin parts/weight normalization, shared animation library and bind-pose fallback after library release. Combined animation/dynamic matrix 8/8 passes; Linux and Android Vulkan/GLES also verified in the later platform run. |
| 15 | Model LOD/combination/merge | Implemented: meshoptimizer static/skin/morph LOD, authored static levels, projected-size/quality selection, dynamic invalidation, model_combine with material groups, transformed normals/tangents, mirrored winding, LOD/from_level and shared images, plus static glTF merge. Windows combined LOD/animation/dynamic matrix 12/12 passes. Animated/morph glTF merge remains explicit unsupported; vertex buffers/deformation currently retain full vertices at lower LODs. |
| 16 | Animated model recolor overloads | Implemented and Windows runtime verified: both overloads snapshot per-draw colors, first duplicate wins, material alpha retained, shared material unchanged. Animation matrix (4 backends/validation) passes. Linux and Android Vulkan/GLES also verified in the later platform run. |
| 17 | Instancing culling, LOD and poses | Hi-Z cùng frame cho model thường và descriptor instance đục; pool buffer bền vững, dispatch nhỏ gộp và view dùng chung. Caster lọc theo từng projection đèn; nhóm descriptor đục không OIT/SSR chọn frustum/LOD trên GPU, giữ shared pose/orientation. Windows Vulkan/bgfx OpenGL/D3D12 và validation, Linux software Vulkan/OpenGL/validation đã đạt. Không cần visibility history cho thiết kế cùng frame. World-special, OIT/SSR và model thường còn dùng lựa chọn LOD CPU; Android chưa ghi nhận runtime pass. See [instancing](renderer-instancing.md). |
| 18 | GPU and legacy 3D particles | Existing CPU 3D bursts/billboards/streaks/ribbons implemented. Existing 2D GPU analytic trajectory path now runs through bgfx instanced vertices with immutable cross-frame spawn buffers, shapes/textures/drag/spin/lifetime/local-space and CPU-backend switching. This is vertex simulation, not a new 3D compute-particle system. See [integration scope](renderer-completion-batch.md). |
| 19 | Video textures | Implemented and Windows runtime verified (4/4): existing pl_mpeg decoding, renderer-owned textures, per-frame/seek uploads, draw/fit, clock, pause/resume, finish, cleanup and repeated contexts. Existing MPEG-1/MP2 codec and keyframe seek granularity retained. Audio runtime/platform matrix pending. |
| 20 | Gizmos, collision debug drawing and inspector | Windows verified: portable 2D/3D gizmos (homogeneous clipping, batched overlay, lifetime/visibility/virtual screen), collider outlines, inspector TCP server with pause/step/component views/3D snapshots/resource estimates and GPU GIF capture. Gizmos 4/4, inspector 4/4 pass. Inspector desktop UI still uses raylib; other platforms for these new debug ports pending. |

The portable readback path uses the pinned bgfx checkout's `TextureRegion`,
`read` and `BGFX_FRAME_FLUSH` APIs. A staging copy completes without presenting
an unfinished application frame. Render-target and texture resources remain
engine handles; gameplay receives no native GPU objects. Resource replacements
keep already submitted draw bindings valid. Legacy raylib behavior remains on
the explicit OpenGL path during migration.

Initial render-texture evidence: `build/port20-rt-windows-tests.log` (**4/4**,
21.14 s), plus legacy OpenGL and unsupported-scene regression **2/2** in
`build/port20-rt-regression-tests.log` (8.51 s). Tests cover nested/resumed
targets, top-left readback orientation, preserving back-buffer contents across
synchronous saves, 3D depth, texture versioning, active-resource rejection,
cleanup and two contexts. Other platform verification remains separate.

Additional Windows evidence: `build/port20-hdr-target-windows-tests.log`
(**8/8**, 107.88 s), `build/port20-recolor-virtual-windows-tests.log`
(**8/8**, 104.45 s), `build/port20-atlas-windows-tests.log` (**4/4**, 25.09 s),
and `build/port20-video-windows-tests.log` (**4/4**, 35.13 s). These are distinct
focused runs with overlapping controls, not a claim of a complete cross-platform run.
The PNG/MPEG fixtures have reproducible host generators in `src/engine/tests`.

Later focused evidence: `build/port20-procedural-skin-windows-tests.log`
(**8/8**, 56.72 s), `build/port20-lod-windows-tests.log` (**4/4**, 15.10 s),
`build/port20-combine-windows-tests.log` (**12/12**, 86.09 s). LODs reduce index
counts and rasterized triangles while retaining original vertex streams, keeping
skin/morph associations intact; vertex compaction and reduced CPU deformation
cost are future optimization work. Static glTF merging currently rejects animated
input explicitly rather than dropping morph data.

Cross-platform verification of the RT/atlas/virtual/video/dynamic/LOD/animation
ports: Windows **34/34** (`build/port20-all-windows-tests.log`, 354.28 s); Linux
**24/24** after synchronizing SDL programmatic resize (`build/port20-linux-tests.log`,
123.13 s); Android **14/14** native suites on Vulkan and GLES, each `PASS 0` with
controller confirmation in `build/android-device-captures/port20-<suite>-<driver>/status.txt`.
These precede the subsequent gizmo/debugger changes. Those add Windows gizmos
**4/4** (17.33 s), inspector server/GIF **4/4** (16.70 s), and BGFX-OFF legacy
inspector/GIF plus RHI **2/2** (39.88 s). The debugger reports resource-byte
estimates, not exact driver VRAM residency. `debug_server_desc::recording_directory`
optionally redirects GIF output; its empty default retains the game save directory.

Current shader/debugger verification before the sprite-FX addition: Windows
shader matrix **4/4** (hardened binary controls **4/4**, 20.18 s), broader
render-target/temporal/gizmo/RHI/sandbox regression **33/33**, 313.33 s;
inspector **5/5**, 13.82 s; BGFX-OFF inspector/RHI **2/2**, 9.42 s. Linux
shader/gizmo/inspector **10/10**, 14.95 s. These are overlapping focused runs,
not a single complete feature-parity suite. Android subsequently passed the
three shader/sprite-FX/gizmo suites on both Vulkan and GLES (**6/6**).

## Checkpoint before the requested pause (2026-11-04)

The current shader/sprite-FX batch is implemented and validated; the complete
20-family migration is **not finished**. Do not start another feature family
until the user resumes work.

Final focused Windows run: **17/17**, 92.92 s,
`build/port20-shader-fx-final-windows-tests.log` (models, render textures,
custom shaders and sprite FX across the available backends/validation).
Final Linux run: **14/14**, 34.67 s,
`build/port20-shader-fx-linux-tests.log` (shader/FX/gizmo/inspector plus legacy
controls). BGFX-OFF legacy run: **3/3**, 11.97 s,
`build/port20-shader-fx-legacy-tests.log` (sprite FX, inspector/GIF and RHI).

The Linux gizmo fixture now gives persistent markers a 1000-second duration;
its old 10-second lifetime could expire during cold software-driver shader
compilation. The zero-duration expiry control remains separate. Inspector step
verification observes a paused delta of zero followed by a positive single-step
delta, avoiding reliance on a transient stats flag between snapshots.

Next priorities when resumed: finish the remaining shader variants and explicit
composition/text boundaries, then 2D lighting/post_fx and the advanced 3D/world,
instancing/particle/asset families listed above. Existing native inspector desktop
UI and unverified Apple/Web packaging remain outstanding.

Final Android APK was rebuilt and reinstalled after the last input-validation
and fixture changes: all six shader/sprite-FX/gizmo Vulkan/GLES suites passed
again (native `PASS 0`, controller confirmed). Work is paused at the user's request.
