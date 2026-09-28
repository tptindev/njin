# Part 4: World and gameplay {#part_world}

**Level: intermediate.** Read this first: @ref part_core, plus @ref sprites and @ref camera. This part builds levels, makes objects
collide with each other, then puts everything together into njin's two genres.

## Follow this order

**Levels and collision**

| Order | Page | What you get |
|---|---|---|
| 1 | @subpage tilemap | A square-grid map split into chunks, collision against the map |
| 2 | @subpage level | Draw levels in Tiled or LDtk instead of writing each tile in code |
| 3 | @subpage collision | Colliders, bullets hitting enemies, picking up items, blocking walls, raycasts |
| 4 | @subpage prefabs | Entity templates to spawn, parent-child transforms, attaching a weapon to a character |

**Putting it together into a game** (pick the genre you are making, or read both)

| Order | Page | What you get |
|---|---|---|
| 5 | @subpage platformer | Running, jumping with coyote time, slopes, one-way platforms, wall jump, a following camera |
| 5 | @subpage topdown | 8-direction walking, dashing, enemies chasing with A\*, Y-sorting so trees cover the character in the right place |

@ref platformer and @ref topdown combine the pages before them, so read them last.

## What to do next

@ref part_ui_audio, to give the game sound, menus and dialog boxes.
