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

# MSVC: debug info inside each object (/Z7) instead of one PDB per target
# (/Zi), which every parallel compile of the target writes through one
# mspdbsrv process. The linker still makes the .pdb. Before the dependencies
# are fetched, so they get it too.
if(NOT DEFINED CMAKE_MSVC_DEBUG_INFORMATION_FORMAT)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
endif()
# MSVC Debug links write a PDB that points into the objects instead of copying
# their debug info (/DEBUG:FASTLINK): with /Z7 every link would merge the
# STL and EnTT types of each object again (1.45 s -> 0.62 s CPU per game
# link). The debugger reads the objects in the build folder; RelWithDebInfo
# keeps a full PDB, which a crash report from another machine needs.
if(MSVC)
  add_link_options($<$<CONFIG:Debug>:/DEBUG:FASTLINK>)
endif()

# Compile through ccache when it is installed: a source compiled before with
# the same flags and headers (after rm -rf build, back on a branch) comes from
# its cache instead of the compiler. 4.10 or newer caches MSVC's precompiled
# headers, given the two sloppiness options; its config is passed on its
# command line so the user's ccache.conf is left alone. sccache cannot be
# used: it does not cache a source that uses a precompiled header.
option(NJIN_CCACHE "Compile through ccache when it is found" ON)
if(NJIN_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
  find_program(NJIN_CCACHE_PROGRAM ccache)
  if(NJIN_CCACHE_PROGRAM)
    execute_process(COMMAND "${NJIN_CCACHE_PROGRAM}" --version OUTPUT_VARIABLE _njin_ccache_version)
    string(REGEX MATCH "version ([0-9]+\\.[0-9]+)" _ "${_njin_ccache_version}")
    if(CMAKE_MATCH_1 VERSION_GREATER_EQUAL 4.10)
      set(_njin_launcher "${NJIN_CCACHE_PROGRAM}" sloppiness=pch_defines,time_macros depend_mode=true)
      set(CMAKE_C_COMPILER_LAUNCHER ${_njin_launcher})
      set(CMAKE_CXX_COMPILER_LAUNCHER ${_njin_launcher})
      message(STATUS "njin: compiling through ccache ${CMAKE_MATCH_1}")
    else()
      message(STATUS "njin: ccache ${CMAKE_MATCH_1} is older than 4.10, not used")
    endif()
  endif()
endif()

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

option(NJIN_BGFX "Build SDL3 platform and bgfx renderer" ON)
option(NJIN_SDL_GPU "Deprecated SDL GPU prototype (replaced by NJIN_BGFX)" OFF)
if(NJIN_SDL_GPU)
  message(FATAL_ERROR "SDL GPU rendering was retired. Set NJIN_SDL_GPU=OFF and NJIN_BGFX=ON.")
endif()
if(NJIN_BGFX)
  if(ANDROID)
    # SDLActivity and the native renderer must use the same SDL/JNI state.
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
  endif()
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  # SDL provides platform services; bgfx renders. SDL_Render's D3D math symbols collide with raymath
  # when both libraries are static on MinGW.
  set(SDL_RENDER OFF CACHE BOOL "" FORCE)
  set(SDL_GPU OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.18)
  FetchContent_MakeAvailable(SDL3)
  include(cmake/njin_bgfx.cmake)
  include(cmake/njin_rmlui.cmake)
endif()

set(BUILD_EXAMPLES OFF)

# SYSTEM keeps third-party headers from tripping our own warning flags.
FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 6.0
  SYSTEM)

# miniaudio (public domain or MIT-0): the sound device, decoders (wav, mp3, flac and,
# through its stb_vorbis, ogg) and resampling, as raudio used it. Header only: compiled
# once in src/engine/runtime/njin_audio_backend.cpp.
FetchContent_Declare(
  miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG 0.11.24
  # Sources only: its own CMake project would compile a second implementation.
  SOURCE_SUBDIR njin-sources-only
  SYSTEM)
FetchContent_MakeAvailable(miniaudio)

FetchContent_Declare(
  entt
  GIT_REPOSITORY https://github.com/skypjack/entt.git
  GIT_TAG v4.0.0
  SYSTEM)

# Jolt Physics (MIT): 3D collision, rigid bodies and a character controller
# for njin_physics3d.h. Its CMake project lives in Build/. Only the library:
# no tests, samples or viewer. Built for the SSE4.2 baseline so a game runs on
# any x86-64 PC from the last 15 years, with RTTI and exceptions left as the
# rest of njin has them. Linked privately by the physics3d feature
# (njin_physics3d): games never see it.
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
# Njin uses Jolt's CPU rigid-body API, not its optional hair/GPU compute.
# Those extensions otherwise discover graphics SDKs and compile unrelated HLSL
# even for a legacy-only engine build. Keep them available as an explicit opt-in.
option(NJIN_JOLT_GPU_COMPUTE "Build optional Jolt GPU compute extensions (not integrated by Njin)" OFF)
if(NOT NJIN_JOLT_GPU_COMPUTE)
  foreach(opt JPH_USE_DX12 JPH_USE_VK JPH_USE_MTL)
    set(${opt} OFF CACHE BOOL "" FORCE)
  endforeach()
endif()
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)

# meshoptimizer (MIT): simplifies meshes into the levels of detail of
# model_lod_build(). Linked privately by the 3d feature (njin_3d), like Jolt.
FetchContent_Declare(
  meshoptimizer
  GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
  GIT_TAG v1.3
  SYSTEM)

# tinybvh (MIT): CPU-built BVHs in the layout its GPU traversal reads, for the
# bgfx compute ray tracing (light3d::ray_traced_shadows). Header-only; its own
# CMake (demos) is skipped. Linked privately by the 3d feature.
FetchContent_Declare(
  tinybvh
  GIT_REPOSITORY https://github.com/jbikker/tinybvh.git
  GIT_TAG c9be3103cc9693b456b0a0353927c11cac7d710f
  SOURCE_SUBDIR njin-no-cmake
  SYSTEM)

FetchContent_Declare(
  xatlas
  GIT_REPOSITORY https://github.com/jpcy/xatlas.git
  GIT_TAG f700c7790aaa030e794b52ba7791a05c085faf0c
  SOURCE_SUBDIR njin-no-cmake
  SYSTEM)

# Lua 5.4 (MIT) and sol2 (MIT) for njin_script.h, private to the script feature
# (njin_script) like Jolt.
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

# SMAA (MIT) for post3d::aa: its shader (SMAA.hlsl, GLSL-compatible) is
# compiled into the 3d feature and its two lookup images (Textures/*.h) are
# included there. The repository has no tags, so it is pinned to a commit.
FetchContent_Declare(
  smaa
  GIT_REPOSITORY https://github.com/iryoku/smaa.git
  GIT_TAG 71c806a838bdd7d517df19192a20f0c61b3ca29d
  SOURCE_SUBDIR no-cmake
  SYSTEM)

# Asio (Boost Software License 1.0, standalone, header-only) for njin_network.h:
# TCP and UDP on one network thread, written as C++20 coroutines. Private to
# the net feature (njin_net); its CMake-less repository is used for its headers
# only.
FetchContent_Declare(
  asio
  URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-38-2.tar.gz
  URL_HASH SHA256=9f2648fa483e58a6bf848d970ee0ea650ca19ed7769dfa520ed4f7b8d27af1db
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR no-cmake
  SYSTEM)

# ENet (MIT) for njin_multiplayer.h: reliable and unreliable channels over UDP
# for realtime games. Its C sources are compiled into njin_enet below; its CMake
# (install rules, global definitions) is skipped. Private to the net feature.
FetchContent_Declare(
  enet
  URL https://github.com/lsalzman/enet/archive/refs/tags/v1.3.18.tar.gz
  URL_HASH SHA256=28603c895f9ed24a846478180ee72c7376b39b4bb1287b73877e5eae7d96b0dd
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR no-cmake
  SYSTEM)

FetchContent_MakeAvailable(raylib entt JoltPhysics meshoptimizer tinybvh xatlas lua sol2 recastnavigation pl_mpeg smaa asio enet)
# Njin's sound is its own (njin_audio_backend.cpp on miniaudio): raudio.c, which would
# define miniaudio's symbols a second time, is left out of raylib. Only its startup log
# line mentions it. (raylib's CUSTOMIZE_BUILD route is not used: its config.h parser
# turns every `#define SUPPORT_X 0` on, frame pacing control among them.)
get_target_property(_njin_raylib_sources raylib SOURCES)
list(FILTER _njin_raylib_sources EXCLUDE REGEX "raudio\.c$")
set_property(TARGET raylib PROPERTY SOURCES ${_njin_raylib_sources})
add_library(njin_xatlas STATIC EXCLUDE_FROM_ALL "${xatlas_SOURCE_DIR}/source/xatlas/xatlas.cpp")
target_include_directories(njin_xatlas SYSTEM PUBLIC "${xatlas_SOURCE_DIR}/source/xatlas")
set_target_properties(njin_xatlas PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Ray tracing (light3d::ray_traced_shadows) beside the OpenGL renderer: a
# Vulkan device with ray queries through NVRHI (NVIDIA, MIT), sharing images and
# semaphores with OpenGL (render3d_rt_vk.cpp). Off by default; Windows only for
# now. Its compute shaders are compiled at build time by glslc from the Vulkan
# SDK (the SDK's compiler, not linked: glslang's licence has a GPL part).
option(NJIN_RAYTRACING "Ray traced shadows on Vulkan (NVRHI) beside the OpenGL renderer" OFF)
if(NJIN_RAYTRACING)
  if(NOT WIN32)
    message(FATAL_ERROR "njin: NJIN_RAYTRACING is Windows only for now")
  endif()
  file(GLOB _njin_vulkan_sdks LIST_DIRECTORIES true "C:/VulkanSDK/*")
  list(SORT _njin_vulkan_sdks COMPARE NATURAL ORDER DESCENDING)
  set(_njin_glslc_hints "$ENV{VULKAN_SDK}/Bin")
  foreach(_njin_sdk IN LISTS _njin_vulkan_sdks)
    list(APPEND _njin_glslc_hints "${_njin_sdk}/Bin")
  endforeach()
  find_program(NJIN_GLSLC glslc HINTS ${_njin_glslc_hints})
  if(NOT NJIN_GLSLC)
    message(FATAL_ERROR "njin: NJIN_RAYTRACING needs glslc: install the Vulkan SDK (https://vulkan.lunarg.com) "
                        "or set NJIN_GLSLC to its path")
  endif()
  set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
  set(NVRHI_WITH_DX12 OFF CACHE BOOL "" FORCE)
  set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
  set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
  set(NVRHI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    nvrhi
    GIT_REPOSITORY https://github.com/NVIDIA-RTX/NVRHI.git
    GIT_TAG 2e21b39c1eddd9661cf6728b165c132412708869
    SYSTEM)
  FetchContent_MakeAvailable(nvrhi)
  # NVRHI links the Vulkan headers it fetched privately; render3d_rt_vk.cpp
  # needs them too, and the same NVRHI_WITH_AFTERMATH it was built with.
  add_library(njin_nvrhi INTERFACE)
  target_link_libraries(njin_nvrhi INTERFACE nvrhi_vk nvrhi Vulkan::Headers)
  target_compile_definitions(njin_nvrhi INTERFACE NVRHI_WITH_AFTERMATH=0)
endif()
# Jolt is 154 sources sharing most of their headers: compiled 16 to a file
# (unity build) it takes half the time. It is only rebuilt with a new tag.
set_target_properties(Jolt PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 16)

file(GLOB _njin_recast_sources "${recastnavigation_SOURCE_DIR}/Recast/Source/*.cpp"
     "${recastnavigation_SOURCE_DIR}/Detour/Source/*.cpp" "${recastnavigation_SOURCE_DIR}/DetourCrowd/Source/*.cpp")
add_library(njin_recast STATIC ${_njin_recast_sources})
target_include_directories(njin_recast SYSTEM PUBLIC "${recastnavigation_SOURCE_DIR}/Recast/Include"
                           "${recastnavigation_SOURCE_DIR}/Detour/Include" "${recastnavigation_SOURCE_DIR}/DetourCrowd/Include")
add_library(njin_smaa INTERFACE)
target_include_directories(njin_smaa SYSTEM INTERFACE "${smaa_SOURCE_DIR}/Textures")
add_library(njin_plmpeg INTERFACE)
target_include_directories(njin_plmpeg SYSTEM INTERFACE "${pl_mpeg_SOURCE_DIR}")
find_package(Threads REQUIRED)
add_library(njin_asio INTERFACE)
target_include_directories(njin_asio SYSTEM INTERFACE "${asio_SOURCE_DIR}/include")
target_compile_definitions(njin_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
target_link_libraries(njin_asio INTERFACE Threads::Threads)
if(WIN32)
  target_link_libraries(njin_asio INTERFACE ws2_32 mswsock)
endif()

set(_njin_enet_dir "${enet_SOURCE_DIR}")
add_library(njin_enet STATIC
  "${_njin_enet_dir}/callbacks.c" "${_njin_enet_dir}/compress.c" "${_njin_enet_dir}/host.c"
  "${_njin_enet_dir}/list.c" "${_njin_enet_dir}/packet.c" "${_njin_enet_dir}/peer.c"
  "${_njin_enet_dir}/protocol.c" "${_njin_enet_dir}/unix.c" "${_njin_enet_dir}/win32.c")
target_include_directories(njin_enet SYSTEM PUBLIC "${_njin_enet_dir}/include")
if(WIN32)
  target_compile_definitions(njin_enet PRIVATE _WINSOCK_DEPRECATED_NO_WARNINGS _CRT_SECURE_NO_WARNINGS)
  if(MSVC)
    target_compile_options(njin_enet PRIVATE /wd5287) # ENet mixes its enum types on purpose
  endif()
  target_link_libraries(njin_enet PUBLIC ws2_32 winmm)
else()
  # What ENet's own configure step would find on a POSIX system.
  target_compile_definitions(njin_enet PRIVATE HAS_FCNTL=1 HAS_POLL=1 HAS_GETADDRINFO=1 HAS_GETNAMEINFO=1
                             HAS_INET_PTON=1 HAS_INET_NTOP=1 HAS_MSGHDR_FLAGS=1 HAS_SOCKLEN_T=1)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(njin_enet PRIVATE HAS_GETHOSTBYNAME_R=1 HAS_GETHOSTBYADDR_R=1)
  endif()
endif()

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
# JPEG images, off in raylib by default: glTF models carry them (external
# or embedded), and a texture_load() of a .jpg is as common as a .png.
target_compile_definitions(raylib PRIVATE SUPPORT_FILEFORMAT_JPG=1)
# Radiance .hdr images, as float textures: light3d::environment's usual format.
target_compile_definitions(raylib PRIVATE SUPPORT_FILEFORMAT_HDR=1)
# Memory manager: raylib's allocations go through njin's counting allocator
# (src/engine/mem), which also replaces C++ new/delete. It is built into the
# raylib target, so every program that links raylib has it.
target_sources(raylib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/mem/njin_memcore.cpp")
target_compile_features(raylib PRIVATE cxx_std_17)
# Object-like macros: raylib writes RL_MALLOC(sz), which then reads njin_rl_malloc(sz).
target_compile_definitions(raylib PRIVATE
  RL_MALLOC=njin_rl_malloc RL_CALLOC=njin_rl_calloc RL_REALLOC=njin_rl_realloc RL_FREE=njin_rl_free)
if(MSVC)
  target_compile_options(raylib PRIVATE "$<$<COMPILE_LANGUAGE:C>:/FI${CMAKE_CURRENT_SOURCE_DIR}/src/engine/mem/njin_memcore.h>")
else()
  target_compile_options(raylib PRIVATE "$<$<COMPILE_LANGUAGE:C>:SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/mem/njin_memcore.h>")
endif()
target_include_directories(raylib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/mem")

include(cmake/njin.cmake)
# Jolt has its own PCH, outside njin_target_pch(). Keep NJIN_PCH=OFF
# reliable on GCC/MinGW too: its fixed-address PCH mapping can fail even
# when every Njin target already has precompiled headers disabled.
if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT NJIN_PCH)
  set_target_properties(Jolt PROPERTIES DISABLE_PRECOMPILE_HEADERS ON)
endif()


add_subdirectory(src/engine/api)
add_subdirectory(src/engine/gpu)
add_subdirectory(src/engine/runtime)
# A buildable aggregate for standalone consumers. njin_rt is an INTERFACE
# library, so it has no build rule of its own.
add_custom_target(njin_engine DEPENDS njin_core)
foreach(_njin_feature IN LISTS NJIN_FEATURES_ORDER)
  add_dependencies(njin_engine njin_${_njin_feature})
endforeach()
# Export the engine build for standalone games. Private games are never
# discovered or added by this project.
set(_njin_export_targets njin_rt njin_core njin_api njin_gpu njin_warnings raylib Jolt meshoptimizer njin_lua njin_sol2 njin_recast njin_plmpeg njin_smaa njin_asio njin_enet EnTT)
list(APPEND _njin_export_targets njin_xatlas)
if(NJIN_BGFX)
  list(APPEND _njin_export_targets bgfx bx bimg bimg_decode njin_rmlui_lib freetype)
  # SDL exports its headers/static targets under its own namespace. Import
  # that package before njinTargets when consuming this prebuilt engine.
  file(WRITE "${CMAKE_BINARY_DIR}/njinDependencies.cmake"
    "find_package(SDL3 CONFIG REQUIRED PATHS \"${SDL3_BINARY_DIR}\" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)\n")
else()
  file(WRITE "${CMAKE_BINARY_DIR}/njinDependencies.cmake" "")
endif()
foreach(_njin_feature IN LISTS NJIN_FEATURES_ORDER)
  list(APPEND _njin_export_targets njin_${_njin_feature})
endforeach()
if(NJIN_RAYTRACING)
  list(APPEND _njin_export_targets njin_nvrhi nvrhi nvrhi_vk Vulkan-Headers)
endif()
if(TARGET glfw)
  get_target_property(_njin_glfw_type glfw TYPE)
  if(_njin_glfw_type STREQUAL "OBJECT_LIBRARY")
    # raylib already embeds these objects. Its upstream link interface also
    # mentions glfw inside a list expression, which exports as plain -lglfw.
    get_target_property(_njin_raylib_links raylib INTERFACE_LINK_LIBRARIES)
    string(REPLACE ";glfw" "" _njin_raylib_links "${_njin_raylib_links}")
    # Dropping glfw also drops what glfw links privately; on macOS that is
    # Cocoa, IOKit and CoreFoundation, which its objects inside raylib need.
    get_target_property(_njin_glfw_links glfw LINK_LIBRARIES)
    list(FILTER _njin_glfw_links INCLUDE REGEX "^-framework ")
    foreach(_njin_framework IN LISTS _njin_glfw_links)
      list(APPEND _njin_raylib_links "$<LINK_ONLY:${_njin_framework}>")
    endforeach()
    set_property(TARGET raylib PROPERTY INTERFACE_LINK_LIBRARIES "${_njin_raylib_links}")
  else()
    list(APPEND _njin_export_targets glfw)
  endif()
endif()
export(TARGETS ${_njin_export_targets}
  NAMESPACE njin_prebuilt:: FILE "${CMAKE_BINARY_DIR}/njinTargets.cmake")

option(NJIN_BUILD_EXAMPLES "Build the small examples shipped with njin" ON)
option(NJIN_BUILD_RHI_SLICE "Build the common-renderer integration smoke scene" OFF)
if(NJIN_BUILD_RHI_SLICE)
  add_executable(njin_rhi_slice examples/rhi_slice/main.cpp)
  target_link_libraries(njin_rhi_slice PRIVATE njin::rt njin::gpu)
  enable_testing()
  add_executable(njin_model_test src/engine/tests/models3d.cpp)
  add_executable(njin_streaming_test src/engine/tests/streaming.cpp)
  add_executable(njin_residency_test src/engine/tests/residency.cpp)
  target_link_libraries(njin_residency_test PRIVATE njin::rt njin::gpu)
  target_link_libraries(njin_streaming_test PRIVATE njin::rt njin::gpu)
  # One copy per build invocation: concurrent POST_BUILD copies raced on MinGW.
  add_custom_target(njin_model_test_assets
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/src/engine/tests/assets"
      "$<TARGET_FILE_DIR:njin_model_test>/model-test-assets" VERBATIM)

  target_link_libraries(njin_model_test PRIVATE njin::rt njin::gpu)
  file(GLOB _njin_model_test_assets CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/engine/tests/assets/*")
  set_property(TARGET njin_model_test APPEND PROPERTY LINK_DEPENDS "${_njin_model_test_assets}")
  add_dependencies(njin_model_test njin_model_test_assets)
  add_dependencies(njin_streaming_test njin_model_test_assets)
  add_dependencies(njin_residency_test njin_model_test_assets)
  add_executable(njin_animation_asset_test src/engine/tests/animation_asset.cpp)
  target_link_libraries(njin_animation_asset_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_ssao_test src/engine/tests/ssao3d.cpp)
  target_link_libraries(njin_ssao_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_ssr_test src/engine/tests/ssr3d.cpp)
  target_link_libraries(njin_ssr_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_effects_test src/engine/tests/effects3d.cpp)
  target_link_libraries(njin_effects_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_animation_test src/engine/tests/animation3d.cpp)
  target_link_libraries(njin_animation_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_post_test src/engine/tests/post3d_portable.cpp)
  target_link_libraries(njin_post_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_smaa_test src/engine/tests/smaa3d.cpp)
  target_link_libraries(njin_smaa_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_exposure_test src/engine/tests/exposure3d.cpp)
  target_link_libraries(njin_exposure_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_mipmaps_test src/engine/tests/mipmaps.cpp)
  target_link_libraries(njin_mipmaps_test PRIVATE njin::rt njin::gpu)
  add_dependencies(njin_mipmaps_test njin_model_test_assets)
  find_package(Python3 3.9 COMPONENTS Interpreter QUIET)
  add_executable(njin_inspector_test src/engine/tests/inspector.cpp)
  target_link_libraries(njin_inspector_test PRIVATE njin::rt njin::gpu)
  if(NJIN_BGFX)
    include("${PROJECT_SOURCE_DIR}/cmake/njin_shader_assets.cmake")
    add_executable(njin_custom_shader_test src/engine/tests/custom_shader.cpp)
    target_link_libraries(njin_custom_shader_test PRIVATE njin::rt njin::gpu)
    njin_shader_asset(njin_custom_shader_test NAME custom FRAGMENT "${PROJECT_SOURCE_DIR}/src/engine/tests/assets/fs_custom.sc"
      METADATA "${PROJECT_SOURCE_DIR}/src/engine/tests/assets/custom-shader.json" OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/shader-test-assets")
    # draw_instanced (2D) on bgfx: a package with its own vertex stage.
    add_executable(njin_instanced2d_test src/engine/tests/instanced2d.cpp)
    target_link_libraries(njin_instanced2d_test PRIVATE njin::rt njin::gpu)
    njin_shader_asset(njin_instanced2d_test NAME instanced2d
      FRAGMENT "${PROJECT_SOURCE_DIR}/src/engine/tests/shaders/fs_instanced2d.sc"
      VERTEX "${PROJECT_SOURCE_DIR}/src/engine/tests/shaders/vs_instanced2d.sc"
      METADATA "${PROJECT_SOURCE_DIR}/src/engine/tests/shaders/instanced2d.json"
      OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/shader-test-assets")
    set(_njin_inst2d_renderers vulkan bgfx-opengl)
    if(WIN32)
      list(APPEND _njin_inst2d_renderers d3d12)
    endif()
    add_executable(njin_hdr_keep2d_test src/engine/tests/hdr_keep2d.cpp)
    target_link_libraries(njin_hdr_keep2d_test PRIVATE njin::rt njin::gpu)
    foreach(_renderer IN LISTS _njin_inst2d_renderers)
      add_test(NAME hdr_keep2d_${_renderer} COMMAND njin_hdr_keep2d_test ${_renderer})
      set_tests_properties(hdr_keep2d_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    endforeach()
    foreach(_renderer IN LISTS _njin_inst2d_renderers)
      add_test(NAME instanced2d_${_renderer} COMMAND njin_instanced2d_test
        "${PROJECT_BINARY_DIR}/bin/shader-test-assets/instanced2d.njshader" ${_renderer})
      set_tests_properties(instanced2d_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    endforeach()
  endif()
  add_executable(njin_sprite_fx_test src/engine/tests/sprite_fx.cpp)
  target_link_libraries(njin_sprite_fx_test PRIVATE njin::rt njin::gpu)
  add_dependencies(njin_sprite_fx_test njin_model_test_assets)
  add_executable(njin_clustered_test src/engine/tests/clustered_lighting.cpp)
  set_target_properties(njin_clustered_test PROPERTIES NJIN_ENGINE_INTERNAL ON)
  target_link_libraries(njin_clustered_test PRIVATE njin::rt njin::gpu raylib)
  add_executable(njin_gpu_particles_test src/engine/tests/gpu_particles_portable.cpp)
  set_target_properties(njin_gpu_particles_test PROPERTIES NJIN_ENGINE_INTERNAL ON)
  target_link_libraries(njin_gpu_particles_test PRIVATE njin::rt njin::gpu raylib)
  add_executable(njin_light2d_test src/engine/tests/light2d_portable.cpp)
  target_link_libraries(njin_light2d_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_backdrop_test src/engine/tests/backdrop.cpp)
  target_link_libraries(njin_backdrop_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_raytrace_test src/engine/tests/raytrace3d.cpp)
  add_executable(njin_ocean_test src/engine/tests/ocean3d.cpp)
  # Image files without raylib (njin_image_io) give raylib's pixels.
  add_executable(njin_image_io_test src/engine/tests/image_io.cpp)
  set_target_properties(njin_image_io_test PROPERTIES NJIN_ENGINE_INTERNAL ON)
  target_link_libraries(njin_image_io_test PRIVATE njin::rt raylib)
  target_include_directories(njin_image_io_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/runtime")
  add_test(NAME image_io COMMAND njin_image_io_test "${PROJECT_SOURCE_DIR}/examples" "${PROJECT_SOURCE_DIR}/src/engine/tests")
  # Glyph atlases baked without raylib (font_bake_atlas) match raylib's.
  add_executable(njin_font_bake_test src/engine/tests/font_bake.cpp)
  set_target_properties(njin_font_bake_test PROPERTIES NJIN_ENGINE_INTERNAL ON)
  target_link_libraries(njin_font_bake_test PRIVATE njin::rt raylib)
  target_include_directories(njin_font_bake_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/runtime")
  add_test(NAME font_bake COMMAND njin_font_bake_test "${PROJECT_SOURCE_DIR}/examples" "${PROJECT_SOURCE_DIR}/src/engine/tests")
  # Njin's sound output (miniaudio, no raylib), mixed offline.
  add_executable(njin_audio_backend_test src/engine/tests/audio_backend.cpp)
  set_target_properties(njin_audio_backend_test PROPERTIES NJIN_ENGINE_INTERNAL ON)
  target_link_libraries(njin_audio_backend_test PRIVATE njin::rt)
  target_include_directories(njin_audio_backend_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/runtime")
  add_test(NAME audio_backend COMMAND njin_audio_backend_test "${PROJECT_SOURCE_DIR}/examples")
  # Njin's own raylib types and raymath (njin_rl_own.inl) match raylib bit for bit.
  add_executable(njin_rl_own_test src/engine/tests/rl_own.cpp)
  set_target_properties(njin_rl_own_test PROPERTIES NJIN_ENGINE_INTERNAL ON)
  target_link_libraries(njin_rl_own_test PRIVATE raylib)
  target_include_directories(njin_rl_own_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/runtime")
  add_test(NAME rl_own COMMAND njin_rl_own_test)
  # Tiled's zlib/gzip layers inflated with miniz match raylib's DecompressData.
  add_executable(njin_level_inflate_test src/engine/tests/level_inflate.cpp)
  set_target_properties(njin_level_inflate_test PROPERTIES NJIN_ENGINE_INTERNAL ON)
  get_property(_njin_bimg_src GLOBAL PROPERTY NJIN_BIMG_SOURCE_DIR)
  target_link_libraries(njin_level_inflate_test PRIVATE njin::rt raylib bimg_decode)
  target_include_directories(njin_level_inflate_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/runtime"
    "${_njin_bimg_src}/3rdparty/tinyexr/deps/miniz")
  add_test(NAME level_inflate COMMAND njin_level_inflate_test)
  # The rmlui feature: a document with a data model drawn by Njin, clicks and data-if.
  # Engine side only to push SDL events as the window would send them.
  if(NJIN_BGFX)
    add_executable(njin_rml_test src/engine/tests/rml.cpp)
    set_target_properties(njin_rml_test PROPERTIES NJIN_ENGINE_INTERNAL ON)
    target_link_libraries(njin_rml_test PRIVATE njin::rt njin::gpu SDL3::SDL3)
    set(_njin_rml_renderers vulkan bgfx-opengl)
    if(WIN32)
      list(APPEND _njin_rml_renderers d3d12)
    endif()
    foreach(_renderer IN LISTS _njin_rml_renderers)
      add_test(NAME rml_${_renderer} COMMAND njin_rml_test
        "${PROJECT_SOURCE_DIR}/examples/topdown/assets/fonts/BeVietnamPro-Bold.ttf" ${_renderer})
      set_tests_properties(rml_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    endforeach()
  endif()
  add_executable(njin_transparent_test src/engine/tests/transparent3d.cpp)
  add_executable(njin_primitives_test src/engine/tests/primitives3d.cpp)
  target_link_libraries(njin_primitives_test PRIVATE njin::rt njin::gpu)
  target_link_libraries(njin_transparent_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_terrain_relief_test src/engine/tests/terrain_relief.cpp)
  target_link_libraries(njin_terrain_relief_test PRIVATE njin::rt njin::gpu)
  target_link_libraries(njin_ocean_test PRIVATE njin::rt njin::gpu)
  target_link_libraries(njin_raytrace_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_advanced_material_test src/engine/tests/advanced_materials.cpp)
  add_executable(njin_lightmap src/tools/lightmap/main.cpp)
  target_link_libraries(njin_lightmap PRIVATE njin::rt njin::gpu)
  add_executable(njin_lightmap_test src/engine/tests/lightmap.cpp)
  target_link_libraries(njin_lightmap_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_lightmap_model_test src/engine/tests/lightmap_model.cpp)
  target_link_libraries(njin_lightmap_model_test PRIVATE njin::rt njin::gpu)
  add_dependencies(njin_lightmap_model_test njin_model_test_assets)
  add_test(NAME lightmap_cpu COMMAND njin_lightmap_test --cpu-only)
  if(NJIN_BGFX AND WIN32)
    add_test(NAME lightmap_legacy_refusal COMMAND njin_lightmap_test --legacy-refusal)
    set_tests_properties(lightmap_legacy_refusal PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME lightmap_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_lightmap_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/lightmap-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(lightmap_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME lightmap_model_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_lightmap_model_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/lightmap-model-vulkan-validation.log" -DSMOKE_TIMEOUT=225
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(lightmap_model_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 240)
  endif()
  target_link_libraries(njin_advanced_material_test PRIVATE njin::rt njin::gpu)
  add_dependencies(njin_advanced_material_test njin_model_test_assets)
  add_executable(njin_portable_world_test src/engine/tests/world3d_portable.cpp)
  set_target_properties(njin_portable_world_test PROPERTIES NJIN_ENGINE_INTERNAL ON)
  target_link_libraries(njin_portable_world_test PRIVATE njin::rt njin::gpu raylib)
  add_dependencies(njin_portable_world_test njin_model_test_assets)
  add_executable(njin_renderer_batch_test src/engine/tests/renderer_batch.cpp)
  target_link_libraries(njin_renderer_batch_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_world_fx_test src/engine/tests/world_fx.cpp)
  target_link_libraries(njin_world_fx_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_crisp_text_test src/engine/tests/crisp_text.cpp)
  set_target_properties(njin_crisp_text_test PROPERTIES NJIN_ENGINE_INTERNAL ON)
  target_link_libraries(njin_crisp_text_test PRIVATE njin::rt njin::gpu raylib)
  add_executable(njin_gizmo_test src/engine/tests/gizmos.cpp)
  target_link_libraries(njin_gizmo_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_lod_test src/engine/tests/lod.cpp)
  set_target_properties(njin_lod_test PROPERTIES NJIN_ENGINE_INTERNAL ON)
  target_compile_definitions(njin_lod_test PRIVATE NJIN_ENGINE_INTERNAL)
  target_link_libraries(njin_lod_test PRIVATE njin::rt njin::gpu raylib)
  add_dependencies(njin_lod_test njin_model_test_assets)
  add_executable(njin_dynamic_mesh_test src/engine/tests/dynamic_mesh.cpp)
  # Engine-internal fixture exercises split/update helpers used by terrain.
  set_target_properties(njin_dynamic_mesh_test PROPERTIES NJIN_ENGINE_INTERNAL ON)
  target_compile_definitions(njin_dynamic_mesh_test PRIVATE NJIN_ENGINE_INTERNAL)
  target_link_libraries(njin_dynamic_mesh_test PRIVATE njin::rt njin::gpu raylib)
  add_executable(njin_video_texture_test src/engine/tests/video_textures.cpp)
  target_link_libraries(njin_video_texture_test PRIVATE njin::rt njin::gpu)
  add_dependencies(njin_video_texture_test njin_model_test_assets)
  add_executable(njin_virtual_screen_test src/engine/tests/virtual_screen.cpp)
  target_link_libraries(njin_virtual_screen_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_render_texture_test src/engine/tests/render_textures.cpp)
  target_link_libraries(njin_render_texture_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_temporal_test src/engine/tests/temporal3d.cpp)
  target_link_libraries(njin_temporal_test PRIVATE njin::rt njin::gpu)
  add_dependencies(njin_temporal_test njin_model_test_assets)
  add_executable(njin_atlas_test src/engine/tests/atlas.cpp)
  target_link_libraries(njin_atlas_test PRIVATE njin::rt njin::gpu)
  add_dependencies(njin_atlas_test njin_model_test_assets)
  add_executable(njin_oit_test src/engine/tests/oit3d.cpp)
  target_link_libraries(njin_oit_test PRIVATE njin::rt njin::gpu)
  add_dependencies(njin_oit_test njin_model_test_assets)
  add_executable(njin_instancing_test src/engine/tests/instancing3d.cpp)
  add_executable(njin_occlusion_test src/engine/tests/occlusion3d.cpp)
  target_link_libraries(njin_occlusion_test PRIVATE njin::rt njin::gpu)
  add_dependencies(njin_occlusion_test njin_model_test_assets)
  if(NJIN_BGFX)
    set_target_properties(njin_occlusion_test PROPERTIES NJIN_ENGINE_INTERNAL ON)
    target_compile_definitions(njin_occlusion_test PRIVATE NJIN_ENGINE_INTERNAL NJIN_OCCLUSION_TIMING)
    target_link_libraries(njin_occlusion_test PRIVATE bgfx)
  endif()
  target_link_libraries(njin_instancing_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_environment_test src/engine/tests/environment3d.cpp)
  target_link_libraries(njin_environment_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_atmosphere_test src/engine/tests/atmosphere3d.cpp)
  target_link_libraries(njin_atmosphere_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_atmosphere_full_test src/engine/tests/atmosphere_full3d.cpp)
  target_link_libraries(njin_atmosphere_full_test PRIVATE njin::rt njin::gpu)
  if(NJIN_BGFX)
    foreach(_atmosphere_backend vulkan bgfx-opengl d3d12)
      if(_atmosphere_backend STREQUAL d3d12 AND NOT WIN32)
        continue()
      endif()
      add_test(NAME atmosphere_${_atmosphere_backend} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_atmosphere_test>" "-DRENDERER=${_atmosphere_backend}"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(atmosphere_${_atmosphere_backend} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME atmosphere_full_${_atmosphere_backend} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_atmosphere_full_test>" "-DRENDERER=${_atmosphere_backend}" -DSMOKE_TIMEOUT=240
        "-DLOG=${CMAKE_BINARY_DIR}/atmosphere-full-${_atmosphere_backend}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(atmosphere_full_${_atmosphere_backend} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 260)
    endforeach()
    add_test(NAME atmosphere_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_atmosphere_test>" -DRENDERER=vulkan -DVALIDATION=ON
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(atmosphere_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME atmosphere_full_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_atmosphere_full_test>" -DRENDERER=vulkan -DVALIDATION=ON -DSMOKE_TIMEOUT=240
      "-DLOG=${CMAKE_BINARY_DIR}/atmosphere-full-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(atmosphere_full_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 260)
  endif()
  add_executable(njin_material_test src/engine/tests/materials3d.cpp)
  target_link_libraries(njin_material_test PRIVATE njin::rt njin::gpu)
  add_executable(njin_shadow_test src/engine/tests/shadows.cpp)
  target_link_libraries(njin_shadow_test PRIVATE njin::rt njin::gpu)
  add_dependencies(njin_material_test njin_model_test_assets)
  add_dependencies(njin_animation_test njin_model_test_assets)
  # draw_sky3d parity: the legacy OpenGL run writes the reference the bgfx runs compare with.
  add_executable(njin_sky_parity_test src/engine/tests/sky_parity.cpp)
  target_link_libraries(njin_sky_parity_test PRIVATE njin::rt njin::gpu)
  set(_njin_model_renderers opengl)
  if(NJIN_BGFX)
    list(APPEND _njin_model_renderers vulkan)
    if(WIN32 OR (UNIX AND NOT APPLE AND NOT ANDROID))
      list(APPEND _njin_model_renderers bgfx-opengl)
    endif()
    if(WIN32)
      list(APPEND _njin_model_renderers d3d12)
    endif()
  endif()
  foreach(_renderer IN LISTS _njin_model_renderers)
    if(NOT _renderer STREQUAL "opengl")
    add_test(NAME residency_${_renderer} COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_residency_test>" "-DRENDERER=${_renderer}"
      -DSMOKE_TIMEOUT=240
      "-DLOG=${PROJECT_BINARY_DIR}/residency-${_renderer}.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(residency_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 270)
    add_test(NAME streaming_${_renderer} COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_streaming_test>" "-DRENDERER=${_renderer}"
      "-DLOG=${PROJECT_BINARY_DIR}/streaming-${_renderer}.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(streaming_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    if(_renderer STREQUAL "vulkan")
      add_test(NAME residency_vulkan_validation COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_residency_test>" -DRENDERER=vulkan -DVALIDATION=ON
        -DSMOKE_TIMEOUT=240
        "-DLOG=${PROJECT_BINARY_DIR}/residency-vulkan-validation.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(residency_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 270)
      add_test(NAME streaming_vulkan_validation COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_streaming_test>" -DRENDERER=vulkan -DVALIDATION=ON
        "-DLOG=${PROJECT_BINARY_DIR}/streaming-vulkan-validation.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(streaming_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    endif()
    endif()
    add_test(NAME terrain_relief_${_renderer} COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_terrain_relief_test>" "-DRENDERER=${_renderer}" -DFRAMES=40
      "-DLOG=${PROJECT_BINARY_DIR}/terrain-relief-${_renderer}.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(terrain_relief_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    if(_renderer STREQUAL "vulkan")
      add_test(NAME terrain_relief_vulkan_validation COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_terrain_relief_test>" -DRENDERER=vulkan -DFRAMES=40 -DVALIDATION=ON
        "-DLOG=${PROJECT_BINARY_DIR}/terrain-relief-vulkan-validation.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(terrain_relief_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    endif()
    # Every 3D primitive, instanced and immediate (legacy OpenGL and bgfx).
    add_test(NAME primitives_${_renderer} COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_primitives_test>" "-DRENDERER=${_renderer}" -DFRAMES=10
      "-DLOG=${PROJECT_BINARY_DIR}/primitives-${_renderer}.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(primitives_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    # Transparent 3D draws without post3d.oit are alpha blended (legacy OpenGL and bgfx).
    add_test(NAME transparent_${_renderer} COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_transparent_test>" "-DRENDERER=${_renderer}" -DFRAMES=10
      "-DLOG=${PROJECT_BINARY_DIR}/transparent-${_renderer}.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(transparent_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    # The open sea reaches the horizon (legacy OpenGL and bgfx).
    add_test(NAME ocean_${_renderer} COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_ocean_test>" "-DRENDERER=${_renderer}" -DFRAMES=10
      "-DLOG=${PROJECT_BINARY_DIR}/ocean-${_renderer}.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(ocean_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME model_${_renderer} COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_model_test>" "-DRENDERER=${_renderer}" -DFRAMES=16
      "-DLOG=${PROJECT_BINARY_DIR}/model-${_renderer}.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(model_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME shadows_${_renderer} COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_shadow_test>" "-DRENDERER=${_renderer}" -DFRAMES=36
      "-DLOG=${PROJECT_BINARY_DIR}/shadows-${_renderer}.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(shadows_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME materials_${_renderer} COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_material_test>" "-DRENDERER=${_renderer}"
      "-DLOG=${PROJECT_BINARY_DIR}/materials-${_renderer}.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(materials_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME sky_parity_${_renderer} COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_sky_parity_test>" "-DRENDERER=${_renderer}"
      "-DLOG=${PROJECT_BINARY_DIR}/sky-parity-${_renderer}.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    if(_renderer STREQUAL "opengl")
      set_tests_properties(sky_parity_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120 FIXTURES_SETUP njin_sky_reference)
    else()
      set_tests_properties(sky_parity_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120 FIXTURES_REQUIRED njin_sky_reference)
    endif()
    if(NOT _renderer STREQUAL "opengl")
      add_test(NAME environment_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_environment_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/environment-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(environment_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME instancing_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_instancing_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/instancing-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(instancing_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME occlusion_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_occlusion_test>" "-DRENDERER=${_renderer}"
        -DSMOKE_TIMEOUT=240
        "-DLOG=${PROJECT_BINARY_DIR}/occlusion-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(occlusion_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 300)
      add_test(NAME shadow_gpu_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_shadow_test>" "-DRENDERER=${_renderer}" -DEXTRA_ARGS=--gpu-culling -DSMOKE_TIMEOUT=240
        "-DLOG=${PROJECT_BINARY_DIR}/shadow-gpu-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(shadow_gpu_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 300)
      add_test(NAME post_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_post_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/post-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(post_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME animation_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_animation_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/animation-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(animation_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME ssao_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_ssao_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/ssao-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(ssao_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME ssr_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_ssr_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/ssr-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(ssr_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME effects_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_effects_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/effects-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(effects_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME smaa_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_smaa_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/smaa-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(smaa_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME exposure_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_exposure_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/exposure-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(exposure_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME oit_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_oit_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/oit-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(oit_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME mipmaps_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_mipmaps_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/mipmaps-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(mipmaps_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME temporal_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_temporal_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/temporal-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(temporal_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME render_texture_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_render_texture_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/render_texture-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(render_texture_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME atlas_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_atlas_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/atlas-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(atlas_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME virtual_screen_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_virtual_screen_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/virtual-screen-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(virtual_screen_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME video_texture_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_video_texture_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/video-texture-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(video_texture_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME dynamic_mesh_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_dynamic_mesh_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/dynamic-mesh-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(dynamic_mesh_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME lod_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_lod_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/lod-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(lod_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME sprite_fx_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_sprite_fx_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/sprite-fx-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(sprite_fx_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME custom_shader_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_custom_shader_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/custom-shader-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(custom_shader_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME gizmos_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_gizmo_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/gizmos-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(gizmos_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME crisp_text_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_crisp_text_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/crisp-text-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(crisp_text_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME world_fx_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_world_fx_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/world-fx-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(world_fx_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME renderer_batch_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_renderer_batch_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/renderer-batch-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(renderer_batch_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME portable_world_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_portable_world_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/portable-world-${_renderer}.log"
        -DSMOKE_TIMEOUT=285
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(portable_world_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 300)
      add_test(NAME advanced_material_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_advanced_material_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/advanced-material-${_renderer}.log"
        -DSMOKE_TIMEOUT=225
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(advanced_material_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 240)
      if(NOT _renderer STREQUAL "opengl")
        add_test(NAME lightmap_${_renderer} COMMAND "${CMAKE_COMMAND}"
          "-DEXE=$<TARGET_FILE:njin_lightmap_test>" "-DRENDERER=${_renderer}"
          "-DLOG=${PROJECT_BINARY_DIR}/lightmap-${_renderer}.log"
          -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
        set_tests_properties(lightmap_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
        add_test(NAME lightmap_model_${_renderer} COMMAND "${CMAKE_COMMAND}"
          "-DEXE=$<TARGET_FILE:njin_lightmap_model_test>" "-DRENDERER=${_renderer}"
          "-DLOG=${PROJECT_BINARY_DIR}/lightmap-model-${_renderer}.log" -DSMOKE_TIMEOUT=225
          -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
        set_tests_properties(lightmap_model_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 240)
      endif()
      add_test(NAME raytrace_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_raytrace_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/raytrace-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(raytrace_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 180)
      add_test(NAME backdrop_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_backdrop_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/backdrop-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(backdrop_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME light2d_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_light2d_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/light2d-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(light2d_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME gpu_particles_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_gpu_particles_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/gpu_particles-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(gpu_particles_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      add_test(NAME clustered_${_renderer} COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_clustered_test>" "-DRENDERER=${_renderer}"
        "-DLOG=${PROJECT_BINARY_DIR}/clustered-${_renderer}.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(clustered_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      if(Python3_Interpreter_FOUND)
        add_test(NAME inspector_${_renderer} COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/src/engine/tests/check_inspector.py"
          --exe "$<TARGET_FILE:njin_inspector_test>" --renderer "${_renderer}"
          --output "${PROJECT_BINARY_DIR}/inspector-test-${_renderer}")
        set_tests_properties(inspector_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 90)
      endif()
    endif()
  endforeach()
  add_test(NAME sprite_fx_opengl COMMAND "${CMAKE_COMMAND}"
    "-DEXE=$<TARGET_FILE:njin_sprite_fx_test>" -DRENDERER=opengl
    "-DLOG=${PROJECT_BINARY_DIR}/sprite-fx-opengl.log"
    -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
  set_tests_properties(sprite_fx_opengl PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
  if(Python3_Interpreter_FOUND)
    add_test(NAME inspector_opengl COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/src/engine/tests/check_inspector.py"
      --exe "$<TARGET_FILE:njin_inspector_test>" --renderer opengl
      --output "${PROJECT_BINARY_DIR}/inspector-test-legacy")
    set_tests_properties(inspector_opengl PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 90)
  endif()
  if(NJIN_BGFX)
    add_test(NAME model_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_model_test>" -DRENDERER=vulkan -DFRAMES=16 -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/model-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(model_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME shadows_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_shadow_test>" -DRENDERER=vulkan -DFRAMES=36 -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/shadows-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(shadows_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME materials_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_material_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/materials-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(materials_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME environment_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_environment_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/environment-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(environment_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME instancing_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_instancing_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/instancing-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(instancing_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME occlusion_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_occlusion_test>" -DRENDERER=vulkan -DVALIDATION=ON -DSMOKE_TIMEOUT=240
      "-DLOG=${PROJECT_BINARY_DIR}/occlusion-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(occlusion_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 300)
    add_test(NAME shadow_gpu_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_shadow_test>" -DRENDERER=vulkan -DVALIDATION=ON -DEXTRA_ARGS=--gpu-culling -DSMOKE_TIMEOUT=240
      "-DLOG=${PROJECT_BINARY_DIR}/shadow-gpu-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(shadow_gpu_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 300)
    add_test(NAME post_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_post_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/post-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(post_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME animation_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_animation_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/animation-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(animation_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME ssao_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_ssao_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/ssao-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(ssao_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME ssr_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_ssr_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/ssr-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(ssr_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME effects_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_effects_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/effects-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(effects_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME smaa_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_smaa_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/smaa-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(smaa_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME exposure_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_exposure_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/exposure-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(exposure_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME oit_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_oit_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/oit-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(oit_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME mipmaps_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_mipmaps_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/mipmaps-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(mipmaps_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME temporal_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_temporal_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/temporal-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(temporal_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME render_texture_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_render_texture_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/render_texture-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(render_texture_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME atlas_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_atlas_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/atlas-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(atlas_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME virtual_screen_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_virtual_screen_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/virtual-screen-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(virtual_screen_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME video_texture_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_video_texture_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/video-texture-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(video_texture_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME dynamic_mesh_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_dynamic_mesh_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/dynamic-mesh-vulkan_validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(dynamic_mesh_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME lod_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_lod_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/lod-vulkan_validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(lod_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME sprite_fx_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_sprite_fx_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/sprite-fx-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(sprite_fx_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME custom_shader_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_custom_shader_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/custom-shader-vulkan-validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(custom_shader_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME gizmos_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_gizmo_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/gizmos-vulkan_validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(gizmos_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME crisp_text_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_crisp_text_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/crisp-text-vulkan_validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(crisp_text_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME world_fx_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_world_fx_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/world-fx-vulkan_validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(world_fx_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME renderer_batch_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_renderer_batch_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/renderer-batch-vulkan_validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(renderer_batch_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME portable_world_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_portable_world_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/portable-world-vulkan_validation.log"
      -DSMOKE_TIMEOUT=285
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(portable_world_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 300)
    add_test(NAME advanced_material_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_advanced_material_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/advanced-material-vulkan_validation.log"
      -DSMOKE_TIMEOUT=225
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(advanced_material_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 240)
    add_test(NAME raytrace_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_raytrace_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/raytrace-vulkan_validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(raytrace_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 180)
    # The compute traversal on a GPU that has ray queries (config::graphics_ray_query off).
    add_test(NAME raytrace_vulkan_compute COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_raytrace_test>" -DRENDERER=vulkan -DEXTRA_ARGS=--no-ray-query
      "-DLOG=${PROJECT_BINARY_DIR}/raytrace-vulkan_compute.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(raytrace_vulkan_compute PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 180)
    add_test(NAME backdrop_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_backdrop_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/backdrop-vulkan_validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(backdrop_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME light2d_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_light2d_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/light2d-vulkan_validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(light2d_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME gpu_particles_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_gpu_particles_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/gpu_particles-vulkan_validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(gpu_particles_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME clustered_vulkan_validation COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_clustered_test>" -DRENDERER=vulkan -DVALIDATION=ON
      "-DLOG=${PROJECT_BINARY_DIR}/clustered-vulkan_validation.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(clustered_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    if(Python3_Interpreter_FOUND)
      add_test(NAME inspector_vulkan_validation COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/src/engine/tests/check_inspector.py"
        --exe "$<TARGET_FILE:njin_inspector_test>" --renderer "vulkan" --gpu-validation
        --output "${PROJECT_BINARY_DIR}/inspector-test-vulkan_validation")
      set_tests_properties(inspector_vulkan_validation PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 90)
    endif()
  endif()
  add_test(NAME rhi_opengl COMMAND njin_rhi_slice --renderer=opengl)
  set_tests_properties(rhi_opengl PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
  add_test(NAME rhi_invalid_backend COMMAND "${CMAKE_COMMAND}"
    "-DEXE=$<TARGET_FILE:njin_rhi_slice>" -DRENDERER=invalid
    "-DEXPECTED=invalid renderer" -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_failure.cmake")
  set_tests_properties(rhi_invalid_backend PROPERTIES TIMEOUT 35)
  if(NOT NJIN_BGFX)
    add_test(NAME rhi_default_requires_bgfx COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_rhi_slice>"
      "-DEXPECTED=bgfx backend was not built"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_failure.cmake")
    set_tests_properties(rhi_default_requires_bgfx PROPERTIES TIMEOUT 35)
  endif()
  if(NJIN_BGFX)
    if(NOT CMAKE_CROSSCOMPILING)
      if(NJIN_BGFX_SHADERC)
        set(_njin_shaderc_test "${NJIN_BGFX_SHADERC}")
      else()
        set(_njin_shaderc_test "$<TARGET_FILE:shaderc>")
      endif()
      add_test(NAME rhi_bgfx_shader_errors COMMAND "${CMAKE_COMMAND}"
        "-DSHADERC=${_njin_shaderc_test}" "-DBGFX_SRC=${NJIN_BGFX_SOURCE_DIR}"
        "-DVARYING=${PROJECT_SOURCE_DIR}/src/engine/runtime/shaders/bgfx/varying.def.sc"
        "-DOUTPUT_DIR=${PROJECT_BINARY_DIR}/bgfx-shader-tests"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_bgfx_shaders.cmake")
    endif()
    if(WIN32)
      set(_njin_default_driver Direct3D12)
    elseif(APPLE)
      set(_njin_default_driver Metal)
    elseif(EMSCRIPTEN)
      set(_njin_default_driver "OpenGL ES")
    else()
      set(_njin_default_driver Vulkan)
    endif()
    add_test(NAME rhi_default_backend COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_rhi_slice>" "-DEXPECTED_DRIVER=${_njin_default_driver}"
      "-DLOG=${PROJECT_BINARY_DIR}/rhi-default-backend.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(rhi_default_backend PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_executable(njin_sdl_platform_test src/engine/tests/sdl_platform.cpp)
    set_target_properties(njin_sdl_platform_test PROPERTIES NJIN_ENGINE_INTERNAL ON)
    target_compile_definitions(njin_sdl_platform_test PRIVATE NJIN_ENGINE_INTERNAL)
    target_include_directories(njin_sdl_platform_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/runtime")
    target_include_directories(njin_sdl_platform_test SYSTEM PRIVATE "$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
    njin_use(njin_sdl_platform_test)
    target_link_libraries(njin_sdl_platform_test PRIVATE njin_warnings)
    add_test(NAME rhi_sdl_platform COMMAND njin_sdl_platform_test)
    add_executable(njin_rhi_unsupported_test src/engine/tests/unsupported_scene.cpp)
    target_link_libraries(njin_rhi_unsupported_test PRIVATE njin::rt njin_warnings)
    add_test(NAME rhi_unsupported_scene COMMAND njin_rhi_unsupported_test)
    add_test(NAME rhi_vulkan COMMAND "${CMAKE_COMMAND}" "-DEXE=$<TARGET_FILE:njin_rhi_slice>" -DRENDERER=vulkan -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    if(WIN32 OR (UNIX AND NOT APPLE AND NOT ANDROID))
      add_test(NAME rhi_bgfx_opengl COMMAND "${CMAKE_COMMAND}" "-DEXE=$<TARGET_FILE:njin_rhi_slice>" -DRENDERER=bgfx-opengl -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(rhi_bgfx_opengl PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    endif()
    if(WIN32)
      add_executable(njin_bgfx_surface_test src/engine/tests/bgfx_surface.cpp)
      set_target_properties(njin_bgfx_surface_test PROPERTIES NJIN_ENGINE_INTERNAL ON)
      target_compile_definitions(njin_bgfx_surface_test PRIVATE NJIN_ENGINE_INTERNAL)
      target_include_directories(njin_bgfx_surface_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/runtime")
      target_include_directories(njin_bgfx_surface_test SYSTEM PRIVATE "$<TARGET_PROPERTY:SDL3::Headers,INTERFACE_INCLUDE_DIRECTORIES>")
      target_link_libraries(njin_bgfx_surface_test PRIVATE njin::rt njin::gpu njin_warnings)
      add_test(NAME rhi_vulkan_surface_lifecycle COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:njin_bgfx_surface_test>" -DRENDERER=vulkan
        "-DLOG=${PROJECT_BINARY_DIR}/vulkan-surface-lifecycle.log"
        -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
      set_tests_properties(rhi_vulkan_surface_lifecycle PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      foreach(_njin_windows_renderer IN ITEMS d3d12 auto)
        add_test(NAME rhi_bgfx_${_njin_windows_renderer} COMMAND "${CMAKE_COMMAND}"
          "-DEXE=$<TARGET_FILE:njin_rhi_slice>" "-DRENDERER=${_njin_windows_renderer}"
          -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
        set_tests_properties(rhi_bgfx_${_njin_windows_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
      endforeach()
      add_test(NAME rhi_bgfx_unavailable COMMAND "${CMAKE_COMMAND}" "-DEXE=$<TARGET_FILE:njin_rhi_slice>" -DRENDERER=metal "-DEXPECTED=requested backend was not compiled" -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_failure.cmake")
    endif()
    add_test(NAME rhi_vulkan_validation COMMAND "${CMAKE_COMMAND}" "-DEXE=$<TARGET_FILE:njin_rhi_slice>" -DRENDERER=vulkan -DFRAMES=1200 -DVALIDATION=ON -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    add_test(NAME rhi_platform_unavailable COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_rhi_slice>" -DRENDERER=vulkan
      "-DEXPECTED=initialize video" -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_failure.cmake")
    set_tests_properties(rhi_platform_unavailable PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=njin_invalid_driver")
    set_tests_properties(rhi_opengl rhi_sdl_platform rhi_unsupported_scene rhi_vulkan rhi_vulkan_validation
      PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
  else()
    add_test(NAME rhi_vulkan_unavailable COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_rhi_slice>" -DRENDERER=vulkan
      "-DEXPECTED=bgfx backend was not built" -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_failure.cmake")
  endif()
endif()
option(NJIN_BUILD_RENDERER_BENCHMARK "Build reproducible renderer measurements" OFF)
if(NJIN_BUILD_RENDERER_BENCHMARK)
  if(NOT NJIN_BGFX)
    message(FATAL_ERROR "Renderer benchmark requires SDL3/bgfx")
  endif()
  add_executable(njin_renderer_bench src/tools/renderer_bench/main.cpp src/tools/renderer_bench/process_memory.cpp)
  set_target_properties(njin_renderer_bench PROPERTIES NJIN_ENGINE_INTERNAL ON)
  target_compile_definitions(njin_renderer_bench PRIVATE NJIN_ENGINE_INTERNAL $<$<NOT:$<CONFIG:Debug>>:NJIN_CTX_INLINE>
    NJIN_BENCH_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
  target_include_directories(njin_renderer_bench PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/runtime")
  target_link_libraries(njin_renderer_bench PRIVATE njin::rt njin::gpu bgfx)
  if(WIN32)
    target_link_libraries(njin_renderer_bench PRIVATE psapi)
  endif()
endif()

# Which of them: all by default. Working on the engine, one or two are enough,
# and editing a public header then rebuilds only those (the engine preset).
set(NJIN_ALL_EXAMPLES sandbox pong platformer topdown debug_demo render_demo
                      tower_defense lighting_demo fps sokoban platformer3d skylight)
set(NJIN_EXAMPLES "${NJIN_ALL_EXAMPLES}" CACHE STRING "The examples to build (NJIN_BUILD_EXAMPLES), ;-separated")
if(NJIN_BUILD_EXAMPLES)
  foreach(example IN LISTS NJIN_EXAMPLES)
    if(NOT example IN_LIST NJIN_ALL_EXAMPLES)
      list(JOIN NJIN_ALL_EXAMPLES ", " known)
      message(FATAL_ERROR "NJIN_EXAMPLES: unknown example '${example}' (known: ${known})")
    endif()
    add_subdirectory(examples/${example})
  endforeach()
  # Every sample, whole, on every renderer: starts, runs NJIN_SMOKE_FRAMES frames from its
  # own folder, exits cleanly and draws a non-blank frame (src/engine/runtime/njin_smoke.h).
  # The bar for removing the legacy renderer: these pass on bgfx as on OpenGL.
  if(NJIN_BUILD_RHI_SLICE)
    set(_njin_smoke_renderers opengl)
    if(NJIN_BGFX)
      list(APPEND _njin_smoke_renderers vulkan bgfx-opengl)
      if(WIN32)
        list(APPEND _njin_smoke_renderers d3d12)
      endif()
    endif()
    foreach(example IN LISTS NJIN_EXAMPLES)
      if(NOT TARGET njin_${example})
        continue()
      endif()
      foreach(_renderer IN LISTS _njin_smoke_renderers)
        add_test(NAME sample_${example}_${_renderer} COMMAND "${CMAKE_COMMAND}"
          "-DEXE=$<TARGET_FILE:njin_${example}>" "-DWORKDIR=$<TARGET_FILE_DIR:njin_${example}>"
          "-DRENDERER=${_renderer}" -DFRAMES=240 "-DBLANK_OK=$<STREQUAL:${example},sandbox>"
          "-DSHOT=${PROJECT_BINARY_DIR}/sample-${example}-${_renderer}.png"
          "-DLOG=${PROJECT_BINARY_DIR}/sample-${example}-${_renderer}.log"
          -P "${PROJECT_SOURCE_DIR}/cmake/check_example_smoke.cmake")
        set_tests_properties(sample_${example}_${_renderer} PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 180)
      endforeach()
    endforeach()
  endif()
endif()

if(NJIN_BUILD_RHI_SLICE AND TARGET njin_sandbox)
  add_test(NAME rhi_sandbox_opengl COMMAND njin_sandbox --renderer=opengl --frames=8)
  set_tests_properties(rhi_sandbox_opengl PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
  if(NJIN_BGFX)
    add_test(NAME rhi_sandbox_default COMMAND "${CMAKE_COMMAND}"
      "-DEXE=$<TARGET_FILE:njin_sandbox>" -DFRAMES=8
      "-DEXPECTED_DRIVER=${_njin_default_driver}"
      "-DLOG=${PROJECT_BINARY_DIR}/sandbox-default-backend.log"
      -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(rhi_sandbox_default PROPERTIES RESOURCE_LOCK njin_graphics TIMEOUT 120)
    add_test(NAME rhi_sandbox_vulkan COMMAND "${CMAKE_COMMAND}" "-DEXE=$<TARGET_FILE:njin_sandbox>" -DRENDERER=vulkan -DFRAMES=8 -P "${PROJECT_SOURCE_DIR}/cmake/check_rhi_smoke.cmake")
    set_tests_properties(rhi_sandbox_vulkan PROPERTIES RESOURCE_LOCK njin_graphics)
  endif()
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

option(NJIN_BUILD_ANIM_EDITOR "Build the glTF skeleton animation editor" ON)
if(NJIN_BUILD_ANIM_EDITOR)
  add_subdirectory(src/tools/anim_editor)
endif()

option(NJIN_BUILD_MUTEX_TESTS "Build owning mutex concurrency tests" OFF)
if(NJIN_BUILD_MUTEX_TESTS)
  enable_testing()
  find_package(Threads REQUIRED)
  add_executable(njin_mutex_test src/engine/tests/mutex.cpp)
  target_compile_definitions(njin_mutex_test PRIVATE NJIN_ENGINE_INTERNAL)
  target_include_directories(njin_mutex_test PRIVATE "${PROJECT_SOURCE_DIR}/src/engine/runtime")
  target_link_libraries(njin_mutex_test PRIVATE njin_warnings Threads::Threads)
  add_test(NAME mutex_guard COMMAND njin_mutex_test)
  set_tests_properties(mutex_guard PROPERTIES TIMEOUT 30)
endif()
