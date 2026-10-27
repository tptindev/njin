cmake_minimum_required(VERSION 3.28)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(njin_skylight VERSION 0.1.0 LANGUAGES CXX)
endif()
set(NJIN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "njin source checkout")
include("${NJIN_ROOT}/cmake/njin_game.cmake")

# Sunbeams through a skylight in a dark hall: volumetric fog, bloom, and with
# NJIN_RAYTRACING ray traced shadows and bounce. The statue is Ville
# Seppänen's (assets/LICENSE.txt).
add_executable(njin_skylight main.cpp)
njin_use(njin_skylight FEATURES 3d)
njin_precompile_headers(njin_skylight)
target_link_libraries(njin_skylight PRIVATE njin::gpu njin_warnings)

# Each game runs from its own folder (build/bin/skylight/), so two games never share or
# overwrite each other's assets.
set_target_properties(njin_skylight PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/skylight")

njin_add_assets(njin_skylight assets)

njin_icon(njin_skylight)
