# Game projects

Only the small examples listed in the root CMakeLists.txt and .gitignore belong
in the njin repository. New game folders are ignored by default. A serious game
must have its own Git repository; do not force-add it to njin or add it as a
submodule. Local sandtable, puzzle and crowd checkouts are independent repositories.

## Build the engine once

Use the same compiler, architecture and configuration for engine and game.

```sh
cmake --preset release -DNJIN_BUILD_EXAMPLES=OFF
cmake --build --preset release --target njin_rt
```

The engine exports build-tree targets to build-release/njinTargets.cmake.
The engine source checkout and its dependency build must remain available.
This export is for local builds, not a relocatable installed SDK.

## Build one game independently

```sh
cmake -S src/games/pong -B src/games/pong/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build src/games/pong/build
```

Games reuse the built engine without configuring the root project or downloading
its dependencies. Use -DNJIN_PREBUILT=/path/to/engine/build for a different build.
For a game cloned outside njin, supply -DNJIN_ROOT=/path/to/njin as well.
Existing example targets can still be built from the root for convenience.

A new game should call project(), set NJIN_ROOT as a CACHE PATH, include
${NJIN_ROOT}/cmake/njin_game.cmake, and link njin::rt (and optionally njin::gpu
and njin_warnings). Copy its own assets with njin_add_assets().
