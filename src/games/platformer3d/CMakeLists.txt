cmake_minimum_required(VERSION 3.28)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(njin_platformer3d VERSION 0.1.0 LANGUAGES CXX)
endif()
set(NJIN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH "njin source checkout")
include("${NJIN_ROOT}/cmake/njin_game.cmake")

# A 3D platformer on the 3D API (njin_3d.h) and 3D physics: third-person camera,
# an animated robot, floating platforms. The two models in assets/ are built by
# tools/make_assets.py.
add_executable(njin_platformer3d main.cpp platformer3d.cpp)
target_link_libraries(njin_platformer3d PRIVATE njin::rt njin::gpu njin_warnings)

# Each game runs from its own folder (build/bin/platformer3d/), so two games never share or
# overwrite each other's assets.
set_target_properties(njin_platformer3d PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/platformer3d")

njin_add_assets(njin_platformer3d assets)

njin_icon(njin_platformer3d)
