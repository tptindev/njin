# Drawing shapes and text {#drawing}

Every draw function must be called in the three draw phases. The space depends on the phase (see @ref game_loop):

| Phase | Space | Use for |
|---|---|---|
| `phase_render` | World, through the camera | Characters, maps, effects |
| `phase_post_render` | Screen, pixels | UI, score, menus |

@include drawing.cpp

## Shapes

| Function | Draws |
|---|---|
| njin::draw_rect() | A filled rectangle |
| njin::draw_rect_lines() | A rectangle outline, the outline sits inside |
| njin::draw_rect_rotated() | A rectangle rotated around its center |
| njin::draw_circle() / njin::draw_circle_lines() | A filled circle / an outline |
| njin::draw_line() | A line segment with thickness |
| njin::draw_triangle() | A triangle. The order of the three vertices does not matter |

Color is njin::rgba, each channel 0..1. A few colors are available in `njin::colors` (`white`, `black`,
`red`, `green`, `blue`, `yellow`, `gray`, `transparent`).

## Text

njin::draw_text() draws UTF-8 text with its top-left corner at the given position, and supports line breaks
with `\n`. njin::text_measure() gives the size of the text before you draw it, use it for centering or
right-aligning.

The **default font** (when you pass no font) is JetBrains Mono (SIL OFL license), embedded in the
engine so the game needs no font file. It has the Latin alphabet and the full Vietnamese alphabet. If you want a different
font, load a TrueType/OpenType font with njin::font_load(); loaded fonts also have the Latin and Vietnamese
alphabets available.

Each text size that gets drawn has its own glyph image, built at exactly that size the first time it is drawn. The size is rounded
to a whole number of pixels (from 6 to 256) and the position is also rounded to a pixel, because half a pixel of offset is
half a pixel of blur on a letter ten pixels tall. njin::text_measure() measures using that exact glyph image,
so centering and right-aligning match the text that gets drawn.

If loading fails (file missing, unreadable), it returns a handle with id 0, so the text falls back to the default
font instead of disappearing.

### Pixel-style text

njin::font_set_style() with njin::font_pixel (or the `style` parameter of njin::font_load()) turns off
antialiasing and uses nearest filtering: each glyph texel is either on or off, and the text is scaled with
the same nearest filter as sprites. The empty handle `{}` changes the default font.

@snippet drawing.cpp pixel_text

This style is **only crisp at the exact size the font was designed for**: a pixel font like Press Start 2P designed
at 8 pixels should be drawn at 8, 16, 24. A regular vector font (like JetBrains Mono) at a small size will be jagged and
hard to read. Pixel text is always drawn inside the virtual image, and does not go through the crisp text layer below, so turn on
`integer_scale` so that every text pixel is the same size.

### Smooth UI on a screen with a virtual resolution

Besides text, rounded panels, buttons, sliders and every shape drawn in the small image also break up into blocks when
scaled with nearest filtering. Set njin::config::smooth_ui and the **world is still drawn in the virtual image** (pixel
art), while `phase_post_render` (UI, HUD), dialogs, toasts, flashes and fades are drawn **after the image has been
scaled**, straight into the window: the engine sets a transform (translate and multiply by the scale) so coordinates
still follow virtual pixels, but shapes are rasterized at window resolution, and text is built at the font size
multiplied by the scale. The draw order is fully preserved, with none of the layering trade-offs of the crisp text layer
below. In this mode `crisp_text` no longer has any effect.

Notes:
- njin::clip_begin() still takes virtual pixel coordinates. The UI skin shader receives `uiRect` in window pixels.
- Textures drawn in the UI are filtered by their njin::texture_filter at window resolution, so
  pixel art icons need njin::filter_nearest to keep the pixel look.
- Pixel fonts are still scaled with nearest filtering like sprites.
- It has no effect when the scale is 1.

### Crisp text on a screen with a virtual resolution

With njin::config::virtual_size, the whole frame is drawn into a small image (for example 640x360) and then scaled
up to the window, so text drawn in that image blurs with the scale. When the machine has a **real GPU** and
njin::config::crisp_text is on (the default), on-screen text (UI, HUD, dialogs, notifications)
is not drawn into the small image but queued, then drawn **after the image has been scaled**, straight into the window, from
a glyph image built at the font size multiplied by the scale. Text is crisp at every window size, including fractional scales.
Positions still follow virtual pixels, and each line has its letter spacing stretched so it is exactly as wide as what
njin::text_measure() measured.

Trade-offs:
- Queued text sits on top of the virtual image, so the engine records what is drawn **after** the text and covers it:
  njin::draw_rect() and the UI's backgrounds, buttons and sliders. Text under a translucent shape (fade, flash,
  the dimming layer behind a popup, a semi-transparent panel) is blended toward that shape's color; under an opaque shape it
  is dropped. This way a panel drawn later hides text drawn earlier, matching the draw order. Sprites and textures drawn after
  the text do **not** hide it; if text needs to sit under them, draw the text first with a pixel font or
  in the world.
- njin::clip_begin() is respected: queued text is clipped to the clip region at the time it was drawn.
- Text in the world (in `phase_render` with a camera) and text drawn into a render texture are still drawn inside the
  virtual image, because they belong to that space and image.
- Machines that only have a software renderer (llvmpipe, SwiftShader, GDI Generic) always draw text inside the virtual
  image as before. So does a window exactly the size of the virtual image (scale 1).

### Antialiasing with supersampling {#render_scale}

njin::config::render_scale draws both the world and the UI at `render_scale` times the resolution
(2, 4 or 8) and then shrinks it back with a smooth filter when going to the window, smoothing the edges
of shapes, rotated sprites and curves. `1` (the default) is off.

```cpp
njin::config cfg{};
cfg.render_scale = 4; // read it from the game's settings file, do not hardcode
njin::context *ctx = njin::create(cfg);
```

This is **not** the window's MSAA (raylib only has a single 4x level through GLFW,
2x/8x cannot be chosen): this works the same on every GPU and gives exactly the
level you set, in exchange for the GPU drawing that many times more pixels (16 times at level 4).

Notes:
- No coordinate changes: njin::screen_size(), the mouse and the camera are still computed as
  `virtual_size` (if set) or the window size (if not) — the game knows
  nothing about `render_scale`.
- **It cannot be changed while running.** create() creates the window and the render
  textures with exactly this value, once; changing the level in a settings menu means saving
  the choice and restarting the game to apply it, like changing resolution in most
  other games.
- Pixel art uses `virtual_size` with njin::filter_nearest so it usually does not need this;
  it suits games that draw shapes, rotated sprites or vector text better.
- A very large window at a high level may exceed the GPU's maximum texture size; the engine
  lowers the level to the highest one that still fits and writes to the log when that happens.

## Blending

njin::blend_begin() changes how new colors blend with existing ones, until njin::blend_end():

| Mode | Effect | Use for |
|---|---|---|
| njin::blend_alpha | Blends by transparency (default) | Everything normal |
| njin::blend_additive | Adds colors, gets brighter | Fire, light, sparks |
| njin::blend_multiply | Multiplies colors, gets darker | Shadows, color overlays |

## Clipping

njin::clip_begin() only allows drawing inside a rectangle, in **screen pixels**,
until njin::clip_end(). Use it for scrolling panels in the UI.

## Advanced textures

njin::texture_draw_ex() draws a texture with a source region (for sprite sheets), scale, anchor point, rotation
angle and flipping, through njin::texture_draw_desc. You usually do not need to call it directly: the
njin::sprite component does this for you, see @ref sprites.

njin::texture_set_filter() chooses how a texture is sampled when it is scaled up: njin::filter_linear
(the default, smooth) or njin::filter_nearest (keeps the pixels as they are, for pixel art).
