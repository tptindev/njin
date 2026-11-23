# World rendering on SDL3/bgfx

Latest water/grass changes and platform results are recorded in the
[parity continuation](renderer-water-grass-parity.md).

The existing `terrain3d_*`, `grass3d_*`, `water3d_*` and `scatter3d_*` APIs now
submit through the Njin renderer in portable mode. The explicit legacy OpenGL
path retains its raylib/rlgl implementation. CPU heightfields, brush edits,
splat painting, collision queries, deterministic placement and Gerstner wave
queries are shared. This is a working subset, not complete legacy world parity.

## Implemented

- Relief hình học qua vertex texture fetch, lưới mịn gần camera, mip/triplanar,
  trọng số splat, fade, normal riêng và cùng displacement cho màu/depth/shadow.
  Giữ nguyên height query/va chạm; xem [phạm vi và kiểm chứng](renderer-terrain-relief.md).

- Terrain chunks, distance/quality LOD, skirts, frustum rejection, off-camera
  shadow casters, up to four splat-weighted albedo layers, normal maps and PBR
  roughness maps. Edits rebuild renderer-owned chunks; replacing/destroying
  submitted chunks is rejected before releasing their resources.
- Grass uses shared CPU placement and renderer GPU instancing. Blade geometry,
  root/tip/tint/ground coloring, curve, wind, flutter, distance fade and
  shadow casting are implemented. Ground-image versions and terrain edits
  invalidate placement caches. Empty cells are cached too.
- Grass far-cover shades terrain beyond blade reach using the first matching
  grass field, its tip/tint color, fade range, splat layer, slope and minimum
  height. It works when terrain is drawn before grass, as in the legacy path.
- Water uses the existing Gerstner displacement and normals, terrain-relative
  shallow/deep colors, clarity, animated ripples and shore/crest foam. Finite
  rectangular/elliptical lakes and a camera-following ocean patch are supported.
  Water uses the existing weighted transparency pipeline; enable `post3d::oit`.
- Pour and splash rings use the legacy normal-gradient, wave-train decay and
  churn/foam formulas in the fragment shader, with derivative-based filtering.
  Splash timing and the eight-slot replacement policy remain in the shared CPU
  world state. These are visual effects, as specified by the existing API.
- Ice calms the actual wave mesh to match water height/normal queries. Shallow
  water freezes first with ragged edges, frost mottling and snow drifts. Frozen
  coverage flattens detail normals, suppresses foam and becomes opaque. Lighting
  uses the portable material path; legacy sky reflections/glint styling are not
  reproduced until the sky/reflection paths are ported.
- Scatter uses the existing instance buffers and placement, near/far model
  choice and camera/distance culling.

No native bgfx objects enter ECS or gameplay APIs. World resources use the
renderer registry; context destruction releases remaining cached resources.
`RenderInstance` now has an additional default-initialized payload for built-in
grass shaders. Its binary layout changed: rebuild applications and engine
together. Ordinary instance callers do not need source changes.

## Limits

- Terrain splat weights are interpolated from mesh vertices. Each layer is a
  separate additive pass after the base pass; use HDR for lighting composition.
  This does not reproduce legacy fragment splat/triplanar sampling or LDR
  nonlinear blending exactly. Texture UVs project along world X/Z.
- All terrain chunk LODs are built eagerly; brush edits currently rebuild all
  chunks. Very large worlds need dirty-region rebuilds and streaming.
- Terrain snow blends drift coverage by slope and softens mapped normals.
  The existing imprint API preserves pressed/rim stamps and custom stamp shapes.
  Terrain shades the relief, tints the pressed region and reduces local snow
  coverage. The same normals and roughness feed the normal prepass.
  The marks affect shading only. Collision and height queries use terrain geometry.
  Portable uploads replace the full RGBA8 image when stamps change. Fade uses
  the existing centisecond timestamp and requires no per-frame texture upload.
  Set, move and stamp the region before queuing its terrain draws.
- Terrain layer detiling and scale variation sample the same offset pattern
  in albedo, normal and roughness. Glitter uses the existing sun glint
  shader and fades in pressed areas. The projection remains along world X/Z.
- Phong roughness maps explicitly throw; roughness maps require a PBR material.
  Relief đã triển khai trên bgfx. Height map cần RGBA8 chưa đóng atlas; relief
  kết hợp ray tracing báo lỗi vì BVH chưa chứa displacement.
- Camera-culled grass/scatter do
  not supply off-camera shadow instances. Grass wind has no deformation motion
  vectors; TAA/motion blur cannot yet track individual blade movement.
- Water reflection, refraction and caustics use the opaque HDR/depth snapshot
  from OIT. See [water optics](renderer-water-grass-parity.md#water-optics).
  The open sea (`size` {0, 0}) is legacy `make_ocean`'s rings round the camera
  out to 8 km (snapped to 2 m steps); each Gerstner wave fades between four and
  two vertices per wavelength (`water_displace` with a spacing), the per-wave
  form of Wicked Engine's projected-grid displacement fade (reviewed at
  `754848e`, `oceanSurfaceVS.hlsl`), so coarse far rings do not alias. Ripples
  are legacy's two noise slopes faded by the pixel footprint (they were an
  unfiltered sine that striped the whole surface). `ocean3d` checks coverage to
  the horizon and the far band's neighbour change on all renderers.
  CPU-generated water vertices are uploaded each frame. Wave motion vectors
  currently follow the existing dynamic-geometry path.
- Procedural sky/weather is still explicitly unavailable on this path.
- Material layer images must be unpacked textures. Derived roughness/grass
  ground images currently require RGBA8 input.

## Regression fixture

The 63-stage fixture checks the opaque-snapshot water optics, IBL fallback
and exposure, snow and imprints in two engine contexts. Its imprint controls
cover a fading mark across the timestamp wrap, clearing, moving the region,
reconfiguring its size, and lighting from the rim alone. The final stages
compare terrain tiling, scale variation and glitter against their controls.
The expanded fixture passed on Windows Vulkan, bgfx OpenGL, Direct3D 12
and Vulkan validation, on Linux Vulkan and OpenGL, and on Android Vulkan
and OpenGL ES.

The earlier 59-stage fixture passed on Windows Vulkan, bgfx OpenGL, Direct3D 12 and Vulkan
validation, on WSL Linux Vulkan and OpenGL, and on Android Vulkan and OpenGL ES.
The first Android Vulkan run hit the 900-second controller timeout during
its second context. The rerun with a 1,500-second timeout returned `PASS 0`.

Build `njin_portable_world_test`, then run:

```sh
ctest --test-dir build -R '^portable_world_' --output-on-failure
build/bin/njin_portable_world_test --renderer=vulkan --gpu-validation
build/bin/njin_portable_world_test --renderer=bgfx-opengl
build/bin/njin_portable_world_test --renderer=d3d12
```

The fixture compares GPU screenshots across paint/edit, stable caches, animated
instanced grass, animated water, LOD, scatter, resize, shadows, rejected water
features, pour/splash propagation and expiry, frozen geometry/query agreement,
snow cover and grass far-cover layer/height/reset controls. It checks
active-resource destruction and repeats two contexts.
Captures are stored in `build/bin/captures/world3d-<backend>.*.png`.
Normal/roughness maps share the tested material pipeline, but this fixture does
not isolate their visual contribution. Android exposes the same fixture as
`run_device.py --suite world3d`; platform verification is recorded separately.
