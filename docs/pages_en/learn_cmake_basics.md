# Lesson 8: CMake basics {#learn_cmake_basics}

**What this lesson teaches:** writing a `CMakeLists.txt` for a program and a library, understanding `PRIVATE`/`PUBLIC`/`INTERFACE`,
choosing the C++ standard and the build type, and knowing why "delete the `build/` folder and configure again" cures many strange errors.

**What you need to know first:** how to compile a multi-file program by hand with `g++` (compile each file into an object
file, then link, see @ref learn_c_project). You do not need to know anything about CMake yet.

Every example below was actually run with CMake 4.0.2, Ninja 1.13 and GCC 15.2 (w64devkit) on Windows. Long paths in the
output are shortened. Not tried with MSVC, Linux or macOS: where they are mentioned, the page says so.

## What CMake does

Compiling by hand is fine for one file. With twenty files you need to: recompile only the files that changed, know which
file depends on which, pass the right flags (`-I`, `-std=`, `-O2`) to each file, and do all that on both Windows and
Linux. That is the job of a **build system**.

CMake does not compile anything itself. It reads `CMakeLists.txt` and **generates** files for a real build tool (Ninja, Make, Visual
Studio). So every CMake project goes through two steps:

| Step | Command | What it does |
|---|---|---|
| **Configure** | `cmake -S . -B build -G Ninja` | Reads `CMakeLists.txt`, finds the compiler, generates `build/build.ninja` |
| **Build** | `cmake --build build` | Calls Ninja to compile and link |

## The smallest project

Two files:

```cpp
// main.cpp
#include <iostream>

int main() {
  std::cout << "Hello from CMake\n";
}
```

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.28)
project(hello LANGUAGES CXX)

add_executable(hello main.cpp)
```

- `cmake_minimum_required` says which CMake version and later understands this file. njin writes `3.28...4.0`: at least 3.28,
  and it accepts the newest rules up to 4.0.
- `project` sets the project name and the languages used (`CXX` is C++, `C` is C).
- `add_executable(name files...)` creates a program **target** from the source files.

Run the two steps:

```
$ cmake -S . -B build -G Ninja
-- The CXX compiler identification is GNU 15.2.0
...
-- Configuring done (1.7s)
-- Generating done (0.0s)
-- Build files have been written to: .../b1/build

$ cmake --build build
[1/2] Building CXX object CMakeFiles/hello.dir/main.cpp.obj
[2/2] Linking CXX executable hello.exe

$ ./build/hello
Hello from CMake
```

The three flags of the configure step: `-S .` is the folder that holds `CMakeLists.txt` (the **source**), `-B build` is the folder
that holds everything generated (the **binary dir**), `-G Ninja` chooses the build tool. The `build/` folder holds `CMakeCache.txt`,
`build.ninja` and the executable; the source folder is not touched. That is an **out-of-source build**: to
clean up, just delete `build/`.

Build a second time and CMake and Ninja only do the work that is needed:

```
$ cmake --build build          (nothing changed)
ninja: no work to do.

$ touch main.cpp; cmake --build build
[1/2] Building CXX object CMakeFiles/hello.dir/main.cpp.obj
[2/2] Linking CXX executable hello.exe

$ (add a line to CMakeLists.txt); cmake --build build
[0/1] Re-running CMake...
-- added this line
-- Configuring done (0.3s)
```

When you edit `CMakeLists.txt`, the configure step reruns by itself. You rarely need to type `cmake -S . -B build` again.

## Choosing the C++ standard

If you say nothing, GCC 15.2 does not turn on C++20. A program that uses `std::span` (C++20) will not compile:

```
$ cmake --build build
main.cpp:4:14: error: 'span' is not a member of 'std'
```

Fix it by asking for C++20 on the target:

```cmake
target_compile_features(std_demo PRIVATE cxx_std_20)
```

For the whole project, set variables at the top of the file (njin does exactly this):

```cmake
set(CMAKE_CXX_STANDARD 20)             # every target declared after this line
set(CMAKE_CXX_STANDARD_REQUIRED ON)    # fail if C++20 is not available, do not quietly fall back
set(CMAKE_CXX_EXTENSIONS OFF)          # -std=c++20, not -std=gnu++20
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)    # skip the C++20 module scanning step
```

Checked: with `CMAKE_CXX_EXTENSIONS OFF` the actual flag is `-std=c++20`; with `target_compile_features` it is
`-std=gnu++20` (GCC's extended dialect). The last line turns off a step CMake adds on its own from C++20: when it is not turned off, each source
file gets an extra `Scanning ... for CXX dependencies` step and `Generating CXX dyndep file`, and the compile flags gain
`-fmodules-ts -fmodule-mapper=...`. A game that does not use C++20 modules gets nothing from those steps; njin turns it off in the
root `CMakeLists.txt`.

## Everything is a target

Modern CMake thinks in **targets**: a program or a library, together with what it needs and what it
provides to its users. Three commands create targets:

| Command | Creates |
|---|---|
| `add_executable(game main.cpp)` | a program |
| `add_library(rt STATIC a.cpp b.cpp)` | a static library (`.a` or `.lib`) |
| `add_library(api INTERFACE)` | a "library" with no source files: it only carries information (include folders, flags) |

Then you attach information to the target with the `target_*` commands: `target_include_directories`, `target_link_libraries`,
`target_compile_definitions`, `target_compile_features`, `target_compile_options`.

### PRIVATE, PUBLIC, INTERFACE

Every `target_*` command must say who the information is for:

| Keyword | Used by this target itself | Passed on to whoever links to it |
|---|---|---|
| `PRIVATE` | yes | no |
| `INTERFACE` | no | yes |
| `PUBLIC` | yes | yes |

An easy way to remember: ask "**do my headers mention that thing?**". Yes: `PUBLIC`. Only used in my `.cpp` files:
`PRIVATE`.

Try it with three targets: `api` (headers the game may use), `hidden` (something the game must not see), `rt` (the library in
the middle) and `game`:

```cmake
add_library(api INTERFACE)                       # headers only
target_include_directories(api INTERFACE api)

add_library(hidden INTERFACE)                    # something the game must not see
target_include_directories(hidden INTERFACE hidden)

add_library(rt STATIC rt/rt.cpp)
target_link_libraries(rt PUBLIC api PRIVATE hidden)

add_executable(game game.cpp)
target_link_libraries(game PRIVATE rt)
```

`rt.cpp` includes both `api.h` and `hidden.h`, so it compiles. But if `game.cpp` also does `#include <hidden.h>`:

```
$ cmake --build build
game.cpp:2:10: fatal error: hidden.h: No such file or directory
    2 | #include <hidden.h>   // the game reaches into the private dependency
```

Change it to `target_link_libraries(rt PUBLIC api hidden)` and it compiles, and the game prints `run_game() = 42, hidden = 42`.
This is how you force the users of a library to touch only its public part.

With a **static library**, `PRIVATE` only hides the headers, not the linking. Checked: `rt` links `hidden` (a static
library) with `PRIVATE`, and the link command of `game` still has `librt.a libhidden.a`, while the compile command for `game.cpp` has
no `-I` flag for `hidden`. The game links, but cannot see the headers.

**Common mistake:** your public header does `#include` of another library's header, but you link that library with `PRIVATE`.
`render.h` includes `core.h`, `render` links `core` with `PRIVATE`, the game includes `render.h`:

```
render/include/render.h:2:10: fatal error: core.h: No such file or directory
```

The error is in `render.h`, not in `game.cpp`. Change it to `PUBLIC` and you are done.

### How njin is laid out

njin uses exactly that shape (open `src/engine/api/CMakeLists.txt` and `src/engine/runtime/CMakeLists.txt`):

```cmake
# api: headers only, no source files
add_library(njin_api INTERFACE)
add_library(njin::api ALIAS njin_api)
target_include_directories(njin_api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(njin_api INTERFACE cxx_std_20)
target_link_libraries(njin_api INTERFACE EnTT::EnTT)

# runtime: static library, raylib is hidden
add_library(njin_rt STATIC ${NJIN_RT_SOURCES} ...)
add_library(njin::rt ALIAS njin_rt)
target_link_libraries(
  njin_rt
  PUBLIC njin::api
  PRIVATE raylib njin_warnings)
```

A game only writes `target_link_libraries(my_game PRIVATE njin::rt)`. It gets `njin.h` (through `njin::api`), gets C++20
and EnTT, but does **not** see `raylib.h`. That is why the wiki home page says "raylib is completely hidden".

**`njin::api` is an `ALIAS`.** A name with `::` must always be a real target. Mistype a name with `::` and CMake reports an
error right at the configure step:

```
CMake Error at CMakeLists.txt:8 (target_link_libraries):
  Target "game" links to:

    my::libb

  but the target was not found.  Possible reasons include:

    * There is a typo in the target name.
```

Mistype a plain name (`mylibb`) and CMake treats it as a system library, and you only find out at the link step:
`ld.exe: cannot find -lmylibb`. That is why any library that matters is named in the `njin::rt` style.

## Build types and generators

The **generator** (`-G`) is the build tool CMake generates files for. `Ninja` is fast and available on every operating system. There are also `Unix
Makefiles`, `MinGW Makefiles`, `Visual Studio 17 2022`... (`cmake --help` lists what your machine has).

The **build type** decides the optimization and debug flags. With Ninja each build folder has only **one** type, chosen at configure time with
`-DCMAKE_BUILD_TYPE=`. Measured by printing the actual compile flags of the same program:

| `CMAKE_BUILD_TYPE` | Actual flags | `assert()` |
|---|---|---|
| (empty) | no flags at all | on |
| `Debug` | `-g` | on |
| `Release` | `-O3 -DNDEBUG` | off (compiled out) |

Forgetting to set the build type is a common mistake: the program is built **without optimization**. An example in njin: `NDEBUG` only exists in
Release, and the sample games only open the debug port under `#ifndef NDEBUG`.

**Multi-config** generators (Visual Studio, `Ninja Multi-Config`) are different: one build folder holds several types, chosen
at build time with `--config`:

```
$ cmake -S . -B multi -G "Ninja Multi-Config"
$ cmake --build multi --config Debug
[2/2] Linking CXX executable Debug\types_demo.exe
$ cmake --build multi --config Release
[2/2] Linking CXX executable Release\types_demo.exe
```

The two executables are in `multi/Debug/` and `multi/Release/`. The Visual Studio generator is multi-config too, so you use
`--config Release` as above (not tried here).

## Variables, `message` and `if`

```cmake
set(GREETING "hello")                    # a plain variable
message(STATUS "GREETING = ${GREETING}")
message(STATUS "project  = ${PROJECT_NAME} ${PROJECT_VERSION}")

set(SOURCES main.cpp)                    # a list is a string separated by ';'
list(APPEND SOURCES extra.cpp)
message(STATUS "SOURCES  = ${SOURCES}")

foreach(name IN ITEMS a b c)
  message(STATUS "  loop: ${name}")
endforeach()
```

```
-- GREETING = hello
-- project  = vars_demo 1.2.3
-- SOURCES  = main.cpp;extra.cpp
--   loop: a
--   loop: b
--   loop: c
```

`message` has several levels. `STATUS` just prints; `WARNING` prints with the location and carries on; `FATAL_ERROR` stops configuring:

```
CMake Warning at CMakeLists.txt:29 (message):
  this is a warning (carries on)

CMake Error at CMakeLists.txt:30 (message):
  this is an error: stop here

-- Configuring incomplete, errors occurred!
```

Branching on the operating system and the compiler:

```cmake
message(STATUS "CMAKE_SYSTEM_NAME = ${CMAKE_SYSTEM_NAME}")
message(STATUS "CMAKE_CXX_COMPILER_ID = ${CMAKE_CXX_COMPILER_ID}")
if(WIN32)  ...  endif()
if(MSVC)   ...  endif()
```

Result on the machine it was tried on (Windows, GCC):

```
-- CMAKE_SYSTEM_NAME = Windows
-- CMAKE_CXX_COMPILER_ID = GNU
-- WIN32 is true
-- MSVC is false
```

Note: `WIN32` means "building for Windows", **even** when using GCC. `MSVC` is only true when the compiler is MSVC.
They are two different things, and njin uses both (open the root `CMakeLists.txt` and `src/engine/runtime/CMakeLists.txt`):

```cmake
if(MSVC)
  target_compile_options(njin_warnings INTERFACE /W4)                    # MSVC flags
else()
  target_compile_options(njin_warnings INTERFACE -Wall -Wextra -Wpedantic) # GCC/Clang flags
endif()

if(WIN32)
  target_link_libraries(njin_rt PRIVATE ws2_32)     # Windows sockets, needed for the debug port
endif()
```

## Options and the cache

`option(NAME "description" default)` creates a switch that whoever builds can turn on or off with `-D`:

```cmake
option(SHOW_DEBUG "Print extra lines" OFF)
message(STATUS "SHOW_DEBUG = ${SHOW_DEBUG}")
```

njin has a real one: `option(NJIN_BUILD_INSPECTOR "Build the njin_inspector debug tool" ON)`. If you do not want to download Dear
ImGui and build the inspector, configure with `-DNJIN_BUILD_INSPECTOR=OFF`.

The value of an `option` and of every `-D` variable is stored in the **cache**, the file `build/CMakeCache.txt`. The cache outlives
`CMakeLists.txt`. Try it:

```
$ cmake -S . -B build -G Ninja -DSHOW_DEBUG=ON
-- SHOW_DEBUG = ON

$ cmake -S . -B build        (no more -D; the file's default still says OFF)
-- SHOW_DEBUG = ON

$ grep SHOW_DEBUG build/CMakeCache.txt
SHOW_DEBUG:BOOL=ON

$ cmake -S . -B fresh -G Ninja   (a new folder)
-- SHOW_DEBUG = OFF
```

The old value **is still there** in the existing `build/` folder, even though you no longer mention it. The compiler is remembered too:

```
$ CXX=clang++ cmake -S . -B build        (the old build folder)
-- compiler   = C:/Dev/raylib/w64devkit/bin/c++.exe        <- still GCC

$ CXX=clang++ cmake -S . -B fresh2 -G Ninja   (a new folder)
-- The CXX compiler identification is Clang 21.1.8 with GNU-like command-line
```

This is the reason for the familiar advice: **if the configuration looks wrong, a compiler change does not take, or an option is stuck at its old
value, delete the `build/` folder and configure again.** The cache is the cause, and `build/` can be regenerated from scratch, so deleting it
loses nothing. (Changing the `CXX` environment variable only has an effect when a new build folder is created.)

## Self-check

1. How do the two steps `cmake -S . -B build` and `cmake --build build` differ?
2. `target_link_libraries(rt PRIVATE hidden)`: can `game` (which links `rt`) `#include` the headers of `hidden`? And can it
   link with `hidden` if it is a static library?
3. Why is it wrong to use `PRIVATE` for a library that your public header includes?
4. What does `-DCMAKE_BUILD_TYPE=Release` change in the compile flags?
5. You just changed the default value of an `option` in `CMakeLists.txt`, but the build result does not change. What is the cause and how do you
   fix it?

## Exercises

1. **A switch.** Add `option(VERBOSE_GREETING ...)` (default `OFF`) to the `hello` project. When it is on, the program prints a long
   greeting; when it is off, a short one. Do it with `target_compile_definitions`, without editing `main.cpp` between the two runs.
2. **Who sees which header.** There are three things: `core` (INTERFACE, has `core.h`), `render` (a static library, `render.h` includes
   `core.h`) and `game` (links `render`). Write a `CMakeLists.txt` so that `game.cpp` only does `#include <render.h>` and still
   compiles. Show the error if you link `core` into `render` with `PRIVATE`.
3. **Both Debug and Release.** With a program that prints whether `NDEBUG` is defined, build **both types in the same
   build folder** and run each one.

## Answers

**Self-check**

1. Configure reads `CMakeLists.txt` and generates files for Ninja. Build calls Ninja to compile. Configure reruns (automatically) when
   `CMakeLists.txt` changes; build reruns whenever a source file changes.
2. It cannot `#include` the headers of `hidden` (include information is not passed on through `PRIVATE`). But if `hidden` is a static
   library it still links: the link command of `game` has both `librt.a` and `libhidden.a`.
3. Users of your library include `render.h`, `render.h` includes `core.h`, but `game` does not have `core`'s include folder:
   `fatal error: core.h: No such file or directory` right inside `render.h`.
4. From no flags at all (when empty) to `-O3 -DNDEBUG`. Debug is `-g`.
5. The cache keeps the old value of the `SHOW_DEBUG` variable, and `option` only sets the default when the variable is not in the cache yet. Delete `build/`
   and configure again, or pass `-DSHOW_DEBUG=OFF`.

**Exercise 1**

```cmake
cmake_minimum_required(VERSION 3.28)
project(ex1 LANGUAGES CXX)

option(VERBOSE_GREETING "Print the long greeting" OFF)

add_executable(ex1 main.cpp)
if(VERBOSE_GREETING)
  target_compile_definitions(ex1 PRIVATE VERBOSE_GREETING=1)
endif()
```

`main.cpp` uses `#ifdef VERBOSE_GREETING ... #else ... #endif`. The output when run:

```
VERBOSE_GREETING=OFF -> Hello.
VERBOSE_GREETING=ON -> Hello! Very glad to meet you.
```

(Each run uses a new build folder, because the cache keeps the old value.)

**Exercise 2**

```cmake
cmake_minimum_required(VERSION 3.28)
project(ex2 LANGUAGES CXX)

add_library(core INTERFACE)
target_include_directories(core INTERFACE core/include)

add_library(render STATIC render/render.cpp)
target_include_directories(render PUBLIC render/include)
target_link_libraries(render PUBLIC core)      # PUBLIC because render.h includes core.h

add_executable(game game.cpp)
target_link_libraries(game PRIVATE render)
```

Result: `render_value() = 14`. With `target_link_libraries(render PRIVATE core)` the error is:

```
In file included from game.cpp:1:
render/include/render.h:2:10: fatal error: core.h: No such file or directory
```

**Exercise 3**

```
$ cmake -S . -B multi -G "Ninja Multi-Config"
$ cmake --build multi --config Debug
$ cmake --build multi --config Release
$ multi/Debug/types_demo.exe     -> NDEBUG is not set: assert() is active
$ multi/Release/types_demo.exe   -> NDEBUG is set: assert() is compiled out
```

## Next steps

@ref learn_cmake_projects : downloading libraries with `FetchContent`, presets, multiple targets, copying assets, and opening a raylib window.
