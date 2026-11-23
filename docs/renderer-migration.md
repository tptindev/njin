# Njin rendering: SDL3 + bgfx migration

For the proposed complete renderer, see [target architecture](renderer-target-architecture.md).
That design is separate from the implementation/verification status below.
The latest feature checkpoints at the end supersede older dated audit limitations.

## Current startup default (2026-11-04, latest decision)

SDL3/bgfx is now the default. `config{}.graphics_backend` is `GraphicsBackend::Auto`
and `NJIN_BGFX` defaults to ON for fresh CMake caches. Auto retains the existing
platform policy: D3D12 on Windows, Vulkan on Linux/Android, Metal on Apple and
OpenGL ES/WebGL2 on Web. The Apple/Web choices describe selection policy, not
new platform runtime verification. No unavailable driver silently falls back to
raylib. Explicit `vulkan`, `bgfx-opengl`, `d3d12`, etc. continue to override Auto.

Existing caches preserve explicitly stored options; enable bgfx and rebuild the
engine and its consumers with the matching toolchain:

```powershell
cmake -S . -B build -DNJIN_BGFX=ON
cmake --build build --target njin_rhi_slice njin_sandbox -j 4
build/bin/njin_sandbox.exe
build/bin/njin_sandbox.exe --renderer=vulkan
build/bin/njin_sandbox.exe --renderer=bgfx-opengl
```

An explicit `GraphicsBackend::OpenGL` / `--renderer=legacy-opengl` still selects
legacy raylib. `NJIN_BGFX=OFF` builds remain possible, but a default Auto startup
then reports that bgfx was not built; legacy must be explicitly selected.
This avoids a silent renderer change. The library dependency on raylib has not
been removed: CPU asset utilities, audio and retained legacy/tool paths still
use it. Default application window/render ownership is SDL3/bgfx.

Earlier checkpoints below that describe legacy as the default record the
previous decision and are superseded by this section.

Validation for this default switch used the existing GCC/w64devkit toolchain:

- Full engine/examples/tools rebuild succeeded (`build/default-bgfx-all-build.log`).
- No-override RHI scene (textured geometry, resource controls, two contexts) and
  sandbox both passed: **2/2**, `build/default-bgfx-startup-tests.log` (26.12 s).
  Both logs confirm `Njin bgfx renderer: Direct3D 12` and fallback disabled.
- Explicit Vulkan, bgfx OpenGL and legacy OpenGL smoke controls passed in
  `build/default-bgfx-tests.log`. Its first default check used the wrong log
  prefix/display-name spelling; the helper was corrected and both default tests
  were rerun successfully in the log above.
- BGFX=OFF build succeeded; explicit legacy startup, sandbox, invalid selection
  and the default-without-bgfx error passed **4/4** in
  `build/default-bgfx-legacy-tests.log` (10.90 s).

This switch does not claim that remaining legacy-only renderer features or tools
have been ported. They continue to report their existing unsupported diagnostics
on bgfx; they do not automatically force a raylib renderer.


## Architecture and scope

Previously the context/main loop, input and renderer followed raylib/rlgl and its
OpenGL resource stores. The legacy path retains that lifecycle. The portable
path uses the same Njin context, ECS phases, scene and gameplay modules, with the
platform and graphics ownership separated below.

Gameplay/ECS/Scene -> Njin Renderer API -> thin private GraphicsDevice adapter
-> bgfx. SDL3 owns the native window, events, text, mouse, touch and gamepads.
Bgfx owns its graphics context/device and resources. Raylib/OpenGL retains its
independent legacy lifecycle during migration; there is never a raylib window
and SDL window both owning the same native window.

`NJIN_SDL_GPU` is retired: enabling it produces a migration diagnostic. The old
`njin_sdl_gpu.*` and GLSL slice sources are retained as historical work, excluded
from the runtime build. The active SDL configuration has **SDL_GPU=OFF**.
Bgfx is the graphics abstraction; the existing private GraphicsDevice remains a
small compatibility adapter, not a second Vulkan-like RHI.

The existing `njin_renderer.h`, opaque texture/mesh/target IDs, CPU image loading,
legacy sprite/texture adapters, scene, input snapshots, fixed update, Jolt and Lua
integration were retained. No bgfx handle appears in the public API or ECS.

## Build on Windows (verified GCC/MinGW w64devkit)

Use CMake >=3.28, Ninja, Git and GCC with C++20 on PATH. The original local compiler
is w64devkit GCC 15.2.0. Dependencies use FetchContent: SDL 3.4.18 and pinned
bgfx.cmake commit `1af3832b87ad0588c05f1f0bd7ee17a7b14d2315`, which pins compatible
bgfx/bx/bimg revisions. Local source overrides are optional, not hardcoded.
Bgfx shaderc is built from source; Vulkan shaders do not depend on glslc.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ `
  -DNJIN_BGFX=ON -DNJIN_SDL_GPU=OFF -DNJIN_BUILD_RHI_SLICE=ON `
  -DNJIN_EXAMPLES=sandbox -DNJIN_BUILD_INSPECTOR=OFF `
  -DNJIN_BUILD_UI_EDITOR=OFF -DNJIN_BUILD_ANIM_EDITOR=OFF -DNJIN_PCH=OFF `
  -DNJIN_BUILD_MUTEX_TESTS=ON
cmake --build build --target njin_rhi_slice njin_model_test njin_sandbox njin_sdl_platform_test njin_rhi_unsupported_test njin_mutex_test -j6
ctest --test-dir build --output-on-failure -j2
```

For an existing cache, preserve its generator/compiler. The repository now defaults
Njin PCH OFF for GCC/MinGW to avoid the observed fixed-address PCH mapping failure;
other compilers keep the previous default. On GCC/MinGW, `NJIN_PCH=OFF` also
disables Jolt's separate PCH; a clean build reproduced its mapping failure before
this fix. Explicit `NJIN_PCH=ON` is still an opt-in. The primary cache also has
`CMAKE_DISABLE_PRECOMPILE_HEADERS=ON` and `NJIN_CCACHE=OFF`.
Njin retains ownership of executable Optimus/PowerXpress exports; bgfx's duplicate
exports are disabled, fixing the MinGW multiple-definition failure.

A legacy-only build uses `-DNJIN_BGFX=OFF -DNJIN_SDL_GPU=OFF`; it does not fetch/link
SDL3 or bgfx, or require shaderc. Jolt's optional GPU compute extensions are OFF
by default (`NJIN_JOLT_GPU_COMPUTE=OFF`): Njin uses CPU rigid-body physics, so those
extensions must not pull a separate HLSL/GPU SDK toolchain into engine builds.
`NJIN_JOLT_GPU_COMPUTE=ON` opts back into the dependency's extensions, which are
not integrated with Njin gameplay. CPU physics remains enabled.
The public enum/config additions require consumer
rebuilds. Standalone games continue using the generated prebuilt engine export.

## Backend selection

```powershell
./build/bin/njin_rhi_slice.exe --renderer=legacy-opengl
./build/bin/njin_rhi_slice.exe --renderer=vulkan
./build/bin/njin_rhi_slice.exe --renderer=bgfx-opengl
./build/bin/njin_rhi_slice.exe --renderer=d3d12
./build/bin/njin_sandbox.exe --renderer=auto
```

`opengl` remains an alias for legacy raylib/OpenGL to preserve previous callers.
`bgfx-opengl` explicitly selects bgfx OpenGL. Config uses `GraphicsBackend::OpenGL`
for legacy and `BgfxOpenGL` for the new GL backend. Other values are `Vulkan`,
`Direct3D12`, `Metal`, `OpenGLES`, `WebGL2`, `Auto`. Invalid input is rejected.

Auto policy is Windows D3D12; Linux/Android Vulkan; Apple Metal; Emscripten GLES.
Auto chooses a declared default, not a fallback chain. Bgfx `Init::fallback=false`
and a supported-renderer check prevent silent substitution or Noop rendering.
A compiled backend can still fail initialization on unavailable hardware/runtime.
Unrecoverable bgfx failures print diagnostics and terminate per bgfx's callback
contract; device-loss recovery is not implemented. Only one active bgfx context
is supported; repeated initialization is tested sequentially.

Set `config::graphics_validation=true` or use the integration scene's
`--gpu-validation` to request device debug/validation layers. Normal rendering
has it disabled. The Vulkan validation CTest enables it explicitly.

## Shader asset pipeline

`shaders/bgfx/vs_njin.sc`, `fs_njin.sc` and `varying.def.sc` are the shared bgfx
sources. Shaderc generates bgfx shader containers for each selected profile; these
are embedded by `embed_file.cmake`. Runtime assets use no absolute developer paths.
They are distinct from the retained legacy raylib GLSL shaders.

`NJIN_BGFX_SHADER_PROFILES` is a semicolon list:

| Format | shaderc profile | Intended backend |
|---|---|---|
| glsl | 430 | bgfx OpenGL |
| spirv | spirv | Vulkan |
| dx11 | s_5_0 | D3D12 (bgfx accepts DXBC shader containers) |
| metal | metal | Metal |
| essl | 300_es | GLES/WebGL2 |

Windows defaults to glsl/spirv/dx11; Linux glsl/spirv; Apple metal;
Android/Web essl/spirv. Missing a profile reports an error instead of changing the
renderer. Native builds compile shaderc unless `NJIN_BGFX_SHADERC` selects an
existing host tool; cross builds require
`-DNJIN_BGFX_SHADERC=<host shaderc executable>` and don't build a target-platform
shader tool. Invalid/missing shader inputs are covered by a CTest negative check.

## Renderer resources and compatibility

The same integration scene creates a texture, legacy ECS sprite, indexed rotating
perspective cube and depth/blending controls through Njin. It runs twice per
backend and exercises Jolt/Lua without changing gameplay for a native GPU API.
`RenderVertex`/image spans are CPU data. Persistent meshes/textures upload once;
immediate spans are copied using bgfx-owned memory before the call returns.
Context-owned IDs are monotonically allocated and reject stale/foreign references.
Native resources are independent for each backend/context and die before the
platform/window. Resource statistics count logical slots and persistent uploads,
not full engine VRAM usage or performance superiority.

`renderer_create_target`, `renderer_target_texture`, `renderer_begin_target`,
`renderer_end_target`, `renderer_destroy_target` provide fixed-size offscreen
color/depth targets. The target owns its borrowed texture handle. Nested targets,
feedback, sampling before first rendering, and destruction while active/queued
are rejected. Texture and target IDs have separate namespaces; the regression
scene keeps a second unrendered target live while drawing ordinary textures, and
checks that sampling that target is rejected. Each target transition becomes a
sequential bgfx view, preserving
main color/depth when resuming. Legacy GL uses existing raylib framebuffer APIs.
Window resize updates bgfx's drawable-size swapchain and view rects; explicit
fixed-size targets keep their size until destroyed/recreated.

`renderer_draw_mesh` uses column-major MVP and GL-style clip Z [-1,1]; the bgfx
shader adapts depth range to device caps. `renderer_draw_sprite` uses clip-space
coordinates. Existing ECS `sprite`, `texture_draw` and `texture_draw_ex` use
world/screen pixel coordinates, camera transforms, origin, negative scale/UV
flips, rotation, tint, alpha, sorting and point/linear sampling. The public API
remains shared with the raylib renderer.

`screenshot(ctx,path)` uses bgfx's screenshot callback and CPU RGBA conversion.
No desktop capture/control is required. The tests reload frame-90 PNGs and check
orientation, depth, half-alpha blending, direct texture adapters, offscreen
orientation and main depth retained across target transitions. Outputs are
`build/bin/captures/rhi-{legacy-opengl,vulkan,bgfx-opengl,d3d12,auto}.png` for the
backends actually run. Capture may stall requested frames; asynchronous recording
is not implemented.

## Platform matrix: implementation is not verification

| Platform | Planned/selected graphics | Current Njin status |
|---|---|---|
| Windows | D3D12 / Vulkan / bgfx OpenGL; independent raylib GL | Implemented; GCC/MinGW compile verified; Vulkan/bgfx GL/D3D12/Auto/legacy GL runtime verified |
| Linux | Vulkan / OpenGL | GCC 15.2 compile/link verified; X11/Xvfb runtime verified with Mesa llvmpipe software Vulkan, bgfx GL and legacy GL; Wayland runtime unverified |
| macOS | Metal | SDL Metal-view bridge written; manual CI compile job prepared; not locally compiled/run |
| Android | Vulkan / GLES | arm64/API 26 engine and SDLActivity APK compiled with NDK 29; Vulkan/GLES runtime verified on one Mali-T880 device including resume/portrait resize |
| iOS | Metal | Apple bridge source exists; application packaging/lifecycle and remaining raylib dependencies pending; not compiled/run |
| Web | WebGL2 | Backend enum/shader profile planned; canvas/browser main-loop/platform bridge pending; not compiled/run |

`.github/workflows/renderers.yml` is an opt-in `workflow_dispatch` Windows/MSVC,
Linux/GCC, macOS/Clang compile matrix. It has not been executed in this session.
It deliberately does not count hosted CI compilation as GPU runtime verification.
Existing default CI and legacy builds remain intact.

### Linux desktop build and run

On Ubuntu/Debian install the platform development packages:

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build git pkg-config \
  libasound2-dev libx11-dev libxrandr-dev libxi-dev libxcursor-dev \
  libxinerama-dev libgl1-mesa-dev libwayland-dev libxkbcommon-dev libudev-dev \
  libxss-dev libxtst-dev libdrm-dev libgbm-dev libegl-dev wayland-protocols \
  libdecor-0-dev libvulkan-dev
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DNJIN_BGFX=ON -DNJIN_SDL_GPU=OFF '-DNJIN_BGFX_SHADER_PROFILES=glsl;spirv' \
  -DNJIN_BUILD_RHI_SLICE=ON -DNJIN_EXAMPLES=sandbox \
  -DNJIN_BUILD_INSPECTOR=OFF -DNJIN_BUILD_UI_EDITOR=OFF \
  -DNJIN_BUILD_ANIM_EDITOR=OFF -DNJIN_PCH=OFF -DNJIN_CCACHE=OFF \
  -DNJIN_BUILD_MUTEX_TESTS=ON
cmake --build build-linux --target njin_rhi_slice njin_model_test njin_sandbox \
  njin_sdl_platform_test njin_rhi_unsupported_test njin_mutex_test -j4
SDL_VIDEODRIVER=x11 ./build-linux/bin/njin_rhi_slice --renderer=vulkan
SDL_VIDEODRIVER=x11 ./build-linux/bin/njin_rhi_slice --renderer=bgfx-opengl
SDL_VIDEODRIVER=x11 ctest --test-dir build-linux --output-on-failure -j2
```

This recipe normally builds native shaderc. The local WSL engine verification
instead used the already built Windows shaderc as an external host asset tool,
with the same Linux GLSL/SPIR-V profiles. Native Linux shaderc compilation was not
verified in this session. Use `NJIN_BGFX_SHADERC` to select a suitable existing host
tool. In WSL, keep source/build caches in the Linux filesystem and a Linux-only
PATH to avoid slow discovery through inherited Windows tool directories.

For a reproducible software-rendering check without a physical display:

```sh
sudo apt-get install -y xvfb xauth mesa-vulkan-drivers vulkan-validationlayers
SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 \
  VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
  xvfb-run -a -s '-screen 0 1280x720x24' \
  ctest --test-dir build-linux --output-on-failure -j2
```

The lavapipe ICD filename varies between distribution versions; select the
installed file under `/usr/share/vulkan/icd.d`. Software rendering proves this
runtime path works; it does not establish physical-GPU compatibility/performance.

For macOS, use a native compiler with the desktop options and
`-DNJIN_BGFX_SHADER_PROFILES=metal`. The Metal platform bridge and opt-in CI job
are written, but compilation and runtime on a Mac remain unverified.

For Android static-library cross-builds (verified), use the NDK CMake toolchain with
`ANDROID_ABI=arm64-v8a`, `ANDROID_PLATFORM=android-26`, `PLATFORM=Android`,
`NJIN_BGFX_SHADERC` pointing to the host tool, and profiles `spirv;essl`.
A complete Android static-library recipe from PowerShell is:

```powershell
cmake -S . -B build-android -G Ninja -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_TOOLCHAIN_FILE=$env:ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DPLATFORM=Android `
  -DNJIN_BGFX=ON -DNJIN_SDL_GPU=OFF "-DNJIN_BGFX_SHADER_PROFILES=spirv;essl" `
  "-DNJIN_BGFX_SHADERC=$PWD/build/bin/shaderc.exe" `
  -DNJIN_BUILD_EXAMPLES=OFF -DNJIN_BUILD_RHI_SLICE=OFF `
  -DNJIN_BUILD_INSPECTOR=OFF -DNJIN_BUILD_UI_EDITOR=OFF `
  -DNJIN_BUILD_ANIM_EDITOR=OFF -DNJIN_PCH=OFF -DNJIN_CCACHE=OFF
cmake --build build-android --target njin_engine -j4
```

Build the Windows host shaderc first with the desktop recipe. This recipe produces
archives. The SDLActivity regression APK and device controller are implemented in
[examples/android_renderer](../examples/android_renderer/README.md); follow that
recipe to package and test the same Njin scene on a connected device.

For future iOS integration use `CMAKE_SYSTEM_NAME=iOS`, `iphoneos`, arm64, the
host shaderc and `metal`. For future Web integration use Emscripten's `emcmake`,
`PLATFORM=Web`, the host shaderc and `essl`. The iOS/Web parameters remain
preparation only: the pending platform/package work
above must be completed before claiming support. Raylib CPU decoders/audio and
other old platform assumptions still need separating for mobile/Web builds.

## Additional migrated renderer features

The existing sprite module also renders tilemaps and CPU particle emitters through
Njin, preserving map-before-sprite-before-particle ordering within each layer.
Tiles use the existing cell/orientation/animation helpers, source margins/spacing,
flips, tint and camera transforms. Consecutive quads with the same texture/tint
submit batches of up to 1024 without reordering. Legacy GL retains its cached map
chunks. Bgfx builds visible geometry each frame and rejects grids exceeding two
million candidate cells. Orthogonal tiles, animation, negative cells and a sprite
over a map have capture assertions. Isometric/staggered/hexagonal geometry uses
existing helpers and painter order, but dedicated image coverage remains pending.

CPU particle spawning, movement and lifetime reuse the existing simulation module.
Textured particles and generated square/circle/soft-mask textures use portable
quads. `MeshDraw::blend` expresses alpha/additive/multiply without native handles.
Image checks cover textured particles and additive/multiply control swatches;
shape-particle image coverage remains pending. GL compute particles are rejected.

Public 2D rectangles, rounded fills/outlines, circles, lines, rotated rectangles
and triangles now submit CPU triangles through the same Renderer API. Alpha,
offset/scale, blend and clipping state are supported, including target-local clip
orientation. Bgfx shapes currently use tessellation rather than the legacy SDF
edge antialiasing; exact edge pixels are not guaranteed to match.

Fonts use raylib's CPU font rasterizer and atlas packer, then upload their RGBA
pixels through Njin. There is no raylib GPU font texture in bgfx mode. UTF-8 glyphs,
multiline metrics, wrapping and atlas release are supported. A maximum of 24 atlas
sizes per font retains the existing nearest-size cache policy. The current bgfx
canvas scales the design-size atlas: it does not yet rebake text at the physical
canvas pixel size as the legacy crisp-text path does.

The existing UI input/layout module is registered in the portable loop. Flat and
9-slice skins, labels, buttons, ring progress bars, clip and nested UI canvas
scaling use the shared drawing API. Tooltips and toasts retain the existing layout
and animation logic. Custom GLSL skin shaders and backdrop blur report unsupported
feature errors. Dialogue is drawn after game HUD, then toasts, screen flash and
scene fade in the same order as the legacy loop. Dialogue, fade and flash have
separate capture controls; broader widget interaction coverage remains pending.

The pinned bgfx D3D12 screenshot callback reads the last presented back buffer.
Njin submits/presents the requested frame first before requesting that capture,
so single-frame fades are captured consistently with Vulkan/OpenGL. Capture is
synchronous and can stall; it is not an asynchronous recording interface.

## Static 3D model migration

The existing `model_load`, `model_create`, material accessors, `begin_3d`,
`draw_model`, `end_3d`, `model3d` and `shape3d_render` now have a bgfx path.
The implementation is `njin_model_portable.h/.cpp`; no new gameplay component or
native handle was introduced. The legacy model loader/renderer remains separate.

The portable loader uses the repository's cgltf CPU parser rather than raylib's
GPU-uploading `LoadModel`. It supports static glTF/GLB triangle meshes, active
scene nodes, parent transforms, skip/only node filters, POSITION/TEXCOORD/COLOR
attributes, base-color images (external, base64 or GLB buffer views), UV-set and
texture transforms. Each mesh is limited to 65535 vertices; larger assets must be
split. Mesh geometry is registered once with Njin and uploaded lazily through the
selected device. CPU geometry remains available for bounds and ray picking.

The initial static-model milestone used **unlit, opaque/alpha-masked rendering**. To retain that appearance, set the existing
material flag explicitly when a glTF file does not use KHR_materials_unlit:

```cpp
auto model = njin::model_load(ctx, "assets/prop.glb");
for (int i = 0; i < njin::model_material_count(ctx, model); ++i) {
    auto material = njin::model_material_get(ctx, model, i);
    material.surface.unlit = true;
    njin::model_material_set(ctx, model, i, material);
}
// In phase_render:
njin::begin_3d(ctx, camera);
njin::draw_model(ctx, model, transform);
njin::end_3d(ctx); // also draws visible ECS models/shapes when camera.entities
```

Perspective/orthographic cameras and the existing CPU camera-shake calculation
are shared. Cube/sphere/plane primitives and unrounded box/sphere shape components
use cached triangle meshes. Portable sphere shapes are tessellated, not the
legacy per-pixel SDF implementation. Other SDF shapes/rounding report unsupported.

`RenderVertex::color`, `MeshDraw::alpha_cutoff`, `opaque_alpha` and `cull` express
vertex tint, cutouts and face orientation without GPU-specific types. They are
implemented in bgfx and the raylib GraphicsDevice adapter. Mirrored model
transforms preserve front-face orientation. Application albedo overrides are
borrowed: unloading the model releases its imported textures and meshes, never
the application's override texture. Unload models from update/shutdown, outside
an active render frame; the portable path rejects frame-time unload atomically.
The context owns primitive/white caches and also cleans up after failed frames.

Unported requests fail explicitly: textured PBR/advanced material maps, skin/morph animation,
alpha BLEND/fractional base-color alpha, compressed geometry, merged/LOD/combined
models, dynamic vertex updates, custom material shaders and advanced material
maps. Texture wrapping currently supports repeat; point/linear sampling is used.
Requested mipmap/separate min/mag filters report a warning and use the supported
base-level magnification filter. This is a temporary sampling-quality limit.

The dedicated engine test uses the same glTF asset and public APIs on every
backend. It checks hierarchy/filter bounds, CPU ray picking, normalized vertex
colors, texture orientation, MASK holes, mirrored culling, an ECS model and shape,
procedural meshes, depth, albedo overrides, failed-image rollback, unload ownership
and two sequential contexts. The integration fixture also checks vertex tint and
alpha discard/opaque output directly through Renderer API on all Windows backends.

This testing exposed two existing legacy failures: `patch` was used as a GLSL
identifier and prevented built-in 3D shader compilation; a negative model scale
kept the original cull face. The variable was renamed and mirrored model culling
now selects/restores the correct face. Positive-transform legacy rendering keeps
its existing path.

## Remaining rendering work

The bgfx model path now includes directional/ambient/local Blinn-Phong and constant metallic/roughness PBR lighting plus HDR tone mapping. Skeletal/morph animation, textured PBR/IBL,
terrain/world3d, instancing, GPU particles, post-processing, packed
atlases, sprite flash/dissolve, virtual
resolution and render scaling remain on raylib. Adapted unsupported entry points
and visible legacy 3D components reject use; not every historical API has been
audited. Arbitrary user GLSL and mipmaps are not exposed by this adapter.

The implemented milestones cover integrated desktop sprites, basic meshes and
static high-level models; full feature parity and complete platform support
remain pending. Next priorities are desktop matrix
verification on native GPU drivers and Apple hosts, the required legacy
3D/material features, and mobile/browser application lifecycle and packaging. Device-loss recovery, real gamepad/touch
hardware, exhaustive high-DPI/fullscreen transitions, leaks and performance still
need broader testing. No claim that bgfx/Vulkan is faster has been made.

## Implementation files

The main new files are:

- `src/engine/api/njin_graphics_backend.h`, `njin_renderer.h`.
- `src/engine/runtime/njin_sdl_platform.h/.cpp`, `njin_bgfx.h/.cpp`.
- `src/engine/runtime/njin_graphics_device.h`, `njin_graphics_device_gl.cpp`,
  `njin_renderer_resources.h`, `njin_renderer.cpp`.
- `src/engine/runtime/njin_draw_portable.h/.cpp`, `njin_render_capabilities.h`.
- `src/engine/runtime/shaders/bgfx/{vs_njin.sc,fs_njin.sc,varying.def.sc}`.
- `cmake/njin_bgfx.cmake`, `njin_bgfx_shaders.cmake`, shader/smoke/failure checks.
- `src/engine/runtime/njin_model_portable.h/.cpp`, `src/engine/tests/models3d.cpp`,
  static glTF regression assets, and `docs/renderer-target-architecture.md`.
- `examples/rhi_slice/main.cpp`, `src/engine/tests/sdl_platform.cpp`,
  `src/engine/tests/unsupported_scene.cpp`, `.github/workflows/renderers.yml`.

Updated integration files include root/runtime CMake, dependency/prebuilt exports,
config/backend headers, context/main loop/window/view/input/touch adapters,
texture/font/drawing APIs, sprite/UI/dialog/FX/scene modules, portable capability
guards in optional renderer features, the original sandbox, README and this guide.
Unrelated user work, including mutex/network changes, is preserved.

## Verification evidence

Windows uses the existing GCC/MinGW w64devkit 15.2.0 compiler. The complete suite
with the final visible perspective-cube fixture, UI/dialog/fade/flash and D3D12
capture correction passed **15/15 in 102.83 s** (`build/final-windows-tests.log`).
It includes Vulkan validation
(1200 frames per context), repeated initialization, resize, resource reuse/lifetime,
negative shader/backend/platform cases, SDL virtual input, Jolt, Lua and the mutex
test. D3D12 and Auto are now registered in CTest, using the same image-control
scene twice per backend.

A separate clean bgfx-OFF configuration under `build-legacy-pivot` also built and
passed **5/5 tests in 8.56 s** after the Jolt PCH fix. It fetches/links no SDL/bgfx
and requires no shaderc. Logs use `build/legacy-final-*.log`. Silent-sample audio
load/play/stop/unload and repeated audio initialization passed in both Vulkan and
legacy GL (`build/final-audio-*.log`); this does not verify audible playback.
The standalone sandbox previously built and ran against the prebuilt engine in
legacy GL, bgfx GL, Vulkan and D3D12 modes.

Android's complete `njin_engine` static-library target passed again after the
UI/dialog/FX and Jolt build changes (`build/android-final-build.log`), using
NDK 29, arm64-v8a,
API 26 and the host shaderc. ELF64/AArch64 objects were verified. This proves
cross-compilation, not APK packaging/linking, JNI lifecycle or device rendering.

Linux's native ELF64/x86-64 executables compiled/linked with GCC 15.2.0 in
Ubuntu WSL. The final fixture passed **12/12 tests in 61.01 s**, including the
1200-frame-per-context Vulkan validation test, Vulkan/bgfx OpenGL/legacy GL image
controls, SDL virtual input, shader failures, Jolt, Lua, sandbox and mutex tests.
The runtime was X11/Xvfb on Mesa 26.0.8 llvmpipe (LLVM 21.1.8), a software driver;
Wayland and Linux physical GPUs were not runtime-tested. The engine-generated
captures are copied to `build/linux-captures`.

The local Linux build reused Windows shaderc via an ignored host adapter that
stages shader inputs/includes into the workspace, invokes the actual compiler,
and copies the generated GLSL/SPIR-V containers back to the Linux build. It does
not substitute precompiled assets or change runtime rendering. An explicit
`NJIN_BGFX_SHADERC` disables redundant tool builds and is honored by negative
shader tests. Logs: `build/linux-configure.log`, `linux-build.log`,
`linux-final-build.log`, `linux-tests.log`. The local helpers/cache paths are
verification artifacts, not required engine source or runtime paths.

The local primary cache remains bgfx ON, SDL GPU OFF, shader profiles
`glsl;spirv;dx11`, PCH OFF, using the original compiler. Controlled capture pixels
match their assertions; whole PNGs need not be byte-identical across backends.
The opt-in remote CI workflow has not been executed. macOS/iOS/Web builds and
runtime are not verified on this host.


Static-model checkpoint (2026-11-03): Windows **20/20** passed in **194.96 s**
(`build/model-final-windows-tests.log`); bgfx-OFF legacy **6/6** passed in **58.73 s**
(`build/model-legacy-tests.log`); Linux software-rendering **16/16** passed in
**105.35 s** (`build/model-linux-tests.log`). Android's complete static target
compiled again (`build/model-android-build.log`), with no device runtime claim.
The model Vulkan validation log records enabled Khronos layers and is checked
for Validation Error/VUID diagnostics (`build/model-vulkan-validation.log`).
Additional shared material controls passed **5/5 Windows backends** in **41.35 s**
(`build/material-control-tests.log`). The legacy full-shader startup makes these
figures test durations, not performance comparisons between equivalent renderers.

Final importer guards/sampling diagnostics passed a further **4/4** Windows model
tests in **12.61 s** (`build/model-checkpoint-tests.log`). Linux reran the affected
model and shared material-control cases: **6/6** in **19.42 s**
(`build/model-linux-checkpoint-tests.log`). These targeted reruns follow the full
20/16-test suites above; they are not additional platform-support claims.


CPU image decoding now verifies that conversion actually produced RGBA8 before
copying or changing pixels. Compressed DDS input is rejected with a clear error
rather than being read as uncompressed memory. The regression uses a real DXT1
DDS and also checks failed glTF imports roll back their resources. Model capture
probes now scale with drawable resolution instead of assuming physical 640x480.
Affected model tests passed **4/4 Windows** in **51.59 s**
(`build/image-safety-tests.log`) and **4/4 Linux software rendering** in **5.97 s**
(`build/image-safety-linux-tests.log`). The Android static target rebuilt after
these changes (`build/image-safety-android-build.log`). These focused checks
follow the complete suites reported above.


### Constant emissive material (portable path)

The bgfx unlit model path now supports `material3d::emission`: RGB multiplied by
its `a` strength is added after the base texture/tint/vertex color and alpha test.
The same Renderer API field is implemented by the legacy GraphicsDevice adapter.
Emission defaults to zero and is reset on every submission. Negative or nonfinite
values are rejected. It does not affect alpha, depth or culling.

```cpp
auto material = njin::model_material_get(ctx, model, 0);
material.surface.unlit = true;
material.surface.emission = {0.0f, 1.0f, 0.0f, 0.5f};
njin::model_material_set(ctx, model, 0, material);
```

This is constant emission on the current LDR path: output can clamp. It does not
implement HDR, bloom, emissive textures, lighting or PBR. The new MeshDraw field
is appended for aggregate source compatibility; prebuilt engine clients must
rebuild against the updated header. Shaderc regenerates all selected shader
profiles from the changed fragment source. Tests cover additive color, subsequent
draw state reset and high-level model material submission.


Emission verification (2026-11-03): Windows affected model/material tests passed
**9/9 in 111.33 s** (`build/emission-final-tests.log`), including legacy GL,
bgfx GL, Vulkan, D3D12 and Vulkan validation. Shader failure and rebuilt sandbox
integration tests passed **3/3 in 2.66 s** (`build/emission-integration-tests.log`).
Linux software-driver tests passed **7/7 in 21.64 s**
(`build/emission-linux-tests.log`). Android's complete static target compiled
(`build/emission-android-build.log`); Android device rendering remains unverified.
The first Windows run mixed object layouts after a header edit during compilation;
all dependent targets were rebuilt before the successful final run.


### Directional lighting and mesh normals

Portable models and tessellated box/sphere/plane primitives now support the
existing lit material mode (`surface.unlit=false`). The common submission path
carries local normals, world position, inverse-transpose normal transform,
directional/ambient light and camera/specular parameters. Both bgfx and the raylib
GraphicsDevice adapter evaluate the same classic Blinn-Phong formula in the
current non-tone-mapped color space. Emission remains additive after lighting.
This is not a PBR or HDR implementation.

`light3d_set/get` is available on the portable path. Light state is snapshotted
at begin_3d, preserving the documented next-pass setter behavior. Shadow, fog,
environment, HDR, toon and ray-tracing requests fail explicitly before mutating
state. Point/spot sources through light3d_add remain unsupported; introducing them
requires preserving its documented order-independent draw behavior.

Procedural meshes without normals generate area-weighted vertex normals. glTF
normals are transformed by the node inverse transpose; missing normals generate
from the transformed triangles. Object normal transforms handle nonuniform and
negative scales. Singular lit object transforms are rejected. Primitive normals
are outward-facing, with +Y on the plane. Zero-area normals contribute no direct
light instead of producing a normalization NaN.

```cpp
njin::light3d sun;
sun.direction = {-0.4f, -1.0f, -0.3f};
sun.ambient = {0.15f, 0.15f, 0.18f, 1.0f};
njin::light3d_set(ctx, sun); // before begin_3d

auto material = njin::model_material_get(ctx, model, 0);
material.surface.unlit = false;
material.surface.specular = 0.25f;
material.surface.shininess = 32.0f;
njin::model_material_set(ctx, model, 0, material);
```

RenderVertex appends a normal and MeshDraw appends lighting/world/normal data;
existing aggregate initializers retain their defaults. Rebuild static engine
clients after the header change. No native backend object enters gameplay/ECS.
Shaderc rebuilds GLSL/SPIR-V/DXBC variants from source.


Lighting verification (2026-11-03): the affected Windows tests passed **7/7 in
41.80 s** after correcting fixture overlay/culling assumptions
(`build/lighting-fixture-tests.log`). The complete Windows suite then passed
**19/20 in 134.29 s** (`build/lighting-complete-tests.log`): one legacy model
capture was black. Its isolated rerun passed **1/1 in 69.80 s** with both context
initializations (`build/lighting-legacy-recheck.log`). The separate bgfx-OFF build
passed **6/6 in 79.34 s** (`build/lighting-legacy-tests.log`). The intermittent
Windows legacy black capture has not been explained; these results must not be
reported as a clean initial 20/20 run or a proven fix for that symptom.

Linux's complete suite passed **16/16 in 63.70 s**
(`build/lighting-complete-linux-tests.log`), using X11/Xvfb and software GL/Vulkan.
Android's final static engine target compiled (`build/lighting-final-android-build.log`);
no Android device runtime was exercised. Shader controls verify nonuniform normal
transformation, back-facing diffuse rejection, specular on black albedo, emission
and per-draw state reset. High-level model tests cover light state, rejected
unsupported requests, generated normals, mirrored/back-facing meshes and a lit
cube. After testing, the Renderer API header's descriptive comment was updated;
no executable code changed in that final comment edit.


### Portable PBR and HDR pipeline (2026-11-03)

The portable model path supports constant metallic/roughness PBR with GGX normal
distribution, correlated Smith visibility and Schlick Fresnel under the existing
directional light. This is direct lighting plus diffuse ambient; environment IBL,
metallic/roughness/normal/occlusion/emissive texture maps and advanced lobes remain
unsupported. Requested textured PBR glTF assets fail explicitly. Unlit and classic
Blinn-Phong remain available. Reference for the BRDF equations:
https://github.com/KhronosGroup/glTF-Sample-Renderer/blob/main/source/Renderer/shaders/brdf.glsl .

`model_load_desc::pbr=true` imports metallic/roughness factors and converts glTF's
linear base-color/vertex factors into Njin's authored sRGB color convention, so the
shader's linear pipeline reconstructs the original factors. glTF unlit materials
retain their established path. For correct linear lighting, select a tone mapper.
No-tone-map classic rendering retains its previous color behavior.

Linear, ACES fitted RRT/ODT, AgX and Khronos Neutral tone curves are shared with the
legacy engine's existing implementations. The portable adapter uses the same
shared surface source for GLSL and bgfx shaderc formats, embedded by CMake. Light
color is decoded separately from intensity; emission RGB is decoded separately
from its strength. Texture, tint and vertex color are decoded before multiplication.

```cpp
njin::light3d light;
light.tonemap = njin::tonemap3d_agx;
light.hdr = true;
light.exposure = 1.0f;
njin::light3d_set(ctx, light);
auto model = njin::model_load(ctx, {.path="assets/static.gltf", .pbr=true});
```

With HDR requested, begin_3d allocates/reuses an RGBA16F target with depth; end_3d
resolves radiance through the tone curve to the window before subsequent UI.
Resize replaces the target and releases the previous one. Unsupported formats
report errors; the backend is not silently changed. The same Renderer API can
create HDR targets directly in bgfx and raylib GraphicsDevice adapters. The API
keeps opaque resource handles; native formats/framebuffers stay private.

Current HDR scope is one fullscreen 3D pass, cleared from config.clear_bg_color.
Draw 2D/UI after end_3d: earlier window color is not copied into the HDR target.
Nested high-level HDR inside an explicit target is rejected. Swapchain depth is
not populated from the HDR target for subsequent separate 3D passes. Bloom,
auto-exposure and the remaining post3d pipeline are not implemented by this pass.
Unlit content retains its authored energy; exposure applies to lit geometry
before resolving. These boundaries are migration limitations, not full legacy
HDR/post-processing parity.

Resource tests now include two cached white textures (3D and UI) and one HDR target
color after model unload. Context teardown still releases those engine caches.
The HDR control stores red radiance 4 in RGBA16F, resolves through three tone curves,
and compares known output colors; a clamped RGBA8 substitute fails those checks.
High-level model tests verify PBR imports, PBR/HDR output, UI after resolve,
resize, resource retirement and repeated contexts. D3D12 readback validation now
allows omitted padding after the final row; the 800x600 resize exposed that valid
layout. SDL resize tests await bounded OS acknowledgement instead of assuming a
request is applied in one frame.

All five shader formats (GLSL, SPIR-V, DXBC, ESSL and Metal) compiled on the Windows
host. ESSL/Metal shader compilation does not verify Njin Web/iOS/macOS application
builds, packaging or device runtime. Logs: build/pbr-profile-*.log for those extra
formats; the normal build embeds GLSL/SPIR-V/DXBC and Android embeds ESSL/SPIR-V.


PBR/HDR verification checkpoint: Windows **20/20 in 159.14 s**
(build/pbr-verified-tests.log), Linux software rendering **16/16 in 66.87 s**
(build/pbr-final-linux-tests.log), independent bgfx-OFF legacy **6/6 in 51.98 s**
(build/pbr-legacy-tests.log). Android's complete static engine target compiled
(build/pbr-final-android-build.log). Extra Metal/ESSL vertex+fragment profiles
compiled with the pinned Windows shaderc (build/pbr-profile-*.log). Controlled
captures include model-*.png.hdr.png and .hdr-resize.png. The earlier legacy black
capture did not recur in this final suite; its original cause remains unexplained.


### Local lights and Android device checkpoint (2026-11-03)

Portable 3D commands are queued until end_3d. Manual light3d_add calls after
model draws therefore affect the complete pass. ECS light positions come from
transforms. Up to 16 point/spot lights support radius falloff and soft spot cones
in both classic and PBR shaders. Unsupported local shadow requests and larger
light budgets fail explicitly. Queued mesh/texture references cannot be deleted
before submission; failure paths release their retention state.

Affected Windows tests passed 8/8 (build/local-light-tests.log); the Linux suite
passed 16/16 (build/local-light-linux-tests.log). Subsequent desktop regression
exposed intermittent all-black legacy window capture and one Vulkan surface leak;
build/android-bridge-windows-tests.log was 18/20, not a clean suite pass. The
pinned bgfx surface cleanup fix in cmake/njin_bgfx_surface_fix.cmake releases a
surface even when minimized resize already destroyed its swapchain, initializes
the handle, and fails configuration if the expected upstream code has changed.
Current verification of this fix must be read separately from earlier results.

The Android APK now uses SDLActivity and shared SDL3, with EGL-only legacy GL
procedure lookup instead of unresolved GLFW symbols. SDL creates the window with
external graphics context ownership; bgfx resets its swapchain when the native
window changes on resume even at unchanged dimensions. Njin skips submission
while no native window exists.

The connected Mali-T880 device passed Vulkan and GLES runs of the common scene,
including two consecutive contexts. Both then passed background/resume plus
portrait resize with stable process IDs and engine capture pixel assertions:

- build/android-device-captures/vulkan-lifecycle/status.txt: PASS 0, controller_passed=True.
- build/android-device-captures/gles-lifecycle/status.txt: PASS 0, controller_passed=True.

This covers sprites/alpha, models/depth, local lights, PBR/HDR, unload and resize
on one arm64 device. Vulkan reports 1.0.82. Android validation layers, audio/input,
other devices/ABIs and production distribution remain unverified. Reproduce with
[the APK/device instructions](../examples/android_renderer/README.md). Earlier
archive-only checkpoints above describe historical evidence, not current scope.


The audio/device follow-up caught a genuine native shutdown race on Android:
raylib CloseAudioDevice destroyed its mixer mutex before ma_device_uninit stopped
its AAudio callback. The first audio-enabled Vulkan run aborted with FORTIFY
pthread_mutex_lock on a destroyed mutex; its trace is retained under
build/android-device-captures/vulkan-input-audio. The guarded dependency patch
cmake/njin_raylib_audio_fix.cmake stops/joins callbacks before releasing the lock.
After this fix both Vulkan and GLES passed silent PCM upload/play/unload,
background/resume, portrait rendering and injected touch press/motion/release
through the public Njin input API. Both also passed two sequential contexts with
audio enabled. Current proof: build/android-device-captures/{vulkan-audio-fixed,
gles-audio-fixed,vulkan-audio-two-contexts,gles-audio-two-contexts}/status.txt.
The controller now requires bgfx's log to confirm the requested backend.
This tests audio lifecycle; it does not assess audible output quality or gamepads.

The new Windows rhi_vulkan_surface_lifecycle test exercises minimize/restore,
checks the resumed capture, then destroys a second device while minimized under
Vulkan validation. The Windows suite passed 21/21 in 144.65 s before the audio
follow-up (build/android-final-windows-tests.log). Three repetitions each of the
legacy model and Vulkan validation tests passed (build/android-regression-tests.log).
The test executables now link njin::gpu like shipped examples; the Windows legacy
log confirms RTX 3050 instead of Iris Xe. The earlier intermittent Iris Xe black
window capture remains unproven fixed; changing GPU preference is not evidence
of a repaired Intel readback path.


Final Android/platform checkpoint: Windows **21/21 in 153.64 s**
(build/android-verified-windows-tests.log), Linux software rendering **16/16 in
67.34 s** (build/android-bridge-linux-tests.log). Before the final run the
Windows audio follow-up was 20/21, with the legacy capture failure reproduced.
Instrumenting that failure showed a minimized window with read/draw framebuffer
0, GL_BACK read buffer, no pixel-pack buffer, full viewport and all color channels
writable. It occurred on RTX too; it was not established as an Intel shader bug.
The windowed pixel fixture now restores its own native Windows test window before
rendering (GLFW may honor a launcher's initial show state). Five consecutive legacy
model runs passed afterward (build/legacy-visible-tests.log). Production legacy
capture now reports a visible/non-minimized-window requirement instead of saving
an invalid black window image. No gameplay GPU API was introduced by this fixture
preparation. The temporary GL state instrumentation was removed after diagnosis.

Android's unavailable-backend check explicitly rejected Metal with the diagnostic
`requested backend was not compiled: metal` and no successful renderer fallback
(build/android-device-captures/unavailable-metal/result.txt). bgfx's renderer-name
query returns Noop for an uncompiled renderer; the engine error now uses Njin's
requested backend name instead. Android device-loss recovery and validation layers
remain untested, and renderer feature parity remains unfinished.


The final independent bgfx-disabled legacy build passed **6/6 in 9.44 s**
(build/android-verified-legacy-tests.log), and the Android static target rebuilt
successfully after the final capture guard (build/android-verified-static-build.log).
The checked-out bgfx Git revision is b2513038f4b753ebeced40821c193978daa0492f as
pinned by bgfx.cmake; its upstream tracked/generated version.h reports f446c319
at runtime. That embedded version string is not the checkout revision.


### Raster shadows (2026-11-03)

The bgfx path now implements sun shadow boxes/1–4 cascades and up to four shadowed
point/spot sources through the existing high-level APIs. Point lights render all
six faces; mixed-size views share a packed RGB24-depth RGBA8 atlas plus hardware
depth. PCF, normal/depth bias, alpha MASK, cast/receive switches, shadow-only,
mirrored/moving casters and context ownership are implemented. Explicit target
and HDR passes resume without losing caller color/depth. See
[renderer-shadows.md](renderer-shadows.md) for architecture, settings, commands,
limits and the distinction from unported ray tracing/quality extensions.

Earlier checkpoints describing shadows as unsupported are historical. The old
model rejection controls now exercise unported fog and invalid spot cones; the
new shadow fixtures exercise actual rendering. The initial Linux failures caused
by those obsolete assertions and a cache-count expectation were preserved in
build logs; they are not presented as renderer regression passes.


Final shadow checkpoint after the Bevy-derived stable cascade adaptation:
Windows **26/26** (209.79 s), Linux software-driver **20/20** (105.10 s),
independent bgfx-disabled legacy **7/7** (18.41 s). Android Vulkan/GLES each passed
the complete shadow scene, resume/portrait and tiny-camera-translation controls;
the original model/audio two-context regression also passed on both.
See [shadow verification](renderer-shadows.md) for exact logs and limitations.
[Renderer-wide reuse decisions](renderer-reuse.md) cover O3DE, Bevy, Godot and
compatible bgfx sources across PBR/IBL, animation, batching, passes and post effects.
Bevy CSM adaptation notices/license are retained in THIRD_PARTY_NOTICES.md and the
Android APK. Other engine source inspections are candidates, not implemented
Njin features or new runtime dependencies.


### Textured PBR/IBL and GPU instancing (2026-11-03)

The portable renderer now supports normal/MR/AO/emission maps and UV1/tangents,
plus SH9 diffuse/GGX-prefiltered specular IBL through `light3d.environment`.
See [materials and IBL](renderer-materials-ibl.md) for color-space, sampling,
preparation costs and verification limits. Static model/cube/sphere/plane GPU
instancing uses the existing `instance_buffer_*`/`draw_instanced3d` APIs and
shares actual GPU buffers across shadow and forward passes; see
[instancing](renderer-instancing.md). See the next checkpoint for animation and bloom/FXAA; advanced materials and
the remaining depth/temporal post-processing chain are still incomplete.


### Animation, bloom and FXAA (2026-11-03)

Portable glTF animation now evaluates STEP/LINEAR/CUBICSPLINE channels, animated
ancestors, cross-fades, inverse-bind skinning and morphs through the existing
clip/pose and ECS APIs. CPU deformation produces an owned geometry snapshot;
its GPU upload is shared across shadow, forward and mirrored instance groups.
CPU picking with a pose follows the same geometry. See
[animation](renderer-animation.md) for correctness controls and performance limits.

`post3d_set` now supports HDR bloom and FXAA. The chain reuses existing Njin HDR
filter math, preserves constant radiance and later UI, and handles resize. See
[post-processing](renderer-post.md). The later SSAO/SSR checkpoint extends this. TAA/SMAA, motion vectors, OIT and
other depth/temporal effects still report unsupported settings. None of these
checkpoints establishes complete renderer or platform parity.


### SSAO and SSR checkpoint (2026-11-04)

Portable SSAO and SSR now run through `post3d_set` in the existing Njin main
loop. SSR adds `material3d.reflect` support, a mapped normal/roughness/reflect
mask from the actual prepared draw packets, sampled depth, growing-step trace,
binary refinement, roughness rejection/filtering and the public fog fallback.
Animation/instance uploads are reused for the mask, with opaque blending so
nonreflective occluders retain their depth. The original legacy path is retained.
See [post-processing](renderer-post.md) and [renderer reuse](renderer-reuse.md).

Verification for this checkpoint:

- Windows full engine/examples/tools build succeeded (`build/ssr-full-windows-build.log`
  and `build/ssr-final-windows-build.log`). SSR controls passed Vulkan, bgfx
  OpenGL and D3D12, plus a separate Vulkan validation run. Full regression passed
  **55/55** (`build/ssr-final-windows-tests.log`, 627.80 s); after the final
  input-validation/unlit-roughness edits, current SSR controls passed **4/4**
  (`build/ssr-current-windows-tests.log`, 38.90 s). SSAO/post/SSR control
  batch passed **11/11** (`build/ssr-controls-tests.log`).
- Linux X11/Xvfb Mesa software drivers: **41/41** full tests on the initial
  SSR integration (`build/renderer-ssr-linux-tests.log`), then **3/3** current
  SSR controls including Vulkan validation after the mobile sampling fix and
  input-validation changes (`build/renderer-ssr-current-linux-tests.log`).
- Android Mali-T880: SSR **PASS 0**, `controller_passed=True`, Vulkan and GLES,
  two engine contexts each. Final-source results are in
  `build/android-device-captures/ssr-current-{vulkan,gles}`; native build and APK
  logs are `build/ssr-current-android-{build,apk}.log`.
  An initial mobile regression exposed unwanted linear re-filtering of edges;
  the SSR source read now uses point sampling. The nonreflective control allows
  one 8-bit level for a float16 silhouette round-trip; material/roughness disable
  and reset controls still compare the whole images exactly.
- Independent BGFX=OFF legacy build and **8/8** runtime tests passed
  (`build/ssr-legacy-build.log`, `build/ssr-legacy-tests.log`).
- Current fragment shader compiles to Metal (`build/ssr-metal-fs.log`), which
  does not establish an Apple application build or runtime result.
- Supplied `person.glb`, `--quick --ssr`: six actual clips, cross-fade and an
  animated instanced crowd passed Vulkan validation and bgfx OpenGL. Geometry
  uploads remain six per frame despite shadow/forward/reflection-mask passes.
  The D3D12 GPU-validation variant was stopped after excessive runtime; this
  variant is not counted as a pass. The ordinary D3D12 real-asset run then
  passed both contexts (`build/person-ssr-d3d12-runtime.log`); all three desktop
  backends therefore have real-asset SSR/animation/instancing runtime evidence.
  Android Vulkan and GLES also passed this combined scene (`--suite person --ssr`),
  with explicit native SSR confirmation and two contexts per backend:
  `build/android-device-captures/person-ssr-{vulkan,gles}/status.txt`.

Remaining effects still report unsupported settings: TAA/SMAA, motion blur,
shafts/flares, outlines, OIT and auto exposure. GPU skinning, material mipmap
minification, advanced material lobes, culling/LOD and remaining platform
packaging/verification are also outstanding. This checkpoint does not mark
the renderer or cross-platform support complete.


### Depth outlines, sun optics and threepp review (2026-11-04)

Implemented through the existing `post3d` API:

- Depth silhouettes and geometric creases, width/color/alpha, distance fade;
  perspective and orthographic outlines.
- Perspective sun shafts: depth sky mask and 48-tap radial scattering at half
  resolution, with RGBA16F intermediates to avoid another 8-bit quantization.
- Lens flare: 25-tap depth visibility, five ghosts, halo/glow, independent halo
  control, and correct behind-camera/zero-intensity/opaque-occluder behavior.

The implementation reuses Njin legacy `modules/post3d.cpp` formulas and the
existing SDL3/bgfx device, depth, HDR and gameplay interfaces. It adds two cached
HDR composite targets with explicit ping-pong, half-resolution shaft targets,
and a 1x1 flare visibility target. Resize preflights all affected resources.
No new graphics dependency or alternate window owner was added.

`RenderPostProcess` retains its existing fields in their original order; new
fields are appended to preserve old positional aggregate initialization.
Downstream C++ binaries should be rebuilt against the updated headers/libraries.
The high-level `post3d` layout and calls are unchanged.

| Host/path | Actual evidence |
|---|---|
| Windows Vulkan / bgfx OpenGL / D3D12 | Full engine/examples/tools build and **59/59** regression tests (`build/effects-full-windows-tests.log`, 763.29 s). Final float16/API-preserving source: **4/4** effects controls, including Vulkan validation (`build/effects-api-windows-tests.log`, 79.90 s). |
| Linux X11/Xvfb software Vulkan/OpenGL | Full **45/45** (`build/renderer-effects-linux-tests.log`, 509.16 s); final float16/API-preserving source **3/3**, including Vulkan validation (`build/renderer-effects-api-linux-tests.log`, 31.37 s). |
| Independent legacy OpenGL, BGFX=OFF | Full build and **8/8** (`build/effects-legacy-tests.log`, 84.76 s); after the final header refinement, rebuilt RHI slice and **1/1** OpenGL smoke (`build/effects-api-legacy-tests.log`). |
| Android arm64, Mali-T880 | Native compilation succeeded. Float16 APK **PASS 0**, `controller_passed=True`, Vulkan and GLES, two engine contexts per backend (`build/android-device-captures/effects-float-{vulkan,gles}/status.txt`). Final header-order refinement also compiles (`build/effects-api-android-build.log`). |
| Metal | Current fragment shader compiles offline (`build/effects-metal-fs.log`); no Apple application/runtime claim. |

The broad regression runs preceded the final float16/API-order refinements;
focused checks cover those final changes. The effects fixture uses 22 controlled
scenes, including composition with SSAO/SSR/shadows/bloom/FXAA, unchanged later
UI, effect disable/reset, HDR-off rendering, resize and logical resource counts.

Source changes are concentrated in `njin_post_portable.cpp`, `njin_model.h`,
`njin_model_portable.cpp`, `njin_renderer.h/.cpp`, the bgfx/GL device uniform
bindings and `shaders/bgfx/njin_surface.sh`. `tests/effects3d.cpp`, CMake tests and
Android's `effects` suite provide the controls. See
[post-processing](renderer-post.md) for API examples and limits.

The new threepp review covers its renderer/material/view contracts and HDR-sun
policy, as documented in [renderer reuse](renderer-reuse.md). No threepp source
or runtime dependency was imported. Actual borrowed Bevy shadow code retains
its existing MIT notice; the new effects reuse Njin's own legacy algorithms.

Remaining portable work includes TAA/SMAA, motion blur/object velocities, OIT,
auto exposure, material mipmaps, GPU skinning, advanced material lobes and
culling/LOD, plus Apple/Web/platform packaging and target-specific verification.
Sun optics remain screen-space and perspective-only; they do not establish
volumetric lighting or complete legacy/platform feature parity.


## 2026-11-04 migration: SMAA, exposure, OIT and material mipmaps

The renderer default remains legacy OpenGL. This milestone ports missing effects
to SDL3/bgfx; it does not change backend selection or remove raylib.

The bgfx path now implements original three-pass SMAA, GPU log-luminance auto
exposure and two-pass weighted blended OIT through the existing `post3d` and
`light3d` APIs. See [post-processing](renderer-post.md) for ordering and limits.
The earlier checkpoint lists of remaining effects above describe those earlier
versions, not the current feature status.

Core glTF material textures now honor all six minification modes independently
of magnification and wrapping. Explicit mipmap samplers generate full immutable
RGBA8 chains on upload: albedo/emission filter in linear light and are stored
as sRGB bytes, MR/AO filter linearly, and tangent normals are renormalized.
Exact footprint integration retains odd NPOT edges and 1xN tails. Alpha averages
linearly; alpha-mask coverage preservation is not implemented. Undefined glTF
minification retains the prior base-level behavior; mipmaps are generated only
when requested. Compressed textures and RGBA16F material mip generation remain
unsupported with explicit errors. Existing HDR environment preprocessing is
separate and unchanged.

The internal engine texture API appends `RenderMipMode` to CPU image metadata
and `RenderMinFilter` to map sampling. Defaults retain prior behavior. Images
are copied, GPU resources remain owned by renderer handles, and native bgfx
objects do not enter gameplay components. Rebuild downstream C++ binaries when
updating these headers. `njin_mipmaps_test` checks GPU sRGB/linear/base-level
filtering, NPOT tails, normal normalization, a glTF sampler, cleanup and repeated
initialization. Tests run on Windows Vulkan/OpenGL/D3D12, Linux software Vulkan/
OpenGL and Android Vulkan/GLES; Metal/Web device verification is still separate.

Actual evidence for this milestone:

- Windows OIT+mipmap controls: 8/8, including Vulkan validation,
  `build/mipmaps-oit-windows-tests.log` (72.82 s), before the final base-only
  sampling control; the broader current-source regression run records that refinement.
- Linux SMAA/exposure/OIT/mipmap: 12/12, software drivers,
  `build/renderer-oit-mips-linux-tests.log` (170.35 s), before that refinement.
- Independent legacy BGFX=OFF: full build and 8/8,
  `build/migration-features-legacy-tests.log` (66.10 s).
- Android arm64: final mip sampling APK built successfully. Both OIT/mipmap
  suites returned native PASS 0 and controller_passed=True on both Vulkan and GLES:
  `build/android-device-captures/migration-{oit,mipmaps}-{vulkan,gles}/status.txt`.

TAA/object velocities and motion blur are now implemented through the same
Renderer API. Their dedicated controls and final validation results are described
in the checkpoint below and in [post-processing](renderer-post.md).


## 2026-11-04 final feature-port checkpoint

The requested missing effects are now implemented on SDL3/bgfx: SMAA, TAA,
camera/object motion blur, weighted blended OIT, GPU auto exposure and material
mipmaps. Gameplay continues to use `post3d`, `light3d`, model/texture handles and
existing draw calls. There is no second gameplay renderer API or SDL GPU path.
Raylib/OpenGL remains independently selectable and remains the default.

TAA and blur share a real velocity buffer. Static meshes reuse vertex positions;
animated models retain the previous CPU-deformed positions in a separate GPU
stream. Current-pose buffers are reused across forward/shadow/mask/velocity draws.
Opaque scene depth initializes the velocity target, including occlusion by
camera-only instanced geometry. Eight Halton jitter samples, depth rejection,
Catmull-Rom history filtering, YCoCg variance clipping and bounded sharpening
resolve TAA. Motion blur preserves an exact point-sampled stationary center and
uses a second linear sampler for moving taps. Explicit render-target passes do
not consume the first world temporal pass; rejected nested HDR requests also
leave that claim intact. The temporal fixture checks both cases.

| Configuration | Actual result and scope |
|---|---|
| Windows GCC 15.2 / w64devkit, SDL3/bgfx | Full engine, examples and tools build succeeded (`build/temporal-window-final-build.log`). All **79 distinct tests pass across the full run and final focused rerun**: the full run was 78/79 (`build/temporal-final-full-tests.log`, 1153.37 s), with only D3D12 temporal window resize failing. The fixture now uses the same native window restore handling as the other Windows fixtures; all **4/4 temporal controls** pass on final source (`build/temporal-window-final-tests.log`, 90.66 s), covering Vulkan, bgfx OpenGL, D3D12 and Vulkan validation. This is not a claim of a single clean 79/79 invocation. |
| Linux GCC / X11 / Xvfb | **21/21** post/animation/SMAA/exposure/OIT/mipmap/temporal tests passed (`build/renderer-temporal-linux-tests.log`, 371.39 s). After the final explicit-target guard, **3/3 temporal tests** passed (`build/renderer-temporal-guard-linux-tests.log`, 50.95 s). Mesa software Vulkan (lavapipe) and OpenGL (llvmpipe); engine code compiled natively, shaders used the configured Windows host-shaderc bridge. This does not prove a discrete Linux GPU or native Linux shaderc packaging. |
| Independent raylib/OpenGL, BGFX=OFF | Full build and **8/8** (`build/temporal-final-legacy-tests.log`, 76.92 s). The last portable-only target guard additionally compiles (`build/temporal-guard-legacy-build.log`). |
| Android arm64 / NDK 29 / Mali-T880 | APK built successfully. All five shared suites (SMAA, exposure, OIT, mipmaps, temporal) passed **both Vulkan and GLES**, with two engine contexts each and native `PASS 0` plus `controller_passed=True`. Final logs: `build/android-device-captures/final-{smaa,exposure,oit,mipmaps}-{vulkan,gles}/status.txt` and `migration-temporal-guard-{vulkan,gles}/status.txt`. No Android Vulkan validation-layer claim. |
| Metal shaders | All **9** main/instanced/SMAA/velocity/depth-init variants compile offline (`build/temporal-metal-*.log`); main fragment recompiled after the final sampler change. No macOS/iOS application or hardware runtime verification. |

The Android rerun caught a real stationary-center filtering difference on Mali;
point center plus a separate linear gather sampler fixes it without loosening
the static-image equality assertion. The Windows rerun keeps the resize assertion
and restores the fixture's native window before checking resize. CPU assets,
engine capture images and resource counts provide controls beyond successful
window creation or presentation. `git diff --check` also passed.

### Main source additions and changes

- Renderer API/resources: `src/engine/api/njin_renderer.h`,
  `src/engine/runtime/njin_renderer.cpp`, `njin_renderer_resources.h`.
- Devices and sampling: `njin_bgfx.h/.cpp`, `njin_graphics_device.h`,
  `njin_graphics_device_gl.cpp`, `njin_gl_sampling.h`, new `njin_image_mips.h`.
- High-level 3D/post: `njin_model.h`, `njin_model_portable.cpp`,
  `njin_post_portable.cpp`, `njin_shadow_portable.cpp`, `modules/render3d.cpp`,
  new `njin_temporal_portable.h/.cpp`.
- Shaders: `shaders/bgfx/fs_njin.sc`, `njin_surface.sh`, `varying.def.sc`,
  new `fs_njin_smaa.sc`, `vs_njin_velocity.sc`, `fs_njin_velocity.sc`,
  `fs_njin_depth_init.sc`.
- Build: root/runtime CMake, `cmake/njin_bgfx_shaders.cmake`,
  `.github/workflows/renderers.yml` compile targets.
- Controls: new `smaa3d.cpp`, `exposure3d.cpp`, `oit3d.cpp`, `mipmaps.cpp`,
  `temporal3d.cpp`; updated `post3d_portable.cpp`; alpha/mipmap glTF fixtures
  and their deterministic generators in `src/engine/tests`.
- Android: matching suite wrappers plus `main.cpp`, CMake/controller and README
  under `examples/android_renderer`.
- Notices/docs: original SMAA license/notice, root README, renderer post/material/
  migration/target-architecture pages.

### Build and select a path

For a fresh portable build, use the configuration options earlier in this guide,
including `NJIN_BGFX=ON`, `NJIN_BUILD_RHI_SLICE=ON` and a host shaderc for cross
compilation. Preserve the compiler/generator of an existing cache.

```powershell
cmake --build build -j 4
ctest --test-dir build --output-on-failure
build/bin/njin_rhi_slice.exe --renderer=opengl
build/bin/njin_rhi_slice.exe --renderer=bgfx-opengl
build/bin/njin_rhi_slice.exe --renderer=vulkan
build/bin/njin_rhi_slice.exe --renderer=d3d12
```

`opengl` selects legacy raylib; `bgfx-opengl` selects SDL3/bgfx OpenGL. The six
ported effects are exercised through Njin by the shared test targets, for example:

```powershell
cmake --build build --target njin_smaa_test njin_exposure_test njin_oit_test njin_mipmaps_test njin_temporal_test -j 4
ctest --test-dir build -R "^(smaa_|exposure_|oit_|mipmaps_|temporal_)" --output-on-failure
```

### Remaining boundaries and priorities

1. Package and verify the engine itself on Apple/Web targets and additional
   physical Linux/Android GPUs; bgfx platform support alone does not establish this.
2. Add GPU skinning and per-instance object velocity as optimizations/extensions;
   CPU skeletal/morph animation and GPU instancing already work.
3. Extend material lobes/transmission, transparent temporal/refraction treatment,
   visibility/LOD and large-world temporal precision, with dedicated controls.

Weighted transparency is approximate; transparent shadows use alpha cutouts.
Mips currently cover uncompressed RGBA8 material textures, with no alpha-coverage
correction. Temporal effects are first-world-pass effects; instanced draws have
camera-only motion. Earlier 2D content is not preserved through high-level HDR,
and nested high-level HDR into explicit targets remains an explicit error.
Arbitrary user GLSL is not automatically translated to other backends. These
limits do not remove the independently available legacy features.

Custom fragment packages now preserve the existing shader API on SDL3/bgfx; see
[shader assets, lifetime and limitations](renderer-shaders.md). This is separate
from translating arbitrary legacy GLSL or porting custom vertex/compute variants.

## Latest integration scope (2026-11-05)

The [integration batch](renderer-completion-batch.md) supersedes older capability
notes above for world terrain/grass/water, advanced maps/transmission, 2D
G-buffers/pixel shadows, GPU analytic particles, backdrop and clustered lighting.
It records exact limitations and separate runtime evidence; the complete legacy
feature ledger remains unfinished.
