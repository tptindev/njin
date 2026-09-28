# Part 2: Core concepts {#part_core}

**Level: basic.** Read this first: you should have finished @ref part_start (or at least @ref first_jump). This part explains
what every njin game uses, whether it is a platformer or a top-down game: where your code lives, how a frame runs, how
data is organized.

## Follow this order

| Order | Page | What you get |
|---|---|---|
| 1 | @subpage modules_systems | Modules, systems, phases: how your game's logic is split up and registered |
| 2 | @subpage game_loop | What a frame runs, and in what order |
| 3 | @subpage ecs | Entities, components, events with EnTT. Ten operations are enough |
| 4 | @subpage input | Keyboard keys, mouse, gamepad, actions, text input |
| 5 | @subpage time | Delta time, pause, slow motion, fixed update for stable physics |
| 6 | @subpage math | vec2, rectangles, circles, interpolation, reproducible random numbers |
| 7 | @subpage logging | Log by level with the `NJIN_*` macros |

The first three pages are **required**: the later parts all assume you know them. Read the other four as you need,
but you should read @ref input and @ref time before you build gameplay.

## What to do next

@ref part_visual, to give the game visuals and motion.
