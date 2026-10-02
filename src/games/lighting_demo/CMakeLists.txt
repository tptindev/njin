cmake_minimum_required(VERSION 3.28)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(njin_lighting_demo VERSION 0.1.0 LANGUAGES CXX)
endif()
set(NJIN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH "njin source checkout")
include("${NJIN_ROOT}/cmake/njin_game.cmake")

# A gallery of the 2D lighting: twelve rooms, one case in each. Read main.cpp for the rooms and the keys.
add_executable(njin_lighting_demo main.cpp module.cpp common.cpp rooms_lights.cpp rooms_occluders.cpp rooms_surfaces.cpp
                                  hud.cpp)
target_link_libraries(njin_lighting_demo PRIVATE njin::rt njin::gpu njin_warnings)

# Each game runs from its own folder (build/bin/lighting_demo/), so two games never
# share or overwrite each other's assets.
set_target_properties(njin_lighting_demo PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/lighting_demo")

njin_add_assets(njin_lighting_demo assets)

# The njin icon on the exe, its window and its taskbar button (Windows).
njin_icon(njin_lighting_demo)
