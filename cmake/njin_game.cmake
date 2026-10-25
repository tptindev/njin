# Included after project() by a standalone game, or by an example in njin.
include_guard(GLOBAL)
if(TARGET njin::rt)
  return()
endif()

set(NJIN_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH "njin source checkout")
set(NJIN_PREBUILT "${NJIN_ROOT}/build-release" CACHE PATH "Engine build with matching toolchain and configuration")
if(NOT EXISTS "${NJIN_PREBUILT}/njinTargets.cmake")
  message(FATAL_ERROR
    "Engine export missing: ${NJIN_PREBUILT}/njinTargets.cmake. "
    "Configure njin and build target njin_engine first, using the same toolchain/configuration as the game.")
endif()
find_package(Threads REQUIRED)
include("${NJIN_PREBUILT}/njinTargets.cmake")
add_library(njin::rt ALIAS njin_prebuilt::njin_rt)
# The core and the features njin_use() links (NJIN_FEATURES_ORDER in
# njin.cmake). An engine build from before the split has none: a game built on
# it links njin::rt.
foreach(_njin_feature core 3d physics3d nav3d video net script script_3d script_physics3d script_nav3d)
  if(TARGET njin_prebuilt::njin_${_njin_feature})
    add_library(njin::${_njin_feature} ALIAS njin_prebuilt::njin_${_njin_feature})
  endif()
endforeach()
add_library(njin::api ALIAS njin_prebuilt::njin_api)
add_library(njin::gpu ALIAS njin_prebuilt::njin_gpu)
add_library(njin_warnings ALIAS njin_prebuilt::njin_warnings)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include("${NJIN_ROOT}/cmake/njin.cmake")
