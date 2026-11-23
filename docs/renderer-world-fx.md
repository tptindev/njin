# 3D flash, dissolve and camera masking

SDL3/bgfx supports the existing `fx3d_set`, `model3d::fx` and
`shape3d_render::fx` API. No gameplay component stores a GPU object.
Manual draws snapshot effects when queued; subsequent resets do not change
earlier draws. Each `begin_3d` starts with effects disabled.

The built-in material stages implement flash tint, world-space noise dissolve,
directional dissolve sweep, burn edge and camera-to-target sphere masking.
They reuse the legacy formulas, including the ground-preserving mask and its
4x4 ordered dithering. Dissolve zero preserves the object; one removes it
completely. Coverage is shared by color, shadow, normal/SSR and velocity
shaders. Camera masking affects color/normal/velocity coverage and preserves
shadow coverage. HDR flash and edge colors are converted to linear radiance;
LDR uses authored display colors. Effects also apply before OIT accumulation.

As documented by the public legacy API, custom game fragment shaders control
their own appearance and do not receive built-in 3D effects. Built-in GPU
instances share their draw's world-space effect settings; independent per-instance
FX records are not implemented. Advanced SDF shapes remain a separate port.

Portable settings reject non-finite parameters, colors outside [0,1], dissolve,
sweep or edge width outside [0,1], nonpositive grain and negative mask radius.
Invalid settings leave the previous state intact. The legacy path is preserved.

`njin_world_fx_test --renderer=vulkan` exercises manual/ECS draws, pass resets,
repeated contexts, endpoint and partial coverage, directional sweep, camera
masking, HDR, OIT, normal/temporal coverage and disappearance of a shadow caster.
The same fixture supports `bgfx-opengl`, `d3d12` and Android's `world_fx` suite.
Build profiles compile the shared coverage include with the engine shaderc.

This port covers the FX portion of backlog family 8. It does not complete
advanced material features or the complete renderer migration.

## Verification, 2026-11-04

- Windows FX matrix: **4/4**, 49.50 s, Vulkan/bgfx OpenGL/D3D12/Vulkan
  validation (`build/port20-world-fx-windows-tests.log`).
- Windows related shader/text/OIT/temporal regression: **21/21**, 265.42 s
  (`build/port20-world-fx-regression-tests.log`).
- Windows existing shadows: **4/4**, 85.31 s
  (`build/port20-world-fx-shadow-windows-tests.log`).
- Android arm64 APK built and installed; Vulkan and GLES FX suites both report
  native `PASS 0` and `controller_passed=True` in
  `build/android-device-captures/port20-world-fx-<renderer>/status.txt`.
- BGFX-OFF legacy build succeeded; legacy sprite FX and RHI **2/2**, 10.57 s
  (`build/port20-world-fx-legacy-tests.log`).
- Linux FX/shadow matrix: **7/7**, 124.53 s, including legacy OpenGL,
  bgfx OpenGL, Vulkan and Vulkan validation
  (`build/port20-world-fx-linux-final-tests.log`). Nine related text/OIT/temporal
  checks passed in the preceding run. That run's three FX process launches
  raced the fixture relink and returned permission errors; the completed
  binary passed all three in the final matrix. These are separate executions.

These are focused runtime checks. Metal, WebGL and other untested targets are
not runtime verified by this batch.
