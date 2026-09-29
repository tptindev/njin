# njin {#mainpage}

njin is a small game engine written in C++20, making both **2D** and **3D** games. It uses **EnTT**
for ECS and **raylib** for the window, graphics and input. raylib is completely hidden: a game only
includes `njin.h` and never sees raylib.

Most of the API (modules, systems, ECS, input, time, audio, UI, particles/effects, debugging) is
**shared** between 2D and 3D. Drawing, sprites, tilemaps, the camera and collision in Parts 2-4 are
**2D** (`njin_draw.h`, `njin_camera.h`, `njin_collision.h`...); @ref graphics_3d is a dedicated
**3D** page (`njin_3d.h`): a perspective camera, primitives and SDF shapes, glTF models, shadowed
lighting, instancing. A game uses only the part it needs; both are built on the same ECS and the
same loop, so they mix (a 2D HUD drawn over a 3D scene, for example).

This site is documentation for **using** njin and for **understanding** it.

**Source code:** <https://github.com/tptindev/njin>. Report bugs and give feedback in the repo's Issues. The version
history is in `CHANGELOG.md` in the root folder.

## Roadmap for beginners

New to C, C++, CMake or shaders? Read the group of 13 lessons in @ref learn first (skip it if you already know them). Then follow this order:

1. @ref setup : set up your environment (skip if you already have a C++20 compiler, CMake, Ninja and Git)
2. @ref getting_started : build and run your first program
3. @ref first_jump (platformer) or @ref first_walk (top-down) : a character that moves on a map, in under 50 lines
4. @ref ecs : `entt::registry` and `view`, if the lessons above still look strange. Ten operations are enough
5. @ref sprites and @ref animation : replace rectangles with a character that has art
6. @ref level : draw levels in Tiled or LDtk instead of writing each tile in code
7. @ref audio, @ref ui, @ref dialog : sound, menus, dialog boxes
8. @ref samples : read a complete game
9. @ref cheatsheet : look things up when you need to know "to do X, which function do I use"

## The docs come in 7 parts

The pages are ordered by **increasing difficulty**: go from Part 1 down to Part 7, or jump straight to the part you
need. The sidebar on the left shows the same tree. The **Scope** column says which kind of game a
part's API applies to: "Shared" works for both 2D and 3D games (modules, ECS, input, audio, UI,
packaging...); "2D" is drawing, sprites, tilemaps, the 2D camera and collision; "3D" is only
@ref graphics_3d, its own page for `njin_3d.h` in Part 7.

| Part | Level | Scope | What you get |
|---|---|---|---|
| @subpage part_start | Beginner | Shared | Set up your environment, run your first program, get a character that moves right away. Includes 13 foundation lessons on C, C++, CMake and shaders |
| @subpage part_core | Basic | Shared | Understand modules, systems, how a frame runs, entities and components, input, time, math, logging |
| @subpage part_visual | Basic | 2D | Draw shapes and text, sprites, animation, camera, particles, pixel art |
| @subpage part_world | Intermediate | 2D | Square-grid maps, Tiled and LDtk, collision, prefabs; put it all together into a platformer and a top-down game |
| @subpage part_ui_audio | Intermediate | Shared | Sound effects and music, menus, dialog boxes, localization |
| @subpage part_ship | Intermediate | Shared | Splitting the game into screens, saving, the player's settings, the window, reading the sample games, packaging |
| @subpage part_advanced | Advanced | 2D + 3D | Shaders, instancing, post-processing, automatic map generation, **@ref graphics_3d (3D)**, debugging with the inspector |

After the seven parts comes @subpage part_appendix , which has a quick lookup table ("to do X, use what"), a per-function
reference by API group, and the engine's internal architecture.

## The smallest program

@include minimal_main.cpp

The `njin_run` function runs the loop until the window closes. All of your game's logic lives in
**modules**, see @ref modules_systems.

## A complete game

`src/games/pong` is a full Pong game written on njin: a menu, play against the computer or with two
players, pause, slow motion at the deciding point, generated sound, a saved high score, a resizable
window. Read it from top to bottom like a tour of the engine. Build the target
`njin_pong` and run `build\bin\njin_pong.exe`.

## Source layout

```
src/
  engine/
    api/        public headers. Including njin.h is enough
    runtime/    the implementation, uses raylib. Games do not include it directly
  games/
    sandbox/    sample game
    pong/       complete Pong game
  tools/
    inspector/  njin_inspector, a debug tool that runs next to the game
```
