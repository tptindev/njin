# Third-party notices

## Renderer benchmark analysis tool — 2026-11-23

The host-only image analysis script uses Pillow **12.2.0**, pinned in
`src/tools/renderer_bench/requirements.txt`. The installed distribution's
`pillow-12.2.0.dist-info/licenses/LICENSE` was read: Pillow/PIL is MIT-CMU
(Copyright 1997–2011 Secret Labs AB, 1995–2011 Fredrik Lundh and contributors,
2010–present Jeffrey 'Alex' Clark and contributors). Upstream:
https://github.com/python-pillow/Pillow/tree/12.2.0 . No Pillow source was
copied, modified or bundled with Njin; the host wheel retains its complete
license file and bundled-library notices. It is used for PNG decoding,
pixel differences and writing heatmaps, outside performance runs.

The benchmark itself is independent Njin code using the already pinned bgfx
Stats/capture infrastructure. Source reviewed for frame/timer identity:
bgfx `b2513038f4b753ebeced40821c193978daa0492f`, `include/bgfx/bgfx.h`,
`src/bgfx.cpp`, `src/bgfx_p.h`, `src/renderer_{vk,gl,d3d12}.cpp` (existing
BSD-2-Clause dependency notices apply). No profiler or upstream algorithm
was copied or translated.

## Resource streaming audit — 2026-11-20 (independent implementation)

Residency extension: OBJ async reuses Njin's existing fast_obj parser and
OBJ/MTL material conversion (fast_obj from the already pinned meshoptimizer v1.3,
MIT). Worker LOD preparation reuses Njin's welding/attribute/simplification
policy and the same pinned meshoptimizer API; no new upstream code is copied.
The mip-tail replacement policy, NVT1 cooker/page directory, atlas/page-table
mapping and `njin_virtual.sh` are independent Njin implementations.
Godot `4.5-stable`, `servers/rendering/renderer_rd/storage_rd/texture_storage.cpp`
(file MIT header) was opened for updateable texture/default-resource ownership;
its RenderingDevice implementation was reviewed, not copied or translated,
and is not claimed to provide Njin's virtual-texture algorithm.
bgfx `b2513038f4b753ebeced40821c193978daa0492f` texture creation/update API was
checked in the pinned dependency: atlases use creation without initial memory
so `updateTexture2D` is permitted; immutable asset textures keep their old path.

Inspected local renderer snapshot `engine/src/stream/Streamer.{h,cpp}`,
`JobSystem.{h,cpp}`, `engine/include/fl/Streaming.h`, `RendererStream.cpp`
and `game/tests/StreamingTest.cpp`. No accessible Git metadata/root license
or file-specific permission was found for these files. Snapshot pins (SHA256):
`Streamer.cpp` = `a119b70e999f0cec14835aaf17519255242f16dfe8671180d7ee78dcd9e8324a`;
`JobSystem.cpp` = `9341519d23037e5e44e5274b501c90f8f3daafbdbe6be57b1307e5ebc6bf8bcc`.
No source, assets, format implementation, Vulkan driver or scene framework
from that snapshot is imported or translated. Njin independently implements
request generations, CPU completion handoff, admission and upload budgets,
pinning and idle eviction using its existing stores and loaders.

Reviewed Bevy `v0.18.0`, `crates/bevy_asset/src/server/mod.rs` for deduplicated
requests and separate dependency/load states; no Rust source was copied.
Reused dependencies remain the already pinned raylib/cgltf image/model CPU
loaders, meshoptimizer decompression and bgfx/bimg. New code in
`njin_stream.cpp` and the preparation refactor in `njin_model_portable.cpp`
are Njin-owned. No additional third-party runtime or license is introduced.

## Atmosphere audit — 2026-11-14 (independent implementation)

Extension audit (same archive, no copying/translation):
`engine/shaders/atm_multiscatter.fs` SHA-256
`031843e300ade9a3a4df3aa58185dc880d51b3ea6856e8cb7b3f0ddc3e2b0e74`,
`engine/shaders/atm_aerial.fs` SHA-256
`f81ff2c5788fcafba1e2afaa8d6eb03a1d564508e639e3614ebb74b28af8d18c`.
Reviewed their pass dependencies and Hillaire's isotropic multiple-scattering
closure; the new Njin CPU/GPU implementations use independent Fibonacci-sphere
quadrature, RGB aerial transmittance, existing Njin SH9/GGX conventions and a
budgeted explicit per-view environment override. No archive source/assets or
Bevy implementation are imported. Night sky uses Njin-owned procedural stars,
an analytic phased lunar disk and user-configurable lunar illumination/airglow;
it is not an astronomical ephemeris or weather simulator.

Reviewed the local `D:/projects/sdl3-proj` archive (no `.git` directory or
root LICENSE supplied). Pinned SHA-256 of the reviewed files:

- `engine/src/Atmosphere.cpp`: `ec2d5327a729025422868632bc9a4b4f5ea17b236521836c5b4165de23869da0`
- `engine/shaders/atmosphere.glsl`: `ab8e3a36ae6dd59b0e2b26866cdaa10b94ae4de9acdddcf1eaac628f71bd286d`
- `engine/shaders/atm_skyview.fs`: `85146c18a18f913a4b956cc455c691500c0e709b509317e8264a67abc35d8eec`

These files name Bevy/Hillaire/Bruneton ancestry, but their notices do not
establish a license for all archive-specific code. No archive code/assets
are copied or translated into Njin. Also reviewed Bevy at the release tag `v0.18.0`,
`crates/bevy_pbr/src/atmosphere/{functions,transmittance_lut}.wgsl` as a
pass/dependency reference; no WGSL or Bruneton mapping is imported.

`njin_atmosphere_portable.cpp` and `fs_njin_atmosphere.sc` are independent
Njin implementations of spherical ray intersection, Beer-Lambert extinction,
Rayleigh and Henyey-Greenstein scattering, isotropic multiple-scattering closure,
exponential density and triangular ozone absorption. References: Sebastien Hillaire, *A Scalable and
Production Ready Sky and Atmosphere Rendering Technique*, EGSR 2020,
https://sebh.github.io/publications/egsr2020.pdf . Differences from the reviewed
implementations: squared-height/signed-square-root angle transmittance mapping,
Fibonacci quadrature, paired RGB aerial LUTs, asynchronous CPU SH with immutable
GPU sky/GGX publication, analytic phased lunar disk and existing Njin shader
bindings/target ownership/HDR. The initial single-only defaults remain compatible;
the full physical preset explicitly enables the new capabilities per view.

## Hi-Z được chưng cất từ renderer fl

Đối chiếu renderer fl cục bộ, HEAD `fb549b1119c040192d0f405bdd43b0048acabc72`;
ghim thêm SHA-256 của working files vì HEAD không mô tả được mọi sửa đổi cục bộ:

- `engine/shaders/cull.comp`: `2d7b3ff7b1ca6f4201247607b52126413edcc47944cb9c10ceaed1915c372965`.
- `engine/shaders/hiz_down.comp`: `c95d4539d44385812b0bfc0d01b941ff065a2fea872d29c69fc1520da8ad0171`.
- `engine/shaders/hiz_first.comp`: `54b7d3e7c44b30e060c799f0db21df9a6cfe83f0cb9a11299f6e1bc0208b4388`.
- `engine/src/Renderer.cpp`: `9bc543ff11fd509c2dfcfd3aab349e08d202fe1c2dd32258006f8564e8d7f09e`.

Đã đọc các file và notices: fl ghi đây là thuật toán từ niagara/Bevy, không
chép mã; snapshot không có LICENSE gốc để áp một license chung cho toàn bộ fl.
Njin dùng lại thiết kế max-depth pyramid, chiếu hộp bao và thử bốn texel, không
nhập nguyên file/driver. Mã mới dùng binding shaderc, render packet và allocator
bgfx hiện có; depth lấy từ occluder của frame hiện tại thay cho lịch sử hai pha.

Đã đối chiếu thêm ví dụ `37-gpudrivenrendering` của bgfx
`b2513038f4b753ebeced40821c193978daa0492f`: `cs_gdr_downscale_hi_z.sc`,
`cs_gdr_stream_compaction.sc` và `gpudrivenrendering.cpp` (Copyright 2018 Kostas
Anagnostou; BSD-2-Clause, license trỏ tới LICENSE của bgfx, đã đọc).
Njin dùng API indirect/buffer của bgfx; không nhập prefix-scan 1024 thread của ví dụ.
License bgfx đi kèm `third_party/licenses/bgfx-BSD-2-Clause.txt`.

Đợt mở rộng pool/model/caster/frustum/LOD đọc lại các source trên, cùng
`include/bgfx/bgfx.h` và `src/bgfx_compute.sh` tại cùng revision bgfx để dùng
dynamic buffers, shared atomic/barrier và indirect commands. Đã review
Bevy v0.18.0 `crates/bevy_pbr/src/render/mesh_preprocess.wgsl` (MIT/Apache-2.0,
chỉ tham khảo work item theo view và early/late culling). Không sao chép hay
dịch mã mới từ Bevy/fl/bgfx: pool và shader mới dùng API bgfx cùng công thức
LOD của Njin; không thêm mã bên thứ ba cần đóng gói.

Njin's existing dependencies retain their own licenses and notices in their
source distributions. This file additionally records renderer source adaptations.

## Android GPU occlusion compatibility

`cmake/njin_bgfx_occlusion_fix.cmake` patches bgfx
`b2513038f4b753ebeced40821c193978daa0492f`, guarded by revision and exact source
anchors. Reviewed `src/renderer_gl.cpp`, `src/glimports.h`, `src/glcontext_egl.cpp`,
`src/renderer_vk.cpp`, `src/config.h`, the public capability/buffer API and
example 37's Hi-Z shader. The affected files retain their bgfx copyright headers
and BSD-2-Clause terms; license read and preserved in
`third_party/licenses/bgfx-BSD-2-Clause.txt`.

Modifications: GLES capability detection and compute barriers reuse bgfx's
runtime ESSL 3.1 check and imported core entry points while retaining the ES 3.0
context baseline. Vulkan advertises indirect draws when first-instance support
exists, splits ordinary indirect submissions into single native draws when
`multiDrawIndirect` is absent, and keeps indirect-count support gated on
multi-draw. No GPU readback or native rendering path is added by this patch.
Vulkan's single-draw rule was checked against `vkCmdDrawIndexedIndirect`
(VUID `drawCount-02718`) in the Khronos Vulkan specification. Njin commands
already use firstInstance=0. Bevy v0.18.0 `mesh_preprocess.wgsl` was reviewed for
the per-view compute/storage contract; no additional Bevy source is copied.

## Bevy cascade stabilization

The cascade sizing and texel anchoring in
`src/engine/runtime/njin_shadow_portable.cpp` are adapted into Njin's C++/raymath
coordinate and atlas conventions from Bevy v0.18.0, `crates/bevy_light/src/cascade.rs`:

https://github.com/bevyengine/bevy/blob/v0.18.0/crates/bevy_light/src/cascade.rs

Bevy is contributed by the Bevy contributors and is dual-licensed under MIT or
Apache-2.0. Njin elects MIT for this adaptation. The upstream MIT license is
included verbatim in [third_party/licenses/bevy-MIT.txt](third_party/licenses/bevy-MIT.txt).
Retain this notice and license when redistributing this adaptation. The Android
regression packager embeds these files in the APK's license assets.

O3DE and Godot renderer files listed in `docs/renderer-reuse.md` are currently
design references, not imported source. Future copied or translated components
must add their source revision, file-specific notices and license here.


## SMAA

The portable SMAA 1x HIGH passes include the same unmodified `SMAA.hlsl` and
`Textures/AreaTex.h` / `Textures/SearchTex.h` used by Njin legacy, from
iryoku/smaa commit `71c806a838bdd7d517df19192a20f0c61b3ca29d`.
The bgfx adapter supplies shader-language macros, explicit level-zero sampling
and Njin's top-left texture convention. Upstream's MIT license and copyright
notices are preserved in `third_party/licenses/smaa-LICENSE.txt` and the fetched
source headers. The Android packager includes the license directory.

## fast_obj

The bgfx model loader parses OBJ/MTL files with fast_obj 1.3 by Richard Knight,
compiled unmodified from `extern/fast_obj.h` of the already pinned meshoptimizer
`v1.3` checkout (`src/engine/runtime/njin_model_obj.cpp`). No new dependency is
fetched. The upstream MIT license is preserved in
`third_party/licenses/fast_obj-LICENSE.txt` and in the source header; the
Android packager includes the license directory.

## meshoptimizer glTF decompression

`decompress_meshopt` in `src/engine/runtime/njin_model_portable.cpp` is
translated from `decompressMeshopt` in `gltf/parsegltf.cpp` of meshoptimizer
`v1.3` (Arseny Kapoulkine, MIT), the dependency Njin already pins. Changes:
C++ exceptions instead of cgltf result codes, raylib's allocator so cgltf
frees the buffers, and no colour filter (the bundled cgltf does not parse it).
meshoptimizer's MIT license is in its fetched source tree (`LICENSE.md`).

## bimg image decoding

`image_decompress_rgba8` (`src/engine/runtime/njin_renderer.cpp`) calls
`bimg::imageDecodeToRgba8` from the bimg revision pinned by the bgfx.cmake
wrapper; no bimg source is copied. bimg is BSD-2-Clause; its license ships in
the fetched source tree (`LICENSE`).

## Image files and glyph atlases without raylib

`src/engine/runtime/njin_image_io.cpp` reads image files with the standard
library and decodes them with `bimg::imageParse` from the same pinned bimg
(lodepng, stb_image, tinyexr inside bimg; BSD-2-Clause, public domain, BSD-3).
PNG and TGA are written with bimg; the BMP (BITMAPV4HEADER) and QOI
(qoiformat.org specification 1.0) encoders are Njin's own.

`font_bake_atlas` (`src/engine/runtime/njin_font.cpp`) is translated from
raylib's `LoadFontData` and `GenImageFontAtlas` (packMethod 0) in `src/rtext.c`
of raylib as pinned by `CMakeLists.txt` (tag `6.0` of
https://github.com/raysan5/raylib),
so the bgfx path bakes the same atlases without calling raylib. raylib is under
the zlib license, Copyright (c) 2013-2026 Ramon Santamaria (@raysan5); this
translation is an altered version of that source, so marked here. The glyph
bitmaps still come from stb_truetype v1.26 (public domain).

## IQM, VOX and M3D models

`src/engine/runtime/njin_model_formats.cpp` translates raylib 6.0's `LoadIQM`,
`LoadModelAnimationsIQM`, `LoadVOX`, `LoadM3D` and `LoadModelAnimationsM3D`
(`src/rmodels.c`, tag `6.0` of https://github.com/raysan5/raylib; zlib licence,
Copyright (c) 2013-2026 Ramon Santamaria): altered from the original, so marked here
(no GPU upload, Njin's own CPU structures). It compiles two single-header libraries
vendored unmodified from raylib 6.0's `src/external/`: `third_party/vox_loader/vox_loader.h`
(MIT, Copyright (c) 2021 Johann Nadalutti) and `third_party/m3d/m3d.h` (MIT, Copyright (C)
2020 bzt, https://gitlab.com/bztsrc/model3d), their licence headers kept; their symbols
are renamed with an `njin_` prefix where included.

## Dear ImGui for the tools

njin_inspector, njin_ui_editor and njin_anim_editor use Dear ImGui (Omar Cornut,
MIT), fetched at tag `v1.92.9-docking` of https://github.com/ocornut/imgui,
compiled unmodified with its SDL3 platform backend (`backends/imgui_impl_sdl3.cpp`),
and ImPlot (Evan Pezent, MIT) at tag `v1.0` of https://github.com/epezent/implot.
Their licences ship in the fetched sources. `src/tools/common/njin_imgui_host.cpp`
draws ImGui through Njin's renderer API; its texture requests and draw loop follow
`backends/imgui_impl_sdlgpu3.cpp` of the same ImGui tag (MIT), rewritten for Njin's
calls. rlImGui is no longer used.

## RmlUi and FreeType (the rmlui feature)

The rmlui feature compiles RmlUi (Michael R. P. Ragazzon and contributors, MIT),
fetched at tag `6.3` of https://github.com/mikke89/RmlUi: its core, the FreeType font
engine, the debugger and `Backends/RmlUi_Platform_SDL.cpp`, unmodified. Its render
interface (`src/engine/runtime/njin_rml.cpp`) follows the conventions of
`Backends/RmlUi_Renderer_GL3.cpp` of the same tag, written for Njin's renderer.

Fonts are rasterized by FreeType, fetched at tag `VER-2-14-3` of
https://github.com/freetype/freetype and built without zlib, bzip2, PNG, HarfBuzz or
Brotli. FreeType is used under the FreeType License (FTL), which asks a product that
ships it to credit it in its documentation, for example: "Portions of this software
are copyright (c) 2026 The FreeType Project (www.freetype.org). All rights reserved."
A game that links the rmlui feature carries that obligation. The licences ship in
the fetched sources (`LICENSE.txt` of RmlUi, `LICENSE.TXT` and `docs/FTL.TXT` of
FreeType).

## raylib types and raymath without raylib

`src/engine/runtime/njin_rl_own.inl` holds the raylib structs and enums Njin's
shared runtime uses, the raymath functions it calls, and `GetMeshBoundingBox`,
`GetModelBoundingBox`, the `GetRayCollision*` functions (rmodels.c),
`IsTextureValid`, `IsRenderTextureValid` and `GetPixelDataSize` (rtextures.c).
They are copied from raylib as pinned by `CMakeLists.txt` (tag `6.0` of
https://github.com/raysan5/raylib; `src/raylib.h`, `src/raymath.h`,
`src/rmodels.c`, `src/rtextures.c`). raylib is under the zlib license,
Copyright (c) 2013-2026 Ramon Santamaria (@raysan5). The copy is altered from
the original, so marked here: `RMAPI` became `inline`, only what Njin uses is
kept, and the struct comments are dropped.

Tiled's zlib and gzip layers (`src/engine/runtime/njin_level.cpp`) are inflated
with miniz (MIT, Rich Geldreich, RAD Game Tools and Valve Software), the copy
bundled with tinyexr in the pinned bimg and compiled into `bimg_decode`; no
miniz source is copied into Njin.

## miniaudio sound output

Njin's sound (`src/engine/runtime/njin_audio_backend.cpp`) is miniaudio 0.11.24
by David Reid, fetched at tag `0.11.24` of https://github.com/mackron/miniaudio
and compiled unmodified in that file, with its bundled `extras/stb_vorbis.c`
(Sean Barrett) for Ogg Vorbis. Both are public domain, with miniaudio also under
MIT No Attribution; their notices ship in the fetched sources. raylib is built
without its raudio module. The mixing follows raudio's (raylib 6.0, zlib): its
pan law and per-voice converters are reimplemented, not copied.

## xatlas lightmap unwrap

The lightmap asset tool calls xatlas unmodified, pinned to
`f700c7790aaa030e794b52ba7791a05c085faf0c` from `jpcy/xatlas`.
Checked `source/xatlas/xatlas.h`, `source/xatlas/xatlas.cpp` and root `LICENSE`:
MIT, Jonathan Young; their file headers also retain Thekla, NVIDIA/Ignacio
Castano and (cpp) Fast-BVH/Brandon Pelfrey notices. The combined file header
notices are preserved in `third_party/licenses/xatlas-MIT.txt` and shipped by
the existing Android license packager. No upstream code is translated or
modified; Njin adapts mesh declarations, progress cancellation, vertex remaps
and chart pages while keeping both material UV sets and source geometry intact.

## tinybvh

The CPU lightmap baker also calls the unmodified `BVH::Build`,
`BVH::IsOccluded` and `BVH::Intersect` APIs from the same pinned tinybvh
revision below for direct visibility and diffuse multi-bounce transport.
The MIT header of `tiny_bvh.h` and the root MIT `LICENSE` were checked;
`tiny_bvh_base.h` has no separate license header/override and is included by it.
No additional third-party code is translated or copied by this baker.

Lightmap reference audit: the local renderer snapshot's `Lightmap.cpp`
(SHA256 `32d424609b56e2de44e20f62bdff5aa125014efd1b4cb58b625013088b04b37d`),
`Lightmap.h`, `LightmapGpu.cpp` and sample were inspected. It has no accessible
Git revision/root license in this checkout; no source from it is copied.
Godot `4.5-stable`, `modules/lightmapper_rd/lightmapper_rd.cpp` (file MIT header)
was reviewed for atlas padding, progress/cancellation and storage boundaries;
no Godot source is copied. Njin owns the UV validation, rasterization, direct
and indirect integration, material sampling, dilation and asset format in
`njin_lightmap.cpp`.

The bgfx compute ray tracing (`light3d::ray_traced_shadows`) builds its BVH with
tinybvh by Jacco Bikker, fetched at commit
`c9be3103cc9693b456b0a0353927c11cac7d710f` (MIT) and compiled unmodified in
`src/engine/runtime/njin_raytrace_portable.cpp`. The traversal in
`src/engine/runtime/shaders/bgfx/cs_njin_rtshadow.sc` is translated from that
commit's `external/vulkanrt/shaders/tiny.comp` (GLSL to bgfx shader syntax,
stopped at the first hit for shadow rays). The MIT license is preserved in
`third_party/licenses/tinybvh-LICENSE.txt`; the Android packager includes the
license directory.

The pinned bgfx (BSD-2-Clause) is patched at configure time by
`cmake/njin_bgfx_surface_fix.cmake` and `cmake/njin_bgfx_rayquery_fix.cmake`
(Vulkan ray query extensions, native handle access and a compute dispatch hook);
the patched files keep bgfx's license header.

## Bộ nhớ tạm shader OpenGL/GLES của bgfx

`cmake/njin_bgfx_shader_stack_fix.cmake` vá `src/renderer_gl.cpp` tại bgfx
`b2513038f4b753ebeced40821c193978daa0492f` (revision được ghim bởi bgfx.cmake
`1af3832b87ad0588c05f1f0bd7ee17a7b14d2315`). Đã kiểm tra header license của
file và LICENSE tại revision đang dùng: BSD-2-Clause, Copyright 2010–2026
Branimir Karadzic. Bản license đi kèm trong
`third_party/licenses/bgfx-BSD-2-Clause.txt`.

Thay đổi của Njin: bốn buffer có kích thước theo chiều dài shader trong
`ShaderGL::create` dùng `bx::MemoryBlock` trên heap và được giải phóng khi hàm
kết thúc, thay cho `BX_STACK_ALLOC`. Giữ nguyên phép vá NDC, mã shader và
lifetime tới `glShaderSource`; không đổi driver hay kết quả rendering.
Backtrace trên Pixel 6a/GLES xác định stack overflow tại `patchNdcConstant` khi
shader lớn đang nằm trên SDLThread. Các header bản quyền upstream được giữ nguyên.

Wicked Engine (`turanszkij/WickedEngine` commit
`1be904f79e7bcaa450d3f419b0d034ade519aedd`, MIT) `shaders/raytracingHF.hlsli`
was reviewed as the design reference for software shadow rays
(`TraceRay_Any`); no Wicked Engine source is copied.
