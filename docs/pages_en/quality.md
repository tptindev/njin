# Graphics quality for weak hardware {#quality}

A game that looks great on a machine with a discrete card may run at 15 FPS on an office
laptop with an integrated GPU. Instead of making the game turn off each effect itself, njin
has a **quality ceiling** (njin::graphics_quality) that applies to all of a game's graphics
settings: shadows, screen effects, 2D lighting, particle counts, LOD, draw distance, and the
resolution the world is drawn at.

@include graphics_quality.cpp

## The game sets effects, the player sets the ceiling

The game still calls light3d_set(), post3d_set(), post_fx_set(), lighting_set() as usual,
with the best-looking image it wants. graphics_quality_set() only limits what actually gets
drawn: turning off shadows, turning off SSAO, lowering the light map's resolution, cutting
particles. The `_get` functions still return exactly what the game set, so:

- the game's code does not need `if (weak machine)` scattered around;
- changing the level while playing takes effect from the next frame, and going back to
  `quality_high` returns everything to how it was;
- a game that never calls graphics_quality_set() draws exactly as before: the default value
  of every field is "no limit".

## Three ready-made levels

graphics_quality_preset() returns the limit set for a level. Take it and tweak individual
fields if the game wants something different.

| Field | `quality_low` | `quality_medium` | `quality_high` |
|---|---|---|---|
| `world_scale` (the world's resolution) | 0.7 | 1 | 1 |
| 3D sun shadows | off | at most 1024 | as the game set |
| 3D point and spot light shadows | off | off | as the game set |
| SSAO | off | at most 8 samples | as the game set |
| SSR, TAA, motion blur | off | off | as the game set |
| Light shafts, lens flare | off | as the game set | as the game set |
| Bloom, depth of field (post_fx) | off | as the game set | as the game set |
| 2D light map (`lighting_desc::scale`) | at most 0.5 | at most 0.75 | as the game set |
| Particle count (2D and 3D) | 50% | 75% | 100% |
| Model LOD switch distance | ×2.5 | ×1.5 | ×1 |
| Grass and scatter draw distance; terrain LOD range | 50% | 75% | 100% |

The most expensive thing on an integrated GPU is usually **point light shadows** (each light
redraws the scene six times), then **the number of pixels to shade**: a 1920×1080 screen has
two million pixels, each running the lighting shader.

## Drawing the world at a lower resolution

`world_scale` is the strongest knob. 0.7 leaves only about half the pixels, 0.5 leaves a
quarter. The world (`phase_render`, both 2D and 3D) is drawn into a smaller image and then
scaled up smoothly; the UI (`phase_post_render`) and text still draw at the window's real
resolution, so menus and the HUD stay sharp.

Coordinates do not change at all: screen_size(), the mouse, the camera, w2scr() still work in
screen space as before.

`world_scale` does not apply when the game uses a virtual resolution
(window_set_virtual_size()): pixel art is already drawn into a small image, and shrinking it
further only breaks the look. It is also different from `config::render_scale`: that one draws
**larger** than the screen for antialiasing and can only be set at create(); `world_scale`
draws **smaller** and can be changed at any time. Setting both multiplies them together.

## Guessing a level for the first run

graphics_quality_suggest() reads the GPU name the OpenGL driver reports and returns a level:

- `quality_low`: machines with no real GPU (a software renderer), old Intel HD, Intel GMA, and
  mobile GPUs (old Mali, Adreno below 600, the Raspberry Pi's VideoCore, PowerVR);
- `quality_medium`: newer integrated GPUs (Intel UHD, Iris, an APU's Radeon Graphics/Vega,
  Adreno 600 and above, Mali-G7x);
- `quality_high`: everything else.

This is only a suggestion: a GPU name does not say how fast a machine actually is, and the
suggestion leans low to stay safe. Use it when there is no settings file yet, then let the
player change it in the menu.

## What the engine already does on its own, nothing to set

The following always run, at every level, and never change the picture:

- **A shadow pass only draws what can cast a shadow into that shadow map.** Each shadow pass
  (the sun, each face of a point light, a spot light) skips models, primitives and SDF shapes
  outside that light's range. A scene with many scattered objects and a few shadowed lights
  runs many times faster this way.
- **Primitives and SDF shapes outside the camera are not drawn**, same as models.
- **A light's shadow map is only as large as the number of shadowed lights**, not always
  sized for four lights.
- **3D particles outside the camera are not drawn**, and particle trails
  (`particles3d_desc::stretch`) draw in a single call.
- Drawing sprites, text, 2D collision and grass keeps temporary lists between frames instead
  of reallocating them every frame.

## The physics tick on a slow machine

When a machine cannot keep up with `config::fixed_hz`, `phase_fixed_update` runs several
steps in one frame, making that frame even longer. `config::fixed_max_steps` (default 8)
caps the number of steps per frame; a game for weak hardware should set 2–4 when calling
create(): physics slows down a little when FPS drops, instead of a growing stutter. See
@ref time.

## Saved along with settings

After the first call to graphics_quality_set(), settings_save() stores the whole limit set
under the `graphics` key of `settings.json`, and settings_load() loads it back. A game that
never calls graphics_quality_set() has no such key in its settings file. The level name shown
in the menu ("Low", "Medium"...) is the game's own, stored under the `game` key as in the
example above.
