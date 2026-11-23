# Portable HDR post-processing

The SDL3/bgfx path implements `post3d_set`/`post3d_get` for SSAO, SSR, outlines, sun shafts, lens flare,
HDR bloom, FXAA, SMAA, TAA, camera/object motion blur, weighted transparency and GPU auto exposure, using the existing Njin public API and scene loop. The raylib post-processing
path remains independent.

The portable shaders adapt Njin's existing `modules/render3d_hdr.cpp` algorithms:
13-tap downsampling with a first-level Karis average, additive 9-tap tent
upsampling, normalized level energy, and Lottes FXAA. Bloom has **no brightness
threshold**. It mixes the original linear HDR radiance with the normalized
blurred chain, preserving a constant image's energy. Up to six float16 levels
are allocated, down to approximately eight pixels. `bloom_radius` controls the
tent footprint. As in the existing API, HDR bloom applies when `light3d::hdr`
is enabled.

The order is geometry/lighting/shadows → linear HDR scene → SSAO → SSR → TAA → outlines →
weighted transparency → sun shafts/flare → motion blur → bloom → GPU exposure adaptation → tone mapping and sRGB encoding → FXAA/SMAA → later 2D/UI. ACES, AgX, Neutral and linear tone curves
remain available. FXAA can run without enabling HDR in gameplay; the renderer
then uses a linear intermediate for the 3D pass. UI drawn after `end_3d` is not
filtered. Earlier 2D content is not preserved by this intermediate path, an
existing HDR limitation that still needs composition work.

All intermediate textures and framebuffers use engine handles. Resize releases
and recreates the cached chain and AA target. Destruction preflights resources
used by the current frame, so a failed same-frame resize cannot partially free
the chain. Feedback sampling and invalid parameters fail explicitly.

```cpp
auto light = njin::light3d_get(ctx);
light.hdr = true;
light.tonemap = njin::tonemap3d_aces;
njin::light3d_set(ctx, light);
njin::post3d_set(ctx, {.bloom = 0.08f, .bloom_radius = 1.0f,
                     .aa = njin::aa3d_fxaa});
```

`njin_post_test` checks constant-radiance energy, a real emissive halo, diagonal
edge filtering, disable/reset, preserved later UI, resize and resource counts,
atomic rejection of unsupported settings and two engine contexts.

```powershell
cmake --build build --target njin_post_test
ctest --test-dir build -R "^post_" --output-on-failure
```

Windows controls passed Vulkan, bgfx OpenGL, D3D12 and Vulkan validation. Linux
and Android require their own build/runtime results. The Android controller can
select `--suite post`; mobile controls check the post passes and two contexts,
while desktop controls additionally resize the native window.

The controls below distinguish implemented effects from platform validation;
they do not establish complete legacy/platform parity.


## SSAO

`post3d.ssao` now runs real screen-space ambient occlusion using sampled scene
depth and the camera's projection/inverse projection. It supports perspective
and orthographic cameras, 4–32 hemisphere samples, world-space radius, half/full
resolution and two depth-aware blur passes. The result modulates linear scene
radiance before bloom/tone mapping. Samples are aligned to depth texel centers
to avoid half-resolution reconstruction artifacts on planar surfaces.

The depth texture is an opaque borrowed target view. It is destroyed with its
target and cannot be released separately. bgfx owns all native format/barrier
handling; unsupported sampled depth reports an error. The independent legacy
OpenGL effects retain their existing depth implementation. Logical resource
statistics count the borrowed view as a texture handle, without claiming a
second native depth allocation.

`njin_ssao_test` checks contact darkening, unoccluded surfaces, isolated planes,
perspective/orthographic reconstruction, half/full resolution, disable/reset,
resize, target/depth ownership, resource counts and composition with shadows,
HDR, bloom and FXAA. It also checks a sloped, unoccluded shadow receiver for
self-shadowing. The fixture is available as Android `--suite ssao`.

SSAO is screen-space: off-screen or hidden occluders are absent. Thin geometry,
depth discontinuities and low sample counts still limit quality. It currently
modulates the lit scene, matching Njin's existing post effect, rather than
separating direct and indirect lighting into additional buffers.

## SSR

`post3d.ssr` and `material3d.reflect` control actual screen-space reflections.
The port reuses Njin legacy's growing ray steps, thickness tests, six binary
refinement steps and fog-color miss fallback. Reviews of
[Godot 4.5 SSR](https://github.com/godotengine/godot/blob/4.5-stable/servers/rendering/renderer_rd/shaders/effects/screen_space_reflection.glsl)
and [Bevy 0.18 SSR](https://github.com/bevyengine/bevy/blob/v0.18.0/crates/bevy_pbr/src/ssr/ssr.wgsl)
informed normal/roughness inputs, orthographic rays, roughness rejection and
miss handling; neither shader was copied into Njin.

A float16 normal/roughness/reflectivity target with depth is rendered from the
same prepared opaque packets as the forward pass. It reuses mapped normals,
metallic-roughness textures, alpha masks, geometry and instance buffers. Even
nonreflective occluders write this target; blending is disabled so zero
reflectivity cannot erase their depth or reveal a reflective surface behind.
Meshes remain alive until both passes have consumed their packets.

SSR runs in linear HDR before bloom/tone mapping. `ssr_steps` accepts 8–128,
`ssr_distance` and `ssr_thickness` use view/world units, and perspective and
orthographic projection are supported. `ssr_sky` preserves the public legacy
semantics: misses blend toward `light3d.fog_color`; setting it to zero keeps
existing scene lighting/IBL on misses. Edge/distance fading hides hard viewport
cutoffs. Roughness uses a small spatial filter and fade, with roughness >= .95
skipping SSR. This is not hierarchical, temporal, cone-traced or ray-traced SSR;
hidden/off-screen geometry cannot be reflected. No temporal denoiser is present.

`njin_ssr_test` checks colored reflections on otherwise neutral floor pixels,
perspective/orthographic views, zero-reflectivity occluders, roughness rejection,
miss/sky fallback, disable/reset, resize, resource counts, two engine contexts,
and composition with shadows/SSAO/bloom/FXAA. Android uses `--suite ssr`.
`njin_animation_asset_test --ssr` additionally exercises the user-supplied rig
and instanced animated crowd; its upload counter detects duplicate deformation
uploads across shadow, forward and reflection-mask passes.

```cpp
njin::post3d effects;
effects.ssr = 0.8f;
effects.ssr_sky = 0.0f; // keep existing lighting when the ray misses
njin::post3d_set(ctx, effects);
njin::begin_3d(ctx, camera);
njin::material3d floor;
floor.pbr = true;
floor.reflect = 0.8f;
floor.roughness = 0.15f;
njin::material3d_set(ctx, floor);
njin::draw_plane3d(ctx, {0,0,0}, {10,10}, {0.3f,0.3f,0.3f,1});
// Submit the other scene objects with their own material settings here.
njin::end_3d(ctx);
```

```powershell
cmake --build build --target njin_ssr_test
ctest --test-dir build -R "^ssr_" --output-on-failure
```

Current verification logs and platform-specific results are recorded in
[migration status](renderer-migration.md); shader compilation alone is not a
claim of runtime verification.


## Depth outlines and sun optics

`post3d.outline` supports depth silhouettes on the nearer surface, geometric
crease detection, width .5–4 pixels, line color/alpha and distance fading.
Normals are reconstructed from the nearest depth neighbors to avoid using a
far silhouette neighbor as a surface tangent. Sky/background pixels are left
unchanged. Perspective and orthographic outline cameras are supported.

`post3d.shafts` projects the directional sun into the perspective viewport,
uses opaque scene depth as a sky occlusion mask, then performs the existing
Njin 48-tap radial scattering at half resolution. Both intermediate targets
are RGBA16F, avoiding an 8-bit quantization step before bloom/tone mapping.
`shafts_length`, `shafts_color` and strength 0–2 retain their public controls.

`post3d.flare` measures the sun disc's visibility with 25 depth taps in a 1x1
texture, then adds five lens ghosts, the halo and sun glow. `flare_halo` controls
the ring independently. Behind-camera/off-screen sun and zero sun intensity do
not add light. A fully opaque occluder covering the sun suppresses the flare.
The directional sun must be above the legacy horizon cutoff. Sun optics with
an orthographic camera report an unsupported-camera error before opening a 3D
pass; outlines continue to support orthographic cameras.

These functions port the existing Njin `modules/post3d.cpp` math into the shared
shader, reusing bgfx target/depth resources and existing gameplay APIs. There is
no new graphics dependency. Scene composites use two cached float16 targets
with explicit ping-pong to prevent feedback; resize preflights and releases all
these targets together. Later 2D/UI is drawn after the effects. Sun colors are
converted to linear light, and the directional-light intensity applies to both
sun effects. The passes run before HDR bloom/tone mapping, even when gameplay
did not request `light3d.hdr` (a linear intermediate is still required).

Limitations: these are screen-space optics, not volumetric scattering or
physically simulated lenses. Hidden/off-screen occluders and transparent objects
are absent from the opaque depth buffer. Shaft masks use half resolution;
flare visibility currently has no temporal smoothing. These limitations do not
change the independent legacy renderer.
High-level effects into an explicit render target retain the existing portable
HDR restriction and report an error; this port does not add per-view effect
chains. The fixture's backend choices are `vulkan`, `bgfx-opengl` and `d3d12`;
legacy OpenGL is covered by its independent regression suite.

```cpp
njin::post3d effects;
effects.outline = .8f;
effects.outline_width = 1.5f;
effects.outline_crease = 50;
effects.shafts = .6f;
effects.flare = .3f;
effects.bloom = .04f;
effects.aa = njin::aa3d_fxaa;
njin::post3d_set(ctx, effects);
```

`njin_effects_test` uses 22 controlled scenes and two engine contexts: outline
width/color/crease/fade, orthographic outlines, shafts/flare visibility, depth
occlusion, behind-camera and zero-intensity controls, halo/strength, effect
reset, HDR-off rendering, resize, composition with SSAO/SSR/shadows/bloom/FXAA,
untouched later UI and resource counts. Android selects `--suite effects` and
omits only the desktop window-size changes.

```powershell
cmake --build build --target njin_effects_test
ctest --test-dir build -R "^effects_" --output-on-failure
```


## SMAA, GPU exposure and weighted transparency

`post3d.aa = aa3d_smaa` uses the original pinned iryoku/SMAA HIGH-preset
algorithm: color edges, area/search-table blending weights and neighborhood
resolve. All three stages are compiled by bgfx shaderc for each selected shader
profile. The original source and lookup bytes are retained; see the SMAA notice
in `THIRD_PARTY_NOTICES.md`. Weight alpha is data and uses opaque writes.

`light3d.auto_exposure` meters linear HDR entirely on the GPU. A center-weighted
log-luminance pyramid reduces 64x64 samples to 1x1. Two float16 history targets
adapt in log space using elapsed time and `auto_exposure_speed`; key/min/max
retain the existing API. Speed zero freezes an initialized history. First use,
resize, disable/re-enable and leaving HDR reset it. Manual exposure precedes
metering; the adapted factor applies before tone mapping. No pixel readback or
CPU synchronization is needed for metering.

`post3d.oit = true` enables weighted blended order-independent transparency.
Two real raster passes accumulate weighted HDR radiance/alpha and multiplicative
revealage. A fullscreen pass composites over the opaque scene. Transparent
fragments sample opaque scene depth for occlusion; they do not write that depth.
This supports textured glTF BLEND, tinted models and instancing. OPAQUE ignores
texture/factor alpha; MASK keeps its cutoff. A transparent draw without OIT
raises an explicit error. Weights use the existing legacy renderer's formula,
normalized by a common factor to preserve more float16 HDR headroom.

Weighted blending approximates intersecting translucent surfaces; it does not
implement sorted refraction, colored transmission or transparent layers in
SSAO/SSR. Transparent shadow casters use a 0.5 alpha-cutout approximation.
Later UI is not filtered. OIT adds two full-size HDR buffers and two geometry
submissions per transparent group, rather than duplicating instance uploads.

```powershell
cmake --build build --target njin_smaa_test njin_exposure_test njin_oit_test
ctest --test-dir build -R "^(smaa_|exposure_|oit_)" --output-on-failure
```

SMAA/exposure passed 8/8 Windows controls and 6/6 Linux software-driver controls.
OIT passed Vulkan, bgfx OpenGL, D3D12 and Vulkan validation on Windows. The Android
`smaa`, `exposure`, `oit` suites passed both Vulkan and GLES on the connected
Mali-T880, with two engine contexts and native/controller success checks. Mobile
suites omit desktop window resize. These are renderer test results, not a claim
of completed Apple/Web builds or cross-platform visual parity.


## Temporal AA and camera/object motion blur

`post3d.taa` enables eight-sample Halton projection jitter, reprojection and
linear HDR history. The renderer applies it to the first world 3D pass in each
engine frame. Two radiance/history targets and two depth-history targets are
owned by the renderer. The history filter reuses Njin's existing five-tap
Catmull-Rom sampling and YCoCg mean/variance clipping, checks previous view-space
depth, reduces history feedback for moving surfaces and bounds sharpening by
the current neighborhood. `taa_sharpen` retains its existing 0..1 control.

A separate depth-tested velocity pass uses current and previous object transforms
and, for animated models, previous deformed vertex positions. CPU skinning/morph
snapshots are immutable. The current pose upload is shared by forward, shadow,
mask and velocity draws; previous positions use a separate cached vertex stream.
Static meshes reuse their vertex buffer with a position-only layout. This does
not claim GPU skinning or eliminate the previous-position upload.

The velocity target's own depth is first initialized from the opaque scene,
so hidden moving geometry cannot contaminate camera-only surfaces. Uncovered
pixels reconstruct camera motion from sampled scene depth and current/previous
camera matrices. Model and procedural mesh draws have object velocity. Instanced
draws retain camera-only motion; this does not add per-instance, native SDF or
terrain velocity support. Transparent surfaces are composed after TAA and use camera-only motion
for subsequent blur. Transparent-layer temporal AA/refraction remains separate.

Draw order identifies repeated calls for the same model by default. For objects
whose draw order changes, provide a unique stable `draw3d_motion_id(ctx, id)`
before each model/mesh draw; it is consumed by that logical draw, including its
material submeshes. ECS model entities receive stable identities automatically
in a separate namespace. IDs contain no backend handles.

`post3d.motion_blur` gathers 2–32 samples using camera/object velocity and nearby
foreground motion. Strength is 0..1 and the screen-space footprint is bounded.
The stationary center uses point sampling; moving taps use a second linear
sampler for the same texture. This preserves an unchanged static image even
on the Mali driver, where linear sampling at interpolated pixel centers caused
small edge differences. No extra copy of the scene is needed for that sampler.

Resize, interrupted/nonconsecutive frames, long frame gaps, camera cuts and
feature toggles invalidate motion/history. Changes to manual exposure invalidate
TAA history. The default 0.25 s gap guard suppresses temporal reuse after a pause;
workloads consistently slower than four frames per second will also reset it.
Radiance, velocity and view-space depth history use float16; this is not a claim
of large-world temporal precision. Later UI is outside both effects.

```cpp
auto post = njin::post3d_get(ctx);
post.taa = true;
post.taa_sharpen = 0.25f;
post.motion_blur = 0.5f;
post.motion_blur_samples = 8;
njin::post3d_set(ctx, post);
```

```powershell
cmake --build build --target njin_temporal_test
ctest --test-dir build -R "^temporal_" --output-on-failure
```

The same Android fixture is selected by `--suite temporal`. It checks static
identity, subpixel TAA coverage, camera/object/bone/morph blur, disappearance,
stable IDs, an instanced opaque occluder, UI, repeated contexts and live resource
counts; desktop additionally checks window resize. Native Vulkan/OpenGL/D3D12,
Vulkan validation, Linux software drivers and Android need their own recorded
results. Offline Metal shader compilation does not establish Apple runtime
support. See the final migration checkpoint for the actual validation matrix.
