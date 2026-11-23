# Portable renderer batch — 2026-11-04

This batch integrates additional features into Njin's SDL3/bgfx renderer. It
does not complete all remaining renderer families. The existing public gameplay
APIs are retained; no native GPU types are added to components.

## Implemented paths

- Distance fog uses the public exponential-squared density and authored color.
  Toon shading supports 2–8 bands, threshold and softness. Both use the shared
  built-in material shader and work with existing mesh/instance submissions.
- Rim lighting, a direct-light clearcoat lobe and tangent-directed anisotropic
  GGX extend the built-in material path. Input validation is atomic.
- `post_fx_set` now runs grading, sepia, tint, vignette, CRT warp, pixelation,
  chromatic separation, scanlines, grain, blur, thresholded bloom and DOF in
  the portable world pass. The grading/particle formulas reuse the legacy
  engine sources. DOF supports camera-distance focus and world-radius focus,
  orthographic/perspective depth and haze. Depth is exposed only when needed.
- Effects run before screen-space UI. A world image is allocated even without
  a virtual resolution when effects require it. Ping-pong targets prevent
  read/write feedback, preserve composition order and resize with the world.
  UI canvas/scissor transforms also work on the added offscreen world target.
- 2D lighting supports ambient, point/spot/directional lights, temperature,
  falloff, HDR accumulation, three tone maps and polygon occlusion. Geometry
  collection reuses the existing occluder implementation. Soft shadows sample
  the source disc with eight deterministic rays.
- 3D particles reuse the existing CPU simulation, spawn/emit/count/clear,
  gravity, drag and lifetime behavior. The portable adapter submits billboards,
  velocity streaks, textures/atlas regions, alpha/additive/multiply blending,
  all five procedural particle looks and both ribbon looks. Depth testing is
  retained while depth writes are disabled, so particles do not occlude each
  other. Alpha particles are sorted within each burst.
- Volumetric scattering ray-marches color and scene depth with height falloff,
  moving density noise, Henyey–Greenstein phase, sun/local-light illumination,
  shadow-atlas sampling, extinction and an optional maximum step length.
  It is composed into HDR before bloom/tonemapping. A dedicated bgfx pipeline
  keeps the volume loop out of ordinary material shaders.

## Limits still explicit

- World terrain/water/grass/weather/sky rendering is not ported by this batch.
- Height/lightmap/under-material maps, transmission/refraction, snow, glitter,
  clay, vertex wind and other advanced material properties still reject use.
  Clearcoat environment/IBL integration is pending; anisotropy needs mesh
  tangents and rejects primitives/meshes without them. Its tangent-based lobe
  is compile verified; the image fixture exercises rim and clearcoat, not an
  authored anisotropic asset. This is not complete advanced-material parity.
- Toon quantization currently precedes shadow multiplication; the combined
  shadow/toon bands are not exact legacy parity.
- 2D sprite normal/material/emissive G-buffers, sprite silhouette/pixel shadows
  and spatial edge buckets remain pending. Their existing capability errors
  remain. Portable polygon passes reject more than 64 edges rather than lose
  shadows; the current path supports at most 64 lights. Soft shadow filtering
  differs from the legacy analytic disc integration.
- GPU particle simulation is not implemented. These 3D particles simulate on
  the CPU and render through bgfx. Bursts are not globally transparency-sorted,
  and independent per-particle OIT is not implemented.
- Volume currently evaluates at full resolution; half-resolution evaluation
  and depth-aware upsampling are pending. Up to 256 steps are allowed when a
  maximum step length is requested; that cap can exceed the requested stride.
  Volume is intended for world rendering, not an independent explicit target.
- Backdrop blur and custom camera post shaders remain separate pending paths.
- Enable world effects before `phase_render`; enabling a chain after world
  rendering began cannot recover an image of a previously direct window draw.

## Build and checks

```powershell
cmake --build build --target njin_renderer_batch_test
ctest --test-dir build -R '^renderer_batch_' --output-on-failure
build/bin/njin_renderer_batch_test.exe --renderer=vulkan --gpu-validation
build/bin/njin_renderer_batch_test.exe --renderer=bgfx-opengl
build/bin/njin_renderer_batch_test.exe --renderer=d3d12
```

The shared fixture captures 23 scenes, verifies visible controls, compares
polygon shadows on/off, checks UI isolation/canvas clipping, validates particle
cleanup and repeats initialization. Android exposes the same fixture with
`--suite renderer_batch`. Runtime evidence is recorded below. Other platform
support is not inferred from
bgfx's supported-driver list.

The shared test-asset copy is now one dependency target per build invocation.
Concurrent MinGW `POST_BUILD` copies into the same directory previously raced
when multiple fixtures linked together.

## Actual verification

- Windows final integrated fixture **4/4**, 44.41 s, Vulkan, bgfx OpenGL,
  D3D12 and Vulkan validation: `build/renderer-batch-verified-windows-tests.log`.
  The broader run passed **35/36**; its D3D12 volume failure was fixed and
  rerun successfully before this final 4/4. Do not label the earlier run 36/36.
- Linux final related shadows/materials/batch/unsupported suite **12/12**,
  200.21 s (`build/renderer-batch-linux-final-tests.log`), followed by the latest
  specialized-volume fixture **3/3**, 28.61 s
  (`build/renderer-batch-linux-verified-tests.log`). Earlier related shader/
  sprite/text/FX checks also passed 16/16; these are overlapping focused runs.
- Android final APK rebuilt and installed. Both `renderer_batch` suites report
  native `PASS 0` and controller confirmation under
  `build/android-device-captures/renderer-batch-verified-<renderer>/status.txt`
  for Vulkan and GLES on the attached Mali-T880 device.
- BGFX-OFF legacy OpenGL build succeeded and shadows/materials/sprite-FX/RHI
  **4/4** passed, 58.99 s (`build/renderer-batch-legacy-tests.log`).

The volume bug was a missing inverse-matrix binding after D3D compilation of a
comma-grouped matrix declaration whose first matrix was unused. Separate
declarations retain it. `cmake/check_bgfx_volume.cmake` now checks required
bindings before embedding every volume shader profile. Temporary diagnostic
colors were removed before the final runtime runs.

The fixture runtime-verifies point/ambient lighting and polygon shadows,
orthographic distance-based DOF, rim/clearcoat, plain homogeneous volume,
all five particle looks and glow ribbons. Spot/directional lighting,
perspective/world-radius DOF, authored anisotropic assets, flat ribbons and
the volume's noise/height/local-shadow combinations are implemented but still
need dedicated image controls. They must not be described as independently
runtime verified by the common matrix. Invalid/missing shader controls passed
separately: `build/renderer-batch-shader-errors-tests.log` (1/1).

## Main source files

- `njin_lighting_portable.cpp`, `njin_post_fx_portable.cpp`,
  `njin_particles3d_portable.cpp`: new runtime adapters.
- `njin_model_portable.cpp`, `modules/render3d.cpp`, `feature_3d.cpp`,
  `modules/particles3d.cpp`: validation, submissions, state/module integration.
- `njin_renderer.h`, `njin_renderer.cpp`, `njin_bgfx.cpp`: neutral packets,
  depth-write control, uniform/resource ownership and specialized pipeline.
- `njin_view.cpp/.h`, `njin_draw.cpp`, `njin_draw_portable.cpp`, `njin.cpp`:
  world/post/UI lifecycle, lazy DOF depth metadata and canvas/scissor handling.
- `shaders/bgfx/njin_legacy_post.sh`, `njin_light2d.sh`, `njin_particles.sh`,
  `njin_volume.sh`, `fs_njin_volume.sc`, `fs_njin.sc`, `njin_surface.sh`:
  shader implementation and shared surface extensions.
- `njin_graphics_device_gl.cpp` and runtime CMake: legacy shared-surface
  compatibility; root CMake and shader CMake: assets, fixtures and compilation.
- `tests/renderer_batch.cpp`, `tests/unsupported_scene.cpp`, Android wrapper,
  dispatcher/controller and CMake: integrated regression coverage.

Runtime files above are under `src/engine/runtime`, API packets under
`src/engine/api`, tests under `src/engine/tests` and Android files under
`examples/android_renderer`. The remaining migration is tracked in
`docs/renderer-port-backlog.md`.
