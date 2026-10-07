# Lesson 9: CMake for real projects {#learn_cmake_projects}

**What this lesson teaches:** the CMake you use every day in a project with several folders, downloaded libraries and assets:
`add_subdirectory`, `FetchContent`, presets, copying assets, warning flags, `compile_commands.json`. At the end you build a raylib
window with CMake, and read njin's `CMakeLists.txt` from top to bottom.

**What you need to know first:** @ref learn_cmake_basics (targets, `PRIVATE`/`PUBLIC`, configure and build, the cache).

Every example was actually run with CMake 4.0.2, Ninja 1.13 and GCC 15.2 (w64devkit) on Windows; long paths in the output
are shortened. MSVC, Linux and macOS were not tried here.

## A project with several folders

A small project, with the library and the game kept apart:

```
mini/
  CMakeLists.txt          root: shared settings, calls the subfolders
  cmake/assets.cmake      the add_assets function
  lib/
    CMakeLists.txt        the `mini` library
    include/mini.h
    src/add.cpp
  game/
    CMakeLists.txt        the `game` program
    main.cpp
    assets/hello.txt
```

```cmake
# mini/CMakeLists.txt
cmake_minimum_required(VERSION 3.28)
project(mini LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)      # write compile_commands.json for clangd

# Warning flags live in an INTERFACE target: any target that wants them links it.
add_library(mini_warnings INTERFACE)
if(MSVC)
  target_compile_options(mini_warnings INTERFACE /W4)
else()
  target_compile_options(mini_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

include(cmake/assets.cmake)

add_subdirectory(lib)
add_subdirectory(game)
```

```cmake
# mini/lib/CMakeLists.txt
file(GLOB_RECURSE MINI_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(mini STATIC ${MINI_SOURCES})
target_include_directories(mini PUBLIC include)   # PUBLIC: game needs this header too
target_link_libraries(mini PRIVATE mini_warnings)
```

```cmake
# mini/game/CMakeLists.txt
add_executable(game main.cpp)
target_link_libraries(game PRIVATE mini mini_warnings)

# Each game runs from its own folder, so two games never share assets.
set_target_properties(game PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/game")

add_assets(game assets)
```

`add_subdirectory(lib)` reads `lib/CMakeLists.txt` as a sub-project. Two things to remember:

- **Variables** set in a parent folder are visible in its subfolders: `CMAKE_CXX_STANDARD 20` at the root makes the files in `lib/`
  compile with `-std=c++20` (checked in `compile_commands.json`). Variables set in a subfolder do **not** travel back up,
  unless you write `set(NAME "value" PARENT_SCOPE)` (tried: the parent folder sees `NAME` but not a variable set with a plain `set`).
- **Targets** are global: `mini_warnings` is created at the root, yet both `lib/` and `game/` can link it. A relative path in
  `target_include_directories(mini PUBLIC include)` is relative to the folder of the `CMakeLists.txt` being run.

Build:

```
$ cmake -S . -B build -G Ninja && cmake --build build
[4/10] Building CXX object lib/CMakeFiles/mini.dir/src/add.cpp.obj
[7/10] Linking CXX static library lib\libmini.a
[8/10] Building CXX object game/CMakeFiles/game.dir/main.cpp.obj
[9/10] Linking CXX executable bin\game\game.exe

$ cd build/bin/game && ./game
add(2, 3) = 5
assets/hello.txt: hello v1
```

(The `Scanning ... for CXX dependencies` lines are left out: they come from the C++20 module scanning step, see
@ref learn_cmake_basics.)

njin is laid out exactly the same way: the root `CMakeLists.txt` calls `add_subdirectory` for `src/engine/api`, `src/engine/runtime` and each game
in `src/games/`; each game has its own `RUNTIME_OUTPUT_DIRECTORY`, for example `build/bin/platformer/`.

## How `include` differs from `add_subdirectory`

`include(cmake/assets.cmake)` runs that file **right in the current folder**, as if you had pasted its content there. That is why
the `add_assets` function defined in it can be used in every subfolder added afterwards. `add_subdirectory`, on the other hand, opens a new scope
for the subfolder's `CMakeLists.txt`. njin does exactly this: `include(cmake/njin.cmake)` so every game can call
`njin_add_assets` and `njin_package`, and only then `add_subdirectory(src/games/...)`.

## Listing source files with `file(GLOB)`

The reliable way: write the name of each file in `add_executable(game main.cpp play.cpp menus.cpp)`. njin does that for each game.
The convenient way: `file(GLOB_RECURSE ... "*.cpp")` collects every `.cpp` file in a folder; njin uses it for the runtime library because it has
dozens of files.

But the list is **captured at configure time**. Try it: after configuring, add `lib/src/mul.cpp`, while
`game` already calls `mul()`, then run only `cmake --build`.

With `CONFIGURE_DEPENDS` (as in the example above):

```
[0/2] Re-checking globbed directories...
[1/2] Re-running CMake...
[6/14] Building CXX object lib/CMakeFiles/mini.dir/src/mul.cpp.obj
add(2, 3) = 5
mul(2, 3) = 6
```

Without `CONFIGURE_DEPENDS`:

```
FAILED: bin/game/game.exe
ld.exe: game/CMakeFiles/game.dir/main.cpp.obj:main.cpp:(.text+0x41): undefined reference to `mul(int, int)'
```

You have to configure again by hand (`cmake -S . -B build`) before the new file is seen. The cost of `CONFIGURE_DEPENDS`: on every build
CMake checks the folders again (the `Re-checking globbed directories...` line), which is a little slower on big projects. The CMake documentation still
recommends listing files by hand, because then a change to the file list shows up clearly in the git diff. Which one you pick is
a trade-off between convenience and clarity.

## Warning flags in an INTERFACE target

Put the flags in `mini_warnings`, then link it only into **your** targets. Add `int unused = 5;` to `game/main.cpp`:

```
game/main.cpp:5:7: warning: unused variable 'unused' [-Wunused-variable]
```

Remove `mini_warnings` from `target_link_libraries(game ...)`: the warning count drops to 0. Most important of all, do **not** link it into
downloaded libraries (such as raylib): you cannot fix other people's code, so you do not need their hundreds of warnings. njin does this:
`njin_warnings` is linked only into `njin_rt` and the games, and a comment in the root `CMakeLists.txt` says "Never linked into fetched
dependencies".

**`SYSTEM`** solves a related problem: headers of an outside library that you include produce warnings. Tried with a header
containing an unused `static int helper() { return 1; }`:

```
target_include_directories(app PRIVATE vendor):         1 warning
target_include_directories(app SYSTEM PRIVATE vendor):  0 warnings
```

A folder marked `SYSTEM` is treated by the compiler as "belonging to the system", so it stays quiet. `FetchContent_Declare(... SYSTEM)`
(below) does that for every header of a downloaded library.

## `compile_commands.json`

With `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)`, configuring also writes `build/compile_commands.json`: the list of compile commands
for **each source file**. Editors use it (through clangd) to understand your `#include`s, flags and C++ standard:

```json
{
  "directory": ".../build",
  "command": "c++.exe -I.../lib/include -std=c++20 -Wall -Wextra -Wpedantic ... -c .../lib/src/add.cpp",
  "file": ".../lib/src/add.cpp"
}
```

clangd looks for this file in the project's root folder, so many people copy it up there: njin's `build.bat` has
`copy /Y build\compile_commands.json compile_commands.json`, and `.gitignore` ignores that copy. njin's presets also
set `CMAKE_EXPORT_COMPILE_COMMANDS` to `ON`. Note: while the C++20 module scanning step is not turned off, every command gets the extra flags
`-fmodules-ts -fmodule-mapper=...`, which add noise to this file.

## Copying assets next to the executable

The game looks for `assets/hello.txt` by a relative path, so the `assets` folder must sit next to the executable. Here is the `add_assets` function,
shortened from `njin_add_assets` in `cmake/njin.cmake`:

```cmake
# cmake/assets.cmake
function(add_assets target dir)
  cmake_path(ABSOLUTE_PATH dir BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
             NORMALIZE OUTPUT_VARIABLE source)
  cmake_path(GET source FILENAME name)
  add_custom_target(
    ${target}_${name}
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${source}"
            "$<TARGET_FILE_DIR:${target}>/${name}"
    COMMENT "Copying ${name}/ next to ${target}"
    VERBATIM)
  add_dependencies(${target} ${target}_${name})
endfunction()
```

- `function(...)` defines your own command; its parameters are `target` and `dir`.
- `cmake_path(ABSOLUTE_PATH ...)` turns `assets` into an absolute path (relative to the folder of the `CMakeLists.txt` that
  calls the function), and `cmake_path(GET ... FILENAME)` takes the last folder name, `assets`.
- `add_custom_target` creates a build step you write yourself; `add_dependencies(game game_assets)` makes it run before `game`
  counts as done.
- `$<TARGET_FILE_DIR:game>` is a **generator expression**: CMake fills in the value at **build** time, not at configure time.
  That keeps it correct even when the output folder changes with the build type.

The result, actually run:

```
$ ls build/bin/game
assets  game.exe

$ echo "hello v2" > game/assets/hello.txt; cmake --build build
[1/2] Copying assets/ next to game
$ ./game | tail -1
assets/hello.txt: hello v2
```

The copy step **runs on every build** (you always see the `Copying ...` line), but `copy_directory_if_different` only copies
files that changed. Two interesting things were checked:

- A file **deleted** from the source folder is **not deleted** from the copy: add `temp.txt`, build, delete it from the source, build
  again, and `temp.txt` is still in `build/bin/game/assets`. This behaviour is noted in the comment of `njin_add_assets`.
- With `Ninja Multi-Config`, assets follow each configuration: `mc/Debug/assets/a.txt` and `mc/Release/assets/a.txt`. That is
  exactly because of `$<TARGET_FILE_DIR:...>`, not a hard-coded path.

## Downloading libraries with FetchContent

Need EnTT? No install, no copying code into the repo. CMake downloads it at configure time. This is exactly how njin's root
`CMakeLists.txt` gets EnTT (and raylib):

```cmake
include(FetchContent)
FetchContent_Declare(
  entt
  GIT_REPOSITORY https://github.com/skypjack/entt.git
  GIT_TAG v4.0.0
  SYSTEM)
FetchContent_MakeAvailable(entt)

add_executable(fetch_demo main.cpp)
target_link_libraries(fetch_demo PRIVATE EnTT::EnTT)
```

- `FetchContent_Declare` **declares** where to get the library, and downloads nothing yet. `FetchContent_MakeAvailable` downloads it (if needed), then calls
  `add_subdirectory` on the downloaded code, so its targets (here `EnTT::EnTT`) can be used like your own.
- **`GIT_TAG` should be pinned** to a version (`v4.0.0`, `6.0`) or a commit hash, not a branch such as `main`. Branches
  move: today it builds, next week the library changes and your game breaks without you changing anything. njin's inspector pins
  Dear ImGui to `v1.92.9` and rlImGui to a full commit hash, with a comment explaining why the two versions must go together.
- **`SYSTEM`**, as described above: headers of the downloaded library do not flood you with warnings.

How many times do you need the network? Measured with EnTT:

```
$ cmake -S . -B build -G Ninja      (first time, with network)
-- Configuring done (11.8s)

$ cmake -S . -B build               (second time)
-- Configuring done (1.5s)

$ ls build/_deps
entt-build  entt-src  entt-subbuild
```

The library is downloaded into `build/_deps/`. The first time takes about 12 seconds (with the download); the second time only 1.5 seconds because the code is already there. But
**deleting `build/` also deletes `_deps`**: configuring again took 10.4 seconds, meaning it downloaded again. Two ways to avoid that:

- Put the download location outside `build/` with `-DFETCHCONTENT_BASE_DIR=<folder>`. Tried: first configure 10.3 seconds, delete `build/`,
  configure again 4.1 seconds (no download).
- Use a copy already on your machine with `-DFETCHCONTENT_SOURCE_DIR_<NAME>=<folder>` (`<NAME>` in upper case: `RAYLIB`, `ENTT`).
  CMake uses that folder and downloads nothing. This also works offline, if you already have the source code.

Downloads go through git, so the machine needs Git (see @ref setup).

**A library without a `CMakeLists.txt`**: `FetchContent_MakeAvailable` only downloads the code, and you write the target yourself. njin's
inspector does this with Dear ImGui: a comment says "Neither ships a CMakeLists.txt: this only downloads them", then
`add_library(njin_imgui STATIC ${imgui_SOURCE_DIR}/imgui.cpp ...)` lists its `.cpp` files by hand. The variable
`imgui_SOURCE_DIR` is set by FetchContent (library name + `_SOURCE_DIR`).

## Presets: one command instead of a whole line of arguments

Typing `-G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` every time is error-prone. A **preset** is a
`CMakePresets.json` file in the root folder that stores those arguments under a name:

```json
{
  "version": 6,
  "cmakeMinimumRequired": { "major": 3, "minor": 28, "patch": 0 },
  "configurePresets": [
    {
      "name": "base",
      "hidden": true,
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": { "CMAKE_EXPORT_COMPILE_COMMANDS": "ON" }
    },
    { "name": "debug",   "inherits": "base", "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" } },
    { "name": "release", "inherits": "base", "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" } }
  ],
  "buildPresets": [
    { "name": "debug",   "configurePreset": "debug" },
    { "name": "release", "configurePreset": "release" }
  ]
}
```

- A `"hidden": true` preset cannot be used directly; it exists only so other presets can **inherit** (`inherits`) the shared
  configuration. `debug` and `release` differ by exactly one line.
- `${sourceDir}` is the folder that holds the preset file, `${presetName}` is the preset's name: each preset gets its own build folder.
- The `buildPresets` section enables `cmake --build --preset debug`.

Run:

```
$ cmake --list-presets
Available configure presets:

  "debug"
  "release"

$ cmake --preset debug
-- Build files have been written to: .../build/debug

$ cmake --build --preset debug
[2/2] Linking CXX executable preset_demo.exe

$ cmake --preset release && cmake --build --preset release
[2/2] Linking CXX executable preset_demo.exe

$ ls build
debug  release
```

`base` does not appear in the list because it is hidden. The two build folders exist side by side, so switching between Debug and
Release needs no reconfigure. If you misspell the name of an inherited preset, CMake reports `Invalid preset: "debug"`.

njin's presets (open `CMakePresets.json` in the root folder) are the same, except for `binaryDir`: `debug` uses `build/`, `release` uses
`build-release/`. CMake also reads `CMakeUserPresets.json` for personal presets that stay out of git (not tried here).

## Full example: opening a raylib window

raylib is the C graphics library njin is built on. Use `FetchContent` to download it. The file `main.c`:

@include learn_cmake_raylib_window.c

And the `CMakeLists.txt` in the same folder:

```cmake
cmake_minimum_required(VERSION 3.28)
project(raylib_window LANGUAGES C)

include(FetchContent)

set(BUILD_EXAMPLES OFF)   # raylib reads this variable: ON adds its examples/ folder to the build

FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 6.0
  SYSTEM)
FetchContent_MakeAvailable(raylib)

add_executable(raylib_window main.c)
target_link_libraries(raylib_window PRIVATE raylib)
```

`target_link_libraries(... raylib)` alone is enough: the system libraries raylib needs (OpenGL graphics, audio) are declared by the
`raylib` target itself, so you do not have to list them. Configure and build:

```
$ cmake -S . -B build -G Ninja
$ cmake --build build
[30/30] Linking C executable raylib_window.exe
```

If you already have the raylib 6.0 source on your machine (for example, njin has downloaded it into `build/_deps/raylib-src`), add
`-DFETCHCONTENT_SOURCE_DIR_RAYLIB=<that folder>` to avoid downloading it again. This exact setup was run: configuring took 6.7 seconds, building the 30
steps took 6 seconds. The first time **without** a local copy, CMake downloads raylib from GitHub, and the download time depends on your network
(not measured separately; only EnTT was measured above).

Run `./build/raylib_window` (Windows: `build\raylib_window.exe`). A window opens; raylib's log says:

```
INFO: Initializing raylib 6.0
INFO: DISPLAY: Device initialized successfully
INFO: GLAD: OpenGL extensions loaded successfully
```

@image html learn_cmake_raylib_window.png "The first raylib window, built with CMake"

(The picture was taken with `TakeScreenshot` at the 20th frame of a copy of this program, built with exactly the
`CMakeLists.txt` above; the copy only adds one line that calls `TakeScreenshot`. The FPS number varies by machine.) Press Esc or the close button to quit. This is also the frame the shader lesson reuses: the same
`CMakeLists.txt`, only the content of `main.c` changes.

## Reading njin's CMakeLists.txt from top to bottom

Open `CMakeLists.txt` in njin's root folder. Each block does one job:

**1. CMake version.** `cmake_minimum_required(VERSION 3.28...4.0)`, with a comment: raylib 6.0 needs 3.25 or newer, EnTT v4.0.0
needs 3.28 or newer.

**2. Read njin's version from a header.** The version number exists only in `njin_version.h`; CMake reads it from there so the two can never
drift apart:

```cmake
file(READ ".../njin_version.h" NJIN_VERSION_HEADER)
foreach(part MAJOR MINOR PATCH)
  string(REGEX MATCH "#define NJIN_VERSION_${part} ([0-9]+)" _ "${NJIN_VERSION_HEADER}")
  set(NJIN_VERSION_${part} "${CMAKE_MATCH_1}")
endforeach()
set(NJIN_VERSION "${NJIN_VERSION_MAJOR}.${NJIN_VERSION_MINOR}.${NJIN_VERSION_PATCH}")
```

`file(READ ...)` reads a file into a variable; `string(REGEX MATCH ...)` finds the line `#define NJIN_VERSION_MAJOR 0`, and the group in
parentheses `([0-9]+)` ends up in the variable `CMAKE_MATCH_1`. Running just this snippet with `cmake -P` (script mode) on a copy of the
header gives `-- njin 0.3.0`.

**3. `project(njin_lab VERSION ${NJIN_VERSION} LANGUAGES C CXX)`.** Both C (raylib is C) and C++.

**4. Standard and shared settings:** C++20 required, no compiler extensions, module scanning turned off,
`CMAKE_EXPORT_COMPILE_COMMANDS ON`.

**5. Where build output goes:**

```cmake
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
```

Every executable goes into `build/bin/`, static libraries into `build/lib/`. Each game can override this with its own
`RUNTIME_OUTPUT_DIRECTORY` (as above).

**6. `njin_warnings`:** an INTERFACE target holding `/W4` (MSVC) or `-Wall -Wextra -Wpedantic` (everything else), as you learned.

**7. FetchContent for raylib and EnTT**, both pinned to a version and with `SYSTEM`. Before that comes `set(BUILD_EXAMPLES OFF)`.

**8. Changing a downloaded target:** `target_compile_definitions(raylib PRIVATE SUPPORT_SCREEN_CAPTURE=0)`. Because
`FetchContent_MakeAvailable` has already created the `raylib` target, you add a definition to it from outside: here it turns off raylib saving a screenshot
by itself when F12 is pressed (the comment explains: a game made with njin may want F12 for something else, and `njin::screenshot()` is there
instead).

**9. `include(cmake/njin.cmake)`.** Loads the `njin_add_assets` and `njin_package` functions for every game (this lesson taught
a shortened version of the first one).

**10. `add_subdirectory`** in order: `src/engine/api`, `src/engine/gpu`, `src/engine/runtime` (the libraries), then each
game.

**11. An option:** `option(NJIN_BUILD_INSPECTOR ... ON)` wraps `add_subdirectory(src/tools/inspector)`. Turning it off means
Dear ImGui is not downloaded and the inspector tool is not built.

Then there is `njin_package(<target> ...)` in `cmake/njin.cmake`, which each game calls once to package a release: on Windows it
attaches an icon and version info to the executable, hides the console window in every build type except Debug, and creates a
`<name>_dist` target that gathers the executable, the assets folder and a few extra files (readme, licence) into a `.zip` file. This lesson has only **read**
this function to describe it; it has not run it. See @ref samples for how to use it.

## Self-check

1. Why does `FetchContent_MakeAvailable` need the network only the first time, and why does deleting `build/` make it download again?
2. What error does `file(GLOB ...)` without `CONFIGURE_DEPENDS` cause when you add a new `.cpp` file?
3. Why not link `mini_warnings` into downloaded libraries?
4. How do `include(file.cmake)` and `add_subdirectory(dir)` differ?
5. Why does `add_assets` use `$<TARGET_FILE_DIR:game>` instead of a hard-coded path?

## Exercises

1. **Several asset folders.** Change `add_assets` so you can call `add_assets(game assets shaders)`, copying both folders next to
   the executable.
2. **A preset with a switch.** The project has `option(SHOW_DEBUG ...)`, `OFF` by default. Add two presets, `plain` and `verbose`, so that
   `cmake --preset verbose` turns it on and `cmake --preset plain` does not, each preset with its own build folder.
3. **Do not download again.** You delete `build/` every day and every time EnTT gets downloaded again. Change the configure command so it is not
   downloaded next time.

## Answers

**Self-check**

1. The code is downloaded into `build/_deps/`; later configures find it there, so they do not download again. Deleting `build/` also deletes
   `_deps`. Put `FETCHCONTENT_BASE_DIR` outside `build/` to avoid that.
2. The file list is captured at configure time. The new file is not in the list, so if something calls a function from it you get a link error,
   `undefined reference` (like `mul(int, int)` above), until you configure again by hand.
3. You cannot fix their code, so the warnings are just noise that hides the warnings in your own code.
4. `include` runs the file right in the current scope (functions and variables are visible there); `add_subdirectory` opens a child scope
   with its own `CMakeLists.txt`.
5. Because the folder holding the executable can change with the target and with the build type (for example `Debug/` and `Release/` with
   `Ninja Multi-Config`); a generator expression is evaluated at build time, so it is always correct.

**Exercise 1**

```cmake
function(add_assets target)
  foreach(dir IN LISTS ARGN)
    cmake_path(ABSOLUTE_PATH dir BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
               NORMALIZE OUTPUT_VARIABLE source)
    cmake_path(GET source FILENAME name)
    add_custom_target(
      ${target}_${name}
      COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${source}"
              "$<TARGET_FILE_DIR:${target}>/${name}"
      COMMENT "Copying ${name}/ next to ${target}"
      VERBATIM)
    add_dependencies(${target} ${target}_${name})
  endforeach()
endfunction()
```

`ARGN` holds the arguments after `target`. The result, run with `add_assets(ex3 assets shaders)`:

```
[1/4] Copying assets/ next to ex3
[2/4] Copying shaders/ next to ex3
build/assets/a.txt
build/shaders/s.fs
```

**Exercise 2**

```json
{
  "version": 6,
  "configurePresets": [
    { "name": "plain", "generator": "Ninja", "binaryDir": "${sourceDir}/build/plain" },
    {
      "name": "verbose",
      "inherits": "plain",
      "binaryDir": "${sourceDir}/build/verbose",
      "cacheVariables": { "SHOW_DEBUG": "ON" }
    }
  ]
}
```

```
$ cmake --preset plain
-- SHOW_DEBUG = OFF
$ cmake --preset verbose
-- SHOW_DEBUG = ON
```

**Exercise 3**

Add `-DFETCHCONTENT_BASE_DIR="$PWD/deps"` to the configure command. Measured: the first time 10.3 seconds (with the download); after
`rm -rf build`, configuring again took only 4.1 seconds and downloaded nothing. The `deps` folder should be outside `build/` and ignored by git.

## Next step

@ref learn_shader_start : what a shader is, and running your first shader in the very raylib window you just built.
