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
| 7 | Post FX | custom camera post |
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

## Removal order

1. Port the groups above, most visible first: 11, 8, 17, then 10 (DXR), 2/7/1,
   9, 18, 13, SDF, 19, 20.
2. For each verified group: put its legacy path behind a build flag off by
   default, run the bgfx image regressions (Windows, Linux, Android), delete it.
3. Move platform, math and tools off raylib.
4. Drop raylib from `CMakeLists.txt`, the guard headers and the boundary check;
   update `CLAUDE.md`, the wiki and `CHANGELOG.md` (a MINOR release that breaks
   compatibility while the version is 0.x).
