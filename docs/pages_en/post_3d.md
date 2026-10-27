# 3D screen effects {#post_3d}

This page adds effects to the 3D scene that work on the image of the whole frame, from the depth of each pixel:
wall corners and the feet of objects darken (SSAO), polished floors reflect (SSR), bullet holes, scorch marks and
paint stick to any surface (decals), the image smears as the camera turns (motion blur), sunlight shines in rays
through gaps, the lens flares, and edges are smoothed over time (TAA) or from the image (FXAA, SMAA), plus bloom on
the HDR image and transparency that does not depend on order (OIT). Everything is declared in `njin_post3d.h` and
runs on OpenGL 3.3 like the rest of 3D.

What you need first: @ref graphics_3d (begin_3d(), lighting, materials). Light shafts go with the sky of
@ref world_3d but do not need it.

@image html post_3d.png "SSAO in the wall corners and at the feet of objects, a polished floor reflecting the sphere, the box and the pillar, a dark red decal on the wall"

## Turning effects on

njin::post3d gathers every effect; each is off while its strength is 0 (the default), so `post3d{}` draws exactly
as if this page did not exist. Set it with post3d_set(); it can change every frame (turn motion blur off when a
menu opens, raise the light shafts at sunset).

```cpp
njin::post3d fx{};
fx.ssao = 0.8f;
fx.ssr = 1.0f;
fx.shafts = 1.0f;
njin::post3d_set(ctx, fx);
```

| Effect | Main fields | Applies to |
|---|---|---|
| Ambient occlusion (SSAO) | `ssao`, `ssao_radius` | Every opaque shape |
| Reflections (SSR) | `ssr`, with the surface's `material3d::reflect` | Surfaces with `reflect` > 0 |
| Decals | decal3d_add() | Every opaque shape in the decal's box |
| Motion blur | `motion_blur` | The whole 3D image, while the camera moves or turns; moving meshes and models |
| Light shafts | `shafts` | The sky round the sun, through gaps between objects |
| Lens flare | `flare` | The whole image, while the sun is in the frame and not hidden |
| Temporal anti-aliasing (TAA) | `taa`, `taa_sharpen` | The first 3D pass into the world of each frame |
| HDR bloom | `bloom`, `bloom_radius`, together with `light3d::hdr` | The bright places of the whole 3D image |
| Anti-aliasing from the image | `aa` (aa3d_fxaa, aa3d_smaa) | The whole world image, after the first 3D pass of each frame |
| Order-independent transparency (OIT) | `oit` | Transparent shapes and model parts drawn with the built-in shader |

The effects only apply to 3D passes into the world, not to passes into a render texture (begin_3d() with a
`target`). They run inside end_3d() in this order: after every opaque shape come decals, SSAO and reflections;
then glass, water and 3D particles; then TAA, light shafts, lens flare and motion blur. post_fx_set()
(@ref post_processing) and the game's own shader run last, on the whole image.

## Ambient occlusion (SSAO) {#post3d_ssao}

Ambient light (`light3d::ambient`) shines evenly on every face, so a block on the floor looks as if it floats.
SSAO looks round each point on screen, within a radius of `ssao_radius` 3D units, at how much of it other surfaces
hide, and darkens the point by `ssao`: wall corners, the gap between two objects and the feet of objects on the
floor darken, open surfaces stay as they are.

By default it is worked out at half resolution (`ssao_half`) and scaled up following the depth, so the edges of
objects stay sharp: four times cheaper and hardly different from the full version. `ssao_samples` is the number of
samples per pixel; fewer is faster, more is smoother.

## Reflections (SSR) {#post3d_ssr}

A reflecting surface is one whose `material3d::reflect` is above 0: set it with material3d_set() for primitive
shapes, or in `model_material::surface.reflect` for models. 1 is a mirror at every viewing angle; less reflects
clearly when seen at a slant and faintly when looked straight down at, like a polished floor or water.
`post3d::ssr` multiplies it for the whole scene.

```cpp
njin::material3d marble{};
marble.reflect = 0.5f;
njin::material3d_set(ctx, marble);
njin::draw_plane3d(ctx, {0, 0, 0}, {20, 20}, {0.9f, 0.9f, 0.92f, 1});
njin::material3d_set(ctx, {});
```

Each reflecting pixel marches its reflected ray over the depth image, up to `ssr_distance` units, and takes the
colour where the ray meets a surface. Since only what is on screen exists, things outside the frame or hidden
behind other objects cannot be reflected: there the reflection fades to the fog colour `light3d::fog_color`
(the horizon colour with draw_sky3d()), by `ssr_sky`. `material3d::reflect` alone does nothing: without
`post3d::ssr` the scene draws as before.

## Decals {#decal3d}

A decal is an image projected onto every opaque shape inside a box: walls, floors, models, terrain, following the
surface's shape (a splash of paint on a sphere curves with the sphere). decal3d_add() adds one; it shows in every 3D
pass into the world until its `lifetime` runs out (game time, which stops while paused; it fades over the last
`fade` seconds), decal3d_remove() removes it, or a newer decal takes its place past decal3d_set_max() (256 by
default, the oldest goes).

The image lies on the box's xz face and projects along its y axis: unturned, it projects down onto the floor.
`size.y` is how deep the decal reaches: thin keeps it off the objects next to it. To place it along a ray hit (a
bullet hole where the shot landed), decal3d_rotation() gives the rotation from the surface's normal. Without an
image a decal is a round, soft-edged spot.

```cpp
const njin::ray3d_hit hit = njin::physics3d_raycast(ctx, shot, 100.0f);
if (hit.hit)
  njin::decal3d_add(ctx, {.position = hit.point,
                          .rotation = njin::decal3d_rotation(hit.normal),
                          .size = {0.25f, 0.2f, 0.25f},
                          .texture = bullet_hole,
                          .lifetime = 30.0f});
```

Two ways to cover (njin::decal3d_blend): `decal3d_multiply` (the default) multiplies the colour into the surface,
only darkening but keeping the surface's light and shadows, right for bullet holes, scorch marks, blood and mud;
`decal3d_paint` lays the colour over like paint, lit by the sun and the ambient light, for light-coloured signs on
dark ground. Surfaces that slant a lot from the projection axis fade (`angle_fade`), so marks do not stretch along
the sides of objects.

## Motion blur {#post3d_motion_blur}

When the camera moves or turns, or an object moves on its own, each pixel slides across the screen between two
frames; `motion_blur` smears the image along that, 1 being a whole frame's slide and 0.5 like a film camera's
shutter. An object running in front of a still camera blurs too, and its smear spreads over the background at its
edge, as in a real photo (see @ref post3d_motion_vectors). With a still camera and still objects the image stays the
same, pixel for pixel. The longest smear is 6% of the screen's width, so a camera that jumps (a cut) does not smear
the image to pieces.

## Light shafts and lens flare {#post3d_sun}

The sun is the opposite of `light3d::direction`; draw_sky3d() sets that direction from the time of day, so the
shafts follow the sun across the sky. The sky is where no 3D shape is (the farthest depth).

`shafts` makes the sky round the sun spread into rays: each pixel gathers the sky lying between it and the sun, so
objects in the way cut dark streaks and the gaps between them become rays of light. `shafts_length` is how much of
the way to the sun the rays reach, `shafts_color` is multiplied with the sunlight's colour. `flare` adds a glow, a
halo ring and spots of light along the line from the sun through the centre of the screen. Both fade as the sun
leaves the frame (entirely gone a quarter of a screen outside), are not there while the sun is behind the camera or
has set, and the lens flare also fades by how much of the sun's disc objects hide.

@image html post_3d_sun.png "The evening sun behind the middle post: shafts spreading through the two gaps, the floor reflecting the posts"

## Temporal anti-aliasing (TAA) {#post3d_taa}

`taa = true` makes the slanted edges of 3D shapes as smooth as if drawn at twice the resolution, and thin edges
(wires, far posts) stop flickering. Each frame the 3D projection moves by a small fraction of a pixel, to a
different place each frame (the Halton 2, 3 sequence, eight positions), so the same pixel sees an object's edge at
different points in turn. The frame's image is blended with the blend of the frames before (about 10% new image),
after the old image is moved back into place by the depth and the camera's motion. After blending, the image is
sharpened back a little (`taa_sharpen`, 0.25 by default), since blending many points softens it.

```cpp
njin::post3d fx{};
fx.taa = true;
njin::post3d_set(ctx, fx);
```

The old image is only used while it still matches, so it leaves no ghosts:

- Each pixel compares the old colour with the colours of the nine pixels round it in the new image; an old colour
  outside that range is pulled back into it.
- The old image's depth, round where it is read, must contain the point being drawn; otherwise the old image there
  shows something else (an object that has moved away, or a spot just uncovered behind one), so it is dropped.
- When the camera jumps (moves more than 3 units or turns more than about 25 degrees in one frame), the image size
  changes, or TAA is turned off and on again, the old image is dropped entirely: the first frame after is the new
  image, not yet smoothed.

Only the first 3D pass into the world of each frame is smoothed; 2D drawn after end_3d() goes over the smoothed
image, so it does not shake. An object moving on its own (meshes and models, see the next section) is brought back
to where it was by its own motion, so its edges are smoothed too; the old image where the background was just
uncovered behind it is dropped, so it leaves no trail.

## Per-object motion {#post3d_motion_vectors}

While TAA or motion blur is on, end_3d() draws the opaque meshes and models once more (writing their motion only,
no colour) to know where each pixel was last frame: from that draw's transform last frame and now, and for a model
with bones from its pose last frame too. This draw runs once a frame, for the first 3D pass into the world.

The engine works out which draw this frame is which draw last frame:

- An entity with njin::model3d: by its entity.
- Other draws (draw_model(), draw_model_anim(), draw_cube3d()...): by their model or shape, and their order in the
  pass. A scene drawn in the same order every frame matches itself.
- When the order changes between frames (a list of enemies thinned out, sorted by distance): call
  draw3d_motion_id() just before the draw, with a number of the object's own that does not change, for example
  its id.

```cpp
for (const enemy &e : enemies) {
  njin::draw3d_motion_id(ctx, e.id); // non-zero, the same every frame
  njin::draw_model_anim(ctx, enemy_model, e.at, e.pose);
}
```

Without names, two objects of the same model that swap places in the list are taken as jumping onto each other:
TAA drops the old image there, and motion blur smears them even though they stand still. An object that jumps
more than a quarter of the screen in one frame (a teleport, a new object appearing where an old one was) is treated
as having only the camera's motion.

SDF shapes (draw_shape3d()), draw_instanced3d(), terrain, grass, water, glass and 3D particles have no motion of their
own: they follow the camera's motion, as before.

## HDR bloom {#post3d_bloom}

With `light3d::hdr` (@ref hdr3d), `bloom` lets bright places spread out by exactly their energy, as Bevy and
Unreal do: the image is shrunk down a chain of images (the 13-tap filter of Call of Duty, with a Karis average on
the first so one over-bright pixel does not flicker), scaled back up step by step, adding up, and mixed into the
image by `bloom` before tone mapping. There is no threshold: everything spreads a little, but only very bright
places (lights, strongly glowing things, the sun reflected on metal) show a clear halo. 0.04 to 0.15 is usual;
`bloom_radius` (0.25 to 2) sets how far it spreads.

```cpp
njin::light3d_set(ctx, {.direction = sun, .tonemap = njin::tonemap3d_agx, .hdr = true});
njin::post3d_set(ctx, {.bloom = 0.08f});
njin::material3d_set(ctx, {.emission = {1, 0.4f, 0.1f, 8.0f}}); // a really bright fireball
```

Without HDR `bloom` does nothing; use the bloom of post_fx_set() (@ref post_processing), which has a threshold
and works on an 8-bit image.

## Anti-aliasing from the image (FXAA, SMAA) {#post3d_aa}

`aa` smooths jaggies from the drawn image itself, once a frame, after the first 3D pass into the world: it does
not blend with the previous frame so there is no ghosting when things move fast as with TAA, but edges thinner
than a pixel can still flicker.

| `aa` | How | Suits |
|---|---|---|
| `aa3d_fxaa` | FXAA (Lottes): finds edges by brightness, blends along them, one pass | Cheapest; blurs text and fine patterns a little |
| `aa3d_smaa` | SMAA 1x (Jimenez et al., the iryoku/smaa library): finds edges, guesses the shape of the jaggies, blends, three passes | Sharper than FXAA, costs a little more |

`aa` applies to the whole world image (2D drawn into it earlier too), not to UI drawn after phase_render. It can be
used with TAA, but usually one of the two is enough.

## Order-independent transparency (OIT) {#post3d_oit}

Ordinary transparent shapes are drawn one by one and write depth, so a pane of glass drawn first hides the pane
behind it, and several layers of smoke, leaves or glass over each other come out wrong depending on order. `oit`
draws them as weighted blended OIT (McGuire and Bavoil): every layer is added with a weight by its coverage and
depth into two floating point images, which are then laid over the image once, so it comes out right whatever the
order they are drawn in.

```cpp
njin::post3d_set(ctx, {.oit = true});
njin::draw_cube3d(ctx, {0, 1, 0}, {2, 2, 0.05f}, {1, 0.2f, 0.2f, 0.5f});    // red glass
njin::draw_cube3d(ctx, {0, 1, -1}, {2, 2, 0.05f}, {0.2f, 0.4f, 1, 0.5f});   // blue glass behind it, still seen
```

It applies to shapes with a colour whose `a` < 1, model parts with a transparent material, and
draw_instanced3d() of those models, when they are drawn with the built-in shader. A game's own shader, water, 3D
particles and things that transmit (material3d::transmission) are still drawn one by one as before, after the OIT
layer. Overlapping colours are a weighted average, an approximation rather than as exact as drawing in the
right order: two very dense layers of different colours give a colour mixed between the two.

## Full example

A corner of a room with a polished floor and a box running round the sphere; keys 1, 2, 3, 4 toggle SSAO, reflections,
motion blur and TAA; the left mouse button leaves a bullet hole on the floor or a wall, fading after 10 seconds.

@include post3d.cpp

## Performance and limits

Measured on an RTX 3050 Laptop, Release build, 1280 x 720, the scene of the image above (mean of three runs):
no effects 0.64 ms a frame; half-resolution SSAO adds about 0.24 ms (full resolution 0.54 ms), reflections
0.41 ms, 50 decals 0.28 ms, motion blur 0.15 ms, light shafts 0.25 ms, lens flare 0.14 ms; all of them with 50
decals add about 1 ms. TAA adds about 0.3 to 0.4 ms (drawing the world into an image of its own and the per-object
motion included). With four moving objects and a character with bones, TAA adds about 0.35 ms and motion blur about
0.25 ms. Turning
any effect on (or having a decal) draws the world into an image of its own with a depth, then copies it to the
screen, as post_fx_set() does.

- Only what is on screen exists: reflections cannot show what is outside the frame or hidden, SSAO knows nothing
  behind an object. The screen's edges are where these two are weakest.
- SDF shapes (draw_shape3d()), terrain, water and grass do not reflect; they still show in the reflections of
  other surfaces, and still get SSAO and decals.
- Glass, water and 3D particles are drawn after decals, SSAO and reflections, so they get none of them; their
  motion blur follows the depth of the opaque shapes behind them.
- Normals come from the depth, so right where two faces meet (floor meeting wall) a reflection can flash for one
  pixel.
- SDF shapes, draw_instanced3d(), terrain, grass and water have no motion of their own (the camera's only): while
  they move on their own, their edges stay jagged under TAA and they do not blur. The shadow of a moving object
  lies on a still floor, so under TAA the shadow's edge softens a little. Shape changes from morphs have no motion
  of their own either.
- Under TAA, the edges of fast-moving objects are a little softer than without anti-aliasing (the new image is
  blended in more so they do not smear behind).
- Only the first 3D pass into the world of each frame is smoothed; 2D drawn into the world image before begin_3d()
  is blended with the 3D image, does not shake, but can soften a little while the camera turns.
- Every 3D pass into the world gets the effects: a game that draws the world with two begin_3d() in one frame gets
  its decals drawn in both.
- FXAA and SMAA run once a frame, after the first 3D pass into the world.
