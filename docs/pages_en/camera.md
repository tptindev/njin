# Camera {#camera}

A camera in njin is an **entity** with three components. There is no separate camera object.

| Component | Role |
|---|---|
| njin::transform | `pos` is the point in the world the camera looks at, `rot` is the rotation angle. `scale` is ignored |
| njin::camera_2d | `offset` is the position on the screen where `pos` is drawn, `zoom` is the magnification |
| njin::camera_on | A tag that marks this as the camera in use |

## Creating a camera

@include camera_follow.cpp

In the example:

- `offset` is **half the screen size**, so the point the camera looks at is always in the middle of the screen.
- `zoom = 2` makes everything twice as big. A value `<= 0` is treated as 1.
- `follow_player` runs in `phase_post_update`, after the player has moved, so the camera does not lag by a frame.

## Switching cameras

The camera is read **directly from the registry** each time it is needed, so changing
`transform` or `camera_2d` takes effect immediately, with no function to call.

To switch to another camera: remove njin::camera_on from the old camera and attach it to the new one.

@warning If **several** entities have njin::camera_on at once, one of them is
used but you cannot control which. Keep only one camera with this tag.

With no camera the world **matches the screen**: coordinate (0, 0) is the top-left corner.

## Converting coordinates

| Function | Converts from | To |
|---|---|---|
| njin::w2scr() | World | Screen pixels |
| njin::scr2w() | Screen pixels | World |

Both go through njin::camera_active(), which is the view in use (njin::camera_view).

The most common case is turning the mouse position into a position in the world.
njin::mouse_pos() returns screen pixels, not passed through the camera:

```cpp
const njin::vec2 target = njin::scr2w(ctx, njin::mouse_pos(ctx));
```

See @ref input.

njin::camera_bounds() returns the region of the world currently shown on screen. Use it to skip
things that are off-screen; tilemaps use it to draw only the visible chunks.

## The camera and the draw phases

The engine's camera module turns the camera on at the start of `phase_pre_render` and off at the start of `phase_post_render`:

- Everything drawn in `phase_pre_render` and `phase_render` is affected by the camera.
- Everything drawn in `phase_post_render` is **not**. This is where the UI goes.

See @ref game_loop.
