# Part 3: Visuals and motion {#part_visual}

**Level: basic.** Read this first: @ref modules_systems, @ref game_loop and @ref ecs. This part takes the game from colored
rectangles to characters with art, animation, a camera that follows them and effects.

**Scope: 2D.** The API here (draw_rect(), sprites, tilemaps, the 2D camera) only draws in 2D space. For a
3D game, read @ref graphics_3d instead (Part 7); this part's particles and effects (@ref particles) are
shared with 3D.

## Follow this order

| Order | Page | What you get |
|---|---|---|
| 1 | @subpage drawing | Draw shapes and text, Vietnamese fonts, the three draw phases and the space of each phase |
| 2 | @subpage sprites | A sprite is a component: attach an image to an entity, sprite sheets, clips |
| 3 | @subpage animation | Animation from Aseprite, an idle/run/jump state machine |
| 4 | @subpage camera | The camera is an entity: follow the character, clamp inside the level, convert between world and screen coordinates |
| 5 | @subpage particles | Explosions, dust, sparks, camera shake, hitstop: the things that give a game "impact" |
| 6 | @subpage screen_timers | A virtual screen for pixel art; per-entity timers and tweens |

Pages 1 to 4 are the essentials. @ref particles and @ref screen_timers are the polish layer: read them once the game runs.

## What to do next

@ref part_world, to build a world for the character to walk in. To write your own shaders or draw thousands of sprites with one
draw call, see @ref part_advanced.
