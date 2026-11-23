# Renderer audit and delivery boundary

**Historical audit:** this describes the initial raylib → SDL GPU investigation.
The SDL GPU decision has been superseded by SDL3 platform + bgfx. Current build
commands and verified platform status are in [renderer-migration.md](renderer-migration.md).

This records the repository examined for the SDL GPU migration. Read
[renderer-migration.md](renderer-migration.md) for build commands, ownership,
the implemented API, verification and remaining limitations.

## Existing repository

The root `CMakeLists.txt` builds C++20 libraries and example executables with
FetchContent dependencies. The runtime lives in `src/engine/runtime`, the
public API in `src/engine/api`. Applications own their `main()` and call
`create`, `mod_register`, `run`, `destroy`; `examples/sandbox/main.cpp` is one
such entry point. The original runtime is not an independent sample renderer.

| Dependency category | Concrete integration points | Migration decision |
|---|---|---|
| Window/context/frame | `njin.cpp`, `njin_frame_impl.h`, `njin_window.cpp`: InitWindow/CloseWindow, BeginDrawing/EndDrawing | Keep raylib lifecycle for GL; own SDL lifecycle for Vulkan |
| 2D drawing/camera | `modules/camera.cpp`, `modules/sprite.cpp`, `njin_draw.cpp`, `njin_view.cpp`: BeginMode2D, DrawTexturePro, rlgl transforms, cached tilemap targets | Preserve GL; adapt sprites/texture draws and CPU camera transforms; reject unported drawing |
| 3D drawing | `modules/render3d*.cpp`, `world3d_draw.cpp`, `njin_model*.cpp`: BeginMode3D, DrawMesh/DrawModel, framebuffer/shader state | Preserve existing GL features; expose a shared unlit indexed mesh path for the Vulkan slice |
| Texture/shader/targets | `njin_texture.cpp`, `njin_shader.cpp`, `njin_font.cpp`, lighting/post modules: LoadTexture, LoadShader, render textures, rlgl | CPU assets plus context-owned opaque resources; separate GPU uploads; separate SPIR-V stages |
| Input | `njin_input.cpp`, `njin_touch.cpp`, `njin_input_impl.h` | SDL events feed the same Njin snapshots and touch gesture processor |
| Audio | `njin_audio.cpp`, `njin_audio3d.cpp`, `modules/audio.cpp` | Retain independent raylib audio device; no GL window required |
| Timing | `njin.cpp`, `njin_cfg.cpp`, `njin_pace.*` | Keep fixed-step scheduler; steady clock and pacing for SDL mode |
| Asset loading/utilities | `njin_path.cpp`, `njin_file.cpp`, `njin_font.cpp`, `njin2rl.cpp`, camera/math helpers | Retain CPU decoders, paths and math where independent; guard routines that allocate GPU atlases |

Preserved GL feature groups include sprite animation and sorting, tilemaps,
particles, UI/fonts, atlases, lighting, post effects, virtual resolution,
render scaling, 3D models/materials/shadows, instancing and existing ray tracing
interop. They remain available through OpenGL; the migration does not imply
these groups have all been ported to SDL GPU.

The original configured build registered zero CTest tests. The README lists
sandbox, Pong, platformer, topdown, render/lighting/debug demos, FPS, Sokoban,
platformer3d and Skylight. These have different feature requirements; switching
an advanced GL demo to Vulkan is not treated as feature parity. No controlled
cross-backend performance baseline existed or is claimed by this change.

## Delivery boundary

- The default OpenGL path and public ECS component layouts are retained.
- Vulkan uses a real Vulkan GPU device, claimed SDL window, swapchain, SPIR-V
  pipelines, independent textures/samplers/vertex/index buffers and depth target.
- Both modes execute the same integration scene with legacy ECS sprites,
  direct texture draws and a perspective indexed textured cube through Njin.
- Handles belong to a context. Resources are released before the owning
  device/window. No GPU objects enter gameplay components or public descriptors.
- Internal render targets support capture; a general public render-target,
  shader/pipeline, mutable-buffer and sampler-handle API is deferred. Fixed unlit
  pipeline variants and immutable mesh buffers keep this slice bounded.
- Advanced legacy rendering remains on GL. Adapted unsupported paths fail with
  diagnostics; physical device-loss recovery and exhaustive coverage of every
  historical API remain follow-up work.

Principal risks addressed during implementation were GL state inherited by
new RHI draws, double viewport Y inversion, SDL static-link symbol collisions,
swapchain semaphore reuse in older SDL, upload ownership on failure, index
buffer alignment and drawable/logical coordinate differences. Regression checks
cover the observed failures. Hardware input and all driver failure modes need
broader testing; Vulkan speed relative to GL has not been benchmarked.
