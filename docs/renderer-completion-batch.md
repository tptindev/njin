# Portable renderer integration batch

The later [water/grass continuation](renderer-water-grass-parity.md) adds
pour/splash rings, ice/snow and far-cover. Its verification results supersede
the older scope below for those paths. Apple/Web work is currently deferred.

This batch implements the remaining basic world, advanced material maps,
transmission, sprite G-buffers/pixel shadows, analytic GPU particles, backdrop
and clustered-light paths through SDL3/bgfx. It does **not** establish complete
legacy feature parity or full platform coverage. The remaining world effects
are listed in [world port scope](renderer-world-port.md).

## Material maps and transmission

The existing `material3d` fields now support height/parallax occlusion mapping,
lightmaps, world-space projection, worn under-color/normal layers and PBR
transmission. They apply to models, primitives and instance draws. Texture
handles stay in the renderer registry, with queued-resource validation and
normal asset lifetime rules. Advanced layer images currently require unpacked
textures.

Parallax reuses the legacy 8–32 step ray/interpolation algorithm. It changes
sample UVs, not mesh silhouettes, physics, shadow geometry or deformation
motion vectors. World projection preserves the legacy orientation and wear
distribution. It blends offset samples with explicit gradients and carries
stains from the top texture into the exposed underlayer. Baked
light replaces ambient/environment lighting, reads UV1 with UV0 fallback and
supports sRGB RGBA8 or linear RGBA16F images. Mesh snow uses world-space patches,
up-facing normals and an uphill rim on alpha-masked UVs. Glitter uses world-space
crystals shaded by the sun and optionally by point/spot lights. The metal-only
mode uses the effective metallic channel, including the material map.
Environment-lit PBR also includes the clearcoat lobe using the same prefiltered
environment atlas.

Transmission reads the current opaque HDR scene through the existing OIT
pipeline: enable `post3d::oit`, use a PBR material, and set `transmission`,
`ior`, `thickness`, `attenuation_color` and `attenuation_distance`. The shader
uses Fresnel, a refracted screen lookup, foreground-depth rejection, five-tap
roughness filtering and Beer attenuation. It has the usual screen-space
limitations: no off-screen geometry or recursively refracted transparent
layers. The approximation is not path tracing or an exact rough BTDF.

With `model_load_desc::pbr` enabled, glTF imports transmission and thickness
textures along with IOR, attenuation and scalar clearcoat. Transmission
multiplies the factor by linear red. Thickness multiplies the factor by linear
green. Each map uses its own UV set, texture transform and sampler.
The volume draw shares sampler slots with secondary post input and parallax,
and validation rejects conflicts. Thickness uses Njin world units. IOR zero
compatibility mode and required clearcoat textures report explicit errors.
The fixture compares a masked region, a UV1 flip and attenuation with separate
thin and opaque controls.
These controls passed on Windows Vulkan, bgfx OpenGL and Direct3D 12, on Linux
Vulkan and OpenGL, and on Android Vulkan and OpenGL ES.

### OBJ models

`model_load()` documents `.obj` alongside glTF, and the bgfx path now accepts
it instead of refusing it. Parsing is in `njin_model_obj.cpp` with fast_obj
1.3, which is part of the pinned meshoptimizer checkout; the upstream
tinyobjloader-c was rejected because raylib already links a modified copy with
the same C symbols. The conversion follows the legacy raylib `LoadOBJ` /
`ProcessMaterialsOBJ` and Njin's slot setup so a game sees the same materials:
the mtllib's materials from index 0 (one default material without an mtllib),
`Kd` as the colour unless `map_Kd` is given, `Ke` kept in `emission_color` (it
only tints an emission map, as in legacy), `map_bump` as the normal map,
nearest filtering, flipped V, +Y normals where the file has none,
fan-triangulated faces, ignored polylines, and faces with an unknown `usemtl`
drawn with material 0. Groups split below the 16-bit index limit. Node
filters and `merge` do not apply to OBJ, as before.

The `obj-materials` fixture (advanced_material stage 33) checks the material
order, names, quantized `Kd`/`Ke`, the white colour of a textured material,
and the rendered Kd quad, the V-flipped texture and the unknown-material
fallback. Verified on Windows Vulkan, bgfx OpenGL, Direct3D 12 and Vulkan
validation, Linux (WSL software) Vulkan/OpenGL/validation and Android
(Samsung SM-A505F) Vulkan and OpenGL ES, PASS 0 with controller confirmation.
The fixture is not pixel-compared against the legacy renderer; parity rests on
the reviewed raylib/Njin conventions above.

### meshopt-compressed glTF

`EXT_meshopt_compression` and `KHR_meshopt_compression` (as produced by
gltfpack `-c`) are accepted, also when required. Each compressed buffer view
is decoded after `cgltf_load_buffers` into cgltf's per-view override with
meshoptimizer's vertex/index/sequence decoders and octahedral, quaternion and
exponential filters, translated from gltfpack's `decompressMeshopt`. The
legacy raylib loader does not decode these buffers, so this is new on bgfx.
`gltf-meshopt.gltf` (fallback buffer, KHR_mesh_quantization, octahedral
int8 normals tilted so a skipped filter changes the lighting) is drawn beside
`gltf-meshopt-plain.gltf` at stage 34 and must match it per channel within 3;
disabling the octahedral decode makes the check fail.

### Procedural sky (draw_sky3d)

`draw_sky3d()` no longer refuses the bgfx path. It updates the same world
state as legacy (sky parameters from `sky_params_of`, wind, wetness, cover
flag) and, with `drive_light`, the light of the current frame too, since bgfx
takes the light at `begin_3d`. `end_3d` draws the sky with the far-plane quad
the environment sky uses: a `RenderSkyEffects` packet (11 rows, display colours
as legacy) selects environment mode 3, and `njin_sky.sh` evaluates the legacy
`sky_glsl` (gradient, sunset glow, sun disc, fbm clouds on a plane, stars,
moon, fog), linearized and exposed for HDR output like the legacy sky.

The cloud value noise hashes its four cell corners in one vec4 instruction
stream, with the far corner taken as `floor(p+1)`: the scalar version showed
seams on Vulkan and Direct3D, where the SPIR-V/fxc optimizers contracted each
call or distributed `(i+1)*k` differently, so a shared corner hashed to two
values. The math is the legacy hash otherwise.

`sky_parity` renders an overcast day, a cloudy sunset and a starry night, each
lighting a cube through `drive_light`, on the legacy OpenGL renderer (the
reference, a CTest fixture) and on each bgfx backend; the bgfx frames must stay
within a mean of 3/255 with at most 0.5% of channels off by more than 12.
Measured on Windows: mean 1.05-1.12 (overcast), 0.24-0.31 (sunset), 0.01
(night). `advanced_material` stages 35-37 compare clear-sky pixels with the CPU
`sky3d_color()` and check clouds, night darkness and the driven light; they
passed on Android (Samsung SM-A505F) Vulkan and OpenGL ES, PASS 0. Rain and snow are refused with an explicit error until precipitation,
its cover map and wetness are ported.

### Ray traced shadows (bgfx compute)

The pinned bgfx has no ray tracing API (no acceleration structures or ray
queries; it only logs D3D12's raytracing tier) but has compute shaders, so the
first bgfx ray tracing is software tracing in a compute shader, the approach of
Wicked Engine's non-RTAPI path (reviewed at `1be904f`). The user chose to add
the hardware path (DXR/Vulkan ray queries through the device's native handles)
later behind the same pass.

Each frame with `light3d::ray_traced_shadows` and `shadows` on a compute
backend, `portable_ray_shadows` gathers the casters' world-space triangles
(built-in shapes, models with their posed snapshots, instances, terrain; not
grass, water, particles or transparent draws), builds a BVH with tinybvh's
scalar binned-SAH builder (`BVH_GPU` layout), draws the opaque draws' depth
from the camera in the shadow atlas's packed format, and calls
`renderer_trace_shadows`. The device uploads the BVH to compute-read buffers and
dispatches `cs_njin_rtshadow.sc`: per pixel it rebuilds the world position and
normal from the packed depth and traces `shadow_rays` rays over the sun's disc
(`sun_angle`) with any-hit traversal translated from tinybvh's `tiny.comp`;
a second dispatch applies the depth-aware `shadow_denoise` filter. Opaque draws
read the mask on sampler 10 (`MeshDraw::ray_shadow`, `u_njinRtShadow`) instead
of the sun's shadow map; transparent receivers keep the shadow map, as on the
legacy renderer. `RendererResourceStats::ray_traces` counts dispatched traces.

`raytrace3d` checks, on Windows Vulkan, bgfx OpenGL, Direct3D 12 and Vulkan
validation: `raytracing_available()`, no traces in shadow-map frames and traces
in ray traced ones, the traced shadow covering the shadow map's (overlap 0.98),
a penumbra only with `sun_angle` (0 vs about 1880 pixels), and the shadow of an
off-screen pillar outside a 5 m `shadow_range` that the shadow map misses
(about 2170 pixels). Without compute (bgfx's GLES 3.0 context on Android) the
test requires that no trace runs and the shadow maps are used. Linux (WSL,
lavapipe/llvmpipe) passes raytrace on Vulkan, bgfx OpenGL and Vulkan validation.
On a vivo Android 13 phone the Vulkan run reports compute ray tracing available
(overlap 0.980, penumbra 0 vs 7508 pixels, 8676 pixels beyond `shadow_range`)
and the GLES run falls back to shadow maps; both PASS 0 with the controller's
backend confirmation.

#### Phase 2: lamps, reflections, irradiance grid, shadow_lod, BVH reuse

The same pass now carries everything the legacy Vulkan ray tracing did, still
in compute shaders (`cs_njin_rtshadow.sc`, five stages), translated from Njin's
own legacy `modules/rt/*.comp`:

- **Lamps.** `portable_render_shadows` gives the first three shadowed lamps of
  a ray traced frame a channel of the mask (`RenderShadowData::ray_channel`,
  g/b/a; legacy `shadow.comp`). Rays go to points of the lamp's sphere
  (`light3d_source::source_size`). Receivers read the channel through the
  local light's fourth cone value; the lamps' shadow maps stay for transparent
  receivers. A frame with traced lamps and no sun shadows still traces.
- **BVH reuse.** The gathered triangles are compared with last frame's; an
  unchanged scene keeps its BVH, the device skips the upload and the renderer
  skips re-validating it (`RendererResourceStats::ray_bvh_builds`).
- **`shadow_lod` / `shadow_lod_gap`.** A static model's draw records the
  coarser LOD mesh (`model_lod_build`/`model_lod_load` levels) that its rays
  meet; rays then ignore hits nearer than `shadow_lod_gap`.
- **Reflections.** `light3d::ray_traced_reflections` is accepted on bgfx. One
  mirror ray a pixel uses closest-hit traversal; the hit is shaded as legacy
  `shade_hit` (draw colour from a per-draw material buffer indexed by the
  triangle's first-edge `w`, sun through its traced shadow, up to eight lamps,
  ambient or the grid, glow). PBR receivers replace the specular environment
  light by the traced light with legacy `rt_reflection`'s roughness weight
  (`reflection_roughness`); without an environment they add the split-sum
  specular share. What the rays see is the shadow casters' BVH, so draws with
  `cast_shadows` off are not reflected.
- **Ray traced irradiance grid.** `irradiance3d_set` with `ray_traced` is kept
  by the bgfx model store; every frame one workgroup per grid point updates the
  ambient cube and the 16 x 16 octahedral DDGI depth tile (tiles 256 to a row)
  from rays shaded as above, so light bounces frame after frame, with the
  legacy `gi.comp` history, inside-point marking and Chebyshev visibility. A
  fourth stage writes the grid's light at each pixel; opaque receivers use it
  in place of `ambient` (and of the environment's diffuse light) inside the box
  with its blend margin, a lightmap still winning. The captured (non traced)
  grid is not ported to bgfx and logs a warning.

All 16 fragment samplers were taken, so the device keeps one RGBA16F image of
the view three times on sampler 10: shadows, reflections, grid light
(`u_njinRtShadow[4..5]` say which parts are valid).

`raytrace3d` (15 stages) adds: the lamp's traced shadow over its shadow map
(overlap 0.969), the static scene's BVH built once across stages, a different
BVH with `shadow_lod` 2 and a sphere shadow of 1822 vs 1762 pixels from the
coarser level, a smooth metal floor reflecting the cube (928 reddish pixels),
and a red wall bouncing onto the floor through the grid (22355 pixels redder,
none greener). `unsupported_scene` now requires bgfx to report ray tracing and
accept the settings, refusing invalid ones. Windows 167/167 (raytrace on
Vulkan, bgfx OpenGL, Direct3D 12, Vulkan validation), Linux WSL 30/30
(raytrace, shadows, clustered, environment and the earlier suites).
Android (vivo, all archives rebuilt with the ray query change): Vulkan passes
(`PASS 0`, controller confirmed) on the compute traversal (the GPU has no ray
queries, 0 hardware dispatches) with the same checks (lamp overlap 0.969,
static BVH built once, 3712 mirrored-cube and 89600 redder pixels at the
phone's resolution); the GLES 3.0 run passes on the shadow-map fallback.

Remaining limits: the BVH is rebuilt on the CPU whenever anything moves (no
refit or two-level scene yet), up to 4 million triangles; SDF shapes, grass,
water and transparent draws do not cast or reflect; transparent receivers keep
shadow maps, the environment and `ambient`.

#### Hardware ray tracing (Vulkan ray query)

On bgfx Vulkan, when the GPU has `VK_KHR_ray_query` and the build found `glslc`
(Vulkan SDK), the tracing stages run on the GPU's ray tracing hardware:

- **bgfx patch** (`cmake/njin_bgfx_rayquery_fix.cmake`, applied to the pinned
  checkout like the surface fix, failing configure if upstream moved): enables
  `VK_KHR_acceleration_structure`, `_ray_query`, `_deferred_host_operations`,
  `_buffer_device_address`, `_spirv_1_4`, `_shader_float_controls` and
  `_push_descriptor` with their features only when the whole set is supported
  and njin asked before `bgfx::init`; exposes the native handles and a
  texture's `VkImage`/layout; and hands a compute dispatch of a chosen program
  to njin inside bgfx's own command buffer (after bgfx bound and transitioned
  the resources), resetting bgfx's cached pipeline afterwards. Nothing changes
  for a game that does not trace.
- **Kernels.** Stages 0, 2 and 3 of `cs_njin_rtshadow.sc` are compiled a second
  time by glslc with `NJIN_RT_HARDWARE`, where `njinRtTrace` is a
  `rayQueryEXT` (opaque, no culling, first hit for shadow rays) instead of the
  BVH loop; `njin_rt_hw.glsl` stands in for `bgfx_compute.sh` and
  `cmake/njin_rt_hw_source.cmake` moves the loose uniforms into one block. The
  shading, grid and denoise code is the same source, so both paths write the
  same images.
- **Scene** (`njin_raytrace_vk.cpp`): one bottom-level structure from the
  `b_rtTris` rows in their order (a primitive's index times three is its row,
  so the draw index and materials still resolve), one instance on top, built in
  bgfx's command buffer before the first query of a frame whose triangles
  changed; the old one is destroyed eight frames later. Uniforms and materials
  go in with `vkCmdUpdateBuffer`, descriptors are pushed
  (`vkCmdPushDescriptorSetKHR`), so nothing is overwritten while a frame is in
  flight.
- **API.** `raytracing_hardware()` says whether the traces use the hardware;
  `config::graphics_ray_query` (default on) turns it off;
  `RendererResourceStats::ray_query_dispatches` counts the dispatches.

Verified on Windows (RTX 3050 Laptop, Vulkan): `raytrace_vulkan` runs 32
dispatches through ray queries, `raytrace_vulkan_validation` passes with the
Khronos validation layer, and `raytrace_vulkan_compute` (the same test with
`--no-ray-query`) produces captures identical to the ray query run in all 15
stages (largest channel difference 0). Skipping the hardware dispatch while
reporting it handled breaks the shadow check (overlap 0.047), so the measured
images are the hardware's. The design matches tinybvh's pinned ray query
reference (`external/vulkanrt/shaders/tinyrq.comp`, `program_vk.cpp`: opaque
geometry, `PREFER_FAST_TRACE`, aligned scratch, an empty
`rayQueryProceedEXT` loop); no code was copied.

Not covered: Direct3D 12 (DXR inline ray tracing would need the same hook in
bgfx's D3D12 renderer, descriptor space in its heaps and DXIL compiled for
shader model 6.5), Metal and Android GPUs (they keep the compute traversal);
the BVH is still built on the CPU because its triangle order is what both
paths index, and a moving scene rebuilds the acceleration structure.

### Weather: rain, snow, cover, wetness, rain rings

Rain and snow from `draw_sky3d()` run on bgfx with the legacy shaders' rules.
The drops are one quad mesh and one persistent buffer of 9000 instances whose
random numbers are the legacy vertex shader's PCG hash evaluated on the CPU
(bit-identical integer steps), carried in the instance colour/data; each frame
only sets `MeshDraw::instance_count` and the mode-5 rows of
`u_njinWorldStyle`. `vs_njin_instanced.sc` places, spins and fades each drop as
`precip_vs_main`; the fragment branch `njinPrecip` is `precip_fs` (soft dots or
`weather3d_snow_flakes_set` cells, glints, HDR exposure). They are drawn after
the opaque draws with alpha blending and no depth writes, like particles3d, so
they do not need OIT.

The weather cover is a `RenderCoverData` packet built once per pass before the
opaque draws: up to 16 `weather3d_cover_set` boxes and, with `cover_auto` while
it rains or snows, a 256x256 top-down packed-depth map of the shadow casters
(36 m round the camera, snapped to texels), drawn like the shadow atlas. Drops
and wet terrain sample it on sampler 8 through `njinCovered` (legacy
`cover_glsl`); validation keeps that sampler free of transmission, clearcoat
maps and post inputs.

`weather3d::wetness` darkens terrain on the flat (dry under cover and snow) and
makes rough layers shinier, with legacy's specular/shininess increase. Legacy
darkens the display colour, so with linear lighting the factor is applied as
`linear(display(albedo)*k)`.

`water3d_desc::rain_rings` adds legacy `rain_rings` to the water normal. bgfx
water had no sky reflection (only the environment or the clear colour), so it
looked black where legacy shows the sky: water draws now carry a non-backdrop
`RenderSkyEffects` (this pass's `draw_sky3d` sky, or one made from the light as
`sky_params_from_light`) and mix it with legacy's Schlick Fresnel share; the
optics path uses it as its reflection fallback. The rest of the water body is
still the bgfx water model, not the legacy one.

`sky_parity` covers it with time paused (deterministic drops): rain under a
roof found by `cover_auto`, soft snow, shaped sparkling snow beside a cover box
(all within the strict pixel threshold; measured mean 0.00-0.08/255), wet
terrain (wet/dry ratio open 0.634 vs legacy 0.633, under the box 0.989 vs
0.990) and rain rings (difference image vs legacy: strength about 1.15x,
correlation 0.41 on Vulkan/D3D12 and 0.71 on bgfx OpenGL). Ring placement uses
a float hash that the SPIR-V/fxc and GL compilers round differently, and the
water body model differs, so the ring check is statistical.
On Android (vivo V2110, Android 13) `advanced_material` (stage 38 rain/snow with
the cover map; its negative control fails without rain) and `world3d` passed on
Vulkan and OpenGL ES, PASS 0 with controller confirmation.

### Compressed images

The legacy renderer gives raylib's compressed images (DDS DXT1/3/5,
KTX/PVR ETC1/ETC2/PVRTC, ASTC files) straight to OpenGL. The bgfx renderer only
has RGBA8 textures, so `renderer_load_image` (and through it `texture_load`)
and glTF/OBJ material images now decode the first level with
`bimg::imageDecodeToRgba8` from the bimg revision pinned with bgfx
(`image_decompress_rgba8`, `njin_image_decode.h`). This costs CPU time and
memory at load instead of native GPU block compression, which needs a
compressed `RenderImageFormat` and per-device format capability checks and
remains open. The DXT1 fixture (8x8, four solid blocks) must decode to exact
red/green/blue/white texels, and the glTF using it must load and release its
resources. Only BC1 is exercised by a fixture; the other mappings follow bimg.

### glTF anisotropy

`KHR_materials_anisotropy` strength and rotation are imported like the legacy
loader (rotation in degrees). The bgfx lobe needs tangents, so the factors are
dropped with a warning (or refused when the extension is required) for a
material used by a primitive without `TANGENT`; anisotropy maps are skipped
the same way. `gltf-anisotropy.gltf` checks both cases and draws the result.

### glTF clearcoat maps

`KHR_materials_clearcoat` textures are imported on the bgfx path: linear red
scales `clearcoatFactor`, linear green scales `clearcoatRoughnessFactor`, and
`clearcoatNormalTexture` (with its scale) perturbs the geometric normal for
the coat lobe only, as the Khronos specification requires. (Filament's
`clearCoatNormal` behaves the same way to our knowledge; its source was not
re-reviewed for this change.) Each map keeps its UV set, `KHR_texture_transform` and
sampler. The coat lobe is applied to the sun, local and clustered lights
(coat N·H and N·L) and to the environment term (coat N·V and reflection).

Sampler budget: all 16 bgfx samplers (the GLES 3.0 minimum) are already
assigned, and samplers 12-15 carry parallax/thickness/imprint, lightmap and
the under-layer pair. A built-in mesh draw only uses samplers 8 and 9 for the
opaque scene and transmission map of transmissive or water draws, so the coat
maps bind there. Identical views (same texture, UV set, transform and
sampler) share one sampler, so the common packed R/G texture plus a coat
normal fits. `rhi::ClearcoatBinding` resolves the bindings once for
validation and for the device. Conflicts are explicit errors:

- `renderer_draw_mesh` rejects coat maps on custom-shader, post, terrain or
  water draws, with transmission, a transmission map or post inputs, and
  more than two distinct bindings;
- `model_material_set` rejects transmission on a material that has coat maps;
- the glTF loader refuses coat maps combined with transmission or three
  distinct coat textures when `KHR_materials_clearcoat` is required, and
  otherwise warns and keeps the scalar factors;
- the legacy OpenGL device refuses low-level coat maps.

When no coat normal map is present, the coat still uses the base mapped
normal as before, so existing scalar-coat output is unchanged; the
specification asks for the geometric normal there, which remains open.

The fixture `gltf-clearcoat-maps.gltf` has two quads on black IOR-1 bases, so
only the coat reflects a strong head-on sun. The first packs coat R (left
column) and roughness G (bottom row) in one shared texture; the second uses a
V-flipped coat texture and a coat normal tilted on its right column, on two
bindings. Stage 31 is compared with a coat-zero control (stage 32) in every
quarter. `gltf-clearcoat-conflict.gltf` requires clearcoat maps together
with transmission and must be refused.

Verification: Windows Vulkan, bgfx OpenGL, Direct3D 12 and Vulkan validation;
Linux (WSL software) Vulkan, OpenGL and validation; legacy OpenGL regressions
unchanged. Android (Samsung SM-A505F, API 30): advanced_material suite, both
contexts, PASS 0 with controller confirmation on Vulkan and OpenGL ES.

## 2D lighting

Visible ECS sprites populate normal, MRA (metallic/roughness/AO) and linear
emissive targets in sprite/layer order. Albedo alpha masks their coverage;
missing maps restore the default surface over earlier sprites. Map/source
rectangles and atlas offsets follow the sprite. Normal directions retain the
existing API convention (green up, no rotation of normal vectors).

`light_occluder_pixels` rasterizes its mask into a renderer-owned color target
with a camera margin. Point, spot and directional light passes march this
alpha image on the GPU, including receiver/light-inside-caster handling and
eight source samples for softness. This first portable implementation marches
directly per receiver; it does not precompute the legacy angular shadow rows.
`shadow_columns` currently bounds march steps (64–4096, auto 2048). Large light
counts/high-resolution pixel shadows need profiling before selecting defaults.
The light passes use a dedicated fragment program; unrelated 3D/material
programs do not include the pixel-shadow marching loop.

`light_occluder_sprite` reuses the existing CPU silhouette tracing and polygon
shadow path. It reads renderer CPU images for silhouette tracing. The bgfx
path uploads RGBA32F edge tables with 32 angular sectors for point/spot lights
or 32 strips for directional lights. Each receiver checks its own bucket.
The binning accounts for the soft light's sampled width. A 4,096-edge bucket
limit and a device-bounded texture budget produce explicit errors when
exceeded. Manual drawing, tilemaps, particles and sprite FX still need
G-buffer coverage.

The 2D fixture compares hard, soft and directional shadows from 320 polygon
edges against no-shadow controls. The advanced-material fixture isolates
snow coverage, glitter from sun and lamps, the metal-only gate, and clearcoat
with only environment light.

```sh
cmake --build build --target njin_advanced_material_test njin_light2d_test njin_portable_world_test
ctest --test-dir build -R '^(advanced_material|light2d|portable_world)_' --output-on-failure
```

The Windows run passed 12 checks across Vulkan, bgfx OpenGL, Direct3D 12 and
Vulkan validation. The standalone raylib/OpenGL build passed four checks.
The Linux run under WSL passed nine checks on Vulkan and OpenGL, including
Vulkan validation. The subsequent untile/clearcoat change passed the three
Linux material checks again. On the Android test device, both Vulkan and
OpenGL ES passed the 320-edge fixture and the material fixture with the
untile/clearcoat controls.

## Signed-distance shapes

`draw_shape3d` and `draw_sdf_blend` reuse Njin's analytic signed distances
and polynomial smooth minimum. On bgfx, a bounded CPU grid is polygonized into
triangles with normals derived from the distance gradient. A context retains up
to eight recent local meshes. Each frame uses one upload per shared mesh across
the regular draw and shadow views. Rigid transforms of primitive shapes reuse
their local mesh.

`material3d::clay` perturbs color and the shading normal with a group-local
noise field. The shader fades fine grain as its projected size falls below the
pixel footprint. Clay leaves the silhouette, depth and collision geometry intact.

The mesh grid uses up to roughly 64 cells along the longest axis. Very thin
parts and extreme close-ups can reveal the grid resolution. Changing blend
parts can trigger retessellation and resets their motion correspondence.
Legacy OpenGL retains per-pixel ray marching. The material fixture checks
the rounded box, cylinder, torus, smooth union, clay and cast shadows.
The Windows SDF/material, world and temporal matrix passed 13 checks.
The independent legacy OpenGL build passed four checks. The Linux material
fixture passed three checks after increasing its timeout for software rendering.
The Android Vulkan and OpenGL ES material runs both returned `PASS 0`.

## GPU particles

The existing 2D `particle_emitter` GPU path now evaluates its analytic
velocity/gravity/exponential-drag trajectory in the bgfx instanced vertex
shader. It supports individual spin/lifetime/size, local-space emitters,
circle/square/soft shapes, textured atlas source rectangles, color interpolation
and the existing blend modes. CPU spawn state is converted only after spawning,
compaction or clock rebasing. Immutable instance snapshots retain GPU buffers
across frames; weak ownership releases unused buffers on a subsequent frame
or device destruction. This is vertex-shader simulation, not compute dispatch;
3D particles retain their existing CPU simulation.

`particle_backend_cpu` remains available. A live emitter retains its backend
until empty, as before. The implementation shares `particles_gpu_step` and
the existing spawn/RNG/compaction logic, rather than changing gameplay.

## Backdrop and presentation

`draw_backdrop_blur` now works in the portable screen/UI pass, including
rounded masking, tint/alpha, clipping, drawing offsets, virtual resolution,
resize and overlapping panels. It snapshots the current GPU color surface,
uses the existing separable blur shader at half resolution, then composites
the masked rectangle. It performs no CPU readback or desktop capture.

The bgfx device now owns an intermediate window color/depth surface and an
opaque final presentation copy. This adds one full-window copy per frame and
window-sized attachments, allowing a snapshot on any backend without trying
to sample a native swapchain image. Backdrop scratch targets are full/half
surface-sized and cached; this implementation has not yet optimized partial
rectangle copies. Call from `phase_post_render`/UI after `end_3d`.

## Clustered lighting

`light3d::lights_max > 16` enables the shared CPU cluster builder: 16 × 9 screen
tiles, 24 logarithmic depth slices (linear for orthographic cameras), up to
1024 local lights. The legacy renderer calls the same extracted CPU algorithm.
Point/spot records, cluster headers and packed light indices occupy one
point-sampled RGBA32F renderer texture. Forward/OIT and volumetric shaders read
only their cluster lists. Shadows work for lights beyond index 16, within the
existing portable budget of four shadowed local sources.

The dedicated volumetric program binds point-sampled depth as its primary
image and linear HDR color as its auxiliary image. On the tested Mali T880
GLES driver, the earlier auxiliary-depth binding returned zero in that program;
the same depth attachment was readable through the primary sampler. Moving
those bindings passed the uniform-light, clustered-scattering and extinction
controls twice in one process. Changing explicit to implicit LOD alone did not
fix the failure. This records the observed compatibility fix, not a general
claim about all GLES drivers.

The builder is CPU based; it is not GPU light-list construction. Lists currently
upload as immutable texture replacements for each pass so multiple views in
one frame keep their own bindings. Large overlapping-light workloads need
benchmarking and upload/streaming optimization. No performance superiority is
claimed. RGBA32F data textures have no generated mip chain.

## Building and checking

```sh
cmake --build build --target njin_portable_world_test njin_advanced_material_test njin_backdrop_test njin_light2d_test njin_gpu_particles_test njin_clustered_test
ctest --test-dir build -R '^(portable_world|advanced_material|backdrop|light2d|gpu_particles|clustered)_' --output-on-failure
```

Each executable accepts `--renderer=vulkan`, `--renderer=bgfx-opengl`,
`--renderer=d3d12` and `--gpu-validation`. OpenGL legacy stays independent via
`--renderer=opengl` in existing applications and the `NJIN_BGFX=OFF` build.
The new fixtures target bgfx and are not legacy fixture replacements.

Android exposes the six suites as `world3d`, `advanced_material`, `backdrop`,
`light2d`, `gpu_particles` and `clustered` in `examples/android_renderer/run_device.py`.
The APK build now incrementally builds **all** engine libraries before linking
the imported static archives. Release's `NJIN_CTX_INLINE` embeds context member
offsets: rebuilding only core/3D after an embedded context layout change can
leave stale offsets in other feature archives. Engine and consumers must be
rebuilt together after the renderer/instance binary layout changes.
New material-map and draw-packet fields are appended after their existing
members to preserve positional aggregate initializer prefixes. This is source
compatibility; it does not preserve the binary layout of those structs.

## Evidence

- Earlier integrated Windows controls: **52/52**, 476.43 s,
  `build/renderer-completion-windows-tests.log`. Covers Vulkan, bgfx OpenGL,
  D3D12, Vulkan validation, animation, temporal, shadows, material maps,
  new feature fixtures, invalid shader packages and unsupported-scene checks.
- After aggregate-prefix compatibility changes: Windows **28/28**, 238.28 s,
  `build/renderer-compatible-windows-tests.log`; Linux **22/22**, 207.87 s,
  `build/renderer-completion-linux-tests.log`.
- After the compatibility changes: BGFX-OFF legacy **4/4**, 45.19 s,
  `build/renderer-compatible-legacy-tests.log`.
- Final Android feature matrix: **12/12**, six suites on both Vulkan and GLES,
  `build/renderer-final-android-tests.log`. Every per-suite `status.txt` records
  `last_result=PASS 0` and `controller_passed=True`. Artifacts are in
  `build/android-device-captures/renderer-final-<suite>-<renderer>/`.
  The clustered fixture checks uniform local-light fog, clustered scattering
  and extinction separately, and captures raw sampled depth. Both contexts
  pass after the depth/color binding fix. No temporary diagnostic shaders remain.
- Existing Android model/presentation controls also pass Vulkan and GLES after
  updating the old fog-rejection fixture: `build/renderer-final-models-vulkan.log`
  and `build/renderer-final-models-gles.log` (native and controller PASS).
- Updated Linux model/presentation controls: **3/3**, 52.81 s, software Vulkan,
  OpenGL and Vulkan validation; `build/renderer-model-linux-tests.log`.
- Focused final volumetric regression: Windows **8/8**, 60.86 s,
  `build/renderer-volume-windows-tests.log`; Linux **6/6**, 49.29 s,
  `build/renderer-volume-linux-tests.log`.
- Full Windows build succeeded. All **159 test cases have passing results**:
  the initial complete run passed **150/159** in 1599.05 s; the remaining **9/9**
  passed after the targeted correction/rerun in 46.10 s. Logs:
  `build/renderer-final-all-windows-tests.log` and
  `build/renderer-final-all-windows-rerun.log`. Four old model cases expected
  valid fog to be unsupported; the fixture now verifies accepted fog state,
  transactional rejection of negative density and restoration of the original
  state. Five inspector cases initially failed because the sandbox blocked
  localhost sockets (`WinError 10013`); the rerun allowed those connections.
  This is a complete run plus targeted rerun, not a claim of one 159/159 run.

Apple and Web runtime/platform packaging remain unverified.
The pinned host shaderc translated all eight main/instanced, volume, 2D-light,
velocity and depth-initialization shaders to Metal successfully:
`build/renderer-metal-shader-check.log`. This is shader translation only;
Apple's native Metal compiler, engine build and runtime were not exercised.

## Source entry points

Remaining parity priorities include the advanced world options in the world
port document, glTF extension maps
and broader 2D G-buffer coverage. Apple and Web are deferred. Profile first-use
pipeline compilation, cluster uploads and per-receiver pixel shadows on
representative workloads.

- World implementation: `src/engine/runtime/njin_world3d_portable.cpp`,
  `njin_world3d.cpp`, `njin_world3d_impl.h`, `feature_3d.cpp` and
  `modules/world3d_draw.cpp`.
- Material/instance submission: `src/engine/api/njin_renderer.h`,
  `src/engine/runtime/njin_model_portable.{h,cpp}`, `njin_model.h`,
  `njin_material_uniforms.h`, `njin_renderer.cpp`, `njin_renderer_resources.h`,
  `njin_bgfx.cpp` and the graphics-device interfaces/adapters.
- Cluster builder: `src/engine/runtime/njin_light_clusters.{h,cpp}`,
  `modules/render3d_ext.h`, `modules/render3d_lights.cpp`,
  `modules/render3d.cpp` and `njin_post_portable.cpp`.
- 2D lights/backdrop: `src/engine/runtime/njin_lighting_portable.cpp`,
  `modules/lighting.h`, `modules/lighting_occluders.cpp`, `modules/sprite.cpp`
  and `njin_backdrop.{h,cpp}`.
- Analytic particles: `src/engine/runtime/njin_particles_gpu_portable.cpp`,
  `modules/particles_gpu.{h,cpp}` and `src/engine/api/njin_particles.h`.
- Shader sources: `src/engine/runtime/shaders/bgfx/njin_advanced_material.sh`,
  `njin_clusters.sh`, `fs_njin_light2d.sc`, `fs_njin.sc`, `fs_njin_volume.sc`,
  `njin_surface.sh`, `njin_light2d.sh`, `njin_volume.sh`,
  `vs_njin_instanced.sc` and `varying.def.sc`.
- Fixtures: `src/engine/tests/world3d_portable.cpp`, `advanced_materials.cpp`,
  `backdrop.cpp`, `light2d_portable.cpp`, `gpu_particles_portable.cpp`,
  `clustered_lighting.cpp` and updated `unsupported_scene.cpp`/`models3d.cpp`.
- Integration: root/runtime `CMakeLists.txt`, `cmake/njin_bgfx_shaders.cmake`,
  six Android wrappers plus its CMake/dispatcher/APK builder/device runner,
  README and renderer migration/backlog/world-port documents.
