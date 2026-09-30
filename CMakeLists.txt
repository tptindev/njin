# raylib 6.0 needs >= 3.25, EnTT v4.0.0 needs >= 3.28.
cmake_minimum_required(VERSION 3.28...4.0)

# The version lives in one place, src/engine/api/njin_version.h; read it here so
# CMake, the log and the inspector never disagree.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/api/njin_version.h" NJIN_VERSION_HEADER)
foreach(part MAJOR MINOR PATCH)
  string(REGEX MATCH "#define NJIN_VERSION_${part} ([0-9]+)" _ "${NJIN_VERSION_HEADER}")
  set(NJIN_VERSION_${part} "${CMAKE_MATCH_1}")
endforeach()
set(NJIN_VERSION "${NJIN_VERSION_MAJOR}.${NJIN_VERSION_MINOR}.${NJIN_VERSION_PATCH}")

project(
  njin_lab
  VERSION ${NJIN_VERSION}
  LANGUAGES C CXX)
message(STATUS "njin ${NJIN_VERSION}")

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

# Jolt Physics (MIT): 3D collision, rigid bodies and a character controller
# for njin_physics3d.h. Its CMake project lives in Build/. Only the library:
# no tests, samples or viewer. Built for the SSE4.2 baseline so a game runs on
# any x86-64 PC from the last 15 years, with RTTI and exceptions left as the
# rest of njin has them. Linked privately by njin_rt: games never see it.
FetchContent_Declare(
  JoltPhysics
  GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
  GIT_TAG v5.6.0
  SOURCE_SUBDIR Build
  SYSTEM)
foreach(opt TARGET_UNIT_TESTS TARGET_HELLO_WORLD TARGET_PERFORMANCE_TEST TARGET_SAMPLES TARGET_VIEWER
            ENABLE_ALL_WARNINGS INTERPROCEDURAL_OPTIMIZATION USE_STATIC_MSVC_RUNTIME_LIBRARY
            DEBUG_RENDERER_IN_DEBUG_AND_RELEASE PROFILER_IN_DEBUG_AND_RELEASE FLOATING_POINT_EXCEPTIONS_ENABLED
            USE_AVX USE_AVX2 USE_AVX512 USE_LZCNT USE_TZCNT USE_F16C USE_FMADD GENERATE_DEBUG_SYMBOLS)
  set(${opt} OFF CACHE BOOL "" FORCE)
endforeach()
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)

# meshoptimizer (MIT): simplifies meshes into the levels of detail of
# model_lod_build(). Linked privately by njin_rt, like Jolt.
FetchContent_Declare(
  meshoptimizer
  GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
  GIT_TAG v1.3
  SYSTEM)

FetchContent_MakeAvailable(raylib entt JoltPhysics meshoptimizer)

# raylib saves a screenshot on F12 by default, silently, in every game. Games
# built on njin may want F12 for something else; njin::screenshot() is the
# explicit replacement.
target_compile_definitions(raylib PRIVATE SUPPORT_SCREEN_CAPTURE=0)

include(cmake/njin.cmake)

add_subdirectory(src/engine/api)
add_subdirectory(src/engine/gpu)
add_subdirectory(src/engine/runtime)
add_subdirectory(src/games/sandbox)
add_subdirectory(src/games/pong)
add_subdirectory(src/games/platformer)
add_subdirectory(src/games/topdown)
add_subdirectory(src/games/debug_demo)
add_subdirectory(src/games/render_demo)
add_subdirectory(src/games/tower_defense)
add_subdirectory(src/games/crowd)
add_subdirectory(src/games/lighting_demo)
add_subdirectory(src/games/sandtable)
add_subdirectory(src/games/fps)
add_subdirectory(src/games/sokoban)
add_subdirectory(src/games/platformer3d)

# Debug inspector: a separate window that connects to a running game (see
# njin_debug.h). Downloads Dear ImGui and rlImGui on first configure.
option(NJIN_BUILD_INSPECTOR "Build the njin_inspector debug tool" ON)
if(NJIN_BUILD_INSPECTOR)
  add_subdirectory(src/tools/inspector)
endif()

# UI Editor: visual editor tool for designing UI layouts (.ui.json) and exporting C++ code.
option(NJIN_BUILD_UI_EDITOR "Build the njin_ui_editor tool" ON)
if(NJIN_BUILD_UI_EDITOR)
  add_subdirectory(src/tools/ui_editor)
endif()

option(NJIN_BUILD_MODEL_EDITOR "Build the SDF model and skeleton editor" ON)
if(NJIN_BUILD_MODEL_EDITOR)
  add_subdirectory(src/tools/model_editor)
endif()
