cmake_minimum_required(VERSION 3.28)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(njin_topdown VERSION 0.1.0 LANGUAGES CXX)
endif()
set(NJIN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH "njin source checkout")
include("${NJIN_ROOT}/cmake/njin_game.cmake")

add_executable(njin_topdown main.cpp play.cpp menus.cpp ../shared/settings_menu.cpp)
target_link_libraries(njin_topdown PRIVATE njin::rt njin::gpu njin_warnings)

# Each game runs from its own folder (build/bin/topdown/), so two games never share or
# overwrite each other's assets.
set_target_properties(njin_topdown PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/topdown")

njin_add_assets(njin_topdown assets)

# `cmake --build <dir> --target njin_topdown_dist` makes dist/njin_topdown-<version>.zip.
njin_package(njin_topdown
  NAME "Old Stone Forest"
  ICON icon.ico
  ASSETS assets)
