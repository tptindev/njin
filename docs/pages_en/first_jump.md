# A jumping character in 50 lines {#first_jump}

This page builds a platformer character that runs and jumps on a tile map, in **under
50 lines of code** (45 lines, not counting blank lines and comment lines). It uses three things: njin::tilemap
as the ground, njin::platformer_body as the character, and njin::platformer_input_map to wire the keys to the
character.

@include first_jump.cpp

Press the left/right arrows to run, Space to jump. The character falls to the ground, can run, can jump, and can jump
up through a platform from below and then stand on top of it.

@image html first_jump.gif "Running right, holding Space to jump up through the platform and stand on it (the yellow rectangle is the character)"

**Hold** Space for a high jump, **release early** for a low jump: this is the `jump_cut` of njin::platformer_body. That is why a light tap on
Space will not get the character onto the 32-pixel-high platform.

## Setup

You need a tileset image made of 16 x 16 tiles at `assets/tiles.png`. Just use the image from the sample game:
`src/games/platformer/assets/tiles.png` (tile 0 is grassy ground, tile 1 is dirt, tile 3 is the platform). The `assets` folder
must sit next to the exe file: call `njin_add_assets(my_game assets)` in CMake so it is copied on every
build (see @ref getting_started and @ref window_files).

## Step by step

### 1. Keys: axis and action

@code
const njin::axis_handle move = njin::axis_define(ctx, "move", {{njin::key_left, njin::key_right}});
const njin::action_handle jump = njin::action_define(ctx, "jump", {njin::key_space, njin::pad_face_down});
@endcode

An **axis** is a range from -1 to 1, here combining two keys into a horizontal axis. An **action** is a
logical name ("jump") tied to one or more sources: a key, a mouse button, a gamepad button. The game does not ask "is the
Space key pressed?" but "is the `jump` action pressed?", so later, changing keys or adding a
gamepad just means adding one element to the list. To add A/D keys to the axis: `{{key_left, key_right},
{key_a, key_d}}`, and to add an analog stick: `njin::axis_define(ctx, "move", {...}, {njin::pad_axis_left_x})`.
See @ref input.

### 2. The map: njin::tilemap

njin::tilemap_set(map, x, y, id) sets tile `(x, y)` to tile number `id` of the tileset. You do not declare the map's size:
set a tile anywhere and the map grows to reach it. Tiles can be at negative coordinates.

The map needs two more things for the character to stand on it:

- Attach a njin::collider with `shape = collider_tiles` to the map's entity. Then every non-empty tile
  becomes an obstacle.
- njin::tilemap_set_shape() gives tile 3 the shape `tile_one_way`: a **one-way platform** that only supports from above,
  while jumping from below passes through. Collision shapes are set by tile index, so they apply to every tile of that kind.
  There are also slopes, see @ref platformer.

The entity's `transform` is the top-left corner of tile (0, 0), so here the tile on row 10 has its top surface at
`y = 160`.

### 3. The character: njin::platformer_body

The character is an entity with three components:

| Component | Role |
|---|---|
| njin::transform | Position. For this character, `pos` is the **feet** because the collider's `offset` is `{0, -7}` |
| njin::collider | A 10 x 14 collision box |
| njin::platformer_body | Run, jump, fall, records `grounded`... |

`platformer_body{}` with all default values already gives a good jump: it has **coyote time** (you can still jump a little
after leaving a ledge), **jump buffer** (pressing jump slightly early still counts), and a low jump when you release the button early.
The engine moves it in `phase_fixed_update`, so the result does not depend on FPS.

njin::platformer_input_map wires the `move` axis and the `jump` action to the body: the engine reads them every frame and writes
`body.input` for you. You do not need to write a system to handle keys.

### 4. The camera: njin::camera_follow

njin::camera_spawn(ctx, 3.0f) creates a camera zoomed in 3 times (good for pixel art), and njin::camera_follow makes it
follow `target`. See @ref camera.

### 5. Drawing the character

There is no sprite yet, so the character is a yellow rectangle drawn in `phase_render` (world space, going
through the camera). njin::rect_from_center() turns a center and a size into a rectangle. When you have a character image, change it to
njin::sprite and njin::sprite_anim, see @ref sprites.

## Tuning the jump feel

All the parameters sit right on njin::platformer_body. Create the body, edit it, then `emplace`:

@code
njin::platformer_body body{};
body.jump_speed = 350.0f;        // jump higher
body.air_jumps = 1;              // double jump
body.wall_slide_speed = 55.0f;   // wall slide
body.wall_jump = {150.0f, 300.0f}; // jump off walls
reg.emplace<njin::platformer_body>(player, body);
@endcode

The jump height is roughly `jump_speed² / (2 * gravity)`: with the default values (300 and 1000) that is about
45 pixels, nearly three tiles (42 pixels measured in practice because the physics runs in discrete ticks). The platform on row 8 is 32 pixels above the
ground so it can be jumped onto; place it higher than 42 pixels and you must raise `jump_speed`.

## Next steps

- @ref platformer : slopes, moving platforms, wall jumping, a camera limited to the level
- @ref first_walk : the same approach, for a top-down game
- @ref ecs : if `registry.emplace<...>` and `view` are still unfamiliar
- @ref tilemap : write the map as text instead of a `for` loop (njin::tilemap_from_text()), no editor needed
- @ref level : or replace the tile-placing code with a level drawn in Tiled or LDtk
- @ref sprites and @ref animation : replace the rectangle with an animated character
- @ref cheatsheet : "I want to do X, which function do I use?"
