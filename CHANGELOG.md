# Changelog

Versions follow [semver](https://semver.org). Before 1.0 the public API may
change between MINOR versions. The number lives in `src/engine/api/njin_version.h`.

To release: edit that header, add a section here, commit, then
`git tag -a vX.Y.Z -m "njin X.Y.Z"` and push the tag.

## 0.3.0

- **Sharper text**: the default font is now JetBrains Mono (SIL OFL), compiled
  into the engine, with the Latin and Vietnamese blocks; before it was raylib's
  ASCII-only bitmap font. Every font, loaded or default, keeps one glyph atlas
  per pixel size drawn, baked on first use, and `draw_text` snaps the position
  to whole pixels; `text_measure` measures with the same atlas. On a real GPU
  with a virtual resolution (`virtual_size`), screen-space text is queued and
  drawn after the scaled image, at window resolution, from an atlas baked at
  size x scale (`njin_cfg::crisp_text`, on by default; off on a software
  renderer, and in world space or a render texture). A full-screen rectangle
  drawn afterwards (fade, flash, modal dim) tints the queued text. `font_load`'s
  `size` is now optional (it only pre-bakes that size). The inspector's font
  entries list the atlases of a font, one per size.
- **Smooth UI**: `njin_cfg::smooth_ui` draws everything after the world (the
  screen-space phase, dialogue, toasts, flash, fade) at the window's resolution
  instead of into the virtual image, so rounded panels, buttons and text stay
  smooth at any scale while the world keeps its pixels. `njin_topdown` uses it;
  `njin_platformer` keeps the pixel UI.
- **Panels fit the screen**: a `ui_begin` panel taller than the screen is shrunk
  (text included, down to half size) to fit it instead of running off the top and
  bottom. It settles on the second frame the panel is shown.
- **Queued text keeps the draw order**: text drawn at window resolution is now
  covered by what is drawn over it afterwards (`draw_rect`, UI panels, buttons,
  the dimming behind a popup) and cut by `clip_begin`; before, it sat above all
  of it, so a game title showed through a settings panel.
- **Sample** `njin_platformer` is pixel art all the way: VT323 (a pixel font
  with the Vietnamese blocks, SIL OFL) in `font_pixel`, square UI, integer
  scaling and no window-resolution text. `shared::apply_style` takes a
  `font_style`; `njin_topdown` keeps its smooth style.
- **Pixel text**: `font_set_style(ctx, font, font_pixel)` (or the new `style`
  argument of `font_load`; a default `{}` handle switches the default font)
  rasterizes without anti-aliasing and samples with the nearest filter. Pixel
  text is always drawn in the virtual image, so it scales like the sprites.
- **The log goes to the inspector while one is connected**: the game's console
  stops printing then, and every message (the window's startup lines included,
  up to 1000 kept from `njin_create`) is sent to `njin_inspector`. With no
  inspector connected, or after it goes away, the game logs to stderr as
  before. A sink set with `log_set_sink` is not affected. The inspector is
  still switched on by the game, with `debug_server_start`.

## 0.2.0

Rendering and GPU work:

- **Particles on the GPU**: on a machine with a real GPU (OpenGL 3.3+, not a
  software renderer) every emitter is simulated in a vertex shader and drawn
  with one instanced call from a vertex buffer of its own, written only when
  particles are spawned or removed; elsewhere the CPU path is unchanged. New:
  `particles_set_backend`, `particles_backend`, `particles_gpu_available`,
  `particle_emitter::gpu`. On the GPU `particle::pos`, `velocity` and `rot`
  keep their spawn values and `age` is the emitter-clock time of birth.
- **Atlas**: `atlas_create`, `atlas_load`, `atlas_destroy` pack small images
  into shared pages so sprites of different images batch into one draw call.
  The result is an ordinary `texture_handle`; unscaled, unrotated drawing is
  pixel-identical to separate textures.
- **Culling**: sprites and particle emitters fully outside the camera are no
  longer sorted or drawn (off while the camera shakes).
- **Inspector** (protocol 3): a Rendering section in Performance with sprites
  drawn and culled, tile chunks, particles (GPU share), instanced calls, post
  passes and an estimated draw-call count.
- **Discrete GPU first**: on a laptop with two GPUs the game asks for the
  discrete one (Windows: the Optimus and PowerXpress exports; Linux: PRIME
  offload variables on laptops only, and only where the user has not set them;
  macOS needs nothing; a desktop uses the card its monitor is plugged into).
  Windows games link the new `njin::gpu` target, which compiles the exports
  into the executable itself (a static library would drop them). CMake option
  `NJIN_PREFER_DISCRETE_GPU`, on by default.
- **Sample** `njin_render_demo`: a forest with thousands of sprites, animated
  water, GPU particles, atlas and post effects, with keys to switch each and a
  HUD of the draw calls. New `render_info_get()` gives a game those numbers.
- **VSync**: `njin_cfg::vsync`, `window_set_vsync`, `window_vsync`; saved in the
  settings file and offered in the sample settings menus.
- **Shaders compile at startup** (sprite flash, post-processing, particles)
  instead of on first use.
- **Post-processing**: a `blur` of 3 px or more runs at half size; the uber
  pass binds its program once and writes only the uniforms that changed.
- **Tilemaps**: animated tiles are found once when a chunk is baked, and drawn
  together per tilemap (fewer draw calls).

## 0.1.0

First numbered version. What the engine has:

- **Core**: EnTT ECS behind `njin.h`, raylib 6.0 hidden, modules and named
  systems in nine phases, scenes with fades and prefabs, fixed-step physics.
- **2D building blocks**: sprites and Aseprite animation with a state machine,
  chunked tilemaps with tile shapes (one-way, 45 and 22.5 degree slopes) and
  animated tiles, Tiled and LDtk levels, box/circle collision with layers and
  raycast, parent-child transforms, particles, screen FX, post-processing,
  a virtual resolution for pixel art.
- **Gameplay**: `platformer_body` (coyote time, jump buffer, wall jump, double
  jump), `topdown_body` (8-way, dash), moving platforms, camera follow with
  deadzone and level bounds, y-sorted drawing, A* navigation, timers and tweens.
- **Game plumbing**: UI (buttons, sliders, popups, toasts, key rebinding, gamepad
  navigation, custom skins and shaders), dialogue, localization, audio buses
  with music crossfade, settings file, JSON, save path, hot reload.
- **Tools**: `njin_inspector`, a separate process over a local socket: entities,
  world view, log, time control, and CPU/RAM/GPU consumption per system,
  component, entity and asset.
- **Samples**: `njin_platformer`, `njin_topdown`, `njin_debug_demo`; release
  packaging with `njin_package()`.

Known gaps: not tried with a real gamepad, not built on Linux or macOS (the
inspector reads GPU figures on Windows only).
