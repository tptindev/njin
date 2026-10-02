cmake_minimum_required(VERSION 3.28)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(njin_fps VERSION 0.1.0 LANGUAGES CXX)
endif()
set(NJIN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH "njin source checkout")
include("${NJIN_ROOT}/cmake/njin_game.cmake")

# First-person aim trainer on the 3D API (njin_3d.h), after Biped-Potato's Bevy
# FPS tutorial; the gun model is theirs (assets/models/LICENSE.txt).
add_executable(njin_fps main.cpp fps.cpp)
target_link_libraries(njin_fps PRIVATE njin::rt njin::gpu njin_warnings)

# Each game runs from its own folder (build/bin/fps/), so two games never share or
# overwrite each other's assets.
set_target_properties(njin_fps PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/fps")

njin_add_assets(njin_fps assets)

njin_icon(njin_fps)
