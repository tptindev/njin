# Examples and standalone games

Start a new game by copying [`templates/game`](../templates/game) to a
folder outside this checkout. Its CMake, source and assets belong to the game;
do not add it to the engine's root CMake. Follow the
[template guide](../templates/game/README.md) to build and package it.
The folders here are engine examples, not the default home for new games.

Only the small examples listed in the root CMakeLists.txt and .gitignore belong
in the njin repository. New game folders are ignored by default. A serious game
must have its own Git repository; do not force-add it to njin or add it as a
submodule. Personal projects live outside the engine checkout, in their own repositories.
On this workstation they are grouped under `D:/projects/njin-proj`.

## Build the engine once

Use the same compiler, architecture and configuration for engine and game.

```sh
cmake --preset release -DNJIN_BUILD_EXAMPLES=OFF -DNJIN_BUILD_INSPECTOR=OFF -DNJIN_BUILD_UI_EDITOR=OFF -DNJIN_BUILD_ANIM_EDITOR=OFF
cmake --build --preset release --target njin_engine
```

The engine exports build-tree targets to build-release/njinTargets.cmake.
The engine source checkout and its dependency build must remain available.
This export is for local builds, not a relocatable installed SDK.

## Build one game independently

```sh
cmake -S examples/pong -B examples/pong/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build examples/pong/build
```

Games reuse the built engine without configuring the root project or downloading
its dependencies. Use -DNJIN_PREBUILT=/path/to/engine/build for a different build.
For a game cloned outside njin, supply -DNJIN_ROOT=/path/to/njin as well.
Existing example targets can still be built from the root for convenience.

A new game should call project(), set NJIN_ROOT as a CACHE PATH, include
${NJIN_ROOT}/cmake/njin_game.cmake, and link njin::rt (and optionally njin::gpu
and njin_warnings). The template uses source assets during development and
`njin_package(... ASSETS assets)` for distribution. `njin_add_assets()` is
available if a game needs to run from arbitrary working directories during development.

## Verify the external project workflow

After building the engine, run the same check used by CI. `TEST_ROOT` must
be a fresh folder outside the engine checkout; the check leaves its build
and distribution there for inspection.

```sh
cmake -DNJIN_ROOT=/path/to/njin -DNJIN_PREBUILT=/path/to/njin/build-release -DTEST_ROOT=/tmp/njin-standalone -P cmake/check_standalone.cmake
```

The check reads the generator from the engine's CMake cache. Use
`-DCXX_COMPILER=/path/to/c++` to select a compiler explicitly.
