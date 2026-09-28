# Sprites and animation {#sprites}

A sprite is a **component**: attach njin::transform and njin::sprite to an entity and the engine
draws it every frame. You do not need to write a draw system.

@include sprites.cpp

## The sprite component

| Field | Meaning |
|---|---|
| `texture` | The image to draw |
| `source` | The region inside the image (pixels). A size of 0 means the whole image |
| `origin` | The anchor point as a ratio: `{0.5, 0.5}` is the center (the default), `{0.5, 1}` is the middle of the bottom edge |
| `tint` | A color multiplied into the image. `a` < 1 is transparent |
| `layer` | The draw layer: lower layers draw first, higher layers draw on top |
| `flip_x`, `flip_y` | Flip the image |
| `visible` | Hide it temporarily without removing the component |

A sprite uses all three fields of njin::transform: `pos` is where the anchor point lands, `rot` is the angle
of rotation around the anchor point, `scale` is the scale.

## Draw order

The engine's sprite module draws in `phase_render`:

1. All sprites, tilemaps and particles, in increasing `layer` order. Within the same layer, tilemaps draw first,
   then sprites, then particles.
2. Only after that come the game's own draw systems in `phase_render`, so they draw on top of sprites.

Within the same layer, the order between sprites is the order the entities were created.

## Animation

Add njin::sprite_anim to play an animation from a **sprite sheet**: one image holding frames
of equal size, numbered from 0 in rows from left to right and then from top to bottom.

| Field | Meaning |
|---|---|
| `frame_size` | The size of one frame |
| `first`, `count` | The animation uses frames from `first` to `first + count - 1` |
| `fps` | Frames per second |
| `loop` | Repeat when it ends |
| `playing` | Set `false` to stop on the current frame |
| `finished` | It has played through (only when not looping) |

The engine advances frames in `phase_post_update` and writes them to `sprite.source`.

If you need a separate duration for each frame, loading from Aseprite, or an idle/run/jump state machine, use
njin::animator, see @ref animation. A white flash when hit: njin::sprite_flash(), see
@ref particles.

Change animation with njin::anim_play(). This function **does nothing if that animation is already playing**,
so you can call it every frame without the animation restarting constantly.

Animation uses time that has already been multiplied by the speed, so it slows down with njin::time_set_scale() and stands
still when paused.
