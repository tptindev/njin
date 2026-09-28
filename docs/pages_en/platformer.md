# Making a platformer game {#platformer}

This page assembles the engine's existing pieces into a platformer character: running, jumping with
good feel, slopes, one-way platforms, moving platforms, and a following camera. The sample game
`njin_platformer` (@ref samples) uses exactly these things. Never made anything before? Start with @ref first_jump : a
jumping character in 50 lines.

@image html platformer.gif "The njin_platformer sample game: running, jumping on slopes, an \"E\" prompt when you get close to the owl, dust on landing"

@include platformer_body.cpp

## The character: njin::platformer_body

Attach njin::platformer_body next to a njin::transform and a box njin::collider. The engine moves
it in `phase_fixed_update` with collision_move(), then writes the state back (`grounded`, `on_wall`,
`velocity`...). The game only needs to feed it input and read the state to choose an animation.

All the familiar feels are built in, tuned with numbers right on the component:

| Feel | Field |
|---|---|
| **Coyote time**: you can still jump after leaving a ledge | `coyote_time` |
| **Jump buffer**: pressing jump early, before landing | `jump_buffer` |
| Releasing the jump button early gives a lower jump | `jump_cut` |
| Falling faster than rising | `gravity`, `fall_gravity`, `max_fall` |
| Double jump | `air_jumps` |
| Wall slide, wall jump | `wall_slide_speed`, `wall_jump` |
| Acceleration on the ground and in the air | `ground_accel`, `air_accel`... |

There are two ways to feed in input:

- **Write it yourself** into `body.input` every frame (`move_x`, `jump`, `jump_held`, `drop`). `jump` is a
  *request*: the engine clears it once it has been handled, so a press on a frame with no physics tick is still not missed.
- Attach njin::platformer_input_map with an axis and an action: the engine reads them for you.

To knock the character back when it gets hit, write straight into `velocity`.

The character sends two events so the game can shake the screen, play a sound, and spawn dust: njin::body_jumped and
njin::body_landed (with the fall speed).

## Slopes, one-way platforms, non-colliding tiles

A tile's collision shape lives in njin::tilemap::shapes, set with tilemap_set_shape():

| njin::tile_shape | What it does |
|---|---|
| `tile_solid` | Blocks from every side (default) |
| `tile_none` | For looking at only, even when it sits in the obstacle layer |
| `tile_one_way` | A platform that only supports from above; you can jump through from below |
| `tile_slope_r`, `tile_slope_l` | A 45-degree slope, high on the right or on the left |
| `tile_slope_*_low`, `*_high` | A 22.5-degree slope, two tiles side by side |

When loading from **Tiled**, set a string property `collision` on the tile in the tileset
(`one_way`, `slope_r`, `slope_l_high`, `none`...). When loading from **LDtk**, name the IntGrid values, or
attach an enum tag / custom data to the tileset using the same names. Tiles used for animation
(water, torches) also come from the tileset, see @ref tilemap.

The character walks up slopes smoothly, and **sticks to the slope surface** when walking down instead of bouncing off it. To drop
down through a one-way platform: hold the down key and press jump (`input.drop`, or `platformer_input_map::down`).

collision_move() takes an extra njin::collision_move_opts if you write your own controller: `drop_through`,
`snap_down`, `test_only` (probe for a wall or the ground without moving). The result has `grounded`, `ground`,
`on_slope`.

## Moving platforms: njin::path_mover

An entity with a transform, a box collider (usually `one_way = true`) and a njin::path_mover follows a repeating
polyline. The engine moves it with collision_move_platform(), **carrying along** every character standing on
it and pushing whatever it runs into. In Tiled, draw the platform as an object with a polyline; the polyline's points
are the stops.

## Following camera: njin::camera_follow

@code
const entt::entity cam = njin::camera_spawn(ctx, 2.0f);
reg.emplace<njin::camera_follow>(cam, njin::camera_follow{
    .target = player, .deadzone = {24, 40}, .lookahead = {36, 0},
    .bounds = njin::level_bounds(ctx, level), .pixel_snap = true});
@endcode

The camera lags slightly (`smoothing`), stays still while the character moves inside the dead zone (`deadzone`), looks ahead
in the running direction (`lookahead`), and **never shows anything outside the level** (`bounds`; a level smaller than the
screen is centered). For pixel art, turn on `pixel_snap` so tiles do not jitter by one pixel. After teleporting the character far away,
set `started = false` so the camera jumps straight there instead of sliding across the whole level.

The screen shake from njin::camera_shake() is added at draw time and does not affect camera_follow.

## Checking with the inspector

njin_inspector shows the `platformer_body` (velocity, whether it is on the ground, walls, the coyote and
buffer counters) and the `camera_follow` of the selected entity. See @ref debug.
