# Sprite flash and dissolve migration

SDL3/bgfx now renders `flash_fx`, `dissolve_fx`, `sprite_flash`,
`sprite_dissolve` and `draw_dissolve_begin/end` through Njin's regular draw path.
The existing ECS components, update functions and public APIs remain in use;
there are no driver objects in gameplay components.

The portable draw packet carries flash RGB/mix, dissolve progress, source-texel
grain, seed, edge color and width. The built-in bgfx fragment stage reuses the
legacy patch hash and threshold formula. UVs drive the pattern, so cropping,
flipping, animation frames and packed atlas page coordinates follow the same
convention as the existing renderer. Flash and dissolve combine in one pass.
Alpha cutouts use the current draw blend mode, matching the legacy implementation.

Existing delta-based timers remove expired flashes, finish reverse dissolves,
retain fully hidden dissolved sprites or destroy their entity when requested.
The SDL3 sprite module now registers these same update systems. Screen flash,
shake and hitstop continue to use the existing frame effects service.

Manual dissolve scopes affect following built-in 2D draws, including images and
UI images, until `draw_dissolve_end`. They cannot nest or overlap custom shader
programs. Invalid finite-value inputs fail explicitly. The engine does not issue
OpenGL shader calls on this path.

Both fragment implementations now handle progress 0 and 1 explicitly. Previously,
`edge_width=0` combined with a patch hash of exactly zero could remove a patch
even at progress 0; the endpoints now match the documented fully shown/hidden
behavior. Intermediate appearance uses the original formula.

## Verification

`njin_sprite_fx_test` uses the public API and GPU screenshots to check solid and
fading flashes, flash+dissolve composition, partial/monotonic coverage, flipped
UV patterns, zero/one endpoints, reverse/expired/destroyed components, packed
atlas sampling, scope restoration and two contexts including supersampling.

Windows sprite-FX/custom-shader matrix: **8/8** in
`build/port20-sprite-fx-tests.log`, Vulkan/bgfx OpenGL/D3D12/Vulkan validation.
Android shader/sprite-FX/gizmo suites: **6/6** on Vulkan/GLES in
`build/android-device-captures/port20-<suite>-<driver>/status.txt`, each native
`PASS 0` and controller confirmation. Final Linux shader/FX/debug matrix: **14/14**, 34.67 s. BGFX-OFF legacy
sprite-FX/inspector/RHI: **3/3**, 11.97 s. The later focused Windows
model/target/shader/FX run passed **17/17**, 92.92 s. Details are in the backlog.

This ports 2D sprite fragment effects. The separate 3D entity sphere mask,
3D dissolve sweep, advanced materials, 2D lighting maps and post-processing
features remain tracked in their respective backlog groups.

Final Android APK was rebuilt and reinstalled after the last input-validation
and fixture changes: all six shader/sprite-FX/gizmo Vulkan/GLES suites passed
again (native `PASS 0`, controller confirmed). Work is paused at the user's request.
