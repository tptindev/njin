# An 8-direction character in 50 lines {#first_walk}

The top-down version of @ref first_jump : a character seen from above that walks in 8 directions, dashes, and bumps into stone walls, in
**under 50 lines of code** (46 lines, not counting blank lines and comment lines). It uses njin::tilemap as the
map, njin::topdown_body as the character, and njin::topdown_input_map to wire the keys to the character.

@include first_walk.cpp

Press the arrow keys or WASD to walk, Space to dash. The character slides along the stone walls instead of sticking to them, and the camera
never shows anything outside the map.

@image html first_walk.gif "Walking diagonally, dashing (hold Space while walking), then walking down. The yellow rectangle is the character, the camera follows it"

## Setup

You need a tileset image made of 16 x 16 tiles at `assets/tiles.png`. Just use the image from the sample game:
`src/games/topdown/assets/tiles.png` (tile 0 is grass, tile 7 is stone wall). The `assets` folder must sit next to
the exe: call `njin_add_assets(my_game assets)` in CMake (see @ref getting_started).

## Step by step

### 1. Keys: two axes and one action

@code
const njin::axis_handle move_x = njin::axis_define(ctx, "move_x", {{njin::key_left, njin::key_right}, {njin::key_a, njin::key_d}});
const njin::axis_handle move_y = njin::axis_define(ctx, "move_y", {{njin::key_up, njin::key_down}, {njin::key_w, njin::key_s}});
const njin::action_handle dash = njin::action_define(ctx, "dash", {njin::key_space, njin::pad_face_down});
@endcode

Top-down needs **two** axes, horizontal and vertical, because the engine has no two-dimensional axis. Each axis takes several key pairs
(arrows and WASD both work) and one gamepad axis. See @ref input.

### 2. The map: grass to walk on, stone to block

A nested `for` loop sets the whole 20 x 12 map to grass (tile 0), except the border, which is stone (tile 7). Three things make stone block
the way while grass does not:

- Attach a njin::collider with `shape = collider_tiles` to the map's entity: every non-empty tile becomes an obstacle.
- njin::tilemap_set_shape() gives tile 0 the shape `tile_none`: grass is **for looking at only**, you can walk through it.
- The stone tile keeps the default shape (`tile_solid`), so it blocks from every side.

One map serves both jobs (drawing and collision), so you do not need a separate layer for walls. Read more in
@ref tilemap.

### 3. The character: njin::topdown_body

| Component | Role |
|---|---|
| njin::transform | Position. `pos` is the **feet** because the collider's `offset` is `{0, -4}` |
| njin::collider | A 10 x 8 collision box, small and low so the character can walk right up to walls without getting stuck |
| njin::topdown_body | Walk, accelerate, decelerate, dash |

The engine moves it in `phase_fixed_update` with njin::collision_move(). Two things worth knowing:

- **Walking diagonally is not faster**: a direction whose length is greater than 1 is brought back to 1, even when you add up two axes.
- **Dash** is off by default. `dash_speed = 230.0f` turns it on; `dash_time` and `dash_cooldown` adjust the length and the
  cooldown.

njin::topdown_input_map reads the two axes and the action for you: you do not need any system to handle keys.

### 4. The camera: limited to the map

njin::camera_follow follows `target`. `bounds` is the region of the world that the view must not go outside of; here it is
the whole map, 320 x 192 pixels. When you load a map from Tiled or LDtk, take it straight from njin::level_bounds()
(see @ref level) instead of computing it yourself.

## Tuning the feel

@code
njin::topdown_body body{};
body.speed = 78.0f;        // walk slower
body.accel = 600.0f;       // slide longer when starting to walk
body.decel = 600.0f;       // slide longer when you release the keys
body.dash_speed = 230.0f;
body.dash_time = 0.2f;     // longer dash
@endcode

Large `accel` and `decel` (the defaults are 900 and 1300) mean the character stops and starts instantly; small values feel slippery,
like walking on ice.

## Next steps

- @ref topdown : Y-sorting (trees hide the character), monsters that chase with A\*, a sword
- @ref tilemap : write the map as text instead of a `for` loop (njin::tilemap_from_text()), no editor needed
- @ref level : or replace the tile-placing code with a level drawn in Tiled or LDtk
- @ref sprites and @ref animation : replace the rectangle with an animated character
- @ref cheatsheet : "I want to do X, which function do I use?"
