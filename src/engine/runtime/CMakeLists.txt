file(GLOB_RECURSE NJIN_RT_SOURCES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

if(NOT NJIN_RT_SOURCES)
  message(
    FATAL_ERROR "njin_rt: no .cpp found under ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# The default font (JetBrains Mono, SIL OFL) is compiled in, so draw_text works
# in a game that ships no assets.
include("${PROJECT_SOURCE_DIR}/cmake/embed_file.cmake")
njin_embed_file("${CMAKE_CURRENT_BINARY_DIR}/font_default_data.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/JetBrainsMono-Regular.ttf"
                font_default_ttf)

add_library(njin_rt STATIC ${NJIN_RT_SOURCES}
                           "${CMAKE_CURRENT_BINARY_DIR}/font_default_data.cpp")
add_library(njin::rt ALIAS njin_rt)

# Engine side of the boundary: raylib and the runtime headers are allowed here
# (see src/engine/api/CMakeLists.txt).
set_target_properties(njin_rt PROPERTIES NJIN_ENGINE_INTERNAL ON)

# raylib stays private. EnTT is part of the public ECS API through njin::api.
target_link_libraries(
  njin_rt
  PUBLIC njin::api
  PRIVATE raylib njin_warnings)

# njin_gpu_hint.cpp checks for the NVIDIA GLX library with dlopen.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(njin_rt PRIVATE ${CMAKE_DL_LIBS})
endif()

# The debug link (njin_net.cpp) uses sockets.
if(WIN32)
  target_link_libraries(njin_rt PRIVATE ws2_32)
endif()

# On Linux, ask for the discrete GPU of a laptop with PRIME offload variables
# (njin_gpu_hint.cpp). Windows is done by njin::gpu, linked into each game; the
# option is defined in src/engine/gpu.
target_compile_definitions(njin_rt PRIVATE NJIN_PREFER_DISCRETE_GPU=$<BOOL:${NJIN_PREFER_DISCRETE_GPU}>)

# Lets runtime files in subfolders (modules/) include runtime headers directly.
target_include_directories(njin_rt PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
