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

# Lua 5.4 (MIT) and sol2 (MIT) for njin_script.h, private to njin_rt like Jolt.
# Lua ships no CMake build, so the official release tarball is fetched
# (checksum from lua.org) and compiled below as C++: a Lua error then unwinds
# C++ frames with an exception instead of longjmp-ing over their destructors,
# which is what sol2 asks for (SOL_USING_CXX_LUA). sol2 is header-only; its own
# CMake is skipped (SOURCE_SUBDIR points nowhere).
FetchContent_Declare(
  lua
  URL https://www.lua.org/ftp/lua-5.4.9.tar.gz
  URL_HASH SHA256=2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SYSTEM)
FetchContent_Declare(
  sol2
  GIT_REPOSITORY https://github.com/ThePhD/sol2.git
  GIT_TAG v3.5.0
  SOURCE_SUBDIR no-cmake
  SYSTEM)

# Recast and Detour (zlib) for njin_nav3d.h: navmesh building, path queries
# and crowds. Only the Recast, Detour and DetourCrowd sources are compiled,
# into one library below; their CMake (demo, tests, install rules) is skipped.
FetchContent_Declare(
  recastnavigation
  GIT_REPOSITORY https://github.com/recastnavigation/recastnavigation.git
  GIT_TAG v1.6.0
  SOURCE_SUBDIR no-cmake
  SYSTEM)

# pl_mpeg (MIT, single header) for njin_video.h: MPEG-1 video and MP2 audio.
# The repository has no tags, so it is pinned to a commit.
FetchContent_Declare(
  pl_mpeg
  GIT_REPOSITORY https://github.com/phoboslab/pl_mpeg.git
  GIT_TAG c871f2be022ece7ef4f64230b4fb8e1fb9eb6023
  SOURCE_SUBDIR no-cmake
  SYSTEM)

FetchContent_MakeAvailable(raylib entt JoltPhysics meshoptimizer lua sol2 recastnavigation pl_mpeg)

file(GLOB _njin_recast_sources "${recastnavigation_SOURCE_DIR}/Recast/Source/*.cpp"
     "${recastnavigation_SOURCE_DIR}/Detour/Source/*.cpp" "${recastnavigation_SOURCE_DIR}/DetourCrowd/Source/*.cpp")
add_library(njin_recast STATIC ${_njin_recast_sources})
target_include_directories(njin_recast SYSTEM PUBLIC "${recastnavigation_SOURCE_DIR}/Recast/Include"
                           "${recastnavigation_SOURCE_DIR}/Detour/Include" "${recastnavigation_SOURCE_DIR}/DetourCrowd/Include")
add_library(njin_plmpeg INTERFACE)
target_include_directories(njin_plmpeg SYSTEM INTERFACE "${pl_mpeg_SOURCE_DIR}")

file(GLOB _njin_lua_sources "${lua_SOURCE_DIR}/src/*.c")
list(FILTER _njin_lua_sources EXCLUDE REGEX "/(lua|luac)\\.c$")
set_source_files_properties(${_njin_lua_sources} PROPERTIES LANGUAGE CXX)
add_library(njin_lua STATIC ${_njin_lua_sources})
target_include_directories(njin_lua SYSTEM PUBLIC "${lua_SOURCE_DIR}/src")
add_library(njin_sol2 INTERFACE)
target_include_directories(njin_sol2 SYSTEM INTERFACE "${sol2_SOURCE_DIR}/include")
target_compile_definitions(njin_sol2 INTERFACE SOL_USING_CXX_LUA=1 SOL_ALL_SAFETIES_ON=1)
target_link_libraries(njin_sol2 INTERFACE njin_lua)

# raylib saves a screenshot on F12 by default, silently, in every game. Games
# built on njin may want F12 for something else; njin::screenshot() is the
# explicit replacement.
target_compile_definitions(raylib PRIVATE SUPPORT_SCREEN_CAPTURE=0)

include(cmake/njin.cmake)

add_subdirectory(src/engine/api)
add_subdirectory(src/engine/gpu)
add_subdirectory(src/engine/runtime)
# Export the engine build for standalone games. Private games are never
# discovered or added by this project.
set(_njin_export_targets njin_rt njin_api njin_gpu njin_warnings raylib Jolt meshoptimizer njin_lua njin_sol2 njin_recast njin_plmpeg EnTT)
if(TARGET glfw)
  get_target_property(_njin_glfw_type glfw TYPE)
  if(_njin_glfw_type STREQUAL "OBJECT_LIBRARY")
    # raylib already embeds these objects. Its upstream link interface also
    # mentions glfw inside a list expression, which exports as plain -lglfw.
    get_target_property(_njin_raylib_links raylib INTERFACE_LINK_LIBRARIES)
    string(REPLACE ";glfw" "" _njin_raylib_links "${_njin_raylib_links}")
    set_property(TARGET raylib PROPERTY INTERFACE_LINK_LIBRARIES "${_njin_raylib_links}")
  else()
    list(APPEND _njin_export_targets glfw)
  endif()
endif()
export(TARGETS ${_njin_export_targets}
  NAMESPACE njin_prebuilt:: FILE "${CMAKE_BINARY_DIR}/njinTargets.cmake")

option(NJIN_BUILD_EXAMPLES "Build the small examples shipped with njin" ON)
if(NJIN_BUILD_EXAMPLES)
  foreach(example sandbox pong platformer topdown debug_demo render_demo
                  tower_defense lighting_demo fps sokoban platformer3d)
    add_subdirectory(src/games/${example})
  endforeach()
endif()

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
