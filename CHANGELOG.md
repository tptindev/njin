# Changelog

Versions follow [semver](https://semver.org). Before 1.0 the public API may
change between MINOR versions. The number lives in `src/engine/api/njin_version.h`.

To release: edit that header, add a section here, commit, then
`git tag -a vX.Y.Z -m "njin X.Y.Z"` and push the tag.

## 0.5.0

- **Supersampling**: `njin_cfg::render_scale` draws the world and UI at 2x, 4x or
  8x resolution, then smoothly downsamples the image to reduce jagged edges.
  Logical coordinates stay the same; higher settings use more GPU work.
- **Instancing**: `instance_buffer_create`, `instance_buffer_upload` and
  `draw_instanced` draw thousands of quads in one draw call through a game's own
  shader, each with 4 to 16 floats of its own data (`instance0..3` in the vertex
  shader). `instancing_available` says whether the machine can. The inspector's
  draw-call and instanced-call counts include these draws. `draw_instanced` can
  also bind a texture or a render texture to `texture0`, so a game can draw its
  poses into a render texture once and have every instance read a frame of it;
  `render_texture_set_filter` sets how it is sampled (nearest by default), and
  `render_texture_save` writes a render texture to an image file exactly as it
  holds it, top-down, alpha untouched.
- **Shader inputs**: `shader_set_texture` gives a shader up to four more
  `sampler2D` inputs besides `texture0` (a colour ramp, noise, a mask), from a
  texture or a render texture; `shader_set_vec4_array` sets a `vec4[]` uniform
  (a list of lights); `vec3` is a new type and `shader_set_vec3` sets a `vec3`
  uniform. The images are bound whenever the shader runs, through
  `camera_set_post_shader`, `draw_instanced` and `shader_begin` (with
  `shader_begin`, raylib's batch forgets them at its next flush, so use it for
  a few draws). `njin_render_demo` keys 7 to 9 show it: night with lights, a
  dusk colour ramp and haze from a noise image, in one whole-frame shader.
- **2D lighting (PBR)**: `lighting_set` turns on lit worlds. `light_2d` is a point,
  spot or directional light (colour, kelvin `temperature`, HDR `intensity`, physical
  inverse-square `falloff` or linear, smooth, none, a `size` that sets the softness of
  its shadows). Surfaces are physically based: the sprite is the albedo, and
  `sprite::normal` and `sprite::material` (roughness, metallic, occlusion) feed a
  Cook-Torrance BRDF (GGX, Smith, Fresnel-Schlick, energy conserving; the same as raylib's
  `pbr.fs`, with the same "MRA" packing of the material map: metallic, roughness, occlusion)
  evaluated in linear 16-bit float HDR, then exposure, a tonemap (`tonemap_shoulder`,
  `tonemap_reinhard` or `tonemap_aces`) and gamma. `sprite::emissive` makes a sprite glow. Shadows come
  from `light_occluder` (box, circle, ellipse, capsule, polygon, wall lines, holes),
  `light_occluder_sprite` (the outline of the sprite frame on show, following
  animation and flips) and `light_occluders_from_tiles`; solids do not shadow
  themselves and a light inside one is not blocked by it. Edges are sorted into
  angular buckets in a data texture, so a pixel tests a few edges rather than all.
  `light_occluder_pixels` casts pixel-perfect shadows from the alpha of a sprite (or a mask
  image), after mattdesl's "2D Pixel-Perfect Shadows": the occluders go to an image, each
  light ray-marches a 1D shadow map through it, and the soft edge is percentage-closer soft
  shadows. Its cost follows the light's area, not the number of occluders. The sun's
  shadows are as long as `lighting_desc::shadow_reach` (an object only so tall), and the
  1D shadow map keeps several runs per column for it.
  `render_demo` keys L, N, O, M, P show it.
- **Logo and icon**: the njin mark and wordmark (SVG, PNG, ICO) are in
  `docs/images/brand/`. `njin_icon(<target> [file.ico])` embeds an icon in a
  game's executable, its window and its taskbar button on Windows, and defaults
  to njin's; `njin_package` uses it, and every sample game without an icon of
  its own and the inspector now carry the njin one.
- **New sample games**: `njin_tower_defense` is a tower-defense game with enemy
  waves; `njin_moteswarm` lets you steer one creature among a swarm of
  wandering creatures, demonstrating procedural motion and a shader pass; and
  `njin_paper_crowd` fills a sheet of watercolour paper with a crowd of tiny
  people who each decide what to do next, drawn with instancing from a sprite
  sheet baked at startup (B compares it with drawing every person live, E writes
  the sheet out as a PNG with a coloured preview and a JSON of its frames).
  Click anyone to follow them up close.
- **Build and editor setup**: Windows scripts now use the CMake presets, with
  VS Code build, run and debug tasks included. Configure-time and compile-time
  checks keep game code behind njin's public API instead of including or
  linking directly to raylib.
- **Logging**: window and audio backend messages use the `njin` log tag.

## 0.4.0

- **Breaking for code that includes a split header directly.** `njin_ctx.h` no
  longer declares the input, audio, texture, shader, render-texture and camera-view
  functions: they moved to `njin_input.h`, `njin_audio.h`, `njin_render.h` and (the
  camera-view helpers `camera_active`, `w2scr`, `scr2w`, `camera_bounds`,
  `camera_set_post_shader`) `njin_camera.h`. No function was removed or renamed,
  and `njin.h` still includes everything, so a game that includes `njin.h`, the
  documented way, is unaffected. A game that includes `njin_ctx.h` on its own to
  reach those functions must include the new header (or `njin.h`). `njin_ctx.h`
  keeps the module and system registration, time and random. The engine's own
  private headers `njin_audio.h` and `njin_input.h` were renamed
  `njin_audio_impl.h` and `njin_input_impl.h` so the names are free for the public
  ones.
- **Input in one call**: `action_define(ctx, "jump", {key_space, key_w,
  pad_face_down})` and `axis_define(ctx, "move", {{key_left, key_right}, {key_a,
  key_d}}, {pad_axis_left_x})` register an action or axis and bind every key,
  mouse button, pad button, key pair and pad axis at once, and return the handle.
  The register and bind calls are unchanged.
- **`sprite_dissolve`**: a sprite dissolves patch by patch with a glowing edge,
  or appears the same way (`dissolve_fx::reverse`). Each patch gets a stable
  random number from a hash of its texel, so no noise texture is needed;
  `dissolve_fx` sets the edge colour and width, the patch size (`grain`), a `seed`
  and `destroy_when_done`. It follows `delta()`, so it freezes in hitstop, shares
  one draw pass with `flash_fx`, and shows in the inspector. A finished dissolve
  leaves the sprite hidden; a finished reverse removes the component.
- **Text maps**: `tilemap_from_text` and `tilemap_from_rows` build a tilemap from
  a multi-line string or a list of rows, one character per tile, with a legend
  (`tile_key`) from characters to tiles. Characters that only mark a place (player,
  enemy, coin) come back as `tile_marker` positions instead of placing tiles, and a
  legend entry can do both. Empty cells and markers never erase existing tiles, so
  calls can be layered. A leading newline in a raw string is dropped, and `\r\n`
  line endings are handled. This is a way to write a map with no editor; Tiled and
  LDtk levels are unchanged.
- **Procedural maps** (`njin_procgen.h`): everything works on a `tile_grid`, a
  plain grid of tile numbers that needs no window, and `tilemap_from_grid` puts it
  in a tilemap. `noise_2d` and `noise_1d` give seeded Perlin or value noise with
  fBm, ridged and billow layering, octaves, gain and domain warp (`noise_desc`).
  Rules that make a map look natural: `grid_majority`, `grid_smooth` (cave
  automaton), `grid_remove_small`, `grid_merge_small`, `grid_keep_largest`,
  `grid_border` and `grid_scatter` (spacing and a predicate). `generate_topdown`
  picks biomes from height and moisture (thresholds are percentiles of the map),
  with an island falloff and a guaranteed single walkable region;
  `generate_platformer` builds a ground line, pits, caves, floating platforms and
  optional slopes under rules that keep the level passable. `grid_autotile`
  rounds corners: 47-tile blob or 16-tile edge sets chosen by the eight
  neighbours, with joins between terrains and per-side control of what lies
  outside the grid. `wfc_learn` and `wfc_generate` run Wave Function Collapse
  (adjacency rules learned from a sample or written by hand, weights, a
  constraint callback, periodic output, retry on contradiction). The same seed
  gives the same map: a hash of 12 seeds of all three generators matched between
  GCC on Windows and on Linux at `-O0`, `-O2` and `-O3 -march=native`. The
  platformer generator was checked with a bot that drives the real
  `platformer_body` and reached the goal on 20 of 20 seeds with the documented
  settings; other settings (wider pits, taller steps than the character can jump)
  are not covered. The samples gained `terrain.png` (47-tile ground, stone and
  shore sets, built by `src/games/shared/tools/make_terrain.py`).
- **Screen recording in `njin_inspector`**: the Performance window has a Screen
  recording section (Record, or F9): the game grabs its finished frame every
  1/fps seconds, shrinks it (100, 75, 50 or 33%) and appends it to an animated
  GIF in its save folder, `recordings/rec_<date>_<time>.gif`, with a time limit
  of 5 to 120 seconds. Each frame has its own 256-colour palette; a frame equal
  to the previous one only lengthens it. The file is finished when recording
  stops, the limit is reached, the inspector goes away or the game exits. The
  inspector shows progress, the path, Open folder and Copy path. **The debug
  protocol is now version 4** (new `rec` command and message), so an inspector
  and a game must be built from the same version. `njin_inspector --layout
  consumption` opens that layout and `--size 1280x720` sets the window size.
  The inspector's panels now scale with its window: both layouts fill the whole
  window at any size (they were placed for 1700 x 960 pixels, leaving a strip of
  empty space in the overview), and resizing keeps each panel's place and share,
  including panels you moved. A screen smaller than 1700 x 960 gets a smaller
  window. Each grabbed frame costs the game a few milliseconds on its own thread
  (GPU read-back, shrink, compress).
- **Builds and releases**: `CMakePresets.json` has `debug` (`build/`) and `release`
  (`build-release/`) presets on Ninja. A Build workflow compiles every target on
  Windows with MSVC and on Linux with GCC, a Version workflow fails when
  `njin_version.h` and this file disagree, and a Release workflow creates the
  GitHub Release for a `vX.Y.Z` tag from its section here.
- **Docs**: a setup page for Windows, Linux and macOS; a 13-lesson track on C, C++,
  CMake, shaders (including signed distance fields) and game patterns to read
  before starting; the `first_jump` and `first_walk` tutorials; a "what do I use
  for X" cheat sheet; and real screenshots and animated captures of the samples,
  effects and `njin_inspector`.
- **Tested on**: Windows with GCC 15.2 (development), and a full build from a fresh
  clone on Ubuntu 26.04 under WSL2 (GCC 15.2). Not tested: MSVC, macOS, and any
  GPU other than an Intel Iris Xe for the shaders.

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
