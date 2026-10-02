cmake_minimum_required(VERSION 3.28)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(njin_tower_defense VERSION 0.1.0 LANGUAGES CXX)
endif()
set(NJIN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH "njin source checkout")
include("${NJIN_ROOT}/cmake/njin_game.cmake")

add_executable(njin_tower_defense main.cpp game.cpp render.cpp effects.cpp)
target_link_libraries(njin_tower_defense PRIVATE njin::rt njin_warnings)
njin_add_assets(njin_tower_defense assets)

# The njin icon on the exe, its window and its taskbar button (Windows).
njin_icon(njin_tower_defense)
