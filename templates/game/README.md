# My Game

An independent C++20 game using njin. Keep this folder in its own repository.
The engine checkout and build remain separate from the game.

First build njin with the same compiler, architecture and configuration:

```sh
cmake --preset release -DNJIN_BUILD_EXAMPLES=OFF -DNJIN_BUILD_INSPECTOR=OFF -DNJIN_BUILD_UI_EDITOR=OFF -DNJIN_BUILD_ANIM_EDITOR=OFF
cmake --build --preset release --target njin_engine
```

Then, from this game folder (replace the engine path):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DNJIN_ROOT=/path/to/njin
cmake --build build
./build/bin/my_game
```

On Windows run `build\bin\my_game.exe`. Launch from the game folder so
`assets/` is read directly; there is no development copy to synchronize.
For Visual Studio, omit `-G Ninja -DCMAKE_BUILD_TYPE=Release`, build with
`--config Release`, and run `build\bin\Release\my_game.exe`. Build the engine
with Visual Studio too and pass its build folder as `NJIN_PREBUILT`.

`NJIN_ROOT` defaults to the sibling `../njin` checkout.
`NJIN_PREBUILT` defaults to `${NJIN_ROOT}/build-release`. Set
`-DNJIN_PREBUILT=/path/to/engine/build` to choose another engine build.
For Debug, build a Debug engine first, then configure the game with
`-DCMAKE_BUILD_TYPE=Debug` and point `NJIN_PREBUILT` at that build. Do not mix
Debug/Release, compilers or architectures between engine and game.

Add game source files to `add_executable()` and optional engine features to
`njin_use(my_game FEATURES ...)` in this project's `CMakeLists.txt`.
Only include public njin headers; no changes to the engine's CMake are needed.

To package the executable and assets:

```sh
cmake --build build --config Release --target my_game_dist
```

The archive is `build/dist/my_game-0.1.0.zip`. Players can extract it and run
the game without the engine checkout. Building the game still requires the
engine source checkout, its dependency sources and its build tree; this is
a local build export, not an installed, relocatable SDK.
