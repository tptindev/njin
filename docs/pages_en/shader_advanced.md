# Advanced shaders: extra images, uniform arrays, lighting {#shader_advanced}

This page is for when a shader with one image and a few numbers (`amount`, `time`) is no longer enough: the shader needs to read **one more image**
(a color ramp, noise, a mask), take **a list** (light sources), or run over **the whole frame**.

If you want **lighting** (lights, shadows, materials), the engine already has it, see @ref lighting; this page teaches you how to write a shader like that yourself, and much more.

You should already know: the Shader section of @ref rendering, and the three shader lessons in @ref learn (lessons 10 to 12). Run `njin_render_demo`
and press keys 7, 8, 9 to see everything below in action.

## Four things the engine gives shaders

| You need | Use | Notes |
|---|---|---|
| Read one more image besides `texture0` | njin::shader_set_texture() | Up to 4 extra images per shader. Takes a texture or a render texture |
| Take a list of numbers | njin::shader_set_vec4_array() | An array of `vec4`. `vec2` and `vec3` are packed into `vec4` |
| Run over the whole frame | njin::camera_set_post_shader() | One full-screen pass, UI not included |
| Run several passes in a chain | njin::render_texture_begin() then njin::shader_set_texture() | The first pass draws into a render texture, the next pass reads it |

## Example: night, color ramp and haze in one shader

A fragment shader that runs over the whole frame, with three independent effects, each with a uniform from 0 to 1 to fade it in:

- **Night** (`night`): the frame darkens, then each light source adds a patch of light. The light sources are a **`vec4` array**.
- **Dusk** (`dusk`): each pixel's brightness is **looked up in a `256 x 1` color ramp image** (`ramp`), so dark becomes
  blue-violet and bright becomes warm cream.
- **Haze** (`haze`): two samples of a **noise image** (`noise`) nudge the spot where the shader reads the frame, so the whole scene
  ripples like heat.

@include shader_scene.fs

On the C++ side you load the shader, attach the two extra images **once**, then set the uniforms and the light list every frame:

@include shader_scene.cpp

@image html render_shader_night.gif "Night (key 7): the character's light, a light at the mouse cursor and four fixed lights, all in one vec4 array. The lamp flickers because its brightness changes over time"

@image html render_shader_dusk.png "Dusk (key 8): each pixel's brightness is looked up in a 256 x 1 color ramp image, so the whole scene gets one single band of colors"

@image html render_shader_haze.gif "Haze (key 9): the noise image nudges where the frame is read, so the trees and the water ripple gently"

@image html render_shader_haze.png "Haze, still image"

The three keys run the same shader and set `night`, `dusk`, `haze` to 0 or 1. Turn on all three and they stack. When all three
are 0, `njin_render_demo` turns the shader off with `camera_set_post_shader(ctx, {})`, so you do not pay for an extra pass.
The built-in effects (blur, bloom, CRT on keys 4 to 6) run **first**, so your shader sees their result.

## Extra images: njin::shader_set_texture()

```cpp
njin::shader_set_texture(ctx, scene, "ramp", njin::texture_load(ctx, "assets/ramp.png"));
```

In the shader, declare `uniform sampler2D ramp;` and sample it like `texture0`. Calling the function **once** is enough: the engine re-attaches
the image every time the shader runs (`shader_begin`, `camera_set_post_shader`, `draw_instanced`). If the image is reloaded by hot reload, the shader sees the new image.

Things to know:

- **Up to 4 extra images** per shader; the fifth is ignored and a warning line is logged. Setting the same name again replaces the image.
- **Use a standalone image** loaded with njin::texture_load(). An image packed in an atlas is rejected with a warning, because the shader would see the whole
  atlas page and not just that image.
- **The filter belongs to the image**: for a color ramp, leave `filter_linear` (the default) so colors blend smoothly; for pixel art that needs exact
  sampling, use njin::texture_set_filter() with `filter_nearest`.
- **A noise image should tile**: by default the image repeats when coordinates go past 1, so `uv * 3.0` reads the image three times
  seamlessly if the image has no seam. `assets/noise.png` is generated that way by `tools/make_assets.py`.
- **Render textures are stored upside down (vertical axis flipped).** When you attach a render texture, read it with `vec2(uv.x, 1.0 - uv.y)`. The
  frame that `camera_set_post_shader` feeds into `texture0` is flipped the same way: in the example above, `pixel` is computed as
  `vec2(uv.x, 1.0 - uv.y) * resolution` so it has the same orientation as screen coordinates.
- Do not attach a render texture that **is being drawn into** (between njin::render_texture_begin() and njin::render_texture_end()).

### Where extra images are reliable

| How the shader is turned on | Extra images |
|---|---|
| njin::camera_set_post_shader() | Good: one full-screen pass |
| njin::draw_instanced() | Good: the engine re-attaches them for each draw call |
| njin::shader_begin() then drawing sprites | Only lives until the next time raylib flushes the batch |

With `shader_begin()`, raylib groups draw calls into a batch and **forgets** the extra images every time it flushes the batch: when the batch is full (8192
shapes), after 256 texture changes, or when a njin::draw_instanced() call or a render texture change comes in between. Use it for a few large draw calls
(a background layer, a few sprites), not for thousands of sprites. If you need thousands of shapes with extra images, use njin::draw_instanced().

## Uniform arrays: njin::shader_set_vec4_array()

```cpp
const njin::vec4 lights[2] = {{320.0f, 180.0f, 190.0f, 1.0f}, {900.0f, 500.0f, 130.0f, 0.9f}};
njin::shader_set_vec4_array(ctx, scene, "lights", lights, 2);
```

In the shader: `uniform vec4 lights[8];`. The name you pass is `lights`, without `[0]`. A few points:

- `count` **must not be larger than** the size declared in the shader. The maximum number of elements also depends on the graphics card;
  a few dozen is safe.
- An array that is **not fully used** is normal: set one more `int` (like `light_count`) that says how many are used, and the shader loops
  up to that.
- **`vec2` and `vec3` are packed into `vec4`.** For a `vec3` color, use the `.rgb` of the `vec4` element; for two `vec2`, use `xy` and `zw`.
  In the example, a light's position, radius and brightness share one `vec4`, and the color is in a second array with the same index.
- Like every uniform, set it **before** the shader runs. For the camera's post shader, setting it in `phase_pre_render` is early enough.
- The light coordinates in the example are **screen pixels** (computed with njin::w2scr()), so the patch of light stays in the right place even when the camera zooms or scrolls.

## Several passes in a chain

To have a later pass read the result of an earlier pass (a light mask, a blurred copy of the background), draw the earlier pass into a render texture and then
attach it to the later pass's shader:

```cpp
mask = njin::render_texture_load(ctx, 256, 256);
njin::shader_set_texture(ctx, sheet_shader, "mask", mask); // once, in phase_startup

// every frame, in phase_post_update or phase_post_render:
njin::render_texture_begin(ctx, mask, {0.0f, 0.0f, 0.0f, 1.0f});
// ... draw the mask ...
njin::render_texture_end(ctx);
```

njin::render_texture_begin() resets the camera transform, so call it only outside the world phases. The full phase rules
are in @ref post_processing. Remember to read it with `1.0 - uv.y` in the shader (see above).

## Editing shaders while the game runs

Turn on hot reload in your game (njin::hot_reload_enable(), see @ref rendering) and saving a `.fs` file reloads the shader
right away; if it fails to compile, the old shader stays and the error shows up in the log. It is the fastest way to tune the width of a patch of light or the haze
factor. `njin_render_demo` does not turn on hot reload: add one line to `main.cpp` if you want to try it.

## Not there yet

These are not in the API because no game has needed them yet: `mat4` uniforms, extra images that survive
every batch flush when drawing thousands of sprites with `shader_begin` (use njin::draw_instanced() or a full-screen pass), and
built-in uniforms like time and resolution (set them yourself with njin::shader_set_f32() and njin::shader_set_vec2(), as in the
example).
