# Pinned wrapper pins compatible bgfx/bx/bimg revisions itself.
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_EXAMPLE_COMMON OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
set(BGFX_CONFIG_VIDEO OFF CACHE BOOL "" FORCE)
# Submit/render on Njin's window thread for the initial migration. No second
# native-window/context owner or renderer worker thread is introduced.
set(BGFX_CONFIG_MULTITHREADED OFF CACHE BOOL "" FORCE)
if(CMAKE_CROSSCOMPILING OR NJIN_BGFX_SHADERC)
  set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
endif()
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(njin_bgfx
  GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
  GIT_TAG 1af3832b87ad0588c05f1f0bd7ee17a7b14d2315)
FetchContent_MakeAvailable(njin_bgfx)
if(BGFX_DIR)
  set(NJIN_BGFX_SOURCE_DIR "${BGFX_DIR}")
else()
  FetchContent_GetProperties(bgfx SOURCE_DIR NJIN_BGFX_SOURCE_DIR)
endif()
set_property(GLOBAL PROPERTY NJIN_SHADER_BGFX_SOURCE_DIR "${NJIN_BGFX_SOURCE_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/njin_bgfx_surface_fix.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/njin_bgfx_rayquery_fix.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/njin_bgfx_shader_stack_fix.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/njin_bgfx_occlusion_fix.cmake")
# Njin's executable-level njin::gpu already owns these Optimus/PowerXpress
# exports and honors NJIN_PREFER_DISCRETE_GPU. Avoid duplicate MinGW symbols.
target_compile_definitions(bgfx PRIVATE BGFX_CONFIG_PREFER_DISCRETE_GPU=0)
