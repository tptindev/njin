<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/images/brand/njin-logo-dark.svg">
    <img src="docs/images/brand/njin-logo.svg" alt="njin" width="360">
  </picture>
</p>

njin is an open-source game engine written in C++20, for both **2D** and **3D**
games. It uses EnTT for its ECS, raylib for windowing, graphics, audio and
input, and Jolt Physics for 3D physics; a game only needs to include njin's
API through `njin.h`.

Current version: **0.7.0**. The API is still evolving and may change between
minor versions before 1.0. See [CHANGELOG.md](CHANGELOG.md) for details.

## Features

- ECS, modules and phase-ordered systems; scenes, prefabs and a fixed game loop.
- Sprites, animation, square tilemaps from Tiled/LDtk, collision and a camera that follows the player.
- Character controllers for platformer and top-down games, A* navigation, particles and post-processing effects.
- UI, audio, settings saves, dialogue and localization.
- 2D lighting (PBR): point, spot and directional lights, soft shadows from occluders, normal maps, material and glow.
- 3D: perspective camera, solid shapes and smooth SDF shapes, glTF models with materials, lights with shadows and fog, 3D particles, instancing, ray picking.
- 3D physics (Jolt Physics): static, kinematic and dynamic bodies; a character that walks on the ground, climbs steps, stands on moving platforms; raycasts and ragdolls.
- Networking: TCP/UDP sockets for simple messaging, realtime multiplayer on ENet, netcode (ticks, prediction, snapshot interpolation, component replication) and deterministic lockstep for games with thousands of simulated things.
- Graphics quality presets for weak hardware (render scale, shadows, effects, particle counts, LOD) and a bit-packed reader/writer for compact network and save data.
- Gizmos for 2D and 3D debugging.
- A virtual screen for pixel art, anti-aliasing via supersampling, and the `njin_inspector` tool to inspect entities, systems, logs, performance, resources and the 3D scene while the game runs.

## Animation editor

Run `run_anim_editor.bat` to open the **njin Animation Editor**: open a rigged
(skinned) `.glb`/`.gltf` model, pose its skeleton with ImGuizmo gizmos and
author clips on a keyframe timeline. The model's own clips open for editing.
Projects are saved as `.anim.json` (the model path plus the clips), and
**Export glTF** writes a `.glb` with the model and every clip, which
`model_load()` plays directly. Its **Mocap** window captures motion from a
webcam (MediaPipe in a Python helper, set up once with
`src/tools/anim_editor/mocap/setup.bat`) and records it onto the bones you
choose. See the [Animation Editor guide](src/tools/anim_editor/README.md).

## Sample games

| Target | Content |
|---|---|
| `njin_sandbox` | Minimal starting point for the engine |
| `njin_pong` | A complete Pong game with a menu, audio and high scores |
| `njin_platformer` | A platformer with a map, slopes, jumping and dialogue |
| `njin_topdown` | A top-down game with tilemaps, combat and enemy pathfinding |
| `njin_debug_demo` | A sample for trying out `njin_inspector` |
| `njin_render_demo` | A sample for atlases, particles, culling and post-processing |
| `njin_lighting_demo` | A gallery of 2D lighting: point, spot and sun lights, shadows from occluders and sprites, PBR surfaces, colour and tonemapping |
| `njin_tower_defense` | A tower defense game with defensive towers and enemy waves |
| `njin_fps` | A first-person shooter: glTF models, shadows, lights, glowing tracers, 3D particles |
| `njin_sokoban` | 2.5D box-pushing: instancing, an SDF-shaped character, lights on target tiles |
| `njin_platformer3d` | A third-person 3D platformer on Jolt physics: double jump, moving platforms, pushable crates |
| `njin_skylight` | Sunbeams through a skylight in volumetric fog onto a statue; bloom, bounce light, ray traced shadows and bounce with `NJIN_RAYTRACING`; orbit the camera and drag things into the sun |

The samples can be built standalone, reusing an already-built engine; see the
[game project guide](examples/README.md). Use `-DNJIN_BUILD_EXAMPLES=OFF`
to configure only the engine and tools. Serious games are managed in their own
repository; new game folders are ignored by default.

## Requirements

- CMake **3.28 or later**
- A compiler with **C++20** support
- Ninja and Git
- Network access on the first configure, for CMake to fetch raylib, EnTT, Jolt Physics and (when the inspector is enabled) Dear ImGui and related libraries

On Linux, raylib needs development libraries for X11 and OpenGL. For example, on Ubuntu/Debian:

```sh
sudo apt install build-essential cmake ninja-build git pkg-config \
  libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev
```

## Create your own game

Keep your game in its own repository outside njin. Build the engine once,
then copy [`templates/game`](templates/game) to your game folder:

```sh
cmake --preset release -DNJIN_BUILD_EXAMPLES=OFF -DNJIN_BUILD_INSPECTOR=OFF -DNJIN_BUILD_UI_EDITOR=OFF -DNJIN_BUILD_ANIM_EDITOR=OFF
cmake --build --preset release --target njin_engine
cmake -E copy_directory templates/game /path/to/njin-proj/my-game
cmake -S /path/to/njin-proj/my-game -B /path/to/njin-proj/my-game/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DNJIN_ROOT=/path/to/njin
cmake --build /path/to/njin-proj/my-game/build
```

Replace the paths with your engine and game workspace locations (for example,
`D:/projects/njin` and `D:/projects/njin-proj/my-game`). From the game folder, run `build/bin/my_game`
(`build\bin\my_game.exe` on Windows). Edit the game's own `src/`, `assets/`
and `CMakeLists.txt`; the engine's source and root CMake need no changes.

Use matching compilers, architectures and build configurations for engine and
game. `NJIN_PREBUILT` selects the engine build folder (default:
`NJIN_ROOT/build-release`). This export needs the engine checkout and build
tree to remain available; it is not a relocatable installed SDK. See the
[template guide](templates/game/README.md) for Debug, Visual Studio and packaging.

## Build and run the samples

Clone the repository, then configure and build the Pong game:

```sh
git clone https://github.com/tptindev/njin.git
cd njin
cmake --preset debug
cmake --build --preset debug --target njin_pong
```

Run the built program:

```sh
# Windows with GCC
build\bin\njin_pong.exe

# Windows with Visual Studio
build\bin\Release\njin_pong.exe

# Linux or macOS
build/bin/njin_pong
```

Drop `--target njin_pong` to build every sample game and the inspector. The
target can be swapped for any game in the table above. Games with their own
output folder run from there, e.g. `build/bin/topdown/`,
`build/bin/platformer/` and `build/bin/tower_defense/`.

On Windows, these helper scripts are available:

- `build.bat` configures and builds Debug with Ninja.
- `run.bat` builds and runs `njin_sandbox`.
- `run_inspected.bat [game_name]` builds and runs a game together with `njin_inspector` (defaults to `debug_demo`).

To make a Release build, use the `release` preset; the output lands in `build-release/`:

```sh
cmake --preset release
cmake --build --preset release
```

Working on the engine itself, the `engine` preset builds only the engine, one
sample (`topdown`) and the inspector into `build-engine/`, so editing a public
header does not rebuild every sample. Pick other samples with
`-DNJIN_EXAMPLES="topdown;platformer3d"`. With ccache 4.10 or newer installed,
njin compiles through it (`-DNJIN_CCACHE=OFF` turns it off).

## Documentation

The guide and reference documentation, in Vietnamese, lives in
[`docs/pages/`](docs/pages/). Start with
[environment setup](docs/pages/setup.md),
[building your first program](docs/pages/getting_started.md), or see the
[sample games](docs/pages/samples.md).

## Repository layout

```text
src/engine/api/       Public API headers; games include njin.h
src/engine/runtime/   Engine implementation
examples/            Sample games and shared code
src/tools/inspector/  The debug tool
templates/game/       Starting point for a game in its own repository
docs/pages/           Documentation in Vietnamese
```

## Contributing and reporting issues

Open a [GitHub Issue](https://github.com/tptindev/njin/issues) to report a
bug or suggest an improvement. njin builds 2D and 3D games; see also
[CHANGELOG.md](CHANGELOG.md) and the docs in `docs/pages/` before you start.
