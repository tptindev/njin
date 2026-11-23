# Text on SDL3/bgfx

`config::crisp_text` now implements the virtual-screen text pass on SDL3/bgfx.
Screen-space text keeps its logical coordinates and measured line width, but its
font atlas is baked at window density and drawn after the virtual image is
scaled. `smooth_ui` also bakes its immediate text at window density, including
the current draw-offset scale.

```cpp
njin::config config;
config.virtual_size = {320, 180};
config.crisp_text = true;
```

World-space text, explicit render targets and pixel fonts keep their immediate
path. Custom shader, flash/dissolve and non-alpha blend scopes also draw text
immediately so those scopes retain their effect. No native graphics resource is
added to the public font API.

Deferred lines retain their resolved font, offset, scale, alpha and logical clip.
Changing the default font affects later submissions. Unloading a referenced font,
changing its style, or invalidating its atlas with new characters throws while
deferred or submitted draws still use it; perform those operations before drawing
or after the frame. A failed operation preserves existing atlases.

The pass reuses the legacy rectangle-occlusion algorithm: opaque panels hide
earlier text and translucent panels blend its color. Rounded rectangles use
their bounding rectangle, as in the legacy path. Arbitrary images, geometry and
custom shader overlays are not tracked as text occluders. Use `smooth_ui` for UI
that needs immediate painter ordering for those overlays. Font atlas size/count
limits still apply.

The engine regression `njin_crisp_text_test` exercises crisp on/off, smooth UI,
supersampling, opaque/translucent panels, logical scissors, alpha/offset scopes,
font-density selection, default-font snapshots and atlas lifetime checks. The
Android harness exposes the same regression as `--suite crisp_text`.

## Runtime evidence (2026-11-04)

Windows crisp-text matrix: 4/4, including Vulkan validation
(`build/port20-crisp-text-windows-tests.log`). Linux focused text/debug/shader
regression: 20/20 (`build/port20-crisp-text-linux-tests.log`). Android text,
virtual-screen, gizmo and sprite-FX suites: 8/8 across Vulkan and GLES, native
`PASS 0` and controller confirmation under
`build/android-device-captures/resume-crisp-<suite>-<renderer>/status.txt`.

The broader Windows run passed 21 tests and encountered five sandbox TCP
restrictions in inspector tests. Those five inspector cases passed when rerun
outside the sandbox (`build/port20-crisp-text-inspector-unrestricted.log`).
These are separate runs, not a single clean 26/26 execution.


