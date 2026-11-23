# Portable materials and image-based lighting

The SDL3/bgfx path uses the existing Njin scene, model, material and lighting APIs.
It imports glTF/GLB base color, tangent-space +Y normals, metallic/roughness
(linear G/B), occlusion (linear R) and emissive images. Base color and emission
are decoded from sRGB; factors and HDR lighting are linear. UV0/UV1, authored
tangents, mirrored transforms, `KHR_texture_transform` and
`KHR_materials_emissive_strength` are supported. Missing required UV sets and
invalid images reject the import and release its newly allocated resources.

Each map has its own UV transform, wrap mode and point/linear sampling state.
An image used by two maps can have different sampler state. Mipmap minification
and separate min/mag filters are still reported as limitations during glTF
import; they are not implemented by the current material sampler abstraction.
Public material texture overrides borrow engine handles. Queued draws retain
their resources until submission, so unloading a referenced map reports an
error. ECS components contain no bgfx objects.

## Environment lighting

Set `light3d.environment` to a loaded **2:1 equirectangular** image, then set
`environment_intensity`, `environment_rotation` (degrees) and optionally
`environment_visible`. Lit PBR materials receive diffuse irradiance and
roughness-dependent specular reflections; unlit materials remain unlit.
`environment_visible` controls the sky independently of its lighting.

The portable implementation reuses Njin's existing environment representation
and mathematical conventions from `modules/render3d_env.cpp` and
`modules/render3d.cpp`, without calling their OpenGL rendering functions:

- Nine radiance SH coefficients, integrated with solid-angle weights, produce
  diffuse irradiance.
- Six GGX-prefiltered equirectangular radiance levels are stacked in a 128×384
  atlas. Each level is 128×64 and uses 128 importance samples per texel.
- A 256×128 linear environment image supplies the background.
- The existing analytic split-sum BRDF fit supplies the specular scale/bias.
- Float16 textures retain radiance above 1. Supported finite radiance is
  0..65504; invalid/out-of-range source values fail explicitly.

Preparation runs on the CPU once for a source/version change, including a
solid-angle-weighted source mip chain for prefilter sampling. File-backed HDR
images are decoded as linear floats; ordinary images and memory RGBA8 textures
are decoded from sRGB. This is real prefiltered IBL, but preparation is
synchronous and can stall loading, particularly in Debug builds. Offline asset
compilation, larger quality presets, probe blending and asynchronous preparation
remain future work. The fixed atlas resolution limits sharp reflection detail.

The model store caches opaque IDs; RendererResources and the graphics device
own their GPU storage. Replaced environments remain alive while the current
frame uses them and are collected on a subsequent pass. Shutdown also releases
them. The sky uses the active camera's inverse projection at far depth and goes
through the same exposure/HDR/tone mapping path as the scene.

## Verification

`njin_material_test` exercises normal orientation, UV1/transforms, MR/AO/emission,
map overrides, rollback, sampler reset and resource retention. On legacy OpenGL
it exercises the common renderer adapter; glTF map import assertions exercise
the portable loader on bgfx backends.

`njin_environment_test` exercises constant irradiance, metallic reflections,
rotation, roughness, HDR radiance `(4,2,1)` at exposure 1/8, disable/reset,
cache replacement and two consecutive engine contexts. Assertions read Njin
captures, rather than assuming successful presentation means correct lighting.

```powershell
cmake --build build --target njin_material_test njin_environment_test
ctest --test-dir build -R "materials_|environment_" --output-on-failure
```

Windows runtime verified: Vulkan, bgfx OpenGL, D3D12 and Vulkan validation.
Material adapter controls also passed legacy OpenGL. Android engine compilation
is verified for the initial map/IBL implementation; the new material/IBL pixel
fixtures have not yet run on Android. Apple application/runtime testing remains
unverified. Consult the current test logs for subsequent build revisions.


## Material mipmaps (2026-11-04)

Explicit core glTF mipmap samplers now upload full immutable RGBA8 mip chains.
Minification (all six glTF modes) is independent of magnification and wrapping.
Albedo/emission mip generation averages decoded linear-light RGB and re-encodes
sRGB; MR/AO average linearly; normal-map vectors are normalized after averaging.
Odd dimensions and 1xN tails retain the entire source footprint. Alpha averages
linearly; alpha-mask coverage correction is not part of this implementation.
The existing shader-side color decoding convention remains in use.

Texture CPU metadata appends `RenderMipMode`; renderer map metadata appends
`RenderMinFilter`. Defaults preserve base-level behavior. Explicit nearest/linear
base-only filters sample LOD zero even when another sampler uses the same mip
chain. This is handled by shader flags on bgfx and sampler objects on legacy GL.
No native GPU handles appear in material/ECS APIs. RGBA16F material mip generation
and compressed source textures are explicitly unsupported; HDR environment
preprocessing retains its separate existing path.

`njin_mipmaps_test` exercises GPU sRGB-vs-linear averaging, base-only sampling,
a glTF trilinear sampler, NPOT tails, normalized normals, resource cleanup and
two engine contexts. `generate_mipmap_fixture.py` reproduces the embedded checker
fixture. Windows Vulkan/OpenGL/D3D12 and Android Vulkan/GLES have passed these
controls; see the latest migration checkpoint for source revisions and Linux
software-driver verification.
