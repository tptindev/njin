# Removing the legacy raylib renderer

Decided with the user on 2026-11-08:

1. **Port first, remove after.** Every missing group below is ported to the SDL3 +
   bgfx renderer while the legacy raylib/OpenGL path still runs, so each port is
   checked against the legacy output (images, behaviour). A group's legacy code
   goes as soon as its port is verified; raylib goes last.
2. **Remove raylib entirely**, not only the OpenGL 3D renderer: window, input,
   audio, image/font loading, 2D, the tools' UI (inspector, anim_editor,
   ui_editor) move to SDL3/bgfx or small libraries (stb...), then the raylib
   dependency, `src/engine/guard/` and `njin_check_boundary()`'s raylib rules go.
   macOS and Web stop running until they are ported to bgfx; that is accepted.
3. Every port follows [renderer-reuse.md](renderer-reuse.md): Njin's own legacy
   code first, then a pinned upstream reference opened for real, notices for any
   import, and runtime verification through Njin's entry points on Windows,
   Linux and Android.

## What is still missing on bgfx

From [renderer-port-backlog.md](renderer-port-backlog.md) (rows) as of 2026-11-08:

| Row | Group | Missing |
|---|---|---|
| 1 | Custom shaders | custom vertex, instanced, OIT and compute variants; camera post variants |
| 2 | Render targets / HDR | per-target TAA/motion histories; keeping earlier 2D content through HDR |
| 7 | Post FX | done for 2D: `camera_set_post_shader` runs a `.njshader` package on bgfx (render_demo) |
| 8 | glTF materials | sheen, specular, iridescence and the remaining extension maps |
| 9 | Volumetrics | half-resolution volume; shadowed sources capped at four |
| 10 | Ray tracing | DXR on Direct3D 12; BVH refit for moving scenes; captured irradiance grid on bgfx |
| 11 | World | Relief hình học đã triển khai: Windows 5/5, Linux relief/ocean 7/7 và Android relief/ocean Vulkan/GLES 4/4 đạt. Shadow map có displacement; BVH ray tracing chưa có và được báo lỗi rõ. [Chi tiết](renderer-terrain-relief.md). |
| 13 | Model formats | IQM, VOX, M3D, Draco, glTF basisu/dds images (refused today) |
| 17 | Instancing | Hi-Z cho model thường, lịch sử hai pha và per-light culling; Hi-Z descriptor instance đục đã port |
| 18 | Particles | a compute 3D particle system (today vertex simulation only) |
| 19 | Video | audio runtime on platforms other than Windows |
| 20 | Inspector | desktop UI still on raylib; other platforms |
| — | SDF | per-pixel ray marching (bgfx samples SDFs into meshes on the CPU) |

## raylib still in use (audit 2026-11-08)

`#include` of raylib.h/rlgl.h/raymath.h: 87 files under `src/engine/runtime`
(36 of them in `modules/`), 12 under `src/tools`, none in `examples/`.

- Renderer (legacy path, goes with its group): `njin_graphics_device_gl.cpp`,
  `njin_draw.cpp`, `njin_shader.cpp`, `njin_texture.cpp`, `njin_backdrop.cpp`,
  `njin_view.cpp`, `njin_atlas.cpp`, `njin_sdf2d.cpp`, `modules/render3d*.cpp`,
  `post3d.cpp`, `post_fx.cpp`, `lighting*.cpp`, `particles*.cpp`,
  `world3d_draw.cpp`, `gizmo.cpp`, `ui.cpp`, `camera.cpp`.
- Platform: `njin.cpp`, `njin_window.cpp`, `njin_input.cpp`, `njin_bindings.cpp`,
  `njin_cfg.cpp`, `njin_log.cpp`, `njin_path.cpp`, `njin_quality.cpp`
  (SDL3 replacements needed where the bgfx path does not already use
  `njin_sdl_platform`).
- Math/data: `njin2rl.cpp`, `rl2njin.cpp`, `njin_model*.cpp`, `njin_instance.cpp`,
  `njin_world3d.cpp`, `njin_level.cpp`, `njin_renderer.cpp` (raymath and raylib
  types: replace with Njin's own math).
- Tools: `src/tools/inspector`, `anim_editor`, `ui_editor`.

## The bar: every sample on every renderer

`sample_<example>_<renderer>` (CMakeLists.txt, `cmake/check_example_smoke.cmake`)
runs each of the 12 samples whole, from its own folder, for 240 frames on
opengl, vulkan, bgfx-opengl and d3d12, through `njin_smoke.h` (environment
variables only, no change to the games): it must exit cleanly, raise no
validation error and draw a non-blank frame. The log also has the average CPU
update/render/present time, uploads and the last frame's 2D draw statistics.
A group's legacy path may go only while these pass on bgfx.

First run (2026-11-23): 37/48. Fixed: the 2D samples did not build
(`njin.h` included `njin_atmosphere.h` outside the 3D feature); sokoban threw
on bgfx (`draw_cylinder3d`/`draw_capsule3d` now draw a cylinder primitive);
render_demo's frame pass (`camera_set_post_shader` now runs on bgfx, its
shader is a `.njshader` package chosen with `graphics_backend()`); bgfx sprites
were neither culled nor batched (about 3000 draws, 650 ms a frame in
render_demo; now culled and batched by GPU texture/atlas page as on OpenGL: 2
draws, about 14 ms). 48/48 pass.

## Step 2: public calls that still threw on bgfx (2026-11-23)

Audited every refusal on the bgfx path. Already ported (the refusal sat behind a bgfx
branch): `texture_draw_ex`, `draw_dissolve_begin/end`, `draw_backdrop_blur`,
`begin_3d` into a render texture, every 3D particle blend mode; no public 3D call
reaches the legacy `open_pass` any more. Fixed:

- `terrain_relief` regression: the lightmap work added `v_lightmap_uv` to the main
  fragment stage but not to the relief vertex stage, so bgfx refused the program. The
  relief fields now also use stages 13-15 under the lightmap/under-map names (all 16
  samplers are taken) and a relief draw with a lightmap is refused clearly.
- Transparent 3D without `post3d.oit` (tint alpha, textures with alpha, alpha blend)
  threw; it is now alpha blended farthest first over the opaque draws without writing
  depth, as OpenGL does; see-through closed primitives show their near side only.
  Transmission still needs OIT (refused clearly); water drops its OIT-only optics.
  `transparent3d`: identical pixel counts on all four renderers.
- `draw_instanced3d` with `mesh3d_cylinder`, `mesh3d_sphere_low`, `mesh3d_cylinder_low`
  (low meshes as legacy: 6 x 8 sphere, 8-sided cylinder); a `pose` given with a
  primitive is ignored as on OpenGL. `primitives3d`: every kind on every renderer.

What still refuses on bgfx is group work (rows 1, 8, 13 above): custom instanced and
transparent shaders, glTF extensions and compressed images, other model formats,
clay on meshes, non-uniform SDF scale.

## Step 3: raylib off the bgfx path (in progress)

What the bgfx path still asked of raylib, from raylib's own log lines in the 48
sample runs: file reading, image decoding, fonts, audio (plus raymath, raylib
types and small utilities that do not log).

- **Images (done, 2026-11-23).** `njin_image_io` reads with the standard
  library, decodes with the pinned bimg and writes PNG/TGA with bimg, BMP/QOI
  with small encoders of its own; renderer_load_image, glTF/OBJ textures, the
  IBL source, render texture saves and screenshots use it. `image_io` test:
  every image in the repository decodes to raylib's exact pixels (grey PNGs
  expanded to grey as raylib does), and the four writers read back unchanged.
  raylib `IMAGE` log lines on bgfx: 178 -> 0.
- **Fonts (done, 2026-11-23).** `font_bake_atlas` is raylib's LoadFontData +
  GenImageFontAtlas translated (zlib, THIRD_PARTY_NOTICES.md) over the same
  stb_truetype; font files are read with the standard library. `font_bake`
  test: 3 fonts x 6 sizes x smooth/pixel, glyphs, rectangles and atlas pixels
  identical to raylib's. raylib `FONT` lines: 82 -> 0.
- **Audio (done, 2026-11-23).** `njin_audio_backend` on miniaudio 0.11.24
  (pinned) with stb_vorbis, for both renderers; raylib is built without raudio.
  It mixes as raudio did (float stereo device, one converter per voice for
  pitch, raudio's pan law, no clipping); music decodes from memory in the
  callback and loops without a gap; video sound is a two-buffer stream.
  `audio_backend` test (offline mixer): pan/volume levels, pitch length,
  aliases, stream depth, music looping and the repository's sound files.
- **File helpers (done, 2026-11-24).** On the bgfx path FileExists,
  GetApplicationDirectory, LoadFileData and DecodeDataBase64 gave way to the
  public file_exists/file_read and `njin_fs` (executable folder, base64);
  heightmaps and splat maps decode through njin_image_io. The rest of raylib's
  utilities sit in the legacy loop and loader and go with step 4.
- **raylib types and raymath (done for the shared runtime, 2026-11-24).** The
  shared runtime includes `njin_rl.h` (or `njin_rlmath.h`, math only, for files
  that also see `<windows.h>`): raylib's headers while raylib is built, and with
  `NJIN_RL_OWN` Njin's own `njin_rl_own.inl`, generated from the pinned raylib
  6.0 sources (zlib, THIRD_PARTY_NOTICES.md): the structs and enums with the
  same layouts, the 39 raymath functions used, rmodels' bounding boxes and ray
  collisions, rtextures' texture validity and pixel data sizes. `rl_own` test:
  layouts and 410,004 results bit for bit equal to raylib's. Tiled's zlib/gzip
  layers inflate with miniz (inside the pinned bimg_decode) instead of
  DecompressData (`level_inflate` test: 252 layers, equal to raylib's output);
  gamepad rumble on the SDL window goes through SDL_RumbleGamepad (it called
  raylib, which has no gamepads open there, so it did nothing on bgfx).
  Compiling with `NJIN_RL_OWN`, 22 of the 35 switched files build; the other 13
  are the legacy path (njin2rl, the raylib model loader, legacy input, window
  and loop, the OpenGL particle looks, the raylib log hook, SetTargetFPS, and
  files that include rlgl) and go with step 4.
- **Tools' UI (done, 2026-11-24).** njin_inspector, njin_ui_editor and
  njin_anim_editor are Njin contexts on SDL3 + bgfx with Dear ImGui, as in
  sdl3-proj: ImGui's own SDL3 backend (fed by an internal SdlPlatform event hook)
  and `src/tools/common/njin_imgui_host`, which draws ImGui through the public
  renderer API (renderer_draw_mesh, scissor, the font atlas as Njin textures;
  draw loop after the pinned ImGui's imgui_impl_sdlgpu3). rlImGui is gone. The
  inspector's World view draws with ImGui's draw list (3D projected as raylib's
  GetWorldToScreenEx, lines cut at the near plane); the UI editor's Viewport draws
  the engine's UI into a render target, its input filtered by an internal input
  hook; the animation editor draws its posed model with begin_3d into a render
  texture and draw_model_anim (model_pose::bones). Tests: tool_inspector_* and
  tool_ui_editor_* on Vulkan, D3D12 and bgfx-OpenGL, tool_anim_editor_self_test,
  _roundtrip and _smoke (a key moves 38,241 pixels of the Viewport). The tools
  still read the runtime's private headers, which name raylib's types until step
  4, so they keep raylib's include path (headers only).
- **RmlUi game UI (added, 2026-11-24).** The opt-in feature `rmlui`
  (`njin_rml.h`): RmlUi 6.3 with FreeType, documents drawn by Njin's renderer on
  top of the frame, data models bound to the game's values, events, input taken
  from the game while the UI has it. SDL3/bgfx only. Tests rml_vulkan, rml_d3d12,
  rml_bgfx-opengl. sdl3-proj's split: RmlUi for the game, ImGui for the tools.

## Removal order

1. Port the groups above, most visible first: 11, 8, 17, then 10 (DXR), 2/7/1,
   9, 18, 13, SDF, 19, 20.
2. For each verified group: put its legacy path behind a build flag off by
   default, run the bgfx image regressions (Windows, Linux, Android), delete it.
3. Move platform, math and tools off raylib.
4. Drop raylib from `CMakeLists.txt`, the guard headers and the boundary check;
   update `CLAUDE.md`, the wiki and `CHANGELOG.md` (a MINOR release that breaks
   compatibility while the version is 0.x).
