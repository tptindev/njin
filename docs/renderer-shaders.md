# Custom shaders on SDL3/bgfx

The existing `shader_load`, `shader_begin/end`, scalar/vector/array setters,
`shader_set_texture`, `texture_set_shader` and `model_material::shader` APIs now
accept a backend-independent compiled fragment package on the SDL3/bgfx path.
Legacy `--renderer=opengl` retains raylib GLSL loading unchanged. Driver handles
remain private to the device. Selecting SDL3/bgfx does not translate old GLSL.

## Build a package

After configuring Njin with `NJIN_BGFX=ON`, include
`cmake/njin_shader_assets.cmake` and attach an asset to your game target:

```cmake
include(path/to/njin/cmake/njin_shader_assets.cmake)
njin_shader_asset(my_game
  NAME tint
  FRAGMENT "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fs_tint.sc"
  METADATA "${CMAKE_CURRENT_SOURCE_DIR}/shaders/tint.json"
  OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/assets/shaders"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/common.sh")
```

This invokes the pinned bgfx `shaderc` for each `NJIN_BGFX_SHADER_PROFILES`
entry (`glsl`, `spirv`, `dx11`, `essl`, `metal`), then emits `tint.njshader`
with relative binary paths. Ship the package and its `.bin` siblings together.
A failed compilation keeps the previous binary; successful output replaces it.
Cross compilation uses the existing `NJIN_BGFX_SHADERC` host executable option.
List included shader files in `DEPENDS` so edits rebuild the asset.

Example metadata:

```json
{
  "uniforms": {
    "color": {"name":"u_gameColor", "type":"vec4"}
  },
  "samplers": {
    "texture0": {"name":"s_gameBase", "stage":0}
  }
}
```

The helper adds version 1 and a `profiles` table. An alias on the left is the
name passed to `shader_set_*`; `name` is the compiled uniform name. Distinct
custom programs should use distinct names when their GPU uniform types differ,
since bgfx's uniform registry is global. The loader rejects conflicting types.
`u_njin*` and `s_njin*` are reserved engine uniforms and cannot be metadata aliases.

A fragment stage must declare **all seven** varying inputs below, even when it
uses only some of them, to match the engine vertex stage's interface hash:

```glsl
$input v_texcoord0, v_color0, v_world, v_normal, v_texcoord1, v_tangent, v_instance_color
#include <bgfx_shader.sh>
SAMPLER2D(s_gameBase, 0);
uniform vec4 u_njinTint;
uniform vec4 u_gameColor;
void main() {
  gl_FragColor = texture2D(s_gameBase, v_texcoord0) * v_color0 * u_njinTint * u_gameColor;
}
```

Use bgfx shader syntax and portable constructors (`vec4(x,x,x,1.0)`, rather
than GLSL-only scalar splats nested inside another constructor). The engine
supplies transforms, clip-depth conversion and target Y orientation in its
vertex stage. A custom fragment is responsible for its own lighting, cutout,
color encoding and HDR radiance output; built-in PBR is not appended to it.

## Use the existing API

```cpp
auto shader = njin::shader_load(ctx, nullptr, "shaders/tint.njshader");
njin::shader_set_vec4(ctx, shader, "color", {1, .5f, .25f, 1});
// During phase_render:
njin::shader_begin(ctx, shader);
njin::texture_draw(ctx, sprite, {32,32}, {1,1,1,1});
njin::shader_end(ctx);
```

`texture_set_shader` auto-binds for manual `texture_draw/texture_draw_ex` calls;
an explicit active shader wins. ECS sprites retain the existing behavior of
not auto-binding a texture's shader. A model's material shader overrides an
active shader for that material. UI skins support packaged fragment shaders and
offer optional aliases `uiState`, `uiTime`, `uiRect`, `uiValue` like the legacy path.

Metadata types `float`, `vec2`, `vec3`, `vec4` use one GPU vec4 (unused lanes zero).
Only `vec4` supports `count` arrays, up to 256 elements. Setters validate type,
capacity and finite values; unwritten array tails become zero. Unknown aliases
warn once and are ignored, matching the legacy API.

`int` preserves all signed 32 bits in two exact float lanes. Decode in `.sc`:

```glsl
int value = int(uint(u_gameInteger.x) | (uint(u_gameInteger.y) << 16u));
```

Declare a sampler at stage 0 and up to four extra 2D samplers at distinct stages
1 through 4. An unset sampler samples the draw's base image. Texture and render
texture setters retain engine handles, resolve replacements on the next draw,
and use the texture's filter. Packed atlas images cannot be extra samplers.
Render-target feedback, uninitialized targets and expired handles are errors.
Sampling a render texture follows the same UV orientation rules as other RHI
maps on that backend; sampling the whole target is covered by the regression.

2D draws snapshot uniforms and samplers independently. As documented by the
existing 3D API, all queued 3D draws use the last uniform/sampler values before
`end_3d`. Shader unload/reload is refused during an open 3D pass or after that
program was submitted in the current frame. Unload is idempotent outside use;
remaining programs are released at device shutdown. Native handles never enter
ECS components.

## Reload, validation and current boundaries

The existing hot-reload service watches both package metadata and the selected
profile's binary. Reload retains compatible alias values and texture bindings;
load/link/reflection errors keep the previous program. Missing profiles/files,
invalid container headers/tables, interface hashes and uniform signatures report
an error and return an invalid handle from initial load. Assets must be compiled
with this checkout's shaderc container version; arbitrary untrusted GPU bytecode
is not a supported input format.

Implemented: custom fragment shading for manual 2D draws, UI skins, opaque basic,
static, CPU-skinned/morphed 3D geometry and material overrides. Built-in geometry
variants continue to serve shadow, normal and velocity passes. Custom vertex
stages, custom instanced/OIT/compute variants and camera post-shader integration
are still explicitly unsupported; this is not complete legacy shader parity.

Windows runtime proof: `build/port20-custom-shader-tests.log`, 4/4, Vulkan,
bgfx OpenGL, D3D12 and Vulkan validation. The fixture uses two contexts and GPU
pixel controls for per-draw uniform/sampler snapshots, render-texture sampling,
plain output, material shaders and final-value 3D semantics. It also exercises
missing assets, invalid metadata/binaries, reload recovery, type/count errors,
feedback and in-use destruction. Linux/Android validation is recorded separately
in the migration backlog as those runs complete.

Subsequent platform proof: Linux shader/gizmo/inspector **10/10**
(`build/port20-shader-linux-tests.log`, 14.95 s), and real Android shader controls
on Vulkan and GLES (both native `PASS 0` with controller confirmation). The APK
build compiles the same fragment/metadata through
`src/engine/tests/compile_shader_fixture.py` using the engine cache's pinned
host shaderc; the Java Activity extracts those package files into private storage.

The final combined model/render-target/shader/sprite-FX Windows controls passed
**17/17** (92.92 s); Linux shader/FX/debug and legacy controls passed **14/14**
(34.67 s). BGFX-OFF legacy shader behavior remains available independently;
sprite-FX, inspector/GIF and RHI controls passed **3/3** (11.97 s).
