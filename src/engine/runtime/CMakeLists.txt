# The runtime is a core and optional features, one static library each
# (njin_features.h). A game names the features it uses with njin_use()
# (cmake/njin.cmake) and links only those; njin::rt is every feature, for the
# games and tools that link it as before.
#
# A source belongs to the core unless a feature lists it below. A new file of
# a feature goes in that feature's list.
set(_njin_rt_dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(NJIN_FEATURE_3D_SOURCES
    feature_3d.cpp
    njin_anim3d.cpp
    njin_audio3d.cpp
    njin_instance.cpp
    njin_model.cpp
    njin_model_lod.cpp
    njin_ray3d.cpp
    njin_world3d.cpp
    modules/particles3d.cpp
    modules/particles3d_looks.cpp
    modules/post3d.cpp
    modules/render3d.cpp
    modules/render3d_env.cpp
    modules/render3d_hdr.cpp
    modules/render3d_lights.cpp
    modules/render3d_probe.cpp
    modules/render3d_volume.cpp
    modules/world3d_draw.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/smaa_shader_data.cpp")
set(NJIN_FEATURE_PHYSICS3D_SOURCES feature_physics3d.cpp njin_physics3d.cpp)
set(NJIN_FEATURE_NAV3D_SOURCES feature_nav3d.cpp njin_nav3d.cpp)
set(NJIN_FEATURE_VIDEO_SOURCES feature_video.cpp njin_plmpeg.cpp njin_video.cpp)
set(NJIN_FEATURE_NET_SOURCES
    feature_net.cpp
    njin_lockstep.cpp
    njin_multiplayer_enet.cpp
    njin_netcode.cpp
    njin_network.cpp
    njin_network_asio.cpp)
set(NJIN_FEATURE_SCRIPT_SOURCES feature_script.cpp njin_script.cpp njin_script_bind.cpp njin_script_bind_2d.cpp
    njin_script_bind_io.cpp njin_script_bind_more.cpp njin_script_bind_vec.cpp)
set(NJIN_FEATURE_SCRIPT_3D_SOURCES njin_script_glue_3d.cpp)
set(NJIN_FEATURE_SCRIPT_PHYSICS3D_SOURCES njin_script_glue_physics3d.cpp)
set(NJIN_FEATURE_SCRIPT_NAV3D_SOURCES njin_script_glue_nav3d.cpp)

file(GLOB_RECURSE NJIN_CORE_SOURCES CONFIGURE_DEPENDS RELATIVE "${_njin_rt_dir}" "${_njin_rt_dir}/*.cpp")
foreach(feature IN LISTS NJIN_FEATURES_ORDER)
  string(TOUPPER "${feature}" upper)
  if(NOT NJIN_FEATURE_${upper}_SOURCES)
    message(FATAL_ERROR "njin: feature '${feature}' lists no sources in ${CMAKE_CURRENT_LIST_FILE}")
  endif()
  list(REMOVE_ITEM NJIN_CORE_SOURCES ${NJIN_FEATURE_${upper}_SOURCES})
endforeach()

# The default font (JetBrains Mono, SIL OFL) is compiled in, so draw_text works
# in a game that ships no assets.
include("${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake")
njin_embed_file("${CMAKE_CURRENT_BINARY_DIR}/font_default_data.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/JetBrainsMono-Regular.ttf"
                font_default_ttf)
# SMAA's shader (post3d::aa), compiled into the 3d feature as text.
njin_embed_file("${CMAKE_CURRENT_BINARY_DIR}/smaa_shader_data.cpp" "${smaa_SOURCE_DIR}/SMAA.hlsl" smaa_hlsl)

# Engine side of the boundary: raylib and the runtime headers are allowed in
# these (see src/engine/api/CMakeLists.txt). raylib stays private; EnTT is part
# of the public ECS API through njin::api.
function(_njin_runtime_library name)
  set_target_properties(${name} PROPERTIES NJIN_ENGINE_INTERNAL ON)
  # Lets runtime files in subfolders (modules/) include runtime headers directly.
  target_include_directories(${name} PRIVATE "${_njin_rt_dir}" "${PROJECT_SOURCE_DIR}/src/engine/mem")
  target_link_libraries(${name} PRIVATE raylib njin_warnings)
  # ctx_ref() (njin_ctx_get.h) is a call in Debug, so a source does not see
  # the context's definition and editing it rebuilds one source; inline in
  # every other configuration, as plain member access was.
  target_compile_definitions(${name} PRIVATE $<$<NOT:$<CONFIG:Debug>>:NJIN_CTX_INLINE>)
  # The script glue libraries reuse the script feature's (below).
  if(NOT name MATCHES "^njin_script_")
    njin_precompile_headers(${name})
  endif()
endfunction()

add_library(njin_core STATIC ${NJIN_CORE_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/font_default_data.cpp")
add_library(njin::core ALIAS njin_core)
_njin_runtime_library(njin_core)
target_link_libraries(njin_core PUBLIC njin::api)

# njin_pace.cpp: timeBeginPeriod/timeEndPeriod (config::low_power).
if(WIN32)
  target_link_libraries(njin_core PRIVATE winmm)
endif()

# njin_gpu_hint.cpp checks for the NVIDIA GLX library with dlopen.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(njin_core PRIVATE ${CMAKE_DL_LIBS})
endif()

# The debug link (njin_net.cpp) uses sockets.
if(WIN32)
  target_link_libraries(njin_core PRIVATE ws2_32)
endif()

# On Linux, ask for the discrete GPU of a laptop with PRIME offload variables
# (njin_gpu_hint.cpp). Windows is done by njin::gpu, linked into each game; the
# option is defined in src/engine/gpu.
target_compile_definitions(njin_core PRIVATE NJIN_PREFER_DISCRETE_GPU=$<BOOL:${NJIN_PREFER_DISCRETE_GPU}>)

# One library per feature: its sources, the features it builds on, and the
# libraries only it uses. Linking it defines NJIN_FEATURE_<NAME> for the game,
# which its public headers check.
function(_njin_feature name)
  cmake_parse_arguments(F "" "" "NEEDS;LIBS" ${ARGN})
  string(TOUPPER "${name}" upper)
  add_library(njin_${name} STATIC ${NJIN_FEATURE_${upper}_SOURCES})
  add_library(njin::${name} ALIAS njin_${name})
  _njin_runtime_library(njin_${name})
  target_link_libraries(njin_${name} PUBLIC njin::core)
  foreach(need IN LISTS F_NEEDS)
    target_link_libraries(njin_${name} PUBLIC njin_${need})
  endforeach()
  if(F_LIBS)
    target_link_libraries(njin_${name} PRIVATE ${F_LIBS})
  endif()
  target_compile_definitions(njin_${name} INTERFACE NJIN_FEATURE_${upper})
endfunction()

_njin_feature(3d LIBS meshoptimizer njin_smaa)
_njin_feature(physics3d NEEDS 3d LIBS Jolt)
_njin_feature(nav3d NEEDS 3d physics3d LIBS njin_recast)
_njin_feature(video LIBS njin_plmpeg)
_njin_feature(net LIBS njin_asio njin_enet)
_njin_feature(script LIBS njin_sol2)
_njin_feature(script_3d NEEDS script 3d LIBS njin_sol2)
_njin_feature(script_physics3d NEEDS script physics3d LIBS njin_sol2)
_njin_feature(script_nav3d NEEDS script nav3d LIBS njin_sol2)

# sol2 bindings (njin_script_bind*.cpp) make more sections than an MSVC Debug
# object holds by default.
if(MSVC)
  foreach(name script script_3d script_physics3d script_nav3d)
    target_compile_options(njin_${name} PRIVATE /bigobj)
  endforeach()
endif()

# sol2 is most of the compile time of the script sources: the script feature
# precompiles it with the common headers, and its glue libraries (one source
# each) reuse that header instead of making their own.
if(NJIN_PCH)
  target_precompile_headers(njin_script PRIVATE <sol/sol.hpp>)
  foreach(name script_3d script_physics3d script_nav3d)
    target_precompile_headers(njin_${name} REUSE_FROM njin_script)
  endforeach()
endif()

# njin::rt: the core and every feature. Its features_install() is a source
# compiled into each target that links it, like the one njin_use() makes.
add_library(njin_rt INTERFACE)
add_library(njin::rt ALIAS njin_rt)
foreach(feature IN LISTS NJIN_FEATURES_ORDER)
  target_link_libraries(njin_rt INTERFACE njin_${feature})
endforeach()
target_link_libraries(njin_rt INTERFACE njin_core)
_njin_features_source("${CMAKE_CURRENT_BINARY_DIR}/njin_features_all.cpp" ${NJIN_FEATURES_ORDER})
target_sources(njin_rt INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/njin_features_all.cpp")
