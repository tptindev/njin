# Drawing images and shaders {#rendering}

This page covers **textures** (images from files) and **shaders**. Shapes and text are in @ref drawing,
animated sprites in @ref sprites, off-screen images in @ref post_processing.

## Handles

Textures, shaders and render textures are managed through **handles**, a small struct holding an `id`:

- `id == 0` is an **invalid** handle, for example when the file does not exist.
- Pass an invalid or already unloaded handle to any function and that function **does nothing**,
  without raising an error.
- An unloaded handle never points to a new resource by mistake.

Resources are released automatically when you call njin::njin_destroy(). You only need to call
`*_unload` when you want to release something early.

## Textures

```cpp
njin::texture_handle tex = njin::texture_load(ctx, "assets/player.png");
njin::vec2 size = njin::texture_size(ctx, tex);
njin::texture_draw(ctx, tex, {100.0f, 100.0f}, {1.0f, 1.0f, 1.0f, 1.0f});
```

- njin::texture_load() loads an image; the path is relative to the working directory when the game runs.
- njin::texture_draw() draws with the **top-left corner** at the given position.
- The last parameter is the **tint color** (njin::rgba, each channel 0..1). White `{1,1,1,1}` keeps
  the image as it is; `{1,0,0,1}` keeps only the red channel.

Draw textures in `phase_render` (world space, affected by the camera) or
`phase_post_render` (screen space). See @ref game_loop.

## Atlas: packing many images into one texture {#atlas}

Raylib merges **consecutive draw commands that share a texture** into one command. Sprites sorted by `layer`
or by y (top-down) that use many different textures get broken up every time the texture changes: 90 sprites
alternating between three images is 90 draw calls. Pack the small images into an atlas and they share one texture, and the number
of draw calls drops to two.

```cpp
const njin::atlas_handle atlas = njin::atlas_create(ctx, {.size = 2048});
const njin::texture_handle hero = njin::atlas_load(ctx, atlas, "assets/hero.png");
const njin::texture_handle tree = njin::atlas_load(ctx, atlas, "assets/tree.png");
```

The result is a regular njin::texture_handle: you can pass it to njin::sprite, njin::tilemap, njin::particle_emitter,
the UI or njin::texture_draw(). Drawing an image without scaling or rotation is **pixel-for-pixel identical**
to loading it on its own.

| Concern | How it is handled |
|---|---|
| An image bleeds into its neighbor when scaled or rotated | Each image has a `padding` border (1 pixel by default) that repeats its outermost pixels |
| The page is full | The atlas adds a new page automatically; images on different pages are still two textures |
| An image larger than the whole page | Loaded like njin::texture_load(), with a warning in the log |
| njin::texture_size() | The size of the image, not of the whole page |
| Your game's own shader | Samples using the coordinates of the whole page, not of the image. An image used with that kind of shader should be loaded with njin::texture_load() |
| njin::texture_set_filter() | Changes the filter of the whole page |
| Hot reload, njin::texture_unload() | Do not apply to images in an atlas; the space they took is not reclaimed |

Put small images that often appear together into an atlas (characters, enemies, items, bullets), and
load big tilesets or background images separately.

## Culling and draw call counts {#render_stats}

The sprite module skips sprites and particle emitters that are **entirely outside the camera**: no sorting, no
vertex generation. With 40,000 sprites scattered over the map and only a few hundred inside the frame, that takes about 1 ms
instead of 9 ms. While the camera is shaking, culling is turned off temporarily, because the shake reveals a bit of what is outside the frame.

In the **Performance** window, njin_inspector shows what the last frame drew: the number of sprites (and how many were culled),
the number of tilemap chunks, the number of particles (and how many are on the GPU), and the **estimated number of draw calls**. Raylib does not report the real number of draw
calls, so this figure is computed from texture and blend mode changes; it goes up when sprites with different
textures alternate, and goes down when you use an atlas.

## Discrete GPU on laptops with two graphics cards {#discrete_gpu}

Laptops with an onboard (integrated) card and a discrete card usually start programs on the onboard card to
save battery. njin games ask to run on the **discrete card** when the machine has one, on every operating system:

| Operating system | How it works |
|---|---|
| Windows | The `.exe` exports two variables, `NvOptimusEnablement` and `AmdPowerXpressRequestHighPerformance`, which the NVIDIA and AMD drivers look for. The two variables must live in the `.exe` itself, so the game links the `njin::gpu` target (compiled straight into the exe): `target_link_libraries(my_game PRIVATE njin::rt njin::gpu)` |
| Linux | Only on **laptops** (detected by the SMBIOS chassis type, or by having a battery): sets `DRI_PRIME=1` (Mesa), and for the proprietary NVIDIA driver, `__NV_PRIME_RENDER_OFFLOAD=1` together with `__GLX_VENDOR_LIBRARY_NAME=nvidia` if NVIDIA's GLX library is present on the machine. Any variable the user has already set is left alone |
| macOS | Nothing to do: on a dual-GPU Mac the discrete card is used for every app that does not ask for automatic graphics switching |

On a machine with only one card, nothing changes.

**Desktop PCs** are different from laptops: the card that runs the game is the one **the monitor is plugged into**. If the monitor is plugged into
the discrete card, the game already runs on the discrete card; if it is plugged into a motherboard port (the onboard card), the game runs on
the onboard card, and the engine cannot change that. On Linux the engine deliberately does not set the offload variables for desktop
PCs, because there "the other card" is the onboard card. For a PC with two cards, the reliable fix is to plug the monitor
into the discrete card. The player still has the final say: the Windows *Graphics
settings* page or the driver control panel overrides this request per game. Turn it off entirely at build time
with `-DNJIN_PREFER_DISCRETE_GPU=OFF`. A game that does not link `njin::gpu` makes no such request on Windows. The startup log says which card is being used (the
`Renderer:` line).

## Shaders

Shaders change how everything is drawn. njin uses GLSL 330.

@include texture_shader.cpp

The flow: set uniforms, `shader_begin`, draw, `shader_end`. Everything drawn between
`shader_begin` and `shader_end` goes through the shader.

@warning Set uniforms **before** `shader_begin`.

An example fragment shader that turns an image gray:

@include gray.fs

A few things to know:

- Pass `nullptr` for the vertex shader (or the fragment shader) to keep raylib's default shader.
- `fragTexCoord`, `fragColor`, `texture0`, `colDiffuse` are names **set by raylib**.
  `texture0` is the image being drawn; `fragColor` is the tint color you pass to `texture_draw`.
- njin::shader_load() returns a handle with `id == 0` if the file is missing or fails to compile, and logs the reason.
- A uniform that does not exist in the shader is only reported with a warning **once**, then ignored.

The functions that set uniforms:

| Function | GLSL type |
|---|---|
| njin::shader_set_i32() | `int` |
| njin::shader_set_f32() | `float` |
| njin::shader_set_vec2() | `vec2` |
| njin::shader_set_vec3() | `vec3` (a color without alpha, a direction) |
| njin::shader_set_vec4() | `vec4` |
| njin::shader_set_vec4_array() | `vec4[]` (an array, for example a list of lights) |
| njin::shader_set_texture() | an extra `sampler2D` besides `texture0` (palette, noise, mask) |

Need to read one more image, take a list of lights, or run over the whole frame? See @ref shader_advanced.

## Instancing: thousands of shapes in one draw call {#instancing}

When you need to draw **lots** of identical shapes that each differ a little (a crowd, a flock of birds,
grass, rain), drawing each one with `texture_draw_ex` makes the CPU build 4 vertices for every one of them, every frame. With
instancing, the game only writes **a few numbers per shape** into a buffer; the GPU builds the quad for each
one itself, all in **one draw call**.

@include instancing.cpp

The vertex shader receives the unit quad and the data of the instance being drawn, then places it in the world itself:

@include dots.vs

@include dots.fs

A few things to know:

- Each instance has 4, 8, 12 or 16 floats. The shader reads them as `vec4 instance0` through
  `instance3`, in order. What each number means is up to the game.
- `vertexPosition.xy` is the corner of the quad, from `(0, 0)` to `(1, 1)`, with y pointing down. `mvp` is the current
  camera: instances follow the camera like everything drawn in `phase_render`.
- Later instances draw over earlier ones. If you need sorting (for example by y in a top-down game), sort
  the data before writing it.
- njin::draw_instanced() draws a **range** of the buffer (`first`, `count`), so you can write once and
  draw several ranges in between things drawn other ways. Each call is one draw call.
- Other shader uniforms are set with njin::shader_set_f32() and similar functions as usual, but
  do **not** call njin::shader_begin(): njin::draw_instanced() turns the shader on itself.
- On a machine without OpenGL 3.3, njin::instancing_available() returns `false` and these functions do
  nothing.
- njin::draw_instanced() also has a version that takes a njin::texture_handle or njin::render_texture_handle: the texture
  is bound to the shader's `uniform sampler2D texture0` (see the next section).

### Draw once up front, read back every frame {#instancing_bake}

Instancing spares the CPU, but if the fragment shader has to **recompute** the whole shape (a long SDF formula)
for every pixel of every instance, the GPU still carries that load every frame. When the number of truly different shapes is small
(a few dozen poses), draw each shape **once** into a render texture, then let the instances only read it back:

1. Create a njin::render_texture_load() big enough to hold every frame, arranged in a grid. Set njin::render_texture_set_filter()
   to `filter_linear`, because the default is `filter_nearest`.
2. In `phase_post_update` of the first frame, write one instance per frame (the position is the cell center, the size
   is the cell), then njin::render_texture_begin(), draw with njin::draw_instanced() **without a texture** (it is drawing into
   itself), njin::render_texture_end().
3. Every frame, write the player's instance with the **frame index** in place of the pose parameters, and draw with
   njin::draw_instanced() with the render texture attached. The fragment shader samples `texture0` at the matching cell.

To **look at** the sprite sheet, or keep it as an asset, call njin::render_texture_save(): it saves the render texture to a
PNG immediately, in the same orientation it was drawn and **keeping every byte** (alpha included), so a sheet that holds
data rather than colors is still saved exactly as it holds it. For an easy-to-read image, draw the frames through the game's own
drawing path into a second render texture, one color per frame, then save both.

A few easy mistakes:

- The texture coordinates of a render texture have their origin at the **bottom-left corner** of the drawn image: `v = 1 - y / height`.
- A frame does not have to store color. Store a few numbers (coverage, which parts take each person's own color) and multiply
  in the color when drawing, and one sprite sheet works for every color. Store it **premultiplied** (numbers already multiplied by coverage) so that
  linear filtering leaves no dark fringe at the edges.
- Two adjacent frames can be blended for smooth motion, but only when the pose changes little between the two frames. If a
  limb changes shape quickly (an elbow popping out as the arm passes the shoulder), blending shows a ghost limb: bake more frames
  and pick the nearest one.
- Zooming the camera past the baked resolution makes the shapes blurry. Switch to direct computation when zoomed in close, when only
  a few instances are left in view.

## Hot reload

Edit an image or a shader while the game is running, save, and see the result right away, with no
restart:

```cpp
#ifndef NDEBUG
njin::hot_reload_enable(*ctx, true);
#endif
```

When enabled, the engine checks the modification time of every loaded texture and shader file a few times per second.
A changed file is reloaded **into the same old handle**: sprites, tilemaps, level images and post shaders that use
it change immediately, with no code edits. A file that just changed is loaded at the next check, once
it has stopped changing, so the engine does not read a file the editor is still halfway through writing.

A shader that **fails to compile keeps the old version**, and the compiler's error is in the log: saving a broken shader
does not break the game, and saving a fixed one brings the new version in right away.

| Function / event | What it does |
|---|---|
| njin::hot_reload_enable() | Turns it on or off, and sets the interval between checks |
| njin::hot_reload_now() | Checks and reloads immediately, even while off (for example bound to the F5 key) |
| njin::asset_reloaded | An event sent through njin::events() after every reload, including failed ones |

Off by default: checking files costs a little time, and a released game does not need it.
