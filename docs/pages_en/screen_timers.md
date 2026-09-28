# Virtual screen, timers and tweens {#screen_timers}

## A virtual screen for pixel art {#virtual_screen}

A pixel art game should draw on a small fixed screen (320 x 180) and then scale it up, so every pixel is the same size
at every window size.

@include virtual_screen.cpp

When it is on, **everything** goes through the virtual screen: the world, UI, toasts, scene transitions. Your game code does not need to know
how big the window is:

- screen_size() returns the virtual size, which does not change when the window is dragged.
- mouse_pos() and mouse_delta() are in virtual pixels, so a click hits a button even when the window is scaled.
- The image is scaled **without smoothing**. `integer_scale` scales only 1, 2, 3... times (thicker borders but even pixels);
  turn it off to scale to fit the window exactly.
- window_size() and window_viewport() give the real window size and the area the image occupies, when you need them.

With a camera, `zoom = 2` on a 640 x 360 virtual screen gives a 320 x 180 world. Combine it with
`camera_follow::pixel_snap` (see @ref platformer).

## Per-entity timers {#entity_timers}

njin::timer in `_tween.h` is a counter you keep and tick yourself. For "do X after 0.5 seconds", there are
functions that need no counter variable:

@include timers_tweens.cpp

| Function | What it does |
|---|---|
| timer_after() | Calls a function once after a delay |
| timer_every() | Calls every interval, forever or a set number of times |
| tween_move(), tween_scale(), tween_rotate(), tween_tint() | Tween an entity's position, scale, angle, color |
| tween_value() | Tween any number and hand it to your function every frame |
| timer_cancel(), tween_cancel(), tween_cancel_all() | Cancel |

General rules:

- **`owner`**: ties a timer to an entity. When the entity is destroyed the timer cancels itself, so the function never
  runs with a dead entity. Tweens on an entity have this rule built in.
- Timers and tweens belong to the **scene that is running when they are created** and are cancelled when you leave that scene, unless
  `keep_across_scenes`.
- They follow game time: they stop on pause and hitstop. Use `real_time = true` to run on real time (menus, UI).
- A tween replaces a tween **of the same kind** on the same entity: call tween_move twice and the second wins.
- `repeat` and `yoyo` give back-and-forth motion (a bouncing button, a bobbing coin).
- A timer's function is free to add and destroy entities and create new timers: they run in `phase_update`
  before the game's systems.
