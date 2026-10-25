cmake_minimum_required(VERSION 3.28)
project(my_game VERSION 0.1.0 LANGUAGES CXX)

set(NJIN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../njin" CACHE PATH "Path to the njin source checkout")
if(NOT EXISTS "${NJIN_ROOT}/cmake/njin_game.cmake")
  message(FATAL_ERROR "Set NJIN_ROOT to your njin checkout: -DNJIN_ROOT=/path/to/njin")
endif()
include("${NJIN_ROOT}/cmake/njin_game.cmake")

add_executable(my_game src/main.cpp)
njin_use(my_game) # Add FEATURES physics3d, script, etc. as the game needs them.
target_link_libraries(my_game PRIVATE njin_warnings)
set_target_properties(my_game PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

# During development, run from the project folder to use assets/ directly.
# Only the distribution contains a copy of the assets.
njin_package(my_game NAME "My Game" ASSETS assets)
