# raylib 6.0 needs >= 3.25, EnTT v4.0.0 needs >= 3.28.
cmake_minimum_required(VERSION 3.28...4.0)

project(
  njin_lab
  VERSION 0.0.1
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Keep every binary we produce in one predictable place.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Warning flags, carried by an interface target so engine and games stay in
# sync. Never linked into fetched dependencies.
add_library(njin_warnings INTERFACE)
if(MSVC)
  target_compile_options(njin_warnings INTERFACE /W4)
else()
  target_compile_options(njin_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

include(FetchContent)

set(BUILD_EXAMPLES OFF)

# SYSTEM keeps third-party headers from tripping our own warning flags.
FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 6.0
  SYSTEM)

FetchContent_Declare(
  entt
  GIT_REPOSITORY https://github.com/skypjack/entt.git
  GIT_TAG v4.0.0
  SYSTEM)

FetchContent_MakeAvailable(raylib entt)

# raylib saves a screenshot on F12 by default, silently, in every game. Games
# built on njin may want F12 for something else; njin::screenshot() is the
# explicit replacement.
target_compile_definitions(raylib PRIVATE SUPPORT_SCREEN_CAPTURE=0)

include(cmake/njin.cmake)

add_subdirectory(src/engine/api)
add_subdirectory(src/engine/runtime)
add_subdirectory(src/games/sandbox)
add_subdirectory(src/games/pong)

# Debug inspector: a separate window that connects to a running game (see
# njin_debug.h). Downloads Dear ImGui and rlImGui on first configure.
option(NJIN_BUILD_INSPECTOR "Build the njin_inspector debug tool" ON)
if(NJIN_BUILD_INSPECTOR)
  add_subdirectory(src/tools/inspector)
endif()
