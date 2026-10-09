# 3D outdoor world {#world_3d}

This page builds an outdoor landscape: hilly terrain with collision, grass swaying in the wind, rocks and trees
scattered by rules, lakes or seas with waves and floating bodies, a sky that follows the time of day, clouds,
rain, snow and fog. Everything is declared in `njin_world3d.h`, runs on OpenGL 3.3 like the rest of the 3D, and
takes the light, shadows, lamps and fog of @ref graphics_3d.

Read first: @ref graphics_3d (begin_3d(), lighting, 3D physics).

@image html world_3d.png "Terrain (a grass layer and a rock layer by slope), scattered rocks, a lake with Gerstner waves, foam along the shore, a floating crate and a cloudy sky"

## Terrain {#terrain3d}

terrain3d_create() builds a square height grid: `resolution` x `resolution` samples covering a square of side
`size` metres, its lowest corner at `origin`. The heights come from one of three sources:

| Source | Field | Notes |
|---|---|---|
| The game's array | `heights` | Metres, by row (z) then column (x) |
| A height map | `heightmap` | A grey image (red channel) or a 16-bit `.r16`/`.raw` file. An 8-bit image has only 256 steps: set `smooth` |
| Noise | `noise` | A njin::noise_desc from @ref procgen, in metres; stretched to 0..1 then multiplied by `height_scale` |

```cpp
const njin::terrain3d_handle ground = njin::terrain3d_create(ctx, {.origin = {-256, 0, -256},
                                                                   .size = 512.0f,
                                                                   .resolution = 513,
                                                                   .height_scale = 45.0f});
```

**Surface layers.** Up to 4 layers (njin::terrain3d_layer): each an image repeated by world position (one tile
every `tile` metres) or a plain colour, with a normal map if you want one. With `auto_splat` on (the default)
the engine works out which layer covers where from each layer's rules: a height range (`min_height`,
`max_height`) and a slope range (`min_slope`, `max_slope`), with soft edges `blend` wide. A later layer covers an
earlier one, so layer 0 is the widest cover (grass), then rock on steep ground, snow up high. With `auto_splat`
off the `splatmap` image is used (its red, green, blue and alpha channels are layers 0 to 3). On steep ground the
images are projected from three sides, so cliffs do not stretch them.

**Level of detail.** The grid is split into chunks of `chunk_quads` x `chunk_quads` squares. Chunks out of view
are skipped; a chunk further than `lod_distance` is drawn with half as many squares per side, twice as far again
half again, down to `lod_levels` levels. Each chunk's edge has a "skirt" hanging down, so two chunks at different
levels of detail never show a gap. Normals always come from the full grid, so the lighting does not change when
a chunk changes level.

**Asking the terrain.** terrain3d_height() gives the height at `(x, z)` exactly as the grid is drawn at full
detail and as the collision body has it: all three split each square into two triangles along the same diagonal.
terrain3d_normal() gives the normal, terrain3d_layer_weight() tells what you are standing on (to change the
footstep sound, the dust when running).

**Collision.** With `collision` (the default) the terrain has a static body, a Jolt height field: characters,
vehicles and dynamic bodies stand on it, raycasts hit it. terrain3d_body() returns that body so you can recognise
it in raycast results.

**Editing at run time.** terrain3d_edit() raises, lowers, flattens or smooths a round area
(njin::terrain3d_brush), terrain3d_paint() paints a layer on. Everything follows at once: the drawn grid, the
collision body, the automatic layers (a hill piled up into a steep slope shows rock), the grass grows again, and
the rocks and trees scattered there take the new ground height. Bodies lying on ground that is raised are lifted
with it, not swallowed by the new ground.

```cpp
// A bomb makes a crater: down 2 m at the centre, with a soft edge.
njin::terrain3d_edit(ctx, ground, {.kind = njin::terrain3d_lower, .center = blast, .radius = 4.0f, .strength = 2.0f});
```

## Grass and scattered objects {#grass3d}

grass3d_create() grows grass on one layer of the terrain (`layer`, -1 for everywhere), not on ground steeper than
`max_slope`, in patches by `patchiness`. Each blade is a few triangles drawn with instancing; grass is only made
for the area round the camera (`draw_distance`), thinner and lower further out, so however wide the terrain is,
only the near part costs anything. Grass sways in the wind: wind3d_set(), or the weather's wind when the sky is
drawn with draw_sky3d(). Grass receives shadows; for it to cast them turn on `cast_shadows` (costly).

scatter3d_create() scatters a model (rocks, trees, bushes) over the terrain by rules: density, a minimum distance
`spacing`, height and slope ranges, a layer, clusters from noise. Objects lean with the ground by `align`, sink by
`sink`, and vary in size. The places are picked once from `seed` and drawn with draw_instanced3d() area by area:
areas out of view are skipped, and `far_model` is used for areas further than `lod_distance`. For trees that need
collision, get their placement with scatter3d_transforms() and make a body for each.

```cpp
const njin::scatter3d_handle trees = njin::scatter3d_create(ctx, {.terrain = ground,
                                                                  .model = pine,
                                                                  .far_model = pine_low,
                                                                  .density = 0.004f,
                                                                  .spacing = 6.0f,
                                                                  .max_slope = 25.0f,
                                                                  .patchiness = 0.8f});
```

## Water {#water3d}

water3d_create() makes a water surface at height `level`: a rectangular lake (`center`, `size`), or a sea reaching
to the horizon when `size` is `{0, 0}` (the grid follows the camera, dense near it and sparse far away). The
surface is made of up to 8 Gerstner waves (njin::water3d_wave): sharp crests, flat troughs, long waves travelling
faster than short ones as in deep water.

With a `terrain` attached the water knows how deep it is everywhere: its colour goes from `shallow_color` to
`deep_color`, shallow water is clear down to the bottom (`clarity`), and there is foam along the shore
(`foam_width`). The water reflects the sky of draw_sky3d() (clouds included), glitters in the sun and receives
shadows. Water is drawn after every opaque shape, like glass.

water3d_height() and water3d_normal() compute exactly the shader's wave formula, so objects the game places on the
water match the picture. water3d_float() makes a dynamic body float: each physics step the part of its volume
below the wave surface pushes it up (`buoyancy` 1 hovers, more floats), the water drags against its movement and
turning, and `flow` carries it along a current.

```cpp
const njin::water3d_handle sea = njin::water3d_create(ctx, {.level = 0.0f, .terrain = island});
njin::water3d_float(ctx, sea, raft, {.buoyancy = 1.8f});
```

## Sky and weather {#sky3d}

draw_sky3d() draws the sky behind everything in the 3D pass (it only covers what nothing was drawn over): a
background by time of day, the sun, clouds drifting with the wind, stars and the moon at night. njin::sky3d sets
the hour (`hour`), latitude and season: the sun rises in the east (`+x`), is highest at 12 o'clock in the south
(`+z`), and sets in the west. With `drive_light` (the default), draw_sky3d() also sets the pass's light: the
direction and colour of the sunlight (golden early and late, the pale blue moon at night), the ambient light from
the sky's colour, and the fog colour from the horizon; the shadow settings of the current light are kept. To
handle it yourself, turn it off and call sky3d_light().

The weather (njin::weather3d) goes in `sky3d::weather`: cloud cover (`clouds`) and how dark the clouds are, fog,
rain, snow, wind, how wet the ground is. Five kinds are ready (weather3d_preset()): clear, overcast, rain, snow and
fog. weather3d_lerp() moves from one to another gradually. Rain and snow fall in a box round the camera, computed
entirely on the GPU; each drop keeps its place in the world as the camera moves, and the wind slants the rain.

```cpp
njin::sky3d sky{.hour = 17.5f};
sky.weather = njin::weather3d_lerp(njin::weather3d_preset(njin::weather3d_clear),
                                   njin::weather3d_preset(njin::weather3d_rain), storm);
njin::draw_sky3d(ctx, sky);
```

@image html world_3d_rain.png "Rain: an overcast sky, wet ground darker and shinier, raindrops slanted by the wind"

**Cover.** Rain and snow do not fall under roofs, and the ground under a roof stays dry. By default
(`weather3d::cover_auto`) the engine finds the roofs itself: while it rains or snows it draws the depth of every
opaque shadow-casting shape seen straight down, over a 36 m square round the camera (14 cm cells), and hides any drop
under a surface. That map is drawn again every 8 frames or when the camera has moved 2 m, so it costs next to nothing
(measured on an RTX 3050 Laptop: no difference from turning it off, within 0.1 ms of noise). Shapes that cast no
shadow give no cover. To mark covered places yourself (a porch that casts no shadow, or with `cover_auto` off to save
that pass), set boxes with weather3d_cover_set(): rain does not fall in the part of a box below its top face.

```cpp
const njin::weather3d_cover porch{.center = {4.0f, 1.5f, -2.0f}, .size = {3.0f, 3.0f, 2.0f}};
njin::weather3d_cover_set(ctx, &porch, 1);
```

## Full example

Hills with grass, rocks and snow; a lake; a floating wooden crate; a day passing in two minutes; the R key turns
the rain on; the left mouse button piles up earth.

@include world3d.cpp

## Performance and limits

Measured on an RTX 3050 Laptop, Release build, 960 x 540: a 1 km² terrain (1025 x 1025 samples, one per metre)
with grass, about 9000 rocks, a lake, the sky and the sun's shadows draws in about 1.7 ms per frame. Making that
terrain takes about 0.4 seconds (generating the noise, the automatic layers, the collision body), so make it while
loading a level, not mid-game.

- Cover (`cover_auto`) only knows shapes within the 36 m square round the camera that cast shadows; slanted rain
  still blows in under the edge of a roof. Wet ground's shine applies to the dry ground under a roof as well (only the
  darkening follows the roof).
- Water does not reflect objects on the shore, only the sky. The shore and depth only follow the attached terrain,
  not other models under the water.
- Scattered objects are placed once at creation; editing the terrain only changes their height.
- Each terrain is one square; for a larger world, put several terrains side by side.
