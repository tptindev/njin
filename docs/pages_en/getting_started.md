# Getting started {#getting_started}

This page shows you how to build njin, run the sample games, and write your first program. Haven't installed a compiler,
CMake or Ninja yet? Follow @ref setup first, which has instructions for each operating system. Not familiar with C++, CMake
or shaders? The @ref learn series teaches from scratch, and does not need njin.

## Requirements

- **CMake** 3.28 or newer
- A **C++20** compiler. The main development environment is GCC in w64devkit on Windows.
  MSVC and GCC on Linux are in CI (`.github/workflows/build.yml`)
- **Ninja** (not needed if you use MSVC with the Visual Studio generator)
- **Git**: CMake downloads raylib 6.0, EnTT v4.0.0 and (for njin_inspector) Dear ImGui on the first configure
- **Linux**: also the X11 and OpenGL development libraries, for example on Ubuntu:
  `sudo apt install ninja-build libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev`

## Getting the source

```
git clone https://github.com/tptindev/njin.git
cd njin
```

## Build and run

On Windows, two scripts in the root folder do everything:

| Script | What it does |
|---|---|
| `build.bat` | Configures with Ninja (Debug), updates `compile_commands.json` for clangd, then builds |
| `run.bat` | Builds the `njin_sandbox` target and runs `build\bin\njin_sandbox.exe` |
| `build\bin\njin_pong.exe` | The sample Pong game, built together with `build.bat` |

On every operating system, use the **presets** in `CMakePresets.json`, so you do not need to remember parameters:

| Preset | Build folder | What it does |
|---|---|---|
| `debug` | `build/` | Ninja, Debug. The game has a debug connection to njin_inspector |
| `release` | `build-release/` | Ninja, Release. Optimized, no debug connection |

```
cmake --preset debug
cmake --build --preset debug --target njin_sandbox
build\bin\njin_sandbox.exe
```

Leave out `--target` to build everything (sample games, njin_inspector). With MSVC you do not need a preset:
`cmake -S . -B build -A x64` then `cmake --build build --config Release`.

@note `run.bat` runs the game with the working directory set to the repo's root folder. Relative
paths such as `assets/player.png` are resolved from there first, then from the folder containing the exe.
Use `njin_add_assets()` in CMake so the game's assets folder is copied next to the exe on every
build, see @ref window_files.

## The smallest program

@include minimal_main.cpp

Three functions to know:

- njin_create() opens the window and returns the engine's context.
- njin_run() runs the loop until the window is closed.
- njin_destroy() releases all resources.

This program opens an empty window. To make it do something, you write a
**module**: see @ref modules_systems.

## Adding your game's module

Create the module in a `.cpp` file, and register it in `main` before njin_run():

@include hello_module.cpp

For a game in `src/games/<name>/`, its `CMakeLists.txt` needs:

```cmake
add_executable(my_game main.cpp modules/hello.cpp)

# njin::rt pulls in njin::api (and its include folder).
target_link_libraries(my_game PRIVATE njin::rt njin_warnings)
```

then add `add_subdirectory(src/games/my_game)` to the root `CMakeLists.txt`.

## Next steps

- @ref first_jump : a jumping character on a map, in 50 lines (platformer)
- @ref first_walk : an 8-direction character on a map, in 50 lines (top-down)
- @ref modules_systems : write the game's logic
- @ref cheatsheet : I want to do X, which function do I use?
- @ref game_loop : learn the order a frame runs in
- @ref ecs : create entities and components
- `src/games/pong`: a complete game to read and tinker with
