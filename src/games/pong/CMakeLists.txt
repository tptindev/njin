cmake_minimum_required(VERSION 3.28)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(njin_pong VERSION 0.1.0 LANGUAGES CXX)
endif()
set(NJIN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH "njin source checkout")
include("${NJIN_ROOT}/cmake/njin_game.cmake")

add_executable(njin_pong main.cpp pong.cpp)

# njin::rt pulls njin::api (and its include dir) in transitively.
target_link_libraries(njin_pong PRIVATE njin::rt njin::gpu njin_warnings)

# The njin icon on the exe, its window and its taskbar button (Windows).
njin_icon(njin_pong)
